#ifndef UPSTREAM_H
#define UPSTREAM_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include "utils.h"

// TODO: hardcoded
#define NUM_UPSTREAMS 2

/* Backend server structure */
struct UpstreamServer {
    char ip[INET6_ADDRSTRLEN];
    uint16_t port;
    // int health; TODO
};

int connect_to_upstream(struct UpstreamServer ups);

#endif