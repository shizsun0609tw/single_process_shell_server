#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "management.h"
#include "server.h"
#include "process.h"

struct pipeTable numberPipeTable[60];
int isUserPipe = 0;
int isNumberPipe = 0;
char command[16000] = "";

void InitPipeTable(struct pipeTable *numberPipeTable, const int tableSize)
{
	numberPipeTable->tableSize = tableSize;
	numberPipeTable->lineCountTable = (int**)malloc(sizeof(int*) * tableSize);
	
	for(int i = 0; i < tableSize; ++i)
	{
		numberPipeTable->lineCountTable[i] = (int*)malloc(sizeof(int) * 2);
		numberPipeTable->lineCountTable[i][0] = 0;
		numberPipeTable->lineCountTable[i][1] = 0;
	}
}

void FreePipeTable(struct pipeTable *numberPipeTable)
{
	for(int i = numberPipeTable->tableSize - 1; i >= 0; --i)
	{
		if (numberPipeTable->lineCountTable[i][0] != 0) close(numberPipeTable->lineCountTable[i][0]);
		if (numberPipeTable->lineCountTable[i][1] != 0) close(numberPipeTable->lineCountTable[i][1]);
		
		free(numberPipeTable->lineCountTable[i]);
	}
	
	free(numberPipeTable->lineCountTable);
	numberPipeTable->tableSize = 0;
}

void Execute(struct command input, char* buffer)
{
	char** process1 = NULL;
	char* redirection = NULL;
	char* separation = (char*)malloc(sizeof(char));
	int clientfd = GetClientfd();
	int processNum = 0;
	int numberPipe = 0;
	int userPipeIdx = 0;
	int numberPipefd = 0;
	int pastReadFd = 0;
	int isHead = 1;

	isUserPipe = 0;
	isNumberPipe = 0;
	strcpy(command, buffer);

	if (numberPipeTable[clientfd].tableSize == 0)
	{
		InitPipeTable(&(numberPipeTable[clientfd]), 1001);
	}

	process1 = CommandProcessing(&input, &separation, &redirection, &numberPipe, &processNum);
	process1 = UserpipeProcessing(process1, buffer, processNum, &numberPipefd, &userPipeIdx);	
	
	if (userPipeIdx == -1) return;

	UpdateNumberPipe(&(numberPipeTable[clientfd]), &numberPipefd);
	
	while(input.currentCommandNumber != input.tokenNumber)
	{
		char** process2 = CommandProcessing(&input, &separation, &redirection, &numberPipe, &processNum);			
		process2 = UserpipeProcessing(process2, buffer, processNum, &pastReadFd, &userPipeIdx);

		if (userPipeIdx == -1) return;

		if (strcmp(separation, "|") == 0 || isUserPipe == 1) 
		{
			pastReadFd = ExeProcessPipe(process1, pastReadFd, NULL, numberPipefd, isHead);
			numberPipefd = 0;
		}

		isHead = 0;
		free(process1);
		process1 = process2;
	}
	
	if (isUserPipe == 1)
	{
		ExeProcessUserPipe(process1, pastReadFd, numberPipefd, userPipeIdx, isHead);
	}
	else if (numberPipe > 0)
	{
		isNumberPipe = 1;	
		ExeProcessNumberPipe(process1, pastReadFd, &(numberPipeTable[clientfd]), numberPipefd, separation, numberPipe, isHead);
	}
	else 
	{
		ExeProcess(process1, NULL, pastReadFd, NULL, numberPipefd, redirection, isHead, 1);
	}
	
	if (pastReadFd != 0) close(pastReadFd);

	free(input.token);
	free(process1);
}

char** CommandProcessing(struct command *input, char** oSeparation, char** oRedirection, int *oNumberPipe, int *processNum)
{
	char** process = (char**)malloc(sizeof(char*) * (input->tokenNumber + 1));

	for(int count = 0; input->currentCommandNumber < input->tokenNumber; ++count, ++input->currentCommandNumber)
	{
		char* argTemp = input->token[input->currentCommandNumber];
		
		if (strcmp(argTemp, "|") == 0 || IsNumberPipe(argTemp, oNumberPipe))
		{
			if (argTemp[0] == '!') *oSeparation = "!";
			else *oSeparation = "|";
			
			process[count] = NULL;
			++input->currentCommandNumber;		
			*processNum = count;	

			break;
		}
		else if (strcmp(argTemp, ">") == 0)
		{
			++input->currentCommandNumber;
			*oRedirection = input->token[input->currentCommandNumber];
		}
		else
		{	
			process[count] = argTemp;
			process[count + 1] = NULL;
			*processNum = count + 1;
		}
	}

	return process;
}

char** UserpipeProcessing(char** process, char* command, int processNum, int *readfd, int *userPipeIdx)
{
	int index = GetIndexByClientfd(GetClientfd());
	int pipe_idx = 0;
	int* allClientfd = GetAllClientfd();

	for (int i = 0; i < processNum; ++i)	
	{
		if (process[i] == NULL || process[i][0] != '<') continue;

		pipe_idx = atoi(process[i] + 1) - 1;
			
		int flag = GetUserpipe(pipe_idx, readfd);
	
		if (flag == 1)
		{
			for (int client = 0; client < GetClientSize(); ++client)
			{
				if (allClientfd[client] == 0) continue;

				dup2(allClientfd[client], STDOUT_FILENO);
				printf("*** %s (#%d) just received from %s (#%d) by '%s' ***\n", 
					GetClientName(index), index + 1, GetClientName(pipe_idx), pipe_idx + 1, command);
			}
			FreeUserpipefds(pipe_idx);
		}
		else if (flag == -1)
		{
			printf("*** Error: user #%d does not exist yet.***\n", pipe_idx + 1);
			*userPipeIdx = -1;
		}
		else
		{
			printf("*** Error: the pipe #%d->#%d does not exist yet. ***\n", pipe_idx + 1, index + 1);
			*userPipeIdx = -1;
		}

		process[i] = NULL;
	}
	
	for (int i = 0; i < processNum; ++i)
	{
		if (process[i] == NULL || process[i][0] != '>') continue;
		
		pipe_idx = atoi(process[i] + 1) - 1;	
			
		int flag = AddUserpipe(pipe_idx);

		if (flag == 1)
		{
			isUserPipe = 1;
			for (int client = 0; client < GetClientSize(); ++client)
			{
				if (allClientfd[client] == 0) continue;
				
				dup2(allClientfd[client], STDOUT_FILENO);

				printf("*** %s (#%d) just piped '%s' to %s (#%d) ***\n", 
					GetClientName(index), index + 1, command, GetClientName(pipe_idx), pipe_idx + 1);
				*userPipeIdx = pipe_idx;
			}
		}
		else if (flag == -1)
		{
			printf("*** Error: user #%d does not exist yet. ***\n", pipe_idx + 1);
			*userPipeIdx = -1;
		}
		else
		{
			printf("*** Error: the pipe #%d->#%d already exists. ***\n", index + 1, pipe_idx + 1);
			*userPipeIdx = -1;
		}

		process[i] = NULL;
	}

	dup2(GetClientfd(), STDOUT_FILENO);

	return process;			
}

void UpdateNumberPipe(struct pipeTable *numberPipeTable, int* ofd)
{
	for(int i = 1; i < numberPipeTable->tableSize; ++i)
	{	
		numberPipeTable->lineCountTable[i - 1][0] = numberPipeTable->lineCountTable[i][0];
		numberPipeTable->lineCountTable[i - 1][1] = numberPipeTable->lineCountTable[i][1];
		numberPipeTable->lineCountTable[i][0] = 0;
		numberPipeTable->lineCountTable[i][1] = 0;
	}

	if (numberPipeTable->lineCountTable[0][0] != 0) 
	{
		*ofd = numberPipeTable->lineCountTable[0][0];
		numberPipeTable->lineCountTable[0][0] = 0;	
		close(numberPipeTable->lineCountTable[0][1]);
	}
}

int ExeProcessPipe(char** process, int pastReadFd, char* numberPipeSeparation, int numberPipefd, int isHead)
{
	int* pipefds = (int*)malloc(sizeof(int) * 2);
	int readFd;
	

	if (pipe(pipefds) == -1)
	{
		printf("pipe error\n");
	}
 
	ExeProcess(process, pipefds, pastReadFd, numberPipeSeparation, numberPipefd, NULL, isHead, 0);

	readFd = pipefds[0];

	if (pastReadFd != 0) close(pastReadFd);

	free(pipefds);

	return readFd;	
}

void ExeProcessUserPipe(char** process, int pastReadFd, int numberPipefd, int userPipeIdx, int isHead)
{
	ExeProcess(process, GetUserpipefds(userPipeIdx), pastReadFd, NULL, numberPipefd, NULL, isHead, 0); 
}

void ExeProcessNumberPipe(char** process, int pastReadFd, struct pipeTable *numberPipeTable, int numberPipefd, char* separation, int line, int isHead)
{
	int* pipefds = (int*)malloc(sizeof(int) * 2);

	if (numberPipeTable->lineCountTable[line][0] == 0)
	{
		pipe(pipefds);
		
		numberPipeTable->lineCountTable[line][0] = pipefds[0];
		numberPipeTable->lineCountTable[line][1] = pipefds[1];
	}
	else
	{
		pipefds[0] = numberPipeTable->lineCountTable[line][0];
		pipefds[1] = numberPipeTable->lineCountTable[line][1];
	}

	ExeProcess(process, pipefds, pastReadFd, separation, numberPipefd, NULL, isHead, 0);
	
	free(pipefds);
}

void ExeSource(char** process)
{
	FILE *fp = fopen(process[1], "r");
	char* buffer = NULL;
	size_t buffer_size = 0;
	int line_size = 0;

	line_size = getline(&buffer, &buffer_size, fp);

	while(line_size > 0)
	{
		Execute(ParseCommand(buffer), buffer);
		line_size = getline(&buffer, &buffer_size, fp);
	}	
}

void ExeProcess(char** process, int *pipefds, int infd, char* numberPipeSeparation, int numberPipefd, char* redirection, int isHead, int isTail)
{
	if (ExeBuiltInCommand(process) == 1) return; 
	
	pid_t pid = fork();

	while (pid == -1) 
	{
		ExeWait(pid);
		pid = fork();
	}

	switch(pid)
	{
		case -1:	
			printf("fork error\n");
			break;
		case 0:
			ExeChild(process, pipefds, infd, numberPipeSeparation, numberPipefd, redirection, isHead, isTail);
			break;
		default:
			ExeParent(process, pid, pipefds, infd, (numberPipeSeparation != NULL), numberPipefd, isTail);
			break;
	}
}

int ExeBuiltInCommand(char** process)
{
	if (strcmp(process[0], "exit") == 0)
	{
		ExeExit();
		return 1;
	}
	else if (strcmp(process[0], "printenv") == 0)
	{
		ExePrintEnv(process);
		return 1;
	}
	else if (strcmp(process[0], "setenv") == 0)
	{
		ExeSetEnv(process);
		return 1;
	}
	else if (strcmp(process[0], "source") == 0)
	{
		ExeSource(process);
		return 1;
	}
	else if (strcmp(process[0], "name") == 0)
	{
		ExeName(process);
		return 1;
	}
	else if (strcmp(process[0], "who") == 0)
	{
		ExeWho(process);
		return 1;
	}
	else if (strcmp(process[0], "yell") == 0)
	{
		ExeYell(process);
		return 1;
	}
	else if (strcmp(process[0], "tell") == 0)
	{
		ExeTell(process);
		return 1;
	}

	return 0;
}

void ExeName(char** process)
{
	if (process[1] == NULL) return;

	if (SetClientName(process[1]) == 0)
	{
		printf("*** User '%s' already exists. ***\n", process[1]);
		return;
	}

	int clientSize = GetClientSize();
	int port = 0;
	int* allClientfd = GetAllClientfd();
	int index = GetIndexByClientfd(GetClientfd());
	char ip[20];
	struct sockaddr_in info = GetClientInfo(index);

	inet_ntop(AF_INET, &info.sin_addr, ip, sizeof(struct sockaddr));
	port = ntohs(info.sin_port);

	for (int i = 0; i < clientSize; ++i)
	{
		if (allClientfd[i] == 0) continue;

		dup2(allClientfd[i], STDOUT_FILENO);
		printf("*** User from %s:%d is named '%s'. ***\n", ip, port, process[1]);
	}

	dup2(GetClientfd(), STDOUT_FILENO);
}

void ExeWho(char** process)
{
	printf("<ID>	<nickname>	<IP:port>	<indicate me>\n");

	int clientSize = GetClientSize();
	int port = 0;
	int* allClientfd = GetAllClientfd();
	struct sockaddr_in clientInfo;
	char* clientName = "";
	char ip[20];

	for (int i = 0; i < clientSize; ++i)
	{
		if (allClientfd[i] == 0) continue;

		clientName = GetClientName(i);
		clientInfo = GetClientInfo(i);	
		inet_ntop(AF_INET, &clientInfo.sin_addr, ip, sizeof(struct sockaddr));
		port = ntohs(clientInfo.sin_port);	

		if (allClientfd[i] == GetClientfd())
		{
			printf("%d	%s	%s:%d	<-me\n", i + 1, clientName,ip, port);
		}
		else
		{
			printf("%d	%s	%s:%d\n", i + 1, clientName, ip, port);
		}
	}
}

void ExeYell(char** process)
{
	if (process[1] == NULL) return;
	
	int clientSize = GetClientSize();
	int index = GetIndexByClientfd(GetClientfd());
	int len = strspn(command, "yell");
	int* allClientfd = GetAllClientfd();

	for (int i = len; i < strlen(command); ++i)
	{
		if (command[i] != ' ') break;
		len = i + 1;
	}

	for (int i = 0; i < clientSize; ++i)
	{
		if (allClientfd[i] == 0) continue;

		dup2(allClientfd[i], STDOUT_FILENO);
		printf("*** %s yelled ***: %s\n", GetClientName(index), command + len);
	}
	
	dup2(GetClientfd(), STDOUT_FILENO);
}

void ExeTell(char** process)
{
	if (process[1] == NULL || process[2] == NULL) return;

	int index = atoi(process[1]) - 1;
	int len = strspn(command, "tell");
	int* allClientfd = GetAllClientfd();
	char buffer[30] = "tell";
	char* space = " ";

	for (int i = len; i < strlen(command); ++i)
	{
		if (command[i] != ' ') break;
		len = i + 1;
		strcat(buffer, space);
	}

	strcat(buffer, process[1]);
	len = strspn(command, buffer);

	for (int i = len; i < strlen(command); ++i)
	{
		if (command[i] != ' ') break;
		len = i + 1;
	}

	if (allClientfd[index] == 0) 
	{
		printf("*** Error: user #%d does not exist yet. ***\n", atoi(process[1]));
	}
	else
	{
		dup2(allClientfd[index], STDOUT_FILENO);

		index = GetIndexByClientfd(GetClientfd());
	
		printf("*** %s told you ***: %s\n", GetClientName(index), command + len);
	
		dup2(GetClientfd(), STDOUT_FILENO);
	}
}

void ExeExit(char** process)
{
	int clientfd = GetClientfd();	
	int serverNum = GetServerNum();

	if (clientfd > 0) 
	{
		FreePipeTable(&(numberPipeTable[clientfd]));
		close(clientfd);
	}

	if (serverNum == 1) exit(EXIT_SUCCESS);
	else if (serverNum == 2) ExeExitService();
}

void ExeSetEnv(char** process)
{	
	if (process[1] == NULL || process[2] == NULL) return;

	if (GetServerNum() == 2) SetEnv(process[1], process[2]);

	setenv(process[1], process[2], 1);	
}

void ExePrintEnv(char** process)
{
	char* env;
	
	if (process[1] != NULL)
	{
		env = getenv(process[1]);
		if (env != NULL) printf("%s\n", env);
	}
}

void ExeChild(char** process, int *pipefds, int infd, char* numberPipeSeparation, int numberPipefd, char* redirection, int isHead, int isTail)
{
	int isPipe = (isHead && isTail ? 0 : 1);
	int isRedirection = (redirection != NULL ? 1 : 0);
			
	if (numberPipefd > 0) ExeNumberPipe(numberPipefd);
	
	if (isRedirection == 1) ExeRedirection(pipefds, infd, redirection);

	if (isPipe == 0 && numberPipefd > 0) close(numberPipefd);
	
	if (isPipe == 0 || isRedirection == 1) DoExecvp(process[0], process);
	
	if (isPipe == 1) ExePipe(process, pipefds, infd, numberPipeSeparation, isHead, isTail);
}

void ExeParent(char** process, pid_t pid, int *pipefds, int infd, int isNumberPipe, int numberPipefd, int isTail)
{
	AddToPidPool(pid);

	if (numberPipefd > 0) close(numberPipefd);
	if (pipefds != NULL && isNumberPipe == 0) close(pipefds[1]);
	if (infd > 0) close(infd);
	
	if (isTail == 0 || isUserPipe == 1 || isNumberPipe == 1) return;
	else ExeWait(pid);
}

void ExeRedirection(int *pipefds, int infd, char* redirection)
{
	int fd = open(redirection, O_TRUNC | O_CREAT | O_WRONLY, 0644); 

	dup2(fd, STDOUT_FILENO);
	
	if (infd > 0) 
	{
		dup2(infd, STDIN_FILENO);
		close(infd);
	}

	if (pipefds != NULL)
	{
		close(pipefds[0]);
		close(pipefds[1]);
	}

	close(fd);
}

void ExePipe(char** process, int *pipefds, int infd, char* numberPipeSeparation, int isHead, int isTail)
{	
	if (isHead)
	{
		ExePipeHead(pipefds, numberPipeSeparation, infd);
	}
	else if (isTail)
	{	
		ExePipeTail(pipefds, infd);
	}
	else
	{
		ExePipeMiddle(pipefds, numberPipeSeparation, infd);
	}
	
	DoExecvp(process[0], process);
}

void ExeNumberPipe(int numberPipefd)
{
	dup2(numberPipefd, STDIN_FILENO);
	close(numberPipefd);
}

void ExePipeHead(int *pipefds, char* numberPipeSeparation, int infd)
{
	close(pipefds[0]);
	
	dup2(pipefds[1], STDOUT_FILENO);

	if (numberPipeSeparation != NULL && numberPipeSeparation[0] == '!') 
	{
		dup2(pipefds[1], STDERR_FILENO);
	}
	close(pipefds[1]);
}

void ExePipeTail(int *pipefds, int infd)
{
	if (pipefds != NULL)
	{
		if (pipefds[0] != 0) 
		{
			close(pipefds[0]);
		}
		if (pipefds[1] != 0)
		{
			close(pipefds[1]);
		}
	}
	dup2(infd, STDIN_FILENO);
	close(infd);
}

void ExePipeMiddle(int *pipefds, char* numberPipeSeparation, int infd)
{
	if (pipefds[0] != 0) close(pipefds[0]);
	dup2(infd, STDIN_FILENO);
	dup2(pipefds[1], STDOUT_FILENO);
	if (numberPipeSeparation != NULL && numberPipeSeparation[0] == '!')
	{
		dup2(pipefds[1], STDERR_FILENO);
	}
	close(infd);
	close(pipefds[1]);
}

void ExeWait(pid_t pid)
{
	int status;
	int waitPID;

	while(1)
	{
		int label = 0;

		for (int i = 0; i < GetPidPoolSize(); ++i)
		{
			waitPID = waitpid(GetPid(i), &status, WNOHANG);

			if (waitPID == GetPid(i))
			{
				ClearPid(waitPID);
			}

			if (waitPID == pid) return;

			if (pid == GetPid(i)) label = 1;
		}

		if (label == 0) return;
	}
}

void DoExecvp(char* process, char** arg)
{
	if (execvp(process, arg) == -1)
	{
		fprintf(stderr, "Unknown command: [%s].\n", process);
		exit(EXIT_FAILURE);
	}
}
