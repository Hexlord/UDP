#pragma once

#include <stdio.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <string>
#include <algorithm>
#include <vector>
#include <iostream>

#pragma comment(lib,"ws2_32.lib") //Winsock Library

namespace sb
{

struct Udp_address
{
	std::string hostname;
	u_short port{ 0 };
};

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

	void terminate()
	{
		if (state == State::Active)
		{
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
		this->config = config;

		std::cout << "Server started on " << adress.hostname << ":" << std::to_string(adress.port) << " socket " << std::to_string(serverSocket) << "\n";

		state = State::Active;
	}

	void send(Udp_address address, Udp_package_out package)
	{
		if (package.message.size() < config.packageSize)
		{
			package.message.resize(config.packageSize);
		}
		else if (package.message.size() > config.packageSize)
		{
			throw Package_overflow_exception{};
		}
		sockaddr_in target;
		target.sin_family = AF_INET;
		InetPtonA(AF_INET, address.hostname.c_str(), &target.sin_addr);
		target.sin_port = htons(address.port);
		int target_size = sizeof(target);
		if (sendto(serverSocket, package.message.data(), static_cast<int>(package.message.size()),
			0, reinterpret_cast<const sockaddr*>(&target), target_size) == SOCKET_ERROR)
		{
			throw NetworkException{ WSAGetLastError() };
		}
	}

	void listen()
	{
		if (state != State::Active) throw Illegal_state_exception{};
		
		if (server_adress)
		{
			send(*server_adress, make_package("UDP punch-through"));
		}

		while (true)
		{
			std::vector<char> buffer(config.packageSize);
			std::fill(buffer.begin(), buffer.end(), 0);

			sockaddr_in source;
			int source_size = static_cast<int>(sizeof(source));

			// wait for data from clients
			auto received = recvfrom(serverSocket, buffer.data(), static_cast<int>(buffer.size()), 0,
				reinterpret_cast<sockaddr*>(&source), &source_size);
			if (received == SOCKET_ERROR)
			{
				if (state == State::Terminated) return;
				throw NetworkException{ WSAGetLastError() };
			}

			char adress_buffer[INET_ADDRSTRLEN];
			InetNtop(AF_INET, &source.sin_addr, adress_buffer, INET_ADDRSTRLEN);


			process(Udp_package_in
				{
					Udp_address
					{
						adress_buffer,
						ntohs(source.sin_port)
					},
					buffer
				});
		}
	}

protected:

	virtual void process(Udp_package_in package) = 0;

private:




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
	Udp_config config;
	State state{ State::Empty };
	SOCKET serverSocket{ INVALID_SOCKET };

};

}

