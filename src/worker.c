#include "worker.h"
#include "connection.h"

// TODO: hardcoded
static struct UpstreamServer upstream_servers[2] = {
    { "127.0.0.1", 4445},
    { "127.0.0.1", 4446}
};

/* Returns WorkerProcess that is already setup if everything goes well,
otherwise it returns WorkerProcess that has "pid" attribute set to -1, error;
utilizing pipe to obtain communication between children and parent */
WorkerProcess spawn_worker(int listenerfd, int index) {
    WorkerProcess new_worker;
    new_worker.idx = index;

    // pipefd[0] -> read; pipefd[1] -> write
    int pipefd[2];
    if(pipe(pipefd) == -1) {
        perror("pipe");
        new_worker.pid = -1;
        return new_worker;
    }

    pid_t wpid = fork();

    if(wpid==0) {
        new_worker.pid = getpid();
        close(pipefd[0]);

        // setup failed, writes '1' to pipe to inform parent about error
        if(setup_worker(listenerfd,  &new_worker) == -1) {
            write(pipefd[1], "1", 1);
            close(pipefd[1]);
            close(listenerfd);
            exit(EXIT_FAILURE);
        }

        // success
        write(pipefd[1], "0", 1);
        close(pipefd[1]);

        employ_worker(listenerfd, &new_worker);
        cleanup_worker(listenerfd, &new_worker);
        exit(0);
    }

    char status;
    if(read(pipefd[0], &status, 1) != 1 || status != '0')
        new_worker.pid = -1;

    close(pipefd[0]);
    close(pipefd[1]); 

    return new_worker;
}

/* sets up epoll array for the worker, returns -1 on fail */
int setup_worker(int listenerfd, WorkerProcess* worker) {
    int efd = epoll_create1(0);

    // error handle
    if(efd == -1) {
        perror("epoll_create1");
        return -1;
    }

    worker->efd = efd;
    worker->num_conn = 0;

    struct epoll_event event;

    event.data.fd = listenerfd;
    event.events = EPOLLIN | EPOLLET;
    if(epoll_ctl(worker->efd, EPOLL_CTL_ADD, listenerfd, &event) == -1) {
        perror("epoll_ctl");
        close(worker->efd);
        return -1;
    }

    return 0;
}

/* job loop for the worker process, handles events */
void employ_worker(int listenerfd, WorkerProcess* worker) {
    for(;;) {
        logg("WORKER [%d] - current connections (%d)", worker->pid, worker->num_conn);

        const int ready = epoll_wait(worker->efd, worker->event_buffers, MAXEVENTS, -1);

        if(ready < 0) {
            if(errno == EINTR) {
                // got interrupted by some signal, continue
                continue;
            }

            perror("epoll_wait");
            cleanup_worker(listenerfd, worker);
            exit(EXIT_FAILURE);
        }

        for(int j=0;j<ready;j++) {
            // new connection
            if(worker->event_buffers[j].data.fd == listenerfd) {
                handle_new_conn(worker, listenerfd);
                continue;
            }

            // hangup
            if(worker->event_buffers[j].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                struct FDInfo* fi = worker->event_buffers[j].data.ptr;

                // client hangup
                if(fi->type == FD_CLIENT) {
                    // cleans and closes connection
                    cleanup_client(worker, fi->ctx);
                } else if(fi->type == FD_UPSTREAM) {
                    // upstream hangup
                    fi->ctx->status = CTX_ERROR;
                    logg("[CTX_ERROR]: UPSTREAM NOT UP.");
                    cleanup_upstream(worker, fi->ctx);
                }

                continue;
            }

            // data ready to be read
            if(worker->event_buffers[j].events & EPOLLIN) {
                struct FDInfo* fi = worker->event_buffers[j].data.ptr;

                // client has data ready to be read
                if(fi->type == FD_CLIENT) {

                    // so far client has been idle
                    if(fi->ctx->status == CTX_IDLE) {
                        // persistent read for epollet
                        ssize_t total = read_all(fi->fd, fi->ctx->cli_buf, REQ_BUF_SIZE);

                        if(total == 0) { // connection closed by client
                            fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: client closed connection.");
                            cleanup_client(worker, fi->ctx);
                            continue;
                        } else if(total < 0) { // error during read
                            fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: encountered error while reading client request, closing connection.");
                            cleanup_client(worker, fi->ctx);
                            continue;
                        }

                        // otherwise, read successfully
                        fi->ctx->cli_buflen = total;

                        fi->ctx->status = CTX_CONNECTING_TO_UPSTREAM;
                    }

                    // fallthrough from CTX_IDLE
                    if(fi->ctx->status == CTX_CONNECTING_TO_UPSTREAM) {
                        // connecting to upstream
                        int upstream_sockfd;
                        if((upstream_sockfd = connect_to_upstream(upstream_servers[0])) == -1) {
                            fi->ctx->status = CTX_ERROR;

                            // soft reset of the ctx, upstream not defined yet so doesn't need cleaning up
                            logg("[CTX_ERROR]: error in trying to establish a connection to upstream.");
                            fi->ctx->upstream = NULL;
                            fi->ctx->status = CTX_IDLE;
                            continue;
                        }

                        logg("1. trying to connect to upstream");

                        // registering FDInfo for upstream
                        struct FDInfo* ufi = initialize_new_fdinfo_structure(upstream_sockfd, FD_UPSTREAM, fi->ctx); // TODO: error handle

                        fi->ctx->upstream = ufi;

                        // nonblocking socket for upstream
                        struct epoll_event ev = {
                            .events = EPOLLOUT | EPOLLET,
                            .data.ptr = ufi,
                        };
                        epoll_ctl(worker->efd, EPOLL_CTL_ADD, upstream_sockfd, &ev);
                        continue;
                    }
                } else if(fi->type == FD_UPSTREAM) {

                    // returning the response to client
                    if(fi->ctx->status == CTX_WAITING_RESPONSE) {
                        // upstream response
                        ssize_t total = read_all(fi->fd, fi->ctx->up_buf, REQ_BUF_SIZE);

                        if(total == 0) { // connection closed
                            fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: upstream closed the connection.");
                            cleanup_upstream(worker, fi->ctx);
                            continue;
                        } else if(total < 0) { // error during read
                            fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: upstream failed to read the request.");
                            cleanup_upstream(worker, fi->ctx);
                            continue;
                        }

                        // otherwise, read successfully
                        fi->ctx->up_buflen = total;

                        logg("4. read response.");

                        // writing back to client
                        int rv = send_chunk(fi->ctx->client->fd, fi->ctx->up_buf, fi->ctx->up_buflen, &(fi->ctx->up_sentoffset));

                        // request succesfully sent
                        if(rv == 1) {
                            logg("5. response successfully sent to client.");
                            fi->ctx->status = CTX_SUCCESS;
                        } else if(rv == -1) {
                            // error while sending data
                            fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: upstream failed to send the resposne back.");
                        }

                        cleanup_upstream(worker, fi->ctx);
                        continue;
                    }
                }
            }

            // EPOLLOUT
            /* Epollout signals when TCP handshake is over, so the socket is ready to be written to */
            /* When epollout signals, we check errno to see if `connect` succeeded */
            if(worker->event_buffers[j].events & EPOLLOUT) {
                struct FDInfo* fi = worker->event_buffers[j].data.ptr;

                // upstream receives data
                if (fi->type == FD_UPSTREAM) {
                    // handle initial connection
                    if(fi->ctx->status == CTX_CONNECTING_TO_UPSTREAM) {
                        int err;
                        socklen_t len = sizeof(err);

                        getsockopt(fi->fd, SOL_SOCKET, SO_ERROR, &err, &len);

                        // connection failed
                        if(err != 0) {
                            fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: UPSTREAM UP, BUT DENIED CONNECTION.");
                            cleanup_upstream(worker, fi->ctx);
                            continue;
                        }

                        logg("2. connection to upstream good");
                        // connection successful
                        if(err == 0) {
                            fi->ctx->status = CTX_FORWARDING_REQUEST;
                        }

                        // fallthrough
                    }

                    // sending request from client to upstream
                    if(fi->ctx->status == CTX_FORWARDING_REQUEST) {
                        int rv = send_chunk(fi->ctx->upstream->fd, fi->ctx->cli_buf, fi->ctx->cli_buflen, &(fi->ctx->cli_sentoffset));

                        // request succesfully sent
                        if(rv == 1) {
                            logg("3. data successfully sent to upstream.");
                            fi->ctx->status = CTX_WAITING_RESPONSE;

                            // upstream doesn't need EPOLLOUT anymore, now only EPOLLIN
                            struct epoll_event ev = {
                                .events = EPOLLIN | EPOLLET,
                                .data.ptr = fi->ctx->upstream,
                            };
                            epoll_ctl(worker->efd, EPOLL_CTL_MOD, fi->fd, &ev);
                        } else if(rv == -1) {
                            // error while sending data
                            fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: sending request to upstream failed.");
                            cleanup_upstream(worker, fi->ctx);
                        }

                        continue;
                    }
                }
            }
        }
    }
}

