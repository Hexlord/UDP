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
	Game_udp_server(Udp_address adress, Udp_config config, Udp_address* server_adress)
	{
		start(adress, config, server_adress);
	}

	bool has_package()
	{
		return queue.size() > 0;
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
}

