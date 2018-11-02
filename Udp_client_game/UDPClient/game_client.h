#pragma once

#include "../../Udp_server_game/UDPServer/server.h"

namespace sb
{

class Game_client
{
public:
	Game_client() { initialize(); }
	Game_client(const Game_client&) = delete;
	Game_client& operator=(const Game_client&) = delete;
	Game_client(Game_client&&) = delete;
	Game_client& operator=(Game_client&&) = delete;
	~Game_client() = default;

	void initialize()
	{
		server.start(Udp_address{ "127.0.0.1", 27016 }, Udp_config{512}, &login_server_adress);

		std::thread network_listen([&] {server.listen_loop(); });
		std::thread network_send([&] {server.resend_loop(); });
		std::thread logic([&] {run(); });
		std::thread console([&] {run_console(); });

		logic.join();
		network_listen.join();
		network_send.join();
		console.join();
	}

	using Message = std::vector<char>;

private:

	void run_console()
	{
		while (running && !server.is_terminated())
		{
			std::string command;
			std::getline(std::cin, command);

			if (command == "exit")
			{
				running = false;
				server.terminate();
			}
			else if (command.find("login ") == 0 ||
				command.find("bet ") == 0 ||
				command.find("say ") == 0 ||
				command == "bets" ||
				command == "money" ||
				command == "roll")
			{
				server.send(login_server_adress, make_package(command));
			}
			else
			{
				std::cout << "Unknown command. List of commands:\nexit\nlogin <name>\nbet <position> <amount>\nbets\nmoney\nroll\nsay <message>\n";
			}
		}
	}

	void run()
	{
		while (running && !server.is_terminated())
		{
			if (server.has_package())
			{
				auto package = server.deque();
				std::string content;
				if (package.message.size() != 0) content = package.message.data();
				//std::cout << "Package from " << package.source.hostname << ":"
				//	<< std::to_string(package.source.port) << ", content: \"" << content << "\"\n";

				if (content.find("say ") == 0) std::cout << content.substr(4, content.size() - 4) << "\n";
			}

			wait_ms(10Ui64);
		}
	}

	Udp_address login_server_adress
	{
		//"109.167.160.14",
		"127.0.0.1",
		27015
	};
	std::atomic_bool running{ true };
	Game_udp_server server;

};

}