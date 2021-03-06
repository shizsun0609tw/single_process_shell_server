#include "server.h"

#include "parser.h"
#include "process.h"
#include "management.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int serverNum = 0;
int clientfd = 0;
struct serviceTable clientTable = {
	.clientMax = 60,
	.clientSize = 0
};
struct userpipeTable pipeTable = {
	.pipeNum = 0
};	

int ExeServer1(int port)
{
	int sockfd = 0, forClientSockfd = 0;

	if (serverNum == 0)
	{
		serverNum = 1;
	}

	printf("Start server1 at port:%d\n", port);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

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

		pid_t pid = fork();
		
		if (pid != 0)
		{
			close(forClientSockfd);

			int waitPID, status;

			while(1)
			{
				waitPID = waitpid(pid, &status, WNOHANG);
				
				if (waitPID == pid) break;
			}				
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

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (serverNum == 0)
	{
		serverNum = 2;
		printf("Start server2 at port:%d\n", port);
	}

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

		for(int i = 0; i < clientTable.clientSize; ++i)
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
				SendLoginInfo(forClientSockfd, clientInfo);

				for(int i = 0; i <= clientTable.clientSize; ++i)
				{
					if (clientTable.clientfds[i] == 0)
					{
						clientTable.clientfds[i] = forClientSockfd;
						clientTable.clientInfo[i] = clientInfo;
						strcpy(clientTable.clientName[i], "(no name)");
						
						if (i == clientTable.clientSize) clientTable.clientSize++;
							
						break;
					}
				}
			}
		}

		for(int i = 0; i < clientTable.clientSize; ++i)
		{
			if (clientTable.clientfds[i] == 0) continue;

			if (FD_ISSET(clientTable.clientfds[i], &sockSet))
			{	
				clientfd = clientTable.clientfds[i];	

				SetClientEnv();
	
				int fd_err = dup(STDERR_FILENO);
				int fd_out = dup(STDOUT_FILENO);

				dup2(clientTable.clientfds[i], STDERR_FILENO);
				dup2(clientTable.clientfds[i], STDOUT_FILENO);
			
				ExeServer2Command();

				dup2(fd_err, STDERR_FILENO);
				dup2(fd_out, STDOUT_FILENO);

				CleanClientEnv();

				close(fd_err);
				close(fd_out);
			}
		}

		PrintUserpipeOnServer();
	}
}

void PrintUserpipeOnServer()
{
	printf("------------------pipe list--------------------------\n");

	printf("client:%d\n", GetIndexByClientfd(clientfd) + 1);

	for (int i = 0; i < pipeTable.pipeNum; ++i)
	{
		printf("pipe_idx:%d,  %d to %d, fd[0]:%d, fd[1]:%d\n",
			i, pipeTable.inIndex[i] + 1, pipeTable.outIndex[i] + 1, pipeTable.inPipe[i], pipeTable.outPipe[i]);
	}
}

void ExeServer2Command()
{
	int bufferLen = 16000;
	struct command input;

	char* buffer = (char*)calloc(bufferLen, sizeof(char));

	if (clientfd != 0)
	{
		recv(clientfd, buffer, sizeof(char) * bufferLen, 0);

		char tempBuffer[16000] = "";
		
		strcpy(tempBuffer, buffer);

		input = ParseCommand(tempBuffer);	

		buffer[strcspn(buffer, "\r\n")] = 0;

		if (input.tokenNumber != 0) Execute(input, buffer);

		send(clientfd, "% ", strlen("% "), 0);

		memset(buffer, 0, bufferLen);
	}

	free(buffer);	
}

void ExeExitService()
{
	int index = GetIndexByClientfd(clientfd);

	CleanClientEnv();
	
	clientTable.clientfds[index] = 0;
	clientTable.clientEnv[index].envNum = 0;

	for (int i = 0; i < pipeTable.pipeNum; ++i)
	{
		if (pipeTable.inIndex[i] == index || pipeTable.outIndex[i] == index)
		{
			if (pipeTable.inPipe[i] != 0) close(pipeTable.inPipe[i]);
			if (pipeTable.outPipe[i] != 0) close(pipeTable.outPipe[i]);
			
			pipeTable.inPipe[i] = 0;
			pipeTable.outPipe[i] = 0;
			pipeTable.inIndex[i] = 0;
			pipeTable.outIndex[i] = 0;
		}
	}

	for (int i = 0; i < clientTable.clientSize; ++i)
	{
		if (clientTable.clientfds[i] != 0)
		{
			dup2(clientTable.clientfds[i], STDOUT_FILENO);
			printf("*** User '%s' left. ***\n", clientTable.clientName[index]);
		}
	}
}

void SendLoginInfo(int clientfd, struct sockaddr_in clientInfo)
{
	char ipv4[20];

	inet_ntop(AF_INET, &clientInfo.sin_addr, ipv4, sizeof(struct sockaddr));

	int fd_old = dup(STDOUT_FILENO);

	dup2(clientfd, STDOUT_FILENO);

	printf("****************************************\n"
	       "** Welcome to the information server. **\n" 
	       "****************************************\n");

	printf("*** User '(no name)' entered from %s:%d. ***\n", ipv4, ntohs(clientInfo.sin_port));
	
	send(clientfd, "% ", strlen("% "), 0);

	for(int i = 0; i < clientTable.clientSize; ++i)
	{
		if (clientTable.clientfds[i] == clientfd) continue;
		if (clientTable.clientfds[i] == 0) continue;			

		dup2(clientTable.clientfds[i], STDOUT_FILENO);
		
		printf("*** User '(no name)' entered from %s:%d. ***\n", ipv4, ntohs(clientInfo.sin_port));
	}

	dup2(fd_old, STDOUT_FILENO);	
	close(fd_old);
}

