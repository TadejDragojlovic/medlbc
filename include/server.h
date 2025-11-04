#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include "utils.h"

#define BACKLOG 10

int create_server_socket(char* port, int set_nonblocking);
int server_listen(int sockfd);
ssize_t send_all(int destfd, char* buf, ssize_t nbytes);

#endif