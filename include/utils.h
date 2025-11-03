#ifndef UTILS_H
#define UTILS_H

#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h> // za logiranje

int set_nonblock(int sockfd);
void logg(const char* format, ...);

#endif