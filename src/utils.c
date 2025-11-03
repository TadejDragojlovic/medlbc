#include "utils.h"

// returns 0 on success, -1 otherwise
int set_nonblock(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl"); 
        return -1; 
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        return -1;
    }

    return 0;
}

/* simple logging func */
void logg(const char* format, ...) {
    va_list args;
    va_start(args, format);

    printf("LOG: ");
    vprintf(format, args);
    printf("\n");

    va_end(args);
}