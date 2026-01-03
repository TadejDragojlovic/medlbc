#ifndef UTILS_H
#define UTILS_H

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>     // logging
#include <sys/socket.h>
#include <signal.h>

int set_nonblock(int sockfd);
void logg(const char* format, ...);
int setup_sigaction(int signum, void (*signal_handler_func)(int), int sa_flags);
ssize_t send_all_blocking(int fd, void* buf, size_t len); 
ssize_t read_all(int fd, void* buf, size_t count);
int send_chunk(int fd, void* buf, size_t len, size_t *sent);

#endif