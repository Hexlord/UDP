#pragma once

#include "server.h"

namespace sb
{

constexpr int Default_money = 1000;
constexpr int Max_bet_size_digit = 5;

std::vector<std::pair<std::string, int>> bet_positions =
{
	{ "zero", 35 },
	{ "red", 1 },
	{ "black", 1 },
	{ "odd", 1 },
	{ "even", 1 },
};

bool is_black(int n)
{
	int black[] = { 2,4,6,8,10,11,13,15,17,20,22,24,26,28,29,31,33,35 };
	for (int i = 0; i < sizeof(black) / sizeof(int); ++i)
	{
		if (black[i] == n) return true;
	}
	return false;
}

bool is_red(int n)
{
	return (n!=0 && !is_black(n));
}

enum class Player_role
{
	Unassigned,
	Player,
	Admin,
	Banned
};

struct Player
{
	Udp_address address;
	std::string name;
	Player_role role{ Player_role::Unassigned };
	int money{ 0 };
	int bet_type{ 0 };
	int bet{ 0 };
};

std::string to_string(Player_role role)
{
	switch (role)
	{
	case sb::Player_role::Unassigned:
		return "unassigned";
	case sb::Player_role::Player:
		return "player";
	case sb::Player_role::Admin:
		return "admin";
	case sb::Player_role::Banned:
		return "banned";
	}
	return "unassigned";
}

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
		for (int i = 1; i < 37; ++i)
		{
			bet_positions.push_back({ std::to_string(i), 35 });
		}