void WaitClientCommand(int clientfd, char* inputBuffer, int bufferLen)
{
	send(clientfd, "% ", strlen("% "), 0);

	recv(clientfd, inputBuffer, sizeof(char) * bufferLen, 0);
}	

void FreeUserpipefds(int pipe_idx)
{
	for (int i = 0; i < pipeTable.pipeNum; ++i)
	{
		if (pipeTable.inIndex[i] == pipe_idx && pipeTable.outIndex[i] == GetIndexByClientfd(clientfd))
		{	
			pipeTable.inPipe[i] = 0;
			pipeTable.outPipe[i] = 0;
			pipeTable.inIndex[i] = 0;
			pipeTable.outIndex[i] = 0;

			return;
		}
	}
}

int SetClientName(char* name)
{
	for(int i = 0; i < clientTable.clientSize; ++i)
	{
		if (strcmp(clientTable.clientName[i], name) == 0) return 0;
	}

	strcpy(clientTable.clientName[GetIndexByClientfd(clientfd)], name);
	return 1;
}

void SetClientEnv()
{
	int index = GetIndexByClientfd(clientfd);

	if (index < 0) return;

	for (int i = 0; i < clientTable.clientEnv[index].envNum; ++i)
	{
		char* env;
		char buffer[1000] = "";
	
		strcpy(buffer, clientTable.clientEnv[index].envValue[i]);
		env = getenv(clientTable.clientEnv[index].envName[i]);
		strcpy(clientTable.clientEnv[index].envValue[i], env);
		setenv(clientTable.clientEnv[index].envName[i], buffer, 1);
	}
}

void CleanClientEnv()
{
	SetClientEnv();
}

void SetEnv(char* envName, char* envValue)
{
	int index = GetIndexByClientfd(clientfd);
	int label = 0;

	for (int i = 0; i < clientTable.clientEnv[index].envNum; ++i)
	{
		if (strcmp(clientTable.clientEnv[index].envName[i], envName) == 0)	
		{
			label = 1;		
		}
	}

	if (label == 0)
	{
		char* env;
		
		strcpy(clientTable.clientEnv[index].envName[clientTable.clientEnv[index].envNum], envName);
		env = getenv(envName);
		strcpy(clientTable.clientEnv[index].envValue[clientTable.clientEnv[index].envNum], env);
		clientTable.clientEnv[index].envNum++;
	}
}

int GetServerNum()
{
	return serverNum;
}

int GetClientfd()
{
	return clientfd;
}

int GetClientSize()
{
	return clientTable.clientSize;
}

int GetIndexByClientfd(int fd)
{
	for(int i = 0; i < clientTable.clientSize; ++i)
	{
		if (clientTable.clientfds[i] == fd) return i;
	}

	return -1;
}

int* GetAllClientfd()
{
	return clientTable.clientfds;
}

char* GetClientName(int clientNum)
{
	return clientTable.clientName[clientNum];
}

int* GetUserpipefds(int pipe_idx)
{
	int *pipefd = (int*)malloc(sizeof(int) * 2);

	for (int i = 0; i < pipeTable.pipeNum; ++i)
	{
		if (pipeTable.inIndex[i] == GetIndexByClientfd(clientfd) && pipeTable.outIndex[i] == pipe_idx)
		{
			pipefd[0] = pipeTable.inPipe[i];
			pipefd[1] = pipeTable.outPipe[i];

			return pipefd;
		}
	}

	return pipefd;
}

int GetUserpipe(int pipe_idx, int *readfd)
{
	if (clientTable.clientfds[pipe_idx] == 0) return -1;

	for (int i = 0; i < pipeTable.pipeNum; ++i)
	{
		if (pipeTable.inIndex[i] == pipe_idx && pipeTable.outIndex[i] == GetIndexByClientfd(clientfd))
		{
			*readfd = pipeTable.inPipe[i];

			return 1;
		}
	}

	return 0;	
}

int AddUserpipe(int pipe_idx)
{
	if (clientTable.clientfds[pipe_idx] == 0) return -1;

	for (int i = 0; i < pipeTable.pipeNum; ++i)
	{
		if (pipeTable.inIndex[i] == GetIndexByClientfd(clientfd) && pipeTable.outIndex[i] == pipe_idx) return 0;
	}
	
	int pipefds[2];

	if (pipe(pipefds) == -1)
	{
		printf("pipe error\n");
	}		
	
	for (int i = 0; i <= pipeTable.pipeNum; ++i)
	{
		if (pipeTable.inIndex[i] == 0 && pipeTable.outIndex[i] == 0)
		{
			pipeTable.inIndex[i] = GetIndexByClientfd(clientfd);
			pipeTable.outIndex[i] = pipe_idx;
			pipeTable.inPipe[i] = pipefds[0];
			pipeTable.outPipe[i] = pipefds[1];

			if (i == pipeTable.pipeNum)
			{
				pipeTable.pipeNum++;
				break;
			}
		
			break;
		}
	}	

	return 1;	
}

struct sockaddr_in GetClientInfo(int clientNum)
{
	return clientTable.clientInfo[clientNum];
}
