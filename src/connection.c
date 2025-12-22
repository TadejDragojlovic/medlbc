#include "connection.h"

/* Returns status name as string from enum CTXStatus */
const char* get_status_name(enum CTXStatus status) {
    switch(status) {
        case CTX_IDLE:
            return "CTX_IDLE";
        case CTX_CONNECTING_TO_UPSTREAM:
            return "CTX_CONNECTING_TO_UPSTREAM";
        case CTX_FORWARDING_REQUEST:
            return "CTX_FORWARDING_REQUEST";
        case CTX_WAITING_RESPONSE:
            return "CTX_WAITING_RESPONSE";
        case CTX_SUCCESS:
            return "CTX_SUCCESS";
        case CTX_ERROR:
            return "CTX_ERROR";
        default:
            return "";
    }
}

/* Add a new ConnectionContext node to linked list */
struct ConnectionNode* add_ctx_node(WorkerProcess* worker, struct ConnectionContext* ctx) {
    struct ConnectionNode* new_node = malloc(sizeof(*new_node));
    if(!new_node) return NULL;

    new_node->ctx = ctx;
    new_node->prev = NULL;
    new_node->next = worker->conn_head;

    if(worker->conn_head) worker->conn_head->prev = new_node;
    worker->conn_head = new_node;

    ctx->node = new_node;
    return new_node;
}

/* Delete a ConncetionContext node from a linked list */
void delete_ctx_node(WorkerProcess* worker, struct ConnectionNode* node) {
    if(node->prev) node->prev->next = node->next;
    else worker->conn_head = node->next;

    if(node->next) node->next->prev = node->prev;

    free(node);
    node=NULL;
}

/* Iterating through the linked list and printing */
void print_ctx_list(WorkerProcess* worker) {
    struct ConnectionNode* tmp = worker->conn_head;

    while(tmp!=NULL) {
        printf("clientfd: %d\nupstreamfd: %d\nconnection status: %s\n",
                tmp->ctx->client->fd,
                tmp->ctx->upstream == NULL ? -1 : tmp->ctx->upstream->fd,
                get_status_name(tmp->ctx->status));
        tmp = tmp->next;
    }
}

/* Close desired socket, update number of connected clients accordingly */
void close_client_conn(WorkerProcess* worker, int client_fd) {
    close(client_fd);
    worker->num_conn--;
    logg("Client %d [Worker: %d] hangup!", client_fd, worker->pid);
}

/* Allocates memory for a new ConnectionContext and sets default values (including client_fd) */
struct ConnectionContext* initialize_new_connection_context(struct FDInfo* fi) {
    struct ConnectionContext* ctx = malloc(sizeof(struct ConnectionContext));
    if(!ctx) return NULL;
    memset(ctx, 0, sizeof(*ctx));

    ctx->client = fi;               // client fd info needs to be accessible from ctx
    ctx->upstream = NULL;           // upstream fdinfo gets defined later on
    ctx->status = CTX_IDLE;

    ctx->cli_sentoffset = 0;
    ctx->up_sentoffset = 0;

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

        // info about the file descriptor
        struct FDInfo* fdinfo = initialize_new_fdinfo_structure(clientfd, FD_CLIENT, NULL); // TODO: error handle

        // connection context
        struct ConnectionContext* ctx = initialize_new_connection_context(fdinfo); // TODO: error handle
        fdinfo->ctx = ctx; // adding connection context to our client fdinfo structure

        struct epoll_event ev = {
            .events = EPOLLIN | EPOLLET | EPOLLRDHUP,
            .data.ptr = fdinfo,
        };
        if(epoll_ctl(worker->efd, EPOLL_CTL_ADD, clientfd, &ev) < 0) {
            perror("epoll_ctl");
            close(clientfd);
            free(fdinfo);
            free(ctx);
            continue;
        }

        // Adding the ctx node to the workers list
        if(add_ctx_node(worker, ctx) == NULL) cleanup_client(worker, ctx);

        logg("New connection accepted!");
        worker->num_conn++;
    }

    return 0;
}

/* frees and cleans up everything related to the upstream (fd info, connection ctx, epoll) */
void cleanup_upstream(WorkerProcess* worker, struct ConnectionContext* ctx) {
    if(!ctx || !ctx->upstream) return;

    // cleanup upstream
    struct FDInfo* ufi = ctx->upstream;
    epoll_ctl(worker->efd, EPOLL_CTL_DEL, ufi->fd, NULL);
    close(ufi->fd);
    free(ufi);

    // reset upstream-related information in ctx
    ctx->upstream       = NULL;
    ctx->up_buflen      = 0;
    ctx->up_sentoffset  = 0;
    ctx->cli_sentoffset = 0;
    memset(ctx->up_buf, 0, REQ_BUF_SIZE);

    ctx->status = CTX_IDLE;
}

/* frees and cleans up everything related to the client (fd info, connection ctx, epoll) */
void cleanup_client(WorkerProcess* worker, struct ConnectionContext* ctx) {
    if(!ctx || !ctx->client) return;

    // if upstream exists, make sure to cleanup that part first
    cleanup_upstream(worker, ctx);

    // deleting the node from linked list
    if(ctx->node) {
        delete_ctx_node(worker, ctx->node);
    }

    struct FDInfo* cfi = ctx->client;
    epoll_ctl(worker->efd, EPOLL_CTL_DEL, cfi->fd, NULL);
    close_client_conn(worker, cfi->fd);
    free(cfi);
    ctx->client = NULL;

    free(ctx);
    ctx = NULL;
}