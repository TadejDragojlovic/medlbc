#ifndef CONNECTION_H
#define CONNECTION_H

#include "worker.h"
#include <sys/socket.h>
#include <errno.h>

#define REQ_BUF_SIZE 4096

struct ConnectionContext {
    int clientfd;
    struct FDInfo* client_info;         // detailed fd info for the client

    int upstreamfd;
    struct FDInfo* upstream_info;       // detailed fd info for the upstream server

    enum { 
        CTX_IDLE,                       // client didn't send a request yet
        CTX_CONNECTING_TO_UPSTREAM,
        CTX_FORWARDING_REQUEST,
        CTX_WAITING_RESPONSE,
        CTX_SUCCESS,
        CTX_ERROR,
    } status;

    char buf[REQ_BUF_SIZE];
    size_t buflen;
};

enum FDType { FD_CLIENT, FD_UPSTREAM };

struct FDInfo {
    int fd;
    enum FDType type;
    struct ConnectionContext* ctx;
};

struct ConnectionContext* initialize_new_connection_context();
struct FDInfo* initialize_new_fdinfo_structure(int fd, enum FDType type, struct ConnectionContext* ctx);
void close_conn(WorkerProcess* worker, int conn_fd);
int handle_new_conn(WorkerProcess* worker, int listenerfd);

#endif