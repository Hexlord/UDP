#pragma once

#define NOMINMAX
#include <stdio.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <string>
#include <algorithm>
#include <vector>
#include <iostream>
#include <inttypes.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

#pragma comment(lib,"ws2_32.lib") //Winsock Library

namespace sb
{

const std::string String_ack = "!ACK";
const std::string String_punch_through = "!PUNCH";

constexpr uint64_t Resend_timeout_ms = 5000;

constexpr bool Debug_drop_first_data_package = false;
constexpr bool Debug_do_not_send_first_package_immediately = false;

using Package_number = int32_t;


uint64_t time_ms()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

void wait_ms(uint64_t length_ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(length_ms));
}

struct Udp_address
{
	std::string hostname;
	u_short port{ 0 };
};

std::string to_string(Udp_address address)
{
	return address.hostname + ":" + std::to_string(address.port);
}

struct Udp_config
{
	int packageSize{ 0 };

};

struct Udp_package_in
{
	Udp_address source;
	std::vector<char> message;

};

struct Udp_package_out
{
	std::vector<char> message;
};

struct Udp_package_out_raw
{
	Package_number number{ 0 };
	std::vector<char> message;

	std::vector<char> serialize() const
	{
		std::vector<char> result(sizeof(number) + message.size() * sizeof(char));
		memcpy(&result[0], &number, sizeof(number));
		memcpy(&result[0] + sizeof(number), message.data(), message.size() * sizeof(char));

		return result;
	}

	void deserialize(std::vector<char> raw)
	{
		memcpy(&number, &raw[0], sizeof(number));
		message.resize(raw.size() - sizeof(number));
		if (message.size() != 0)
		{
			memcpy(&message[0], &raw[0] + sizeof(number), raw.size() - sizeof(number));
		}
	}
};

struct Udp_connection
{
	Udp_address address;
	int32_t number_send{ -1 };
	int32_t number_receive{ 0 };

	std::vector<std::pair<Udp_package_out_raw, uint64_t>> send_sessions;
};

Udp_package_out make_package(std::string message)
{
	Udp_package_out result;
	result.message.resize(message.size());
	for (int i = 0, end = static_cast<int>(message.size()); i != end; ++i)
	{
		result.message[i] = message[i];
	}
	return result;
}

class Udp_server
{
public:
	Udp_server() = default;

	bool is_terminated() const { return terminated; }

	void terminate()
	{
		if (state == State::Active)
		{
			std::cout << "Connection to server terminated\n";
			terminated = true;
			state = State::Terminated;
			shutdown(serverSocket, SD_BOTH);
			closesocket(serverSocket);
		}
	}

	void start(Udp_address adress, Udp_config config, Udp_address* server_adress)
	{
		if (state != State::Empty) throw Illegal_state_exception{};
		this->server_adress = server_adress;

		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			throw NetworkException{ WSAGetLastError() };
		}

		serverSocket = socket(AF_INET, SOCK_DGRAM, server_adress ? IPPROTO_UDP : 0);
		if (serverSocket == INVALID_SOCKET)
		{
			throw NetworkException{ WSAGetLastError() };
		}

		sockaddr_in server;
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = INADDR_ANY;
		server.sin_port = htons(adress.port);

		if (!server_adress)
		{
			if (bind(serverSocket, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == SOCKET_ERROR)
			{
				throw NetworkException{ WSAGetLastError() };
			}
		}
		this->package_raw_size = sizeof(uint64_t) + config.packageSize;
		this->package_message_size = config.packageSize;

		std::cout << "Server started on " << adress.hostname << ":" << std::to_string(adress.port) << " socket " << std::to_string(serverSocket) << "\n";

		state = State::Active;
	}

	void send(Udp_address address, Udp_package_out package)
	{

		if (package.message.size() < package_message_size)
		{
			package.message.resize(package_message_size);
		}
		else if (package.message.size() > package_message_size)
		{
			throw Package_overflow_exception{};
		}

		{
			std::lock_guard<std::mutex> _(connections_mutex);

			auto& connection = get_connection(address);

			++connection.number_send;

			auto out = Udp_package_out_raw
			{
				connection.number_send,
				package.message
			};

			if (Debug_do_not_send_first_package_immediately && connection.number_send == 0)
			{
				// do not send (testing)
			}
			else
			{
				send_immediate(address, out);
			}
			connection.send_sessions.push_back(std::make_pair(out, time_ms()));
		}

	}

	void resend_loop()
	{
		if (state != State::Active) throw Illegal_state_exception{};

		while (!terminated)
		{
			{
				std::lock_guard<std::mutex> _(connections_mutex);
				for (auto& connection : connections)
				{
					for (auto& session : connection.send_sessions)
					{
						if (time_ms() - session.second >= Resend_timeout_ms)
						{
							debug("Package " + std::to_string(session.first.number) + " to " + to_string(connection.address) + " was not acknowledged within timeout, resending");

							send_immediate(connection.address, session.first);
							session.second = time_ms();
						}
					}
				}
			}

			wait_ms(100Ui64);
		}
	}

