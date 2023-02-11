#include <sys/socket.h>
#include <netinet/in.h>

int listenTCP(struct sockaddr_in *my_address);
int bindUDP(struct sockaddr_in *my_address);
char *addr_to_string(struct sockaddr_in addr);
void recvBytes(int sd, size_t bytes, void *buffer);