/* Initializes `n` workers, returns worker_array */
WorkerProcess* init_workers(int listenerfd, int n) {
    WorkerProcess* worker_array = malloc(n * sizeof(WorkerProcess));
    if(!worker_array) {
        perror("malloc");
        return NULL;
    }

    for(int i=0;i<n;i++) {
        worker_array[i] = spawn_worker(listenerfd, i);
        if(worker_array[i].pid == -1) {
            fprintf(stderr, "Error occured while spawning worker %d\n", i);
        }
    }

    // Printing initalized workers and their pids
    // for(int i=0;i<n;i++) {
    //     printf("worker (%d): %d\n", i, worker_array[i].pid);
    // }
    // logg("INITIALIZED WORKERS.");

    return worker_array;
}

/* Ran by master process;
   waits for signals from worker processes, respawns workers if needed
*/
void manage_workers(WorkerProcess* worker_array, int n, int listenerfd) {
    int status, worker_count = n;
    pid_t p;

    while(worker_count > 0) {
        // reaping all children
        while((p = waitpid(-1, &status, WNOHANG)) > 0) {
            for(int i=0;i<n;i++) {
                if(worker_array[i].pid == p) {
                    worker_count--;
                    logg("[WORKER (idx = %d) PROCESS (pid= %d)] TERMINATED. SPAWNING A NEW WORKER...", i, p);

                    // worker respawn
                    WorkerProcess new_worker = spawn_worker(listenerfd, i);
                    if(new_worker.pid == -1) {
                        fprintf(stderr, "Error occured while spawning worker %d\n", i);
                        break;
                    }

                    // respawn successful
                    worker_array[i] = new_worker;
                    worker_count++;

                    break;
                }
            }
        }

        if(p == -1 && errno == ECHILD) break;

        sleep(1);
    }

    logg("NO WORKERS ALIVE. TERMINATING...");
}

/* Cleans up epoll structure, closes all sockets */
void cleanup_worker(int listenerfd, WorkerProcess* worker) {
    for(int i=0;i<worker->num_conn;i++) {
        int cfd = worker->event_buffers[i].data.fd;
        epoll_ctl(worker->efd, EPOLL_CTL_DEL, cfd, NULL);
        close(cfd);
    }

    close(worker->efd);
    close(listenerfd); // closes child copy of listenerfd
}