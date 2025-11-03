#ifndef LB_H
#define LB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <wait.h>

#define PORT "3456"
#define BACKLOG 10
#define MAXEVENTS 64

int get_server_socket();
void handle_new_connection(int listenerfd, int efd, int* br_kon);
void close_connection(int confd, int* br_kon);

// utils (premesti u odvojen source fajl)
int set_nonblock(int sockfd);

#endif