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

/* helper function for constructing sigaction */
int setup_sigaction(int signum, void (*signal_handler_func)(int), int sa_flags) {
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler_func;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = sa_flags;

    if(sigaction(signum, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    return 0;
}

/* blocking send all function */
ssize_t send_all_blocking(int fd, void* buf, size_t len) { 
    ssize_t total = 0; 
    while(len > 0) { 
        ssize_t nbytes = send(fd, buf+total, len, 0); 
        if(nbytes == -1) { 
            perror("send_all"); 
            if(errno == EINTR) continue; 
            return -1; 
        } 

        total += nbytes; 
        len -= nbytes; 
    } 

    return total; 
}

/* persistent read function for event-triggered reading;
   returns 0 if closed connection, -1 if error, otherwise total bytes read */
ssize_t read_all(int fd, void* buf, size_t count) {
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

        if(nbytes == -1) {
            // read successfully
            if(errno == EAGAIN || errno == EWOULDBLOCK) break;

            // interruption
            if(errno == EINTR) continue;

            // otherwise error
            perror("read");
            return -1;
        }
    }

    return total;
}

/* function to send chunks of data based off of `sent` offset;
   returns 0 if the data was partially sent, 1 if the transfer was succesful or -1 on error */
int send_chunk(int fd, void* buf, size_t len, size_t *sent) {
    if(len == 0) return 1;

    while(*sent < len) {
        ssize_t nbytes = send(fd, buf+(*sent), len-(*sent), 0);

        if(nbytes>0) {
            *sent += nbytes;
            continue;
        }

        if(nbytes == -1) {
            // interruption, continue
            if(errno == EINTR) continue;

            // don't block, wait for next EPOLLOUT
            if(errno == EAGAIN || errno == EWOULDBLOCK) return 0;

            // error
            return -1;
        }
    }

    // everything sent successfully
    return 1;
}