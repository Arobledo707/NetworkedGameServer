// NetworkedGame.cpp : Defines the entry point for the console application.
//

//Server code

#include "Server.h"

int main(int argc, char **argv)
{
	Server server;

	if (!server.Init()) 
	{
		return 1;
	}

	server.Update();
	server.CleanUp();

	return 0;
}