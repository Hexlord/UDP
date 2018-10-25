#pragma once

#include "server.h"

namespace sb
{

struct Player
{
	Udp_address address;

};

class Game
{
public:
	Game() { initialize(); }
	Game(const Game&) = delete;
	Game& operator=(const Game&) = delete;
	Game(Game&&) = delete;
	Game& operator=(Game&&) = delete;
	~Game() = default;

	void initialize()
	{
		server.start(Udp_address{ "127.0.0.1", 27015 }, Udp_config{ 512 }, nullptr);
		
		std::thread network([&] {server.listen(); });
		std::thread logic([&] {run(); });
		std::thread console([&] {run_console(); });

		logic.join();
		network.join();
		console.join();
	}

	using Message = std::vector<char>;

private:

	void run_console()
	{
		while (running)
		{
			std::string command;
			std::getline(std::cin, command);

			if (command == "exit")
			{
				running = false;
				server.terminate();
			}

			if (command == "list")
			{
				for (auto address : addresses)
				{
					std::cout << "Connection on " << address.hostname << ":" << std::to_string(address.port) << "\n";
				}
			}

			if (command == "players")
			{
				for (auto player : players)
				{
					std::cout << "Player on " << player.address.hostname << ":" << std::to_string(player.address.port) << "\n";
				}
			}
		}
	}

	void run()
	{
		while (running)
		{
			if (server.has_package())
			{
				auto package = server.deque();

				auto it = std::find_if(addresses.begin(), addresses.end(), [&](auto addr) {return addr.hostname == package.source.hostname && addr.port == package.source.port; });
				if (it == addresses.end()) addresses.push_back(package.source);

				std::cout << "Package from " << package.source.hostname << ":"
					<< std::to_string(package.source.port) << ", content:\n" << package.message.data() << "\n";
			}
		}
	}

	std::vector<Player> players;
	std::vector<Udp_address> addresses;

	std::atomic_bool running{ true };
	Game_udp_server server;

};

}