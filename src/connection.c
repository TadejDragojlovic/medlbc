#include "connection.h"

// TODO: improve this function to work with FDInfo
void close_conn(WorkerProcess* worker, int conn_fd) {
    close(conn_fd);
    worker->num_conn--;
    logg("Client %d [Worker: %d] hangup!", conn_fd, worker->pid);
}

/* Allocates memory for a new ConnectionContext and sets default values (including client_fd) */
struct ConnectionContext* initialize_new_connection_context(int client_fd) {
    struct ConnectionContext* ctx = malloc(sizeof(struct ConnectionContext));
    if(!ctx) return NULL;
    memset(ctx, 0, sizeof(*ctx));

    ctx->clientfd = client_fd;
    ctx->upstreamfd = -1;
    ctx->status = CTX_IDLE;

    return ctx;
}

/* Allocates memory for a new FDInfo structure;
   It initializes values according to parameters, ctx can be NULLED */
struct FDInfo* initialize_new_fdinfo_structure(int fd, enum FDType type, struct ConnectionContext* ctx) {
    struct FDInfo* fi = malloc(sizeof(struct FDInfo));
    if(!fi) return NULL;
    memset(fi, 0, sizeof(*fi));

    fi->fd = fd;
    fi->type = type;
    fi->ctx = ctx;

    return fi;
}

/* Accepting all new queued up connections */
int handle_new_conn(WorkerProcess* worker, int listenerfd) {
    int clientfd;
    struct sockaddr_storage client_addr;
    socklen_t client_addrlen = sizeof(client_addr);

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

        // connection context
        struct ConnectionContext* ctx = initialize_new_connection_context(clientfd); // TODO: error handle

        // info about the file descriptor
        struct FDInfo* fdinfo = initialize_new_fdinfo_structure(clientfd, FD_CLIENT, ctx); // TODO: error handle

        // fdinfo needs to be accessible from ctx
        ctx->client_info = fdinfo;

        struct epoll_event ev = {
            .events = EPOLLIN | EPOLLET | EPOLLRDHUP,
            .data.ptr = fdinfo,
        };
        if(epoll_ctl(worker->efd, EPOLL_CTL_ADD, clientfd, &ev) < 0) {
            perror("epoll_ctl");
            // TODO: implement proper cleanup
            close(clientfd);
            free(ctx);
            free(fdinfo);
            continue;
        }

        logg("New connection accepted!");
        worker->num_conn++;
    }

    return 0;
}