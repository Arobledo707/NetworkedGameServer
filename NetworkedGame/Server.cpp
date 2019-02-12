#include "Server.h"

DWORD WINAPI GameInstance(LPVOID playerSockets)
{
	char recvbuf[DEFAULT_BUFLEN];
	memset(recvbuf, 0, sizeof(char) * DEFAULT_BUFLEN);

	PendingChallenge* test = (PendingChallenge*)playerSockets;

	ClientConnection* challenger = test->challenger;
	ClientConnection* challengee = test->challengee;
	bool gameIsPlaying = false;

	if (challenger->clientConnection == INVALID_SOCKET)
	{
		printf("Invalid challenger");
	}
	if (challengee->clientConnection == INVALID_SOCKET)
	{
		printf("Invalid challengee");
	}

	gameIsPlaying = true;

	Game game;
	Army army1;
	Army army2;

	bool playerOneTurn = true;
	while (gameIsPlaying)
	{
		if (!game.has_army1())
		{
			Server::InitArmy(challenger, challengee, army1, game, recvbuf, true);
		}

		if (!game.has_army2())
		{
			Server::InitArmy(challengee, challenger, army2, game, recvbuf, false);

		}

		// pretty bad but it does work for now
		std::string clearScreen = "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n";
		send(challengee->clientConnection, clearScreen.c_str(), clearScreen.length(), 0);
		send(challenger->clientConnection, clearScreen.c_str(), clearScreen.length(), 0);


		if (playerOneTurn)
		{
			playerOneTurn = false;
			auto enemiesKilled = Server::TakeTurn(army1, army2, challengee, recvbuf);

			std::string killedString = challengee->player.name() + " " + std::to_string(enemiesKilled[0]) + " swordsman killed\n" +
				std::to_string(enemiesKilled[1]) + " archers killed\n" + std::to_string(enemiesKilled[2]) + " cavalry killed";
			send(challenger->clientConnection, killedString.c_str(), killedString.length(), 0);
			send(challengee->clientConnection, killedString.c_str(), killedString.length(), 0);
		}
		else
		{
			playerOneTurn = true;
			auto enemiesKilled = Server::TakeTurn(army2, army1, challenger, recvbuf);

			std::string killedString = challenger->player.name() + " " + std::to_string(enemiesKilled[0]) + " swordsman killed\n" +
				std::to_string(enemiesKilled[1]) + " archers killed\n" + std::to_string(enemiesKilled[2]) + " cavalry killed";
			send(challenger->clientConnection, killedString.c_str(), killedString.length(), 0);
			send(challengee->clientConnection, killedString.c_str(), killedString.length(), 0);
		}

		std::string army1Stats = Server::PrintArmy(army1, challengee->player);
		std::string army2Stats = Server::PrintArmy(army2, challenger->player);

		//send Army stats
		send(challenger->clientConnection, army1Stats.c_str(), army1Stats.length(), 0);
		send(challenger->clientConnection, army2Stats.c_str(), army2Stats.length(), 0);

		send(challengee->clientConnection, army1Stats.c_str(), army1Stats.length(), 0);
		send(challengee->clientConnection, army2Stats.c_str(), army2Stats.length(), 0);

		if (game.army1().archers() <= 0 && game.army1().swordsman() <= 0 && game.army1().cavalry() <= 0)
		{
			gameIsPlaying = false;
			std::string resultString = challenger->player.name() + " defeated " + challengee->player.name();

			challenger->player.set_wins(challenger->player.wins() + 1);
			challenger->player.add_playhistory(resultString);

			challengee->player.set_losses(challengee->player.losses() + 1);
			challengee->player.add_playhistory(resultString);
		}

		if (game.army2().archers() <= 0 && game.army2().swordsman() <= 0 && game.army2().cavalry() <= 0)
		{
			gameIsPlaying = false;
			std::string resultString = challengee->player.name() + " defeated " + challenger->player.name();

			challengee->player.set_wins(challengee->player.wins() + 1);
			challengee->player.add_playhistory(resultString);

			challenger->player.set_losses(challenger->player.losses() + 1);
			challenger->player.add_playhistory(resultString);
		}
	}

	challengee->player.set_playerstate(Player::PlayerState::Player_PlayerState_Lobby);
	challenger->player.set_playerstate(Player::PlayerState::Player_PlayerState_Lobby);

	challengee->player.SerializeToString(test->serializedChallengee);
	challenger->player.SerializeToString(test->serializedChallenger);

	game.release_army1();
	game.release_army2();
	return 0;
}

