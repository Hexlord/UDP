#pragma once

#include <stdio.h>
#include <winsock2.h>

#include <string>
#include <algorithm>
#include <vector>

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
	sockaddr_in source;
	std::vector<char> message;

};

struct Udp_package_out
{
	std::vector<char> message;
};

class Udp_server
{
public:
	Udp_server() = default;
	Udp_server(Udp_address address, Udp_config config)
	{
		start(address, config);
	}

	void terminate() 
	{
		if (state == State::Active)
		{
			state = State::Terminated;
			shutdown(serverSocket, SD_BOTH);
			closesocket(serverSocket);
		}
	}

	void start(Udp_address address, Udp_config config)
	{
		if (state != State::Empty) throw Illegal_state_exception{};

		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			throw NetworkException{ WSAGetLastError() };
		}

		serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (serverSocket == INVALID_SOCKET)
		{
			throw NetworkException{ WSAGetLastError() };
		}

		sockaddr_in server;
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = INADDR_ANY;
		server.sin_port = htons(address.port);

		if (bind(serverSocket, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == SOCKET_ERROR)
		{
			throw NetworkException{ WSAGetLastError() };
		}
		this->config = config;

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
		target.sin_port = address.port;
		target.sin_addr.S_un.S_addr = inet_addr(address.hostname.c_str());
		int target_size = sizeof(target);
		if (sendto(serverSocket, package.message.data(), package.message.size(),
			0, reinterpret_cast<const sockaddr*>(&target), target_size) == SOCKET_ERROR)
		{
			throw NetworkException{ WSAGetLastError() };
		}
	}

	void listen()
	{
		if (state != State::Active) throw Illegal_state_exception{};
		while (true)
		{
			std::vector<char> buffer(config.packageSize);
			std::fill(buffer.begin(), buffer.end(), 0);

			sockaddr_in source;
			int source_size = static_cast<int>(sizeof(source));

			// wait for data from clients
			auto received = recvfrom(serverSocket, buffer.data(), buffer.size(), 0,
				reinterpret_cast<sockaddr*>(&source), &source_size);
			if (received == SOCKET_ERROR)
			{
				if (state == State::Terminated) return;
				throw NetworkException{ WSAGetLastError() };
			}

			process(Udp_package_in
				{
					source,
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
	Udp_config config;
	State state{ State::Empty };
	SOCKET serverSocket{ INVALID_SOCKET };

};

}

