#include "server.h"

#include "parser.h"
#include "process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int clientfd = 0;
struct serviceTable clientTable = {
	.clientMax = 60,
	.clientNum = 0
};

int ExeServer1(int port)
{
	int sockfd = 0, forClientSockfd = 0;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	printf("Start server1 at port:%d\n", port);

	if (sockfd == -1) printf("create socket failed\n");

	struct sockaddr_in serverInfo, clientInfo;
	socklen_t addrlen = sizeof(clientInfo);
	bzero(&serverInfo, sizeof(serverInfo));

	serverInfo.sin_family = PF_INET;
	serverInfo.sin_addr.s_addr = INADDR_ANY;
	serverInfo.sin_port = htons(port);
	bind(sockfd, (struct sockaddr *)&serverInfo, sizeof(serverInfo));
	listen(sockfd, 5);	

	while(1)
	{
		forClientSockfd = accept(sockfd, (struct sockaddr*)&clientInfo, &addrlen);

		printf("-----------------------------------------------------------\n");
		printf("                        New user login                     \n");
		printf("-----------------------------------------------------------\n");

		send(forClientSockfd, "% ", sizeof("% "), 0);

		pid_t pid = fork();
		
		if (pid != 0)
		{
			close(forClientSockfd);
		}
		else if (pid == 0) 
		{
			clientfd = forClientSockfd;

			dup2(clientfd, STDERR_FILENO);
			dup2(clientfd, STDOUT_FILENO);			

			return clientfd;
		}
	}
}

void ExeServer2(int port)
{
	int sockfd = 0, forClientSockfd = 0, sockMax = 0;
	static int init = 0;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (init == 0) printf("Start server2 at port:%d\n", port);

	init = 1;

	if (sockfd == -1) printf("create socket failed\n");

	struct sockaddr_in serverInfo, clientInfo;
	socklen_t addrlen = sizeof(clientInfo);
	bzero(&serverInfo, sizeof(serverInfo));

	serverInfo.sin_family = PF_INET;
	serverInfo.sin_addr.s_addr = INADDR_ANY;
	serverInfo.sin_port = htons(port);
	bind(sockfd, (struct sockaddr *)&serverInfo, sizeof(serverInfo));
	listen(sockfd, clientTable.clientMax);

	while(1)
	{
		fd_set sockSet;
	
		FD_ZERO(&sockSet);
		FD_SET(sockfd, &sockSet);

		sockMax = sockfd;

		for(int i = 0; i < clientTable.clientNum; ++i)
		{
			FD_SET(clientTable.clientfds[i], &sockSet);
			if (sockMax < clientTable.clientfds[i]) sockMax = clientTable.clientfds[i]; 
		}
	
		select(sockMax + 1, &sockSet, NULL, NULL, NULL);

		if (FD_ISSET(sockfd, &sockSet))
		{
			forClientSockfd = accept(sockfd, (struct sockaddr *)&clientInfo, &addrlen);
			
			if (forClientSockfd < 0) printf("accept error\n");
			else
			{
				printf("-----------------------------------------------------------\n");
				printf("                        New user login                     \n");	
				printf("-----------------------------------------------------------\n");

				SendLoginInfo(forClientSockfd);

				clientTable.clientfds[clientTable.clientNum] = forClientSockfd;
				clientTable.clientNum++;
			}
		}

		for(int i = 0; i < clientTable.clientNum; ++i)
		{
			if (FD_ISSET(clientTable.clientfds[i], &sockSet))
			{
				dup2(clientTable.clientfds[i], STDERR_FILENO);
				dup2(clientTable.clientfds[i], STDOUT_FILENO);
			
				ExeServer2Command(clientTable.clientfds[i]);

				dup2(STDERR_FILENO, clientTable.clientfds[i]);
				dup2(STDOUT_FILENO, clientTable.clientfds[i]);
			}
		}
	}
}

void ExeServer2Command(int clientfd)
{
	int bufferLen = 16000;
	struct command input;

	char* buffer = (char*)calloc(bufferLen, sizeof(char));

	if (clientfd != 0)
	{
		recv(clientfd, buffer, sizeof(char) * bufferLen, 0);

		input = ParseCommand(buffer);
	
		if (input.tokenNumber != 0) Execute(input);

		send(clientfd, "% ", sizeof("% "), 0);

		memset(buffer, 0, bufferLen);
	}

	free(buffer);	
}

void SendLoginInfo(int clientfd)
{
	send(clientfd, "% ", sizeof("% "), 0);
}

void WaitClientCommand(int clientfd, char* inputBuffer, int bufferLen)
{
	send(clientfd, "% ", sizeof("% "), 0);

	recv(clientfd, inputBuffer, sizeof(char) * bufferLen, 0);
}	

int GetClientfd()
{
	return clientfd;
}

