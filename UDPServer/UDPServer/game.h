#pragma once

#include "network.h"

#include <thread>
#include <mutex>
#include <iostream>
#include <atomic>

namespace sb
{

class Game_udp_server : public Udp_server
{
public:
	Game_udp_server() = default;
	Game_udp_server(Udp_address address, Udp_config config)
	{
		start(address, config);
	}

	Udp_package_in deque() 
	{
		mutex.lock();
		
		Udp_package_in result = queue.front();
		queue.erase(queue.begin());
		
		mutex.unlock();
		return result;
	}

protected:
	void process(Udp_package_in package) override
	{
		mutex.lock();

		queue.push_back(package);

		mutex.unlock();
	}

private:
	std::mutex mutex;
	std::vector<Udp_package_in> queue;
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
		server.start(Udp_address{ "localhost", 27015 }, Udp_config{});
		
		std::thread network([&] {server.listen(); });
		std::thread logic([&] {run(); });
		std::thread console([&] {run_console(); });

		logic.join();
		network.join();
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
		}
	}

	void run()
	{
		while (running)
		{
			
		}
	}

	std::atomic_bool running{ false };
	Game_udp_server server;

};

}