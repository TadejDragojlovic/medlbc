#include "upstream.h"

int connect_to_upstream(struct UpstreamServer ups) {
    int sockfd;

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    if(set_nonblock(sockfd) == -1) {
        return -1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ups.port);

    if(inet_pton(AF_INET, ups.ip, &addr.sin_addr) <= 0) {
        logg("[Upstream error]: Invalid address");
        return -1;
    }

    if(connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        // because of nonblocking nature
        if (errno == EINPROGRESS) {
            return sockfd;
        }

        close(sockfd);
        return -1;
    }

    return sockfd;
}