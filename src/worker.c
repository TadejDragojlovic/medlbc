#include "worker.h"
#include "connection.h"

// TODO: hardcoded
static struct UpstreamServer upstream_servers[2] = {
    { "127.0.0.1", 4445},
    { "127.0.0.1", 4446}
};

// master shutdown flag
static volatile sig_atomic_t master_shutdown = 0;

// graceful shutdown flag for worker
static volatile sig_atomic_t start_shutdown = 0;
static int shutdown_fd_global = -1;

/* Signal handler function for master process */
void master_sighandler(int signum) {
    master_shutdown = 1;
}

/* Signal handler function, writes to shutdown_fd */
void worker_sighandler(int signum) {
    start_shutdown = 1;

    if(shutdown_fd_global > 0) {
        uint64_t b = 1;
        write(shutdown_fd_global, &b, sizeof(b));
    }
}

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

    /* both processes need to know this info:
        1. master for the worker array (needs to know it's child pid for waitpid later on)
        2. worker needs to know it's pid, for logging purposes mainly
    */
    new_worker.pid = wpid;

    // child process
    if(wpid==0) {
        new_worker.pid = getpid(); // worker needs to know its pid
        close(pipefd[0]);

        // setup failed, writes '1' to pipe to inform parent about error
        if(setup_worker(listenerfd,  &new_worker) == -1) {
            write(pipefd[1], "1", 1);
            close(pipefd[1]);

            cleanup_worker(&new_worker);
            if(listenerfd >= 0) close(listenerfd);
            exit(EXIT_FAILURE);
        } else {
            // success
            write(pipefd[1], "0", 1);
            close(pipefd[1]);

            // making shutdown fd global
            shutdown_fd_global = new_worker.shutdown_fdi->fd;

            // signal handling for the worker process
            if(setup_sigaction(SIGINT, worker_sighandler, SA_RESTART) == -1 ||
               setup_sigaction(SIGTERM, worker_sighandler, SA_RESTART) == -1) {
                cleanup_worker(&new_worker);
                if(listenerfd >= 0) close(listenerfd);
                exit(EXIT_FAILURE);
            }

            // workers lifecycle
            employ_worker(listenerfd, &new_worker);
            cleanup_worker(&new_worker);

            exit(0);
        }
    }

    // parent process
    char status;
    if(read(pipefd[0], &status, 1) != 1 || status != '0')
        new_worker.pid = -1;

    close(pipefd[0]);
    close(pipefd[1]); 

    return new_worker;
}

/* sets up epoll array for the worker; returns -1 on fail */
int setup_worker(int listenerfd, WorkerProcess* worker) {
    int efd = epoll_create1(0);

    // error handle
    if(efd == -1) {
        perror("epoll_create1");
        return -1;
    }

    worker->efd = efd;
    worker->num_conn = 0;
    worker->conn_head = NULL;

    // Listener FDInfo structure for the worker
    struct FDInfo *lfi = malloc(sizeof(*lfi));
    if(!lfi) {
        perror("malloc listenerfd fdinfo");
        exit(1);
    }
    lfi->fd = listenerfd;
    lfi->type = FD_LISTENER;
    lfi->ctx = NULL;
    worker->listener_fdi = lfi;

    struct epoll_event lev = { .events = EPOLLIN, .data.ptr = lfi };
    if(epoll_ctl(worker->efd, EPOLL_CTL_ADD, listenerfd, &lev) == -1) {
        perror("epoll_ctl");
        free(lfi);
        return -1;
    }

    // shutdown_fd used for signaling shutdown by master process
    int shutdownfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(shutdownfd == -1) {
        perror("eventfd");
        free(lfi);
        return -1;
    }

    // Shutdown FDInfo structure for the worker
    struct FDInfo *sfi = malloc(sizeof(*sfi));
    if(!sfi) {
        perror("malloc listenerfd fdinfo");
        free(lfi);
        exit(1);
    }
    sfi->fd = shutdownfd;
    sfi->type = FD_SHUTDOWN;
    sfi->ctx = NULL;
    worker->shutdown_fdi = sfi;

    struct epoll_event sev = { .events = EPOLLIN, .data.ptr = sfi };
    if(epoll_ctl(worker->efd, EPOLL_CTL_ADD, worker->shutdown_fdi->fd, &sev) == -1) {
        perror("epoll_ctl");
        free(lfi);
        free(sfi);
        return -1;
    }

    return 0;
}

