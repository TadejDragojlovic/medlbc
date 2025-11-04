#include "server.h"

/* returns the socket fd [int] for the server, sets to non-blocking if flag argument is 1 */
int create_server_socket(char* port, int set_nonblocking) {
    int sockfd, rv, reuse_opt=1;
    struct addrinfo hints, *serverinfo, *t;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // only IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port, &hints, &serverinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
        exit(1);
    }

    for(t=serverinfo; t!=NULL; t=t->ai_next) {
        // socket creation
        if((sockfd = socket(AF_INET, t->ai_socktype, t->ai_protocol)) == -1) {
            continue;
        }

        // set nonblocking
        if(set_nonblocking == 1) {
            if(set_nonblock(sockfd) == -1) {
                close(sockfd);
                continue;
            }
        }

        // options
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse_opt, sizeof(int)) == -1
        || setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse_opt, sizeof(int)) == -1) {
            perror("setsockopt");
            close(sockfd);
            continue;
        }

        // bind
        if(bind(sockfd, t->ai_addr, t->ai_addrlen) == -1) {
            perror("bind");
            close(sockfd);
            continue;
        }

        break;
    }

    freeaddrinfo(serverinfo);

    if(t==NULL) {
        close(sockfd);
        fprintf(stderr, "Socket failed to bind.\n");
        return -1;
    }

    return sockfd;
}

int server_listen(int sockfd) {
    if(listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        return -1;
    }

    return 0;
}

/* Persistent `send` function */
ssize_t send_all(int destfd, char* buf, ssize_t len) {
    ssize_t sent, total_sent=0;

    while(len > 0) {
        if((sent = send(destfd, buf+total_sent, len, 0)) == -1) {
            perror("sendall");

            if(errno == EINTR) continue;
            return -1;
        }

        total_sent += sent;
        len -= sent;
    }

    return total_sent;
}