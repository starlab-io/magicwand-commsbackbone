/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * @file    test_server.c
 * @author  Mark Mason 
 * @date    10 September 2016
 * @version 0.1
 * @brief   A simple test server to test comms.
 */
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

int	socket_desc;
struct	sockaddr_in server;
int	err = 0;

//char	client_message[20];
//char	*hello = "Hello\n";
char	client_message[60];
char    *long_hello = "Hello from build traffic file: message 1 sock 8\n";


void 	init();
double 	iterate_send(int);
void 	tear_down();


int main(int argc , char *argv[])
{
	
	int intervals = 4;
	double x = 0;
	double time_val = 0.0;
	
	
    init();
	
	//iterate send
	for (x = 0; x < intervals; x++)
	{
	    time_val = iterate_send( (int)pow( 10.0 , x ) );
		
		printf("%d    %lf\n", (int)x, time_val);
	}

    tear_down();
	
}


void init(){
	
	
    //memset( client_message, 0, 20);
    memset( client_message, 0, 60);
    strcpy(client_message, long_hello);

    // 1> Call Socket
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
     
    if (socket_desc == -1)
    {
        printf("Could not create socket");
		exit(1);

    } else {
        printf("Socket Number: %d\n", socket_desc);
    }

    // 2> Call connect
    server.sin_addr.s_addr = inet_addr("192.168.0.8");
    server.sin_family = AF_INET;
    server.sin_port = htons(21845);

    if (connect(socket_desc, (struct sockaddr *)&server , sizeof(server)) != 0)
    {
        printf("connect failed. Error/n");
        exit(1);
    }

    printf("Connected\n");
}


double iterate_send(int iterations)
{
	int i = 0;
	struct timeval start;
	struct timeval end;
	struct timeval elapsed;
 	
	gettimeofday(&start, NULL);

        printf("Number of Iterations: %d\n", iterations);

	for ( i = 0; i < iterations; i++)
	{
		send(socket_desc, client_message , strlen(client_message), 0);
	}

	gettimeofday(&end, NULL);

	timersub(&end, &start, &elapsed);

	return elapsed.tv_sec + elapsed.tv_usec/1000000.0;
}


void tear_down()
{
    // 4> Call close
    close(socket_desc);
}
