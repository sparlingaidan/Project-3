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
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
sem_t sem;
sem_t gameEnd;
pthread_mutex_t lock;
char winner[BUFSIZE];
int winnerList[50] = {0};
int correct = 0;
char playerList[BUFSIZE];
ques_t *questions[25];
int numplayers;
int numactiveplayers;
int waiting = 0;
int through = 0;

typedef struct argss
{
	int ssock;
	char name[200];
	int playerNum;
} arg_t;

int passivesock(char *service, char *protocol, int qlen, int *rport);
void read_questions(char *qfile, ques_t *q[]);

char *getscores()
{
	char *results = calloc(50, sizeof(char));
	strncpy(results, "RESULTS|1|1\n", 8);
	int offset = 8;
	char *localPlayerList = strdup(playerList);
	char *playerName = strtok(localPlayerList, "|");
	for (int i = 0; i < numplayers; i++)
	{
		int score = winnerList[i];
		offset += sprintf(results + offset, "%s|%d|", playerName, score);
		playerName = strtok(NULL, "|");
	}
	return results;
}

void barrier(){
	// wait for everyone
	pthread_mutex_lock(&mutex);
	waiting++;
	if (waiting == numactiveplayers)
	{
		pthread_cond_broadcast(&cond);
	}
	while (waiting != numactiveplayers)
	{
		pthread_cond_wait(&cond, &mutex);
	}
	pthread_mutex_unlock(&mutex);
	pthread_mutex_lock(&lock);
	through++;
	if (through == numactiveplayers)
	{
		waiting = 0;
		through = 0;
	}
	pthread_mutex_unlock(&lock);
}

void *playerManager(void *args)
{
	arg_t *_args = (arg_t *)args;
	int cc;
	int playerNumber = (int)_args->playerNum;
	int ssock = (int)_args->ssock;
	char *nameAgain = malloc(BUFSIZE);
	strcpy(nameAgain, _args->name);
	pthread_mutex_lock(&lock);
	strcat(playerList, nameAgain);
	strcat(playerList, "|");
	pthread_mutex_unlock(&lock);
	char response[BUFSIZE];
	int currentQ = 0;
	int semVal;

	/* start working for this guy */
	for (;;)
	{
		barrier();

		if (questions[currentQ]->qtext == NULL)
		{ // Quiz has ended.
			pthread_mutex_lock(&lock);
			char *results = getscores();
			pthread_mutex_unlock(&lock);
			write(ssock, results, strlen(results));
			printf("Quiz has ended.\n");
			sem_post(&gameEnd);
			break;
		}

		sem_getvalue(&sem, &semVal);
		if (semVal != 1)
		{
			sem_wait(&sem);
		}
		strncpy(response, "QUES|1|1\n", 6);
		sprintf(response + 5, "%ld|", strlen(questions[currentQ]->qtext));
		strncat(response, questions[currentQ]->qtext, strlen(questions[currentQ]->qtext));

		if (write(ssock, response, strlen(response)) < 0)
		{
			/* This guy is dead */
			close(ssock);
			break;
		}
		if ((cc = read(ssock, response, BUFSIZE)) <= 0)
		{
			printf("The client has gone.\n");
			pthread_mutex_lock(&lock);
			numactiveplayers--;
			pthread_mutex_unlock(&lock);
			close(ssock);
			break;
		}
		int rightWrong = strncmp((response + 4), questions[currentQ]->answer, 1);

		sem_getvalue(&sem, &semVal);
		if ((rightWrong == 0) & (semVal == 1)) // correct answer
		{
			sem_post(&sem);
			strcpy(winner, "WIN|");
			strcat(winner, nameAgain);
			winnerList[playerNumber - 1] = winnerList[playerNumber - 1] + 2;
			barrier();

		}
		else if (strncmp((response + 4), "n", 1) == 0)
		{
			barrier();
		}
		else
		{
			winnerList[playerNumber - 1] = winnerList[playerNumber - 1] + 1;
			barrier();
		}

		// If nobody was right
		sem_getvalue(&sem, &semVal);
		// printf("name=%s semval=%d\n", name, semVal);
		if (semVal == 1)
		{
			strcpy(winner, "WIN|\n\0");
		}

		// Broadcast winner

		if (write(ssock, winner, strlen(response)) < 0)
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
int gametime(int argc, char *argv[])
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
	read_questions(argv[1], questions);
	pthread_mutex_init(&lock, NULL);
	sem_init(&gameEnd, 0, 0);


	switch (argc)
	{
	case 2:
		// No args? let the OS choose a port and tell the user
		rport = 1;
		break;
	case 3:
		// User provides a port? then use it
		service = argv[2];
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

		if (guest_num > limit){
			sem_wait(&gameEnd);
			break;
		}
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
			limit = atoi(strtok(NULL, "\r\n")); // limit is the number of players
			numplayers = limit;
			numactiveplayers = limit;
			sem_init(&sem, 0, 1);
			limit--;
			strcpy(args[guest_num].name, player_name);
			args[guest_num].ssock = ssock;
			args[guest_num].playerNum = guest_num;
		}
		else if (guest_num <= limit)
		{
			guest_num++;
			write(ssock, "QS|JOIN\r\n", 9);
			read(ssock, init_response, 49);
			strtok(init_response, "|");
			player_name = strtok(NULL, "\r\n");
			strcpy(args[guest_num].name, player_name);
			args[guest_num].ssock = ssock;
			args[guest_num].playerNum = guest_num;
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
	guest_num = 0;
	memset(winnerList, 0, sizeof(winnerList));
	free(args);
	return 1;
}


int main(int argc, char *argv[])
{
	for(;;){
		gametime(argc, argv);
	}
}