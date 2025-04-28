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

#define QLEN 5
#define BUFSIZE 4096
pthread_barrier_t barrier;

int passivesock(char *service, char *protocol, int qlen, int *rport);

char *getCorrectAns(int questionNumber, char *questions)
{
	char *beginingOfQ = strchr(questions, questionNumber + 48);
	char *correctAns = calloc(250, sizeof(char));
	char *nextQuestion = strchr(questions, questionNumber + 49);
	char *questionEnd;
	if (nextQuestion == NULL){
		questionEnd = beginingOfQ + strlen(beginingOfQ) - 3;
	}
	else{
		questionEnd = nextQuestion - 3;
	}
	strncpy(correctAns, questionEnd, 1);
	return correctAns;
}

char *questionBuilder(int questionNumber, char *questions)
{
	char *beginingOfQ = strchr(questions, questionNumber + 48);
	char *question = calloc(BUFSIZE, sizeof(char));
	strcpy(question,"QUES|\0");
	char *nextQuestion = strchr(questions, questionNumber + 49);
	char *questionEnd;
	if (nextQuestion == NULL){
		questionEnd = beginingOfQ + strlen(beginingOfQ) - 4;
	}
	else{
		questionEnd = nextQuestion - 4;
	}
	int sizeOfQuestion = questionEnd - beginingOfQ;
	sprintf(question + 5, "%d|", sizeOfQuestion);
	strncat(question, beginingOfQ, sizeOfQuestion);
	return question;
}

void *playerManager(void *s)
{
	int cc;
	int ssock = (int)s;
	char *questions = calloc(BUFSIZE, sizeof(char));
	char *response = calloc(BUFSIZE, sizeof(char));
	FILE *fptr = fopen("questions.txt", "r");
	fread(questions,sizeof(char), BUFSIZE - 1, fptr);
	char *num = calloc(5, sizeof(char));
	int currentQ = 1;

	/* start working for this guy */
	/* ECHO what the client says */
	for (;;)
	{
		pthread_barrier_wait(&barrier); // wiat for everyone
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
		if (rightWrong == 0){
			score = 1;
		}
		else{
			score = 0;
		}
		sleep(120);
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
	char *player_name;

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
			limit = atoi(strtok(NULL, "|"));
			pthread_barrier_init(&barrier, NULL, limit);
			limit--;
		}
		else if (guest_num <= limit)
		{
			write(ssock, "QS|JOIN\r\n", 9);
			guest_num++;
			read(ssock, init_response, 49);
			strtok(init_response, "|");
			player_name = strtok(NULL, "|");
		}
		else
		{
			write(ssock, "QS|FULL\r\n", 9);
			continue;
		}
		write(ssock, "WAIT\r\n", 6);
		pthread_create(&thr, NULL, playerManager, (void *)ssock);
	}
	free(init_response);
	pthread_exit(NULL);
}
