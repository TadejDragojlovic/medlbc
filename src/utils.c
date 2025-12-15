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

/* persistent read function for event-triggered reading;
   returns 0 if closed connection, -1 if error, otherwise total bytes read */
ssize_t persistent_read(int fd, void* buf, size_t count) {
    ssize_t total = 0;

    for(;;) {
        ssize_t nbytes = read(fd, buf+total, count-total);

        if(nbytes > 0) {
            total += nbytes;

            if(total == count) break;
            continue;
        }

        // connection closed by peer
        if(nbytes == 0) {
            return 0;
        }

        // read successfully
        if(errno == EAGAIN || errno == EWOULDBLOCK) break;

        // otherwise error
        perror("read");
        return -1;
    }

    return total;
}