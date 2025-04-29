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
#include <qserver.h>

#define QLEN 5
#define BUFSIZE 4096
pthread_barrier_t question_barrier;
pthread_barrier_t answer_barrier;
sem_t sem;

typedef struct argss
{
	int ssock;
	char name[50];
} arg_t;

int passivesock(char *service, char *protocol, int qlen, int *rport);
void read_questions(char *qfile, ques_t *q[]);

void *playerManager(void *args)
{
	arg_t *_args = (arg_t *)args;
	int cc;
	int ssock = (int)_args->ssock;
	char *padding = malloc(BUFSIZE);
	char *padding1 = malloc(BUFSIZE);
	char *padding2 = malloc(BUFSIZE);

	char *nameAgain = malloc(BUFSIZE);

	char *padding3 = malloc(BUFSIZE);
	char *padding4 = malloc(BUFSIZE);
	char *padding5 = malloc(BUFSIZE);

	strcpy(nameAgain, _args->name);
	char response[BUFSIZE];
	printf("name1=%s\n", nameAgain);
	int currentQ = 0;
	int semVal;
	ques_t *questions;
	
	read_questions("questions.txt", &questions);
	printf("name after read questions=%s\n", nameAgain);

	/* start working for this guy */
	for (;;)
	{
		pthread_barrier_wait(&question_barrier); // wait for everyone
		if (questions[currentQ].qtext == NULL)
		{ // Quiz has ended.
			printf("Quiz has ended.\n");
			break;
		}

		sem_getvalue(&sem, &semVal);
		if (semVal != 1)
		{
			sem_wait(&sem);
		}
		strncpy(response, "QUES|1|1\n", 6);
		sprintf(response + 5, "%ld|", strlen(questions[currentQ].qtext));
		strncat(response, questions[currentQ].qtext, strlen(questions[currentQ].qtext));

		if (write(ssock, response, strlen(response)) < 0)
		{
			/* This guy is dead */
			close(ssock);
			break;
		}
		if ((cc = read(ssock, response, BUFSIZE)) <= 0)
		{
			printf("The client has gone.\n");
			close(ssock);
			break;
		}
		int rightWrong = strncmp((response + 4), questions[currentQ].answer, 1);
		sem_getvalue(&sem, &semVal);
		if ((rightWrong == 0) & (semVal == 1)) // correct answer
		{
			printf("in right%s\n", nameAgain);
			sem_post(&sem);
			strcpy(response, "WIN|");
			strcat(response, nameAgain);
			pthread_barrier_wait(&answer_barrier); // wait for everyone
		}
		else
		{
			pthread_barrier_wait(&answer_barrier); // wait for everyone
		}

		// If nobody was right
		sem_getvalue(&sem, &semVal);
		// printf("name=%s semval=%d\n", name, semVal);
		if (semVal == 1)
		{
			strcpy(response, "WIN|\n\0");
		}

		// Broadcast winner

		if (write(ssock, response, strlen(response)) < 0)
		{
			/* This guy is dead */
			close(ssock);
			break;
		}
		currentQ++;
	}
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
	arg_t *args = calloc(10, sizeof(arg_t));



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
		int inital_score = 0;
		ssock = accept(msock, (struct sockaddr *)&fsin, &alen);

		char init_response[50];
		char *player_name;
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
			pthread_barrier_init(&question_barrier, NULL, limit);
			pthread_barrier_init(&answer_barrier, NULL, limit);
			sem_init(&sem, 0, 1);
			limit--;
			strcpy(args[guest_num].name, player_name);
			args[guest_num].ssock = ssock;
		}
		else if (guest_num <= limit)
		{
			guest_num++;
			write(ssock, "QS|JOIN\r\n", 9);
			read(ssock, init_response, 49);
			strtok(init_response, "|");
			player_name = strtok(NULL, "|");
			strcpy(args[guest_num].name, player_name);
			args[guest_num].ssock = ssock;
		}
		else
		{
			write(ssock, "QS|FULL\r\n", 9);
			continue;
		}
		write(ssock, "WAIT\r\n", 6);
		pthread_create(&thr, NULL, playerManager, (void *)&args[guest_num]);
		pthread_detach(thr);
	}
	pthread_exit(NULL);
}
