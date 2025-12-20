#ifndef CONNECTION_H
#define CONNECTION_H

#include "worker.h"
#include <sys/socket.h>
#include <errno.h>

#define REQ_BUF_SIZE 4096

enum FDType { FD_CLIENT, FD_UPSTREAM };

struct FDInfo {
    int fd;
    enum FDType type;
    struct ConnectionContext* ctx;
};

enum CTXStatus {
    CTX_IDLE,                       // client didn't send a request yet
    CTX_CONNECTING_TO_UPSTREAM,
    CTX_FORWARDING_REQUEST,
    CTX_WAITING_RESPONSE,
    CTX_SUCCESS,
    CTX_ERROR,
};

struct ConnectionContext {
    struct FDInfo* client;          // detailed file descriptor information for client
    struct FDInfo* upstream;        // detailed file descriptor information for upstream server

    enum CTXStatus status;

    char cli_buf[REQ_BUF_SIZE];     // buffer to store client request
    size_t cli_buflen;
    size_t cli_sentoffset;          // offset for bytes sent from client buffer

    char up_buf[REQ_BUF_SIZE];      // buffer to store upstream response
    size_t up_buflen;
    size_t up_sentoffset;           // offset for bytes sent from upstream buffer
};

struct ConnectionContext* initialize_new_connection_context(struct FDInfo* fi);
struct FDInfo* initialize_new_fdinfo_structure(int fd, enum FDType type, struct ConnectionContext* ctx);
void close_client_conn(WorkerProcess* worker, int conn_fd);
int handle_new_conn(WorkerProcess* worker, int listenerfd);

void cleanup_upstream(WorkerProcess* worker, struct ConnectionContext* ctx);
void cleanup_client(WorkerProcess* worker, struct ConnectionContext* ctx);

#endif