#pragma once

#include "stdafx.h"
#include <iostream>
#include <conio.h>
#include <string>
#include <unordered_map>
#include <sstream>
#include <random>
#include <functional>
#include <map>
#include <thread>
#include <atomic>

#include "protoTest.pb.h"
#include "ServerCommands.pb.h"

#define DEFAULT_PORT 9999
#define DEFAULT_BUFLEN 512
#define FD_SETSIZE 100

//TODO add gametype and refactor game functions into game base class
// add checkers
//

struct ClientConnection
{
	SOCKET clientConnection;
	Player player;
};

struct PendingChallenge
{
	ClientConnection* challenger;
	ClientConnection* challengee;
	int challengeID;
	bool validChallenge;

	std::string* serializedChallenger;
	std::string* serializedChallengee;
};



class Server
{
private:
	const enum SoldierType
	{
		Swordsman,
		Archer,
		Cavalry
	};

	enum Command
	{
		Challenge,
		Info,
		Quit,
		Login,
		Logout,
		Chat,
		List,
		Commands,
		Help,
		CommandSize
	};

public:
	Server();
	~Server();


	bool Init();
	void Update();
	void CleanUp();

	static void InitArmy(ClientConnection* challenger, ClientConnection* challengee, Army& army, Game& game, char* recvbuf, bool firstArmy);
	static std::vector<int> TakeTurn(Army& army, Army& enemyArmy, ClientConnection* player, char* recvbuf);
	static std::string PrintArmy(Army& army, Player player);
	static int CalculateDamageDone(int armyNum, SoldierType attacker, SoldierType defender);
	static std::vector<int> ArmyAttack(int attackerCount, int attackerType, Army& enemyArmy);

private:
	void InitializeCommandFunctions();
	void InitializeGameInstantce(LPVOID playerSockets);

private:
	SOCKET m_listenSocket = INVALID_SOCKET;
	SOCKET m_connfd;
	char m_buff[DEFAULT_BUFLEN];
	ClientConnection m_clientsConnected[100];

	std::unordered_map<std::string, std::string> m_players;
	std::unordered_map<int, PendingChallenge> m_challenges;

	std::vector<std::thread> m_gameThreads;

	unsigned int m_challengeID = 0;
	int m_noChallenge = -1;

	WSADATA m_wsaData;
	int m_iResult;
	int m_clilen;

	int m_nready, m_client[FD_SETSIZE];
	fd_set m_rset, m_allSet;
	SOCKET m_sockfd;

	int i = 0;
	SSIZE_T n;

	SOCKET m_maxfd;
	int m_maxi;

	struct sockaddr_in m_clientAddr, m_servaddr;

	//TODO Refactor into map
	// challenge and functor
	// Map of Enum and map of command name and functor

	std::map<Command, std::function<bool(std::string, int, int[])>> m_availableCommands;

	std::vector<std::string> m_commands{ "Challenge", "Info", "Quit", "Login", "Logout", "Chat", "List", "Commands, Help" };
	const std::string m_sendString = "Ready to login!\nLogin {username} {password}";
};
