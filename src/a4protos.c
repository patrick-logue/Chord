#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "a4protos.h"

static const int MAXCLIENTS = 1024;

int listenTCP(struct sockaddr_in *my_address) {

    // Create endpoint and return tcp file descriptor
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() failed\n");
        exit(-1);
    }

    // Bind to the local address
    if (bind(sockfd, (struct sockaddr *) my_address, sizeof(struct sockaddr_in)) < 0) {
        perror("bind() failed\n");
        exit(-1);
    }

    // Listen
    if (listen(sockfd, MAXCLIENTS) < 0) {
        perror("listen() failed\n");
        exit(-1);
    }

    return sockfd;
}

int bindUDP(struct sockaddr_in *my_address) {

    // Create endpoint and return udp file descriptor
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket() failed\n");
        exit(-1);
    }

     // Bind to the local address
    if (bind(sockfd, (struct sockaddr *) my_address, sizeof(my_address)) < 0) {
        perror("bind() failed\n");
        exit(-1);
    }

    return sockfd;
}

char *addr_to_string(struct sockaddr_in addr) {
    // Convert ip to string
    char address[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &(addr.sin_addr.s_addr), address, INET_ADDRSTRLEN) == NULL) {
        perror("inet_ntop() failed\n");
        exit(-1);
    }

    // Convert port to string
    char port[6] = {0};
    sprintf(port, "%d", ntohs(addr.sin_port));

    // Concat strings to form: x.x.x.x port
    char *payload = malloc(strlen(address) + strlen(port) + 2);
    strcpy(payload, address);
    strcat(payload, " ");
    strcat(payload, port);

    return payload;
}

void recvBytes(int sd, size_t bytes, void *buffer) {
    ssize_t numBytes = 0;
    unsigned int TotalBytesRcvd = 0;

    while (TotalBytesRcvd < bytes) {
        numBytes = recv(sd, buffer, bytes, 0);
        if (numBytes < 0) {
            printf("recv() failed here\n");
            exit(-1);
        } else if (numBytes == 0) {
            printf("recv() failed, connection closed prematurely\n");
            exit(-1);
        }
        TotalBytesRcvd += numBytes;
    }
}
