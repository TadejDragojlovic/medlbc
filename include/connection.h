#ifndef CONNECTION_H
#define CONNECTION_H

#include "worker.h"
#include <sys/socket.h>
#include <errno.h>

void close_conn(WorkerProcess* worker, int conn_fd);
int handle_new_conn(WorkerProcess* worker, int listenerfd);

#endif