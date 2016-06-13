#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>

#define STREAMLEN 7 
#define HELLO_STRING "Hello\n"

int main(int argc, char **argv) {

	while(1) {

		sleep(2);
	}
	/*
	struct sockaddr_in s_server;
	int sd;
	char buf[STREAMLEN];
	
	if( (sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 ) {
		fprintf(stderr, "Failed to create socket...\n");

		return -1;
	}

	memset(&s_server, 0, sizeof(s_server));
	
	s_server.sin_family = AF_INET	;
	s_server.sin_addr.s_addr = inet_addr(argv[1]);
	s_server.sin_port = htons(atoi(argv[2]));

	connect(sd, (struct sockaddr *)&s_server, sizeof(s_server));

	memset(buf, 0, STREAMLEN);
	memcpy(buf,HELLO_STRING,STREAMLEN);

	send(sd, buf, STREAMLEN, 0);

	fprintf(stdout, "Sent the string over the socket...\n");

	close(sd);
	*/

	return 0;
}
			
