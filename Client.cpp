#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int sock_fd = -1;
    struct sockaddr_in address;
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(8192);
    int connfd = connect(sock_fd, (struct sockaddr *)&address, sizeof(address));
    
    if(connfd == -1)
    {
        perror("连接服务器失败:");
        exit(1);
    }

    char ch = '\0';
    write(sock_fd, &ch, 1);
    read(sock_fd, &ch, 1);
    printf("char from server = %c\n", ch);
    close(sock_fd);
    exit(0);
}