#include <sys/socket.h>
#include <stdio.h>

int main(int argc , char *argv[])
{
    int socket_desc;
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
     
    if (socket_desc == -1)
    {
        printf("Could not create socket");
    } else {
        printf("Socket Number: %d\n", socket_desc);
    }
     
    return 0;
}
