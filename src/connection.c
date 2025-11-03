#include "connection.h"

void close_conn(WorkerProcess* worker, int conn_fd) {
    close(conn_fd);
    worker->num_conn--;
    logg("Client %d [Worker: %d] hangup!", conn_fd, worker->pid);
}

/* Accepting all new queued up connections */
int handle_new_conn(WorkerProcess* worker, int listenerfd) {
    int clientfd;
    struct sockaddr_storage client_addr;
    socklen_t client_addrlen = sizeof(client_addr);

    struct epoll_event event;

    for(;;) {
        clientfd = accept(listenerfd, (struct sockaddr*)&client_addr, &client_addrlen);

        if(clientfd == -1) {
            // no more connections to process
            if(errno == EAGAIN || errno == EWOULDBLOCK) 
                break;
            
            perror("accept");
            return -1;
        }

        // set nonblocking
        if(set_nonblock(clientfd) == -1) {
            close(clientfd);
            continue;
        }

        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        event.data.fd = clientfd;
        if(epoll_ctl(worker->efd, EPOLL_CTL_ADD, clientfd, &event) < 0) {
            perror("epoll_ctl");
            close(clientfd);
            continue;
        }

        // printf("Uspesno prihvacena nova konekcija!\n");
        logg("New connection accepted!");
        worker->num_conn++;
    }

    return 0;
}