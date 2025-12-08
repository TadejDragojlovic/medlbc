#ifndef WORKER_H
#define WORKER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <wait.h>

#include "utils.h"
#include "upstream.h"

#define MAXEVENTS 64 // max numbers of fds for the epoll buffer

typedef struct {
    pid_t pid;                                      // process id
    int idx;                                        // worker index
    int num_conn;                                   // current number of connections

    /* Epoll attributes */
    int efd;                                        // epoll file descriptor
    struct epoll_event event_buffers[MAXEVENTS];    // event buffer for sockets
} WorkerProcess;

WorkerProcess spawn_worker(int listenerfd, int index);
int setup_worker(int listenerfd, WorkerProcess* worker);
void employ_worker(int listenerfd, WorkerProcess* worker);
WorkerProcess* init_workers(int listenerfd, int n);

void manage_workers(WorkerProcess* worker_array, int n, int listenerfd);
void cleanup_worker(int listenerfd, WorkerProcess* worker);

#endif