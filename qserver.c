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

void *game(void *s)
{
	char buf[BUFSIZE];
	int cc;
	int ssock = (int)s;

	/* start working for this guy */
	/* ECHO what the client says */
	for (;;)
	{
		pthread_barrier_wait(&barrier);
		printf("ask Questions n' such\n");

		if ((cc = read(ssock, buf, BUFSIZE)) <= 0)
		{
			printf("The client has gone.\n");
			close(ssock);
			break;
		}

		/*
		if ((cc = read(ssock, buf, BUFSIZE)) <= 0)
		{
			printf("The client has gone.\n");
			close(ssock);
			break;
		}
		else
		{
			buf[cc] = '\0';
			// printf( "The client says: %s\n", buf );
			if (write(ssock, buf, cc) < 0)
			{
				/* This guy is dead */  /*
				close(ssock);
				break;
			}
		}
		*/
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
	char *limit_response = calloc(50, sizeof(char));
	char *host_name;

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
			guest_num ++ ;
			read(ssock, limit_response, 49);
			strtok(limit_response, "|");
			host_name = strtok(NULL, "|");
			limit = atoi(strtok(NULL, "|"));
			pthread_barrier_init(&barrier, NULL, limit);
			limit -- ;
		}
		else if (guest_num <= limit){
			write(ssock, "QS|JOIN\r\n", 9);
			guest_num ++ ;
		}
		else{
			write(ssock, "QS|FULL\r\n", 9);
			continue;
		}
		write(ssock, "WAIT\r\n", 6);
		pthread_create(&thr, NULL, game, (void *)ssock);
	}
	free(limit_response);
	pthread_exit(NULL);
}