Server::Server()
{
}


Server::~Server()
{
}

// Initializes the server
// Initializes winsock
// Sets up the listen socket

bool Server::Init()
{

	// Initialize Winsock
	m_iResult = WSAStartup(MAKEWORD(2, 2), &m_wsaData);
	if (m_iResult != 0)
	{
		printf("WSAStartup failed with error: %d\n", m_iResult);
		return false;
	}

	m_listenSocket = socket(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
	if (m_listenSocket == INVALID_SOCKET)
	{
		printf("socket failed with error: %ld\n", WSAGetLastError());
		WSACleanup();
		return false;
	}


	ZeroMemory(&m_servaddr, sizeof(m_servaddr));
	m_servaddr.sin_family = AF_INET;
	m_servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	m_servaddr.sin_port = htons(DEFAULT_PORT);

	bind(m_listenSocket, (sockaddr*)&m_servaddr, sizeof(m_servaddr));
	if (m_iResult == SOCKET_ERROR)
	{
		printf("bind failed with error: %d\n", WSAGetLastError());
		return false;
	}


	m_iResult = listen(m_listenSocket, 0);
	if (m_iResult == SOCKET_ERROR)
	{
		printf("listen failed with error: %d\n", WSAGetLastError());
		return false;
	}

	m_maxfd = m_listenSocket;
	m_maxi = -1;

	for (int i = 0; i < FD_SETSIZE; ++i)
	{
		m_client[i] = -1;
	}

	FD_ZERO(&m_allSet);
	FD_SET(m_listenSocket, &m_allSet);

	InitializeCommandFunctions();

	return true;
}

// Server Update
void Server::Update()
{
	for (; ;)
	{
		m_rset = m_allSet;
		m_nready = select(m_maxfd + 1, &m_rset, NULL, NULL, NULL);

		if (FD_ISSET(m_listenSocket, &m_rset))
		{
			m_clilen = sizeof(m_clientAddr);
			m_connfd = accept(m_listenSocket, (sockaddr*)&m_clientAddr, &m_clilen);

			m_iResult = send(m_connfd, m_sendString.c_str(), m_sendString.length() + 1, 0);

			if (m_iResult == SOCKET_ERROR)
			{
				std::cout << "send failed: " << WSAGetLastError() << std::endl;
			}

			for (i = 0; i < FD_SETSIZE; ++i)
				if (m_client[i] < 0)
				{
					m_clientsConnected[i].clientConnection = m_connfd;
					m_clientsConnected[i].player.set_playerstate(Player::PlayerState::Player_PlayerState_LoggedOut);
					m_client[i] = m_connfd;
					break;
				}

			if (i == FD_SETSIZE)
			{
				std::cout << "Error: too many clients!" << std::endl;
			}

			FD_SET(m_connfd, &m_allSet);
			if (m_connfd > m_maxi)
			{
				m_maxfd = m_connfd;
			}
			if (i > m_maxi)
			{
				m_maxi = i;
			}

			if (--m_nready == 0)
			{
				continue;
			}
		}

		for (i = 0; i <= m_maxi; ++i)
		{
			if ((m_sockfd = m_client[i]) < 0)
			{
				continue;
			}

			if (FD_ISSET(m_sockfd, &m_rset))
			{
				memset(m_buff, 0, sizeof(char) * DEFAULT_BUFLEN);
				if ((n = recv(m_sockfd, m_buff, sizeof(m_buff), 0)) <= 0)
				{
					closesocket(m_sockfd);
					FD_CLR(m_sockfd, &m_allSet);
					m_client[i] = -1;
				}
				else
				{
					std::string playerInput = m_buff;

					ServerCommand command;
					command.ParseFromString(playerInput);

					// only allow login if logged out
					if (m_clientsConnected[i].player.playerstate() == Player::PlayerState::Player_PlayerState_LoggedOut)
					{
						//TODO refactor login function to be fine with command.content instead of entire string
						if (command.command == Command::Login)
						{
							auto loginFunction = m_availableCommands.find(Command::Login);
							if (loginFunction->second(command.content, i, &m_client[i]))
							{
								std::cout << "Client: " << m_client[i] << " logged in." << std::endl;
							}
							else
							{
								std::cout << "Client: " << m_client[i] << " failed to login" << std::endl;
							}
						}
						//if (playerInput.substr(0, m_commands[Command::Login].size()) == m_commands[Command::Login])
						//{
						//	auto loginFunction = m_availableCommands.find(Command::Login);
						//	//std::function<bool(std::string, int, int[])> function = m_availableCommands.find(Command::Login);
						//	if (loginFunction->second(playerInput, i, &m_client[i]))
						//	{
						//		std::cout << "Client: " << m_client[i] << " logged in." << std::endl;
						//	}
						//	else
						//	{
						//		std::cout << "Client: " << m_client[i] << " failed to login" << std::endl;
						//	}

						//}
					}
					else if (m_clientsConnected[i].player.playerstate() == Player::PlayerState::Player_PlayerState_Challenged &&
						m_clientsConnected[i].player.challengeid() != m_noChallenge)
					{
						std::string yes = "yes";
						std::string no = "no";

						if (playerInput.substr(0, yes.size()) == yes)
						{
							auto it = m_challenges.find(m_clientsConnected[i].player.challengeid());
							if (it != m_challenges.end())
							{
								PendingChallenge& challenge = it->second;
								challenge.challengee->player.set_playerstate(Player::PlayerState::Player_PlayerState_InGame);
								challenge.challenger->player.set_playerstate(Player::PlayerState::Player_PlayerState_InGame);
								challenge.validChallenge = true;

								auto challengeeIt = m_players.find(challenge.challengee->player.name());
								challenge.serializedChallengee = &challengeeIt->second;

								auto challengerIt = m_players.find(challenge.challenger->player.name());
								challenge.serializedChallenger = &challengerIt->second;

								// should work
								CreateThread(NULL, 0, GameInstance, (LPVOID)&m_challenges[challenge.challengeID], 0, 0);
							}
						}
						else if (playerInput.substr(0, no.size()) == no)
						{
							auto it = m_challenges.find(m_clientsConnected[i].player.challengeid());
							if (it != m_challenges.end())
							{
								PendingChallenge& challenge = it->second;
								challenge.validChallenge = false;

								m_clientsConnected[challenge.challengee->player.clientid()].player.set_playerstate(Player::PlayerState::Player_PlayerState_Lobby);
								m_clientsConnected[challenge.challenger->player.clientid()].player.set_playerstate(Player::PlayerState::Player_PlayerState_Lobby);

								std::string challengeCancelled = "Challenged Cancelled";

								send(m_clientsConnected[i].clientConnection, challengeCancelled.c_str(), challengeCancelled.length(), 0);
								send(challenge.challenger->clientConnection, challengeCancelled.c_str(), challengeCancelled.length(), 0);
							}
						}

					}
					else if (playerInput.substr(0, m_commands[Command::Info].size()) == m_commands[Command::Info])
					{

						auto it = m_players.find(playerInput.substr(m_commands[Command::Info].size() + 1, playerInput.size()));
						if (it != m_players.end())
						{
							Player player;
							player.ParseFromString(it->second);

							for (std::string line : player.playhistory())
							{
								send(m_client[i], line.c_str(), sizeof(line), 0);
							}

							std::string wins;
							wins = "Wins: " + std::to_string(player.wins());

							std::string losses;
							losses = "Losses: " + std::to_string(player.losses());


							send(m_client[i], wins.c_str(), sizeof(wins), 0);
							send(m_client[i], losses.c_str(), sizeof(losses), 0);
						}
					}
					else if (playerInput.substr(0, m_commands[Command::Quit].size()) == m_commands[Command::Quit])
					{

						send(m_client[i], "Quitting", sizeof("Quitting"), 0);
						m_clientsConnected[i].player.set_playerstate(Player::PlayerState::Player_PlayerState_LoggedOut);

						closesocket(m_sockfd);
						FD_CLR(m_sockfd, &m_allSet);
						m_client[i] = -1;
					}
					else if (playerInput.substr(0, m_commands[Command::Challenge].size()) == m_commands[Command::Challenge])
					{
						auto it = m_players.find(playerInput.substr(m_commands[Command::Challenge].size() + 1, playerInput.size()));
						if (it != m_players.end())
						{
							Player challengee;
							challengee.ParseFromString(it->second);
							std::string sendChallenge = m_clientsConnected[i].player.name() + " challenges you! \nDo you accept? Yes/No";
							send(m_clientsConnected[challengee.clientid()].clientConnection, sendChallenge.c_str(), sendChallenge.length(), 0);

							m_clientsConnected[i].player.set_playerstate(Player::PlayerState::Player_PlayerState_Challenged);
							m_clientsConnected[challengee.clientid()].player.set_playerstate(Player::PlayerState::Player_PlayerState_Challenged);

							m_clientsConnected[i].player.set_challengeid(m_challengeID);
							m_clientsConnected[challengee.clientid()].player.set_challengeid(m_challengeID);


							printf("%d", m_challengeID);
							PendingChallenge challenge;
							challenge.challengee = &m_clientsConnected[challengee.clientid()];
							challenge.challenger = &m_clientsConnected[i];
							challenge.challengeID = m_challengeID;
							m_challenges.emplace(m_challengeID, challenge);
							++m_challengeID;
						}
						else
						{
							std::string playerNotFound = "There is no player with that name";
							send(m_clientsConnected[i].clientConnection, playerNotFound.c_str(), playerNotFound.length(), 0);
						}
					}
					else if (playerInput.substr(0, m_commands[Command::Chat].size()) == m_commands[Command::Chat])
					{
						std::string sendMessage = m_clientsConnected[i].player.name() + " says: "
							+ playerInput.substr(m_commands[Command::Chat].size() + 1, playerInput.size());
						for (int j = 0; j < 100; ++j)
						{
							if (m_clientsConnected[j].clientConnection != INVALID_SOCKET &&
								m_clientsConnected[j].player.playerstate() == Player::PlayerState::Player_PlayerState_Lobby)
							{
								send(m_clientsConnected[j].clientConnection, sendMessage.c_str(), sendMessage.length(), 0);
							}
						}
					}
					else if (playerInput.substr(0, m_commands[Command::Logout].size()) == m_commands[Command::Logout])
					{
						m_clientsConnected[i].player.set_playerstate(Player::PlayerState::Player_PlayerState_LoggedOut);
						send(m_clientsConnected[i].clientConnection, "You are logged out", sizeof("You are logged out"), 0);
					}

					else if (playerInput.substr(0, m_commands[Command::List].size()) == m_commands[Command::List])
					{
						auto listFunction = m_availableCommands.find(Command::List);
						if (!listFunction->second(playerInput, i, &m_client[i]))
						{
							std::cout << "failed to send player list" << std::endl;
						}
					}

				}

				if (--m_nready <= 0)
				{
					break;
				}
			}
		}
	}
}

void Server::CleanUp()
{
	//TODO close all sockets
	WSACleanup();
}

void Server::InitArmy(ClientConnection* challenger, ClientConnection* challengee, Army& army, Game& game, char* recvbuf, bool firstArmy)
{
	constexpr int k_defaultSwordsman = 34;
	constexpr int k_defaultArchers = 33;
	constexpr int k_defaultCavalry = 33;

	const std::string waitOnChallengee = "Waiting on Challengee to end their turn...";
	const std::string waitOnChallenger = "Waiting on Challenger to end their turn...";
	const std::string howManySwordsMan = "How many Swordsman?";
	const std::string howManyArchers = "How many Archers?";
	const std::string howManyCavalry = "How many Cavalry?";

	int armySize = 0;
	int swordsmenCount = 0;
	int archerCount = 0;
	int cavalryCount = 0;

	send(challenger->clientConnection, waitOnChallengee.c_str(), waitOnChallengee.length(), 0);

	send(challengee->clientConnection, howManySwordsMan.c_str(), howManySwordsMan.length(), 0);
	recv(challengee->clientConnection, recvbuf, sizeof(recvbuf), 0);

	std::stringstream swordsmanStream(recvbuf);
	if (!(swordsmanStream >> swordsmenCount))
	{
		swordsmenCount = 0;
	}

	if (swordsmenCount < 0)
	{
		swordsmenCount = 0;
	}
	else if (swordsmenCount > 100)
	{
		swordsmenCount = 100;
	}

	armySize += swordsmenCount;
	int remainingArmy = 100 - armySize;

	if (remainingArmy > 0)
	{
		memset(recvbuf, 0, sizeof(char) * DEFAULT_BUFLEN);
		send(challengee->clientConnection, howManyArchers.c_str(), howManyArchers.length(), 0);
		recv(challengee->clientConnection, recvbuf, sizeof(recvbuf), 0);

		std::stringstream archerStream(recvbuf);
		if (!(archerStream >> archerCount))
		{
			archerCount = 0;
		}

		if (archerCount < 0)
		{
			archerCount = 0;
		}
		else if (archerCount > remainingArmy)
		{
			archerCount = remainingArmy;
		}

		armySize += archerCount;
		remainingArmy = 100 - armySize;
	}

	if (remainingArmy > 0)
	{
		memset(recvbuf, 0, sizeof(char) * DEFAULT_BUFLEN);
		send(challengee->clientConnection, howManyCavalry.c_str(), howManyCavalry.length(), 0);
		recv(challengee->clientConnection, recvbuf, sizeof(recvbuf), 0);

		std::stringstream cavalryStream(recvbuf);
		if (!(cavalryStream >> cavalryCount))
		{
			cavalryCount = 0;
		}

		if (cavalryCount < 0)
		{
			archerCount = 0;
		}
		else if (cavalryCount > remainingArmy)
		{
			cavalryCount = remainingArmy;
		}
	}
	if (swordsmenCount == 0 && archerCount == 0 && cavalryCount == 0)
	{
		army.set_swordsman(k_defaultSwordsman);
		army.set_archers(k_defaultArchers);
		army.set_cavalry(k_defaultCavalry);
	}
	else
	{
		army.set_swordsman(swordsmenCount);
		army.set_archers(archerCount);
		army.set_cavalry(cavalryCount);
	}

	if (firstArmy)
	{
		game.set_allocated_army1(&army);
	}
	else
	{
		game.set_allocated_army2(&army);
	}

	send(challengee->clientConnection, waitOnChallenger.c_str(), waitOnChallenger.length(), 0);
}

std::vector<int> Server::TakeTurn(Army & army, Army & enemyArmy, ClientConnection * player, char * recvbuf)
{
	std::string rollNow = "Enter 'r' to roll for your damage";
	std::vector<int> totalEnemiesKilled = { 0, 0, 0 };
	do
	{
		send(player->clientConnection, rollNow.c_str(), rollNow.length(), 0);
		recv(player->clientConnection, recvbuf, sizeof(recvbuf), 0);
	} while (*recvbuf != 'r');


	int soldierType = 0;
	int attackedType = 0;

	if (army.swordsman() > 0)
	{
		auto vec = ArmyAttack(army.swordsman(), soldierType, enemyArmy);
		totalEnemiesKilled[0] += vec[0];
		totalEnemiesKilled[1] += vec[1];
		totalEnemiesKilled[2] += vec[2];
	}

	soldierType += 1;
	if (army.archers() > 0)
	{
		auto vec = ArmyAttack(army.archers(), soldierType, enemyArmy);
		totalEnemiesKilled[0] += vec[0];
		totalEnemiesKilled[1] += vec[1];
		totalEnemiesKilled[2] += vec[2];
	}

	soldierType += 1;
	if (army.cavalry() > 0)
	{
		auto vec = ArmyAttack(army.cavalry(), soldierType, enemyArmy);
		totalEnemiesKilled[0] += vec[0];
		totalEnemiesKilled[1] += vec[1];
		totalEnemiesKilled[2] += vec[2];
	}

	return totalEnemiesKilled;
}

std::string Server::PrintArmy(Army& army, Player player)
{
	std::stringstream ss;
	ss << army.swordsman();
	std::string swordsString = ss.str();

	ss.str(std::string());
	ss << army.archers();
	std::string archersString = ss.str();

	ss.str(std::string());
	ss << army.cavalry();
	std::string cavalryString = ss.str();

	std::string armyStat;

	armyStat = player.name() + '\n';
	armyStat += "Swordsman:" + swordsString + "\n";
	armyStat += "Archers: " + archersString + "\n";
	armyStat += "Cavalry: " + cavalryString + "\n";
	return armyStat;
}

int Server::CalculateDamageDone(int armyNum, SoldierType attacker, SoldierType defender)
{
	constexpr float damageMatrix[3][3] =
	{ { 1.0f, 2.0f, 0.5f },
	{ 0.5f, 1.0f, 2.0f },
	{ 2.0f, 0.5f, 1.0f } };

	std::random_device random;
	std::mt19937 rand(random());
	std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

	float roll;
	roll = dist(random);
	float damageModifier = 0.5f;
	damageModifier += roll;
	int damageDone = damageModifier * armyNum * damageMatrix[attacker][defender];

	return damageDone;
}

std::vector<int> Server::ArmyAttack(int attackerCount, int attackerType, Army & enemyArmy)
{
	std::vector<int> enemiesKilledVec = { 0, 0, 0 };
	if (enemyArmy.swordsman() > 0)
	{
		int enemiesKilled = CalculateDamageDone(attackerCount, (SoldierType)attackerType, (SoldierType)0);
		int enemiesRemaining = enemyArmy.swordsman() - enemiesKilled;

		if (enemiesRemaining < 0)
		{
			enemiesRemaining = 0;
		}
		enemiesKilledVec[0] = enemiesKilled;
		enemyArmy.set_swordsman(enemiesRemaining);
	}
	if (enemyArmy.archers() > 0)
	{
		int enemiesKilled = CalculateDamageDone(attackerCount, (SoldierType)attackerType, (SoldierType)1);
		int enemiesRemaining = enemyArmy.archers() - enemiesKilled;

		if (enemiesRemaining < 0)
		{
			enemiesRemaining = 0;
		}
		enemiesKilledVec[1] = enemiesKilled;
		enemyArmy.set_archers(enemiesRemaining);
	}

	if (enemyArmy.cavalry() > 0)
	{
		int enemiesKilled = CalculateDamageDone(attackerCount, (SoldierType)attackerType, (SoldierType)2);

		int enemiesRemaining = enemyArmy.cavalry() - enemiesKilled;

		if (enemiesRemaining < 0)
		{
			enemiesRemaining = 0;
		}

		enemiesKilledVec[2] = enemiesKilled;
		enemyArmy.set_cavalry(enemiesRemaining);
	}
	return enemiesKilledVec;
}

void Server::SendClientCommandList(int client)
{
	std::string tempString = "Here are the available commands:\n";
	for (std::string command : m_commands)
	{
		tempString.append(command + "\n");
		//send(client, command.c_str(), sizeof(command), 0);
		//std::cout << command << "\n";
	}
	std::cout << tempString;
	send(client, tempString.c_str(), tempString.size(), 0);
}



void Server::InitializeCommandFunctions()
{
	std::function<bool(std::string, int, int[])> loginFunction = [noChallenge = this->m_noChallenge, clientsConnected = this->m_clientsConnected,
		&players = this->m_players](std::string playerInput, int i, int client[]) -> bool
	{
		int iResult;
		std::string temp;

		int j = 6;
		while (playerInput[j] != ' ')
		{
			temp += playerInput[j];
			++j;
		}
		printf("Size of tempName %d\n", temp.length());
		printf("Size of playerInput %d\n", playerInput.length());

		if (playerInput.length() < 5 + temp.length())
		{
			std::string noPassword = "No password entered, Enter a password along with your username.";
			iResult = send(client[i], noPassword.c_str(), noPassword.length(), 0);
			if (iResult == SOCKET_ERROR)
			{
				std::cout << "send failed: " << WSAGetLastError() << std::endl;
			}
			return false;
		}
		else
		{

			Player newPlayer;
			newPlayer.set_name(temp);
			newPlayer.set_password(playerInput.substr((temp.length()), playerInput.length()));
			printf(temp.c_str());
			std::unordered_map<std::string, std::string>::iterator it = players.find(newPlayer.name());
			if (it != players.end())
			{
				Player testPlayer;

				testPlayer.ParseFromString(it->second);

				if (testPlayer.password() == newPlayer.password())
				{
					iResult = send(client[i], "Logging in..", sizeof("Logging in.."), 0);

					if (iResult == SOCKET_ERROR)
					{
						std::cout << "send failed: " << WSAGetLastError() << std::endl;
					}

					clientsConnected[i].player = testPlayer;
					clientsConnected[i].player.set_playerstate(Player::PlayerState::Player_PlayerState_Lobby);
					std::cout << "Player: " << testPlayer.name() << " has logged in.";
				}
				else
				{
					iResult = send(client[i], "Incorrect password", sizeof("Incorrect password"), 0);

					if (iResult == SOCKET_ERROR)
					{
						std::cout << "send failed: " << WSAGetLastError() << std::endl;
					}

					std::cout << "Player: " << testPlayer.name() << " has entered an incorrect password.";
				}

			}
			else
			{
				newPlayer.set_wins(0);
				newPlayer.set_losses(0);
				newPlayer.set_clientid(i);
				newPlayer.set_playerstate(Player::PlayerState::Player_PlayerState_Lobby);
				newPlayer.set_challengeid(noChallenge);
				newPlayer.SerializeToString(&temp);

				players.emplace(newPlayer.name(), temp);

				iResult = send(client[i], "Logging in..", sizeof("Logging in.."), 0);

				if (iResult == SOCKET_ERROR)
				{
					std::cout << "send failed: " << WSAGetLastError() << std::endl;
				}

				clientsConnected[i].player = newPlayer;
			}
			//Send logged in user list of commands
			newPlayer.SerializeToString(&temp);
			return true;

		}
	};

	std::function<bool(std::string, int, int[])> commandFunction = [commands = this->m_commands]
	(std::string playerInput, int i, int client[]) -> bool
	{
		std::string tempString = "Here are the available commands:\n";
		//for (std::string command : commands)
		//{
		//	tempString.append(command + "\n");
		//}
		std::cout << tempString;
		int iResult = send(*client, tempString.c_str(), tempString.size(), 0);
		if (iResult == SOCKET_ERROR)
		{
			std::cout << "send failed: " << WSAGetLastError() << std::endl;
			return false;
		}
		return true;
	};

	std::function<bool(std::string, int, int[])> listFunction = [&clientsConnected = this->m_clientsConnected]
	(std::string playerInput, int i, int client[]) -> bool
	{
		int iResult;
		for (ClientConnection client : clientsConnected)
		{
			if (client.player.playerstate() == Player::PlayerState::Player_PlayerState_Lobby)
			{
				iResult = send(clientsConnected[i].clientConnection, client.player.name().c_str(), client.player.name().length(), 0);

				if (iResult == SOCKET_ERROR)
				{
					std::cout << "send failed: " << WSAGetLastError() << std::endl;
				}
			}
		}
		return true;
	};

	std::function<bool(std::string, int, int[])> infoFunction = [commands = this->m_commands]
	(std::string playerInput, int i, int client[]) -> bool
	{
		return true;
	};

	std::function<bool(std::string, int, int[])> logoutFunction = [commands = this->m_commands]
	(std::string playerInput, int i, int client[]) -> bool
	{
		return true;
	};

	std::function<bool(std::string, int, int[])> quitFunction = [commands = this->m_commands]
	(std::string playerInput, int i, int client[]) -> bool
	{
		return true;
	};

	std::function<bool(std::string, int, int[])> chatFunction = [commands = this->m_commands]
	(std::string playerInput, int i, int client[]) -> bool
	{
		return true;
	};

	std::function<bool(std::string, int, int[])> challengeFunction = [commands = this->m_commands]
	(std::string playerInput, int i, int client[]) -> bool
	{
		return true;
	};

	m_availableCommands.emplace(Command::Login, loginFunction);
	m_availableCommands.emplace(Command::Commands, commandFunction);
	m_availableCommands.emplace(Command::List, listFunction);
	m_availableCommands.emplace(Command::Info, infoFunction);
	m_availableCommands.emplace(Command::Logout, logoutFunction);
	m_availableCommands.emplace(Command::Quit, quitFunction);
	m_availableCommands.emplace(Command::Challenge, challengeFunction);
	m_availableCommands.emplace(Command::Chat, chatFunction);
	assert(m_availableCommands.size() == Command::CommandSize);
}

