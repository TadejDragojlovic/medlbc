#include "dummy_backend.h"
#define BUF_SIZE 256

/* Simple TCP Echo server */

int main(int argc, char* argv[]) {
    if(argc != 2) {
        fprintf(stderr, "usage: %s <server_port>\n", argv[0]);
        exit(1);
    }

    char* port = argv[1];
    int listenerfd;

    if((listenerfd = create_server_socket(port, 0)) == -1) exit(1);
    if(server_listen(listenerfd) == -1) exit(1);

    logg("[SERVER]: Listening on port %s", port);

    int clientfd;
    char buf[BUF_SIZE];

    while(1) {
        if((clientfd = accept(listenerfd, NULL, NULL)) == -1) {
            if(errno == EINTR) continue;

            perror("accept");
            continue;
        }

        logg("[SERVER]: Connection established.");

        ssize_t nbytes;
        while(1) {
            nbytes = read(clientfd, buf, BUF_SIZE);

            if(nbytes == 0) {
                logg("[SERVER]: Client closed connection.");
                break;
            } else if(nbytes < 0) {
                if(errno == EINTR) continue;

                perror("read");
                break;
            } else {
                buf[nbytes] = '\0';
                printf("[SERVER]: Received %s", buf);
                if(send_all(clientfd, buf, nbytes) == -1) {
                    break;
                }
            }
        }

        close(clientfd);
        logg("[SERVER]: Connection closed and cleaned up.");
    }

    close(listenerfd);

    return 0;
}