	void listen_loop()
	{
		if (state != State::Active) throw Illegal_state_exception{};

		if (server_adress) // UDP punch-through
		{
			auto package = make_package(String_punch_through);
			auto raw = Udp_package_out_raw
			{
				-1,
				package.message
			};
			send_immediate(*server_adress, raw);
		}

		std::vector<char> buffer_raw(package_raw_size);
		std::vector<char> buffer_message(package_message_size);

		sockaddr_in source;
		int source_size = static_cast<int>(sizeof(source));

		while (!terminated)
		{
			std::fill(buffer_raw.begin(), buffer_raw.end(), 0);
			std::fill(buffer_message.begin(), buffer_message.end(), 0);

			// wait for data from clients
			auto received = recvfrom(serverSocket, buffer_raw.data(), static_cast<int>(buffer_raw.size()), 0,
				reinterpret_cast<sockaddr*>(&source), &source_size);
			if (received == SOCKET_ERROR)
			{
				if (state == State::Terminated) return;
				
				// abnormal termination
				// throw NetworkException{ WSAGetLastError() };
				terminate();
				return;
			}

			char adress_buffer[INET_ADDRSTRLEN];
			InetNtop(AF_INET, &source.sin_addr, adress_buffer, INET_ADDRSTRLEN);

			auto address = Udp_address
			{
				adress_buffer,
				ntohs(source.sin_port)
			};

			Udp_package_out_raw in;
			in.deserialize(buffer_raw);

			{
				std::lock_guard<std::mutex> _(connections_mutex);

				auto& connection = get_connection(address);
				auto& sessions = connection.send_sessions;

				if (in.message.size() > 0 && std::string(in.message.data()) == String_punch_through) // UDP punch-through
				{
					// ignore
				}
				else if (in.message.size() > 0 && std::string(in.message.data()) == String_ack) // acknowledge
				{
					auto it = std::find_if(sessions.begin(), sessions.end(),
						[&](auto session) {return session.first.number == in.number; });

					if (it != sessions.end())
					{
						sessions.erase(it); // stop resending this message
					}
					else
					{
						debug("Received ACK package " + std::to_string(in.number) + " from " + to_string(address) + ", but there are no such message to acknowledge");
					}
				}
				else // data
				{
					// drop if wrong order
					static bool drop_first = Debug_drop_first_data_package;
					if (drop_first)
					{
						// drop (testing)
						drop_first = false;
					} else
					{
						if (connection.number_receive == in.number)
						{
							process(Udp_package_in
								{
									address,
									in.message
								});
							++connection.number_receive;
						}

						if (connection.number_receive > in.number) // already processed, send acknowledge
						{
							Udp_package_out_raw ack;
							ack.number = in.number;
							ack.message.resize(package_message_size);
							memcpy(&ack.message[0], String_ack.c_str(), String_ack.size() * sizeof(char));
							send_immediate(address, ack);
						}

						if (connection.number_receive < in.number) // to early to process, drop
						{
							debug("Received package " + std::to_string(in.number) + " from " + to_string(address) + ", but next package number is "
								+ std::to_string(connection.number_receive) + ", dropping");
						}
					}
				}
			}
		}
	}

protected:

	virtual void process(Udp_package_in package) = 0;

private:

	void debug(std::string message)
	{
		std::cout << "Debug: " << message << "\n";
	}

	void send_immediate(Udp_address address, Udp_package_out_raw out)
	{
		sockaddr_in target;
		target.sin_family = AF_INET;
		InetPtonA(AF_INET, address.hostname.c_str(), &target.sin_addr);
		target.sin_port = htons(address.port);

		auto raw = out.serialize();

		if (sendto(serverSocket, raw.data(), static_cast<int>(raw.size()),
			0, reinterpret_cast<const sockaddr*>(&target), static_cast<int>(sizeof(target))) == SOCKET_ERROR)
		{
			throw NetworkException{ WSAGetLastError() };
		}
	}

	Udp_connection& get_connection(Udp_address address)
	{
		auto it = std::find_if(connections.begin(), connections.end(), [&](auto it) {return address.hostname == it.address.hostname &&
			address.port == it.address.port; });
		if (it != connections.end()) return *it;
		connections.push_back(Udp_connection
			{ address });
		return connections.back();
	}

	std::mutex connections_mutex;
	std::vector<Udp_connection> connections;

	std::atomic_bool terminated{ false };

	struct Package_overflow_exception { };
	struct Illegal_state_exception { };
	struct NetworkException { int code{ 0 }; };
	enum class State
	{
		Empty,
		Active,
		Terminated
	};
	Udp_address* server_adress = nullptr;
	int package_raw_size{ 0 };
	int package_message_size{ 0 };
	State state{ State::Empty };
	SOCKET serverSocket{ INVALID_SOCKET };

};

}

