#include "lb.h"
#include "utils.h"
#include "worker.h"

#define N_WORKERS 1

/* returns the socket fd [int] for the server, set to non-blocking */
int get_server_socket(char* port) {
    int sockfd, rv, reuse_opt=1;
    struct addrinfo hints, *serverinfo, *t;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // only IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port, &hints, &serverinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(t=serverinfo; t!=NULL; t=t->ai_next) {
        // socket creation
        if((sockfd = socket(AF_INET, t->ai_socktype, t->ai_protocol)) == -1) {
            continue;
        }

        // set nonblocking
        if(set_nonblock(sockfd) == -1) {
            close(sockfd);
            continue;
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
        fprintf(stderr, "Socket nije uspeo da nadje prikladno mesto za bindovanje.\n");
        exit(1);
    }

    if(listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        exit(1);
    }

    return sockfd;
}

int main(int argc, char* argv[]) {
    int listenerfd;
    listenerfd = get_server_socket(PORT);

    // 1. Master process starts up N_WORKERS
    WorkerProcess* worker_array = init_workers(listenerfd, N_WORKERS);
    if(!worker_array) {
        // TODO: cleanup
        exit(1);
    }

    // 2. Master process manages workers (monitoring)
    manage_workers(worker_array, N_WORKERS, listenerfd);

    // TODO: cleanup
    free(worker_array);

    return 0;
}


/* TODO: STA RADITI SAD
1. struktura bekend servera
    1.1 dummy_backend.c (tcp echoserver ili nesto)
2. client.c

VECI KORACI: 
- implementacija algoritma za load balancing
- citanje config fajla (kao nginx sto radi)

BONUS:
- graceful exit logika za signale (child procesi)
    * da li treba da koristim `kill(pid, signal_koji_zelim)` za workere?
*/