/* job loop for the worker process, handles events */
void employ_worker(int listenerfd, WorkerProcess* worker) {
    // when '0', listener is deleted from the epoll set, meaning the worker entered "shutdown mode",
    // it is now waiting for the rest of conns to finish up, before exiting
    int listener_active = 1;

    // timeout for the epoll_wait function, initially -1, could be longer if in "shutdown mode"
    int ewait_timeout = -1;

    for(;;) {
        if(!start_shutdown) logg("WORKER [%d] - current connections (%d)", worker->pid, worker->num_conn);

        const int ready = epoll_wait(worker->efd, worker->event_buffers, MAXEVENTS, ewait_timeout);

        if(ready < 0) {
            if(errno == EINTR) {
                continue;
            }

            perror("epoll_wait");
            break;
        }

        for(int j=0;j<ready;j++) {
            struct FDInfo *curr_fi = worker->event_buffers[j].data.ptr;

            // signal on shutdown_fd for graceful exit
            if(curr_fi->type == FD_SHUTDOWN) {
                uint64_t b;
                read(worker->shutdown_fdi->fd, &b, sizeof(b));

                logg("[Worker]: Shutdown signal received.");
                if(listener_active) {
                    epoll_ctl(worker->efd, EPOLL_CTL_DEL, listenerfd, NULL);
                    listener_active = 0;
                    ewait_timeout = 200;
                }
                continue;
            }

            // new connection
            if(curr_fi->type == FD_LISTENER) {
                handle_new_conn(worker, listenerfd);
                continue;
            }

            // hangup
            if(worker->event_buffers[j].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                // client hangup
                if(curr_fi->type == FD_CLIENT) {
                    // cleans and closes connection
                    cleanup_client(worker, curr_fi->ctx);
                } else if(curr_fi->type == FD_UPSTREAM) {
                    // upstream hangup
                    curr_fi->ctx->status = CTX_ERROR;
                    logg("[CTX_ERROR]: UPSTREAM NOT UP.");
                    cleanup_upstream(worker, curr_fi->ctx);
                }

                continue;
            }

            // data ready to be read
            if(worker->event_buffers[j].events & EPOLLIN) {
                // client has data ready to be read
                if(curr_fi->type == FD_CLIENT) {

                    // so far client has been idle
                    if(curr_fi->ctx->status == CTX_IDLE) {
                        // persistent read for epollet
                        ssize_t total = read_all(curr_fi->fd, curr_fi->ctx->cli_buf, REQ_BUF_SIZE);

                        if(total == 0) { // connection closed by client
                            curr_fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: client closed connection.");
                            cleanup_client(worker, curr_fi->ctx);
                            continue;
                        } else if(total < 0) { // error during read
                            curr_fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: encountered error while reading client request, closing connection.");
                            cleanup_client(worker, curr_fi->ctx);
                            continue;
                        }

                        // otherwise, read successfully
                        curr_fi->ctx->cli_buflen = total;

                        curr_fi->ctx->status = CTX_CONNECTING_TO_UPSTREAM;
                    }

                    // fallthrough from CTX_IDLE
                    if(curr_fi->ctx->status == CTX_CONNECTING_TO_UPSTREAM) {
                        // connecting to upstream
                        int upstream_sockfd;

                        // round-robin
                        uint64_t total_rrcounter = atomic_fetch_add(rrindex, 1);
                        uint64_t curr_rrindex = total_rrcounter % NUM_UPSTREAMS;
                        // logg("[DEBUG]: (total_rrcounter %lu), (curr_upstreams_idx %lu)", total_rrcounter, curr_rrindex);

                        if((upstream_sockfd = connect_to_upstream(upstream_servers[curr_rrindex])) == -1) {
                            curr_fi->ctx->status = CTX_ERROR;

                            // soft reset of the ctx, upstream not defined yet so doesn't need cleaning up
                            logg("[CTX_ERROR]: error in trying to establish a connection to upstream.");
                            curr_fi->ctx->upstream = NULL;
                            curr_fi->ctx->status = CTX_IDLE;
                            continue;
                        }

                        logg("1. trying to connect to upstream");

                        // registering FDInfo for upstream
                        struct FDInfo* ufi = initialize_new_fdinfo_structure(upstream_sockfd, FD_UPSTREAM, curr_fi->ctx);
                        if(!ufi) {
                            perror("malloc upstream fdinfo");
                            logg("[CTX_ERROR]: error in registering FDInfo for upstream.");

                            curr_fi->ctx->upstream = NULL;
                            curr_fi->ctx->status = CTX_IDLE;
                            continue;
                        }

                        curr_fi->ctx->upstream = ufi;

                        // nonblocking socket for upstream
                        struct epoll_event ev = {
                            .events = EPOLLOUT | EPOLLET,
                            .data.ptr = ufi,
                        };
                        epoll_ctl(worker->efd, EPOLL_CTL_ADD, upstream_sockfd, &ev);
                        continue;
                    }
                } else if(curr_fi->type == FD_UPSTREAM) {

                    // returning the response to client
                    if(curr_fi->ctx->status == CTX_WAITING_RESPONSE) {
                        // upstream response
                        ssize_t total = read_all(curr_fi->fd, curr_fi->ctx->up_buf, REQ_BUF_SIZE);

                        if(total == 0) { // connection closed
                            curr_fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: upstream closed the connection.");
                            cleanup_upstream(worker, curr_fi->ctx);
                            continue;
                        } else if(total < 0) { // error during read
                            curr_fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: upstream failed to read the request.");
                            cleanup_upstream(worker, curr_fi->ctx);
                            continue;
                        }

                        // otherwise, read successfully
                        curr_fi->ctx->up_buflen = total;

                        logg("4. read response.");

                        // writing back to client
                        int rv = send_chunk(curr_fi->ctx->client->fd, curr_fi->ctx->up_buf, curr_fi->ctx->up_buflen, &(curr_fi->ctx->up_sentoffset));

                        // request succesfully sent
                        if(rv == 1) {
                            logg("5. response successfully sent to client.");
                            curr_fi->ctx->status = CTX_SUCCESS;
                        } else if(rv == -1) {
                            // error while sending data
                            curr_fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: upstream failed to send the resposne back.");
                        }

                        cleanup_upstream(worker, curr_fi->ctx);
                        continue;
                    }
                }
            }

            // EPOLLOUT
            /* Epollout signals when TCP handshake is over, so the socket is ready to be written to */
            /* When epollout signals, we check errno to see if `connect` succeeded */
            if(worker->event_buffers[j].events & EPOLLOUT) {
                // upstream receives data
                if (curr_fi->type == FD_UPSTREAM) {
                    // handle initial connection
                    if(curr_fi->ctx->status == CTX_CONNECTING_TO_UPSTREAM) {
                        int err;
                        socklen_t len = sizeof(err);

                        getsockopt(curr_fi->fd, SOL_SOCKET, SO_ERROR, &err, &len);

                        // connection failed
                        if(err != 0) {
                            curr_fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: UPSTREAM UP, BUT DENIED CONNECTION.");
                            cleanup_upstream(worker, curr_fi->ctx);
                            continue;
                        }

                        logg("2. connection to upstream good");
                        // connection successful
                        if(err == 0) {
                            curr_fi->ctx->status = CTX_FORWARDING_REQUEST;
                        }

                        // fallthrough
                    }

                    // sending request from client to upstream
                    if(curr_fi->ctx->status == CTX_FORWARDING_REQUEST) {
                        int rv = send_chunk(curr_fi->ctx->upstream->fd, curr_fi->ctx->cli_buf, curr_fi->ctx->cli_buflen, &(curr_fi->ctx->cli_sentoffset));

                        // request succesfully sent
                        if(rv == 1) {
                            logg("3. data successfully sent to upstream.");
                            curr_fi->ctx->status = CTX_WAITING_RESPONSE;

                            // upstream doesn't need EPOLLOUT anymore, now only EPOLLIN
                            struct epoll_event ev = {
                                .events = EPOLLIN | EPOLLET,
                                .data.ptr = curr_fi->ctx->upstream,
                            };
                            epoll_ctl(worker->efd, EPOLL_CTL_MOD, curr_fi->fd, &ev);
                        } else if(rv == -1) {
                            // error while sending data
                            curr_fi->ctx->status = CTX_ERROR;
                            logg("[CTX_ERROR]: sending request to upstream failed.");
                            cleanup_upstream(worker, curr_fi->ctx);
                        }

                        continue;
                    }
                }
            }
        }

        // all connections respected, safe shutdown
        if(start_shutdown && worker->num_conn <= 0) {
            break;
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

    // TODO: hardcoded, should be configurable in the config
    int respawn_attempts = 3;

    // used as a flag for sending signals only once
    int shutdown_signal_sent = 0;

    while(worker_count > 0) {
        if(master_shutdown && !shutdown_signal_sent) {
            logg("[Master]: Shutdown signal received. Sending SIGTERM to workers.");

            for(int i=0;i<n;i++) {
                kill(worker_array[i].pid, SIGTERM);
            }

            shutdown_signal_sent = 1;
        }

        // reaping all children
        while((p = waitpid(-1, &status, WNOHANG)) > 0) {
            for(int i=0;i<n;i++) {
                if(worker_array[i].pid == p) {
                    worker_count--;

                    // parsing status
                    if(WIFEXITED(status)) {
                        int exit_code = WEXITSTATUS(status);

                        if(exit_code == 0) {
                            // no respawn, send SIGTERM to others
                            logg("[WORKER (idx = %d, pid = %d)]: exited normally (0), graceful exit.", i, p);

                            for(int j=0;j<n;j++) {
                                if(worker_array[j].pid != p) kill(worker_array[j].pid, SIGTERM);
                            }
                        } else {
                            logg("[WORKER (idx = %d, pid = %d)]: exited with code %d, respawning...", i, p, exit_code);

                            logg("RESPAWNING WORKER...");
                            for(int t=0;t<respawn_attempts;t++) {
                                WorkerProcess new_worker = spawn_worker(listenerfd, i);

                                // success
                                if(new_worker.pid != -1) {
                                    worker_array[i] = new_worker;
                                    worker_count++;
                                    break;
                                }
                            }
                        }
                    } else if(WIFSIGNALED(status)) {
                        int sig = WTERMSIG(status);

                        logg("[WORKER (idx = %d, pid = %d)]: killed by signal %d, respawning...", i, p, sig);

                        logg("RESPAWNING WORKER...");
                        for(int t=0;t<respawn_attempts;t++) {
                            WorkerProcess new_worker = spawn_worker(listenerfd, i);

                            // success
                            if(new_worker.pid != -1) {
                                worker_array[i] = new_worker;
                                worker_count++;
                                break;
                            }
                        }
                    } else {
                        logg("[WORKER (idx = %d, pid = %d)]: exited abnormally, respawning...", i, p);

                        logg("RESPAWNING WORKER...");
                        for(int t=0;t<respawn_attempts;t++) {
                            WorkerProcess new_worker = spawn_worker(listenerfd, i);

                            // success
                            if(new_worker.pid != -1) {
                                worker_array[i] = new_worker;
                                worker_count++;
                                break;
                            }
                        }
                    }

                    break;
                }
            }
        }

        if(p == -1 && errno == ECHILD) { 
            break;
        }

        sleep(1);
    }

    logg("[Master]: No workers alive. Terminating...");
}

/* Cleans up the worker process:
   - frees the linked list connection contexes for through cleanup_client for each client
   - closes all used sockets */
void cleanup_worker(WorkerProcess* worker) {
    struct ConnectionNode* curr_node = worker->conn_head;
    
    while(curr_node) {
        struct ConnectionNode* next = curr_node->next;
        cleanup_client(worker, curr_node->ctx);
        curr_node = next;
    }

    // logg("Closing epoll fd = %d", worker->efd);
    if(worker->efd >= 0) close(worker->efd);

    // closes child copy of listenerfd
    if(worker->listener_fdi && worker->listener_fdi->fd >= 0)
        close(worker->listener_fdi->fd);

    if(worker->shutdown_fdi && worker->shutdown_fdi->fd >= 0)
        close(worker->shutdown_fdi->fd);

    // free FDInfo structures
    if(worker->listener_fdi) free(worker->listener_fdi);
    if(worker->shutdown_fdi) free(worker->shutdown_fdi);
}