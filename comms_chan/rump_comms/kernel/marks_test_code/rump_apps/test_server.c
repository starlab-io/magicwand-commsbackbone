/* COMPILE: gcc -lpthread -o elmer_daemon_new elmer_daemon_new.c
 *
 * USAGE: (On Each "generals" vm) ./elmer_daemon_new <model directory> <port to listen on>
 * Example: ./elmer_daemon_new /home/jonathankline/models/iomanager/0 16200
 *
 * Note: If you run 4 instances of elmer as the same user on the same host for testing, funny things happen (looks like they delete each others sockets)
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#define STREAMLEN 10

#include <rump/rump.h>
#include <rump/netconfig.h>
#include <rump/rump_syscalls.h>

int main(int argc, char **argv) {

	struct sockaddr_in s_server;
	struct sockaddr_in s_client;
	int res;
	int sd;
	int cd;
	int drecv;;
	char buf[STREAMLEN];

	if( (sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 ) {
		fprintf(stderr, "Failed to create socket...\n");

		return -1;
	}

	memset(&s_server, 0, sizeof(s_server));
	
	s_server.sin_family = AF_INET	;
	s_server.sin_addr.s_addr  = htonl(INADDR_ANY);
	s_server.sin_port = htons(atoi(argv[1]));

	if( (res = bind(sd, (struct sockaddr *)&s_server, sizeof(s_server))) < 0 ) {
		fprintf(stderr, "Failed to bind socket.. (%i - %s)\n", res, strerror(errno));

		return -1;
	}

	if( listen(sd, 1) < 0 ) {
		fprintf(stderr, "Failed to listen...\n");

		return -1;
	}

	while(1) {
		unsigned int clen = sizeof(s_client);

		cd = accept(sd, (struct sockaddr *)&s_client, &clen);

		fprintf(stdout, "Accepted connection.\n");

		drecv = recv(cd, &buf, STREAMLEN, 0);

		fprintf(stdout, "Received Data: %s\n", buf);

		break;
	}

	close(cd);
	close(sd);

	return 0;
}
