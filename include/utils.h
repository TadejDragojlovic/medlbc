#ifndef UTILS_H
#define UTILS_H

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h> // za logiranje

int set_nonblock(int sockfd);
void logg(const char* format, ...);
ssize_t persistent_read(int fd, void* buf, size_t count);

#endif