		server.start(Udp_address{ "127.0.0.1", 27015 }, Udp_config{ 512 }, nullptr);

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
					std::cout << "Player " + player.name + " connected on " << player.address.hostname << ":" << std::to_string(player.address.port) << "\n";
				}
			}

			if (command.find("ban ") == 0)
			{
				std::string name = command.substr(4, command.size() - 4);
				bool found = false;
				for (auto& player : players)
				{
					if (player.name == name)
					{
						player.role = Player_role::Banned;
						found = true;
					}
				}
				std::cout << "Player " + name + (found ? " banned" : " not found");
			}
		}
	}

	Player* get_player(Udp_address address)
	{
		auto it = std::find_if(players.begin(), players.end(), [&](auto it) {return address.hostname == it.address.hostname &&
			address.port == it.address.port; });
		if (it != players.end()) return &(*it);

		return nullptr;
	}

	Player* add_player(Udp_address address, std::string name)
	{
		players.push_back(Player
			{
				address,
				name
			});
		auto& player = players.back();
		player.role = name == "admin" ? Player_role::Admin : Player_role::Player;
		player.money = Default_money;
		return &(player);
	}

	bool validate_name(std::string name)
	{
		if (name.size() == 0) return false;
		for (auto& player : players)
		{
			if (player.name == name) return false;
		}
		return true;
	}

	bool is_int(const std::string& number)
	{
		if (number.size() == 0) return false;
		std::string except_first = number.substr(1, number.size() - 1);
		auto last_sign = except_first.find_last_of("+-");
		return number.find_first_not_of("0123456789") == std::string::npos &&
			(last_sign == 0 || last_sign == std::string::npos);
	}

	std::pair<std::string, std::string> extract_two_words(std::string text)
	{
		std::string first;
		std::string second;
		bool after_delim = false;
		for (int i = 0, end = static_cast<int>(text.size()); i != end; ++i)
		{
			char c = text[i];
			if (c == ' ')
			{
				after_delim = true;
				continue;
			}
			if (!after_delim) first += c;
			else second += c;
		}
		return { first, second };
	}

	bool validate_bet(std::string string_bet)
	{
		auto words = extract_two_words(string_bet);
		auto bet_position = words.first;
		auto bet_amount = words.second;
		
		auto position = std::find_if(bet_positions.begin(), bet_positions.end(),
			[&](auto it) {return it.first == bet_position; });
		if (position == bet_positions.end()) return false;

		if (bet_amount.size() > Max_bet_size_digit || !is_int(bet_amount) ||
			stoi(bet_amount) <= 0)
		{
			return false;
		}

		return true;
	}

	void process_bet(Player& player, std::string string_bet)
	{
		if (player.role != Player_role::Player)
		{
			server.send(player.address,
				make_package("say Only players can place bets, your current role is "
					+ to_string(player.role)));
			return;
		}
		if (player.bet != 0)
		{
			server.send(player.address, make_package("say You already have bet " + std::to_string(player.bet)));
			return;
		}

		if (!validate_bet(string_bet))
		{
			std::string text = "say " + string_bet + " is not a valid bet. Use \"bet <position> <amount>\" where position can be:\n";
			for (auto& position : bet_positions)
			{
				text += position.first + " (x" + std::to_string(position.second) + "); ";
			}
			server.send(player.address, make_package(text));
			return;
		}

		auto words = extract_two_words(string_bet);
		auto bet_position = words.first;
		auto bet_amount = words.second;

		auto position = std::find_if(bet_positions.begin(), bet_positions.end(),
			[&](auto it) {return it.first == bet_position; });

		int bet = stoi(bet_amount);

		if (bet > player.money)
		{
			server.send(player.address, make_package("Insufficient money. You only have " + std::to_string(player.money)));
			return;
		}
		player.money -= bet;

		player.bet = bet;
		player.bet_type = static_cast<int>(std::distance(bet_positions.begin(), position));
		server.send(player.address, make_package("say Your current bet is now " + std::to_string(player.bet) + " on " + position->first));
	}

	void process_login(Udp_address source, Player* player, std::string name)
	{
		if (player != nullptr)
		{
			server.send(source, make_package("say " + player->name + ", you are already logged in"));
		}
		else
		{
			if (!validate_name(name))
			{
				server.send(source, make_package("say Name \"" + player->name + "\" is not available"));
			}
			player = add_player(source, name);
			server.send(source, make_package("say Login successful. Your role is " + to_string(player->role)));
		}
	}

	std::string bet_info(int bet_type)
	{
		return bet_positions[bet_type].first
			+ " (x" + std::to_string(bet_positions[bet_type].second)
			+ ")";
	}

	bool is_win(int type, int roll)
	{
		if (type == 0) // zero
		{
			return roll == 0;
		}
		else if (type == 1) // red
		{
			return is_red(roll);
		}
		else if (type == 2) // black
		{
			return is_black(roll);
		}
		else if (type == 3) // odd
		{
			return roll % 2 == 1;
		}
		else if (type == 4) // even
		{
			return roll % 2 == 0;
		}
		else
		{
			return roll == type - 5;
		}
	}

	int get_win(int bet, int bet_type, int roll)
	{
		auto coef = bet_positions[bet_type].second;
		return is_win(bet_type, roll) ? coef * bet : 0;
	}

	void process_bets(Player& player)
	{
		std::string bets = "Current bets are:\n\n";
		bool any_bets = false;
		for (auto& player_it : players)
		{
			if (player_it.bet == 0) continue;
			bets += player_it.name;
			if (player_it.name == player.name) bets += " (you)";
			bets += " -> " + std::to_string(player_it.bet) + " on "
				+ bet_info(player_it.bet_type) + "\n";
			any_bets = true;
		}
		if (!any_bets) bets = "There are no bets yet\n";

		server.send(player.address, make_package("say " + bets));
	}

	void process_money(Player& player)
	{
		std::string money = "Current players money:\n\n";
		bool any_money = false;
		for (auto& player_it : players)
		{
			if (player_it.role != Player_role::Player) continue;
			money += player_it.name;
			if (player_it.name == player.name) money += " (you)";
			money += " -> " + std::to_string(player_it.money) + "\n";
			any_money = true;
		}
		if (!any_money) money = "There are no players yet\n";

		server.send(player.address, make_package("say " + money));
	}

	void process_roll(Player& player)
	{
		if (player.role != Player_role::Admin)
		{
			server.send(player.address,
				make_package("say Only admins can roll the roulette, your current role is "
					+ to_string(player.role)));
			return;
		}
		int bets_sum = 0;
		for (auto& player_it : players)
		{
			bets_sum += player_it.bet;
		}
		if (bets_sum == 0)
		{
			server.send(player.address,
				make_package("say There are no bets yet"));
			return;
		}

		int roll = rand() & 37; // 0..36
		std::string string_roll = std::to_string(roll);
		if (roll % 2 == 1) string_roll += " odd";
		if (roll % 2 == 0) string_roll += " even";
		if (is_red(roll)) string_roll += " red";
		if (is_black(roll)) string_roll += " black";

		std::string result = "Roulette stopped on " + string_roll
			+ " resulting in:\n";

		for (auto& player_it : players)
		{
			auto win = get_win(player_it.bet, player_it.bet_type, roll);
			if (win > 0)
			{
				result += player_it.name; 
				if (player_it.name == player.name) result += " (you)";
				result += " placed " + std::to_string(player_it.bet)
					+ " on " + bet_info(player_it.bet_type) + " and won "
					+ std::to_string(win);
				player_it.money += win + player_it.bet;
			}
			player_it.bet = 0;
		}

		for (auto& player_it : players)
		{
			server.send(player_it.address, make_package("say " + result));
		}
	}

	void process_say(Player& player, std::string message)
	{
		for (auto& player_it : players)
		{
			if (player_it.name == player.name) continue;
			server.send(player_it.address, make_package("say " + player.name + ": " + message));
		}
	}

	void process_package(Udp_address source, std::string content)
	{
		Player* player = get_player(source);
		if (content.find("login ") == 0)
		{
			process_login(source, player, content.substr(6, content.size() - 6));
			return;
		}

		if (player == nullptr)
		{
			server.send(source, make_package("say Please log in"));
			return;
		}

		if (player->role == Player_role::Banned)
		{
			server.send(source, make_package("say You have been banned"));
			return;
		}

		if (content.find("bet ") == 0)
		{
			process_bet(*player, content.substr(4, content.size() - 4));
			return;
		}

		if (content == "bets")
		{
			process_bets(*player);
			return;
		}

		if (content == "money")
		{
			process_money(*player);
			return;
		}

		if (content == "roll")
		{
			process_roll(*player);
			return;
		}

		if (content.find("say ") == 0)
		{
			process_say(*player, content.substr(4, content.size() - 4));
			return;
		}

		server.send(source, make_package("say Unknown command \"" + content + "\""));

	}

	void run()
	{
		while (running)
		{
			if (server.has_package())
			{
				auto package = server.deque();

				std::string content;
				if (package.message.size() != 0) content = package.message.data();

				auto it = std::find_if(addresses.begin(), addresses.end(), [&](auto addr) {return addr.hostname == package.source.hostname && addr.port == package.source.port; });
				if (it == addresses.end()) addresses.push_back(package.source);

				//std::cout << "Package from " << package.source.hostname << ":"
				//	<< std::to_string(package.source.port) << ", content: \"" << content << "\"\n";

				process_package(package.source, content);
			}
		}
	}

	std::vector<Player> players;
	std::vector<Udp_address> addresses;

	std::atomic_bool running{ true };
	Game_udp_server server;

};

}