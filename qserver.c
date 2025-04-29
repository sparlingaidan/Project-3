#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>

#define QLEN 5
#define BUFSIZE 4096
pthread_barrier_t question_barrier;
pthread_barrier_t answer_barrier;
sem_t sem;

typedef struct playerState
{
	int score;
	char *name;
} playerState_t;

typedef struct args
{
	int *ssock;
	char *name;
	char *qwin;
	playerState_t *playersState;
} arg_t;

int passivesock(char *service, char *protocol, int qlen, int *rport);

char *getCorrectAns(int questionNumber, char *questions)
{
	char *beginingOfQ = strchr(questions, questionNumber + 48);
	char *correctAns = calloc(250, sizeof(char));
	char *nextQuestion = strchr(questions, questionNumber + 49);
	char *questionEnd;
	if (nextQuestion == NULL)
	{
		questionEnd = beginingOfQ + strlen(beginingOfQ) - 3;
	}
	else
	{
		questionEnd = nextQuestion - 3;
	}
	strncpy(correctAns, questionEnd, 1);
	return correctAns;
}

char *questionBuilder(int questionNumber, char *questions)
{
	char *beginingOfQ = strchr(questions, questionNumber + 48);
	char *question = calloc(BUFSIZE, sizeof(char));
	strcpy(question, "QUES|\0");
	char *nextQuestion = strchr(questions, questionNumber + 49);
	char *questionEnd;
	if (nextQuestion == NULL)
	{
		questionEnd = beginingOfQ + strlen(beginingOfQ) - 4;
	}
	else
	{
		questionEnd = nextQuestion - 4;
	}
	int sizeOfQuestion = questionEnd - beginingOfQ;
	sprintf(question + 5, "%d|", sizeOfQuestion);
	strncat(question, beginingOfQ, sizeOfQuestion);
	return question;
}

void *playerManager(void *args)
{
	arg_t *_args = (arg_t *)args;
	int cc;
	int ssock = (int)*_args->ssock;
	char *questions = calloc(BUFSIZE, sizeof(char));
	char *response = calloc(BUFSIZE, sizeof(char));
	FILE *fptr = fopen("questions.txt", "r");
	fread(questions, sizeof(char), BUFSIZE - 1, fptr);
	char *num = calloc(5, sizeof(char));
	int currentQ = 1;
	int semVal;

	/* start working for this guy */
	/* ECHO what the client says */
	for (;;)
	{
		pthread_barrier_wait(&question_barrier); // wait for everyone
		char *question = questionBuilder(currentQ, questions);
		int score;

		if (write(ssock, question, strlen(question)) < 0)
		{
			/* This guy is dead */
			close(ssock);
			break;
		}
		free(question);
		if ((cc = read(ssock, response, BUFSIZE)) <= 0)
		{
			printf("The client has gone.\n");
			close(ssock);
			break;
		}
		char *correctAnswer = getCorrectAns(currentQ, questions);
		int rightWrong = strncmp((response + 4), correctAnswer, 1);
		sem_getvalue(&sem, &semVal);
		if ((rightWrong == 0) & (semVal == 1))
		{
			sem_post(&sem);
			_args->playersState->score++;
			strcpy(_args->qwin + 4, _args->name);
			pthread_barrier_wait(&answer_barrier); // wait for everyone
		}
		else
		{
			pthread_barrier_wait(&answer_barrier); // wait for everyone
		}

		// If nobody was right
		sem_getvalue(&sem, &semVal);
		if (semVal == 1)
		{
			strncpy(_args->qwin + 4, "\n\0", 2);
		}
		else //someone was right reset for next round.
		{
			sem_wait(&sem);
		}

		// Broadcast winner
		if (write(ssock, _args->qwin, strlen(_args->qwin)) < 0)
		{
			/* This guy is dead */
			close(ssock);
			break;
		}
	}
	free(questions);
	free(num);
	pthread_exit(NULL);
}

/*
 */
int main(int argc, char *argv[])
{
	char *service;
	struct sockaddr_in fsin;
	int alen = sizeof(fsin);
	int msock;
	int ssock;
	int rport = 0;
	int guest_num = 0;
	int limit = QLEN;
	char *init_response = calloc(50, sizeof(char));
	char *qwinner = calloc(50, sizeof(char));
	strncpy(qwinner, "WIN|\n", 5);
	char *player_name;
	playerState_t *gameState;

	switch (argc)
	{
	case 1:
		// No args? let the OS choose a port and tell the user
		rport = 1;
		break;
	case 2:
		// User provides a port? then use it
		service = argv[1];
		break;
	default:
		fprintf(stderr, "usage: server [port]\n");
		exit(-1);
	}

	// Call the function that sets everything up
	msock = passivesock(service, "tcp", QLEN, &rport);

	if (rport)
	{
		// Tell the user the selected port number
		printf("server: port %d\n", rport);
		fflush(stdout);
	}

	for (;;)
	{
		int ssock;
		pthread_t thr;
		arg_t args;
		int inital_score = 0;
		args.qwin = qwinner; // same for everyone.

		ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
		if (ssock < 0)
		{
			fprintf(stderr, "accept: %s\n", strerror(errno));
			break;
		}

		printf("A client has arrived for echoes - serving on fd %d.\n", ssock);
		fflush(stdout);

		if (guest_num == 0)
		{
			write(ssock, "QS|ADMIN\r\n", 10);
			guest_num++;
			read(ssock, init_response, 49);
			strtok(init_response, "|");
			player_name = strtok(NULL, "|");
			limit = atoi(strtok(NULL, "|")); // limit is the number of players
			gameState = calloc(limit, sizeof(playerState_t));
			pthread_barrier_init(&question_barrier, NULL, limit);
			pthread_barrier_init(&answer_barrier, NULL, limit);
			sem_init(&sem, 0, 1);
			limit--;
			args.ssock = &ssock;
			args.playersState = gameState;
			args.playersState->score = 0;
			player_name = strdup(player_name);
			args.playersState->name = player_name;
			args.name = player_name;
		}
		else if (guest_num <= limit)
		{
			write(ssock, "QS|JOIN\r\n", 9);
			read(ssock, init_response, 49);
			strtok(init_response, "|");
			player_name = strtok(NULL, "|");
			args.ssock = &ssock;
			args.playersState = &gameState[guest_num];
			args.playersState->score = 0;
			player_name = strdup(player_name);
			args.playersState->name = player_name;
			args.name = player_name;
			guest_num++;
		}
		else
		{
			write(ssock, "QS|FULL\r\n", 9);
			continue;
		}
		write(ssock, "WAIT\r\n", 6);
		pthread_create(&thr, NULL, playerManager, (void *)&args);
	}
	free(init_response);
	pthread_exit(NULL);
}
