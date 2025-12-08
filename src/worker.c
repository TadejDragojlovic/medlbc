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
    // TODO: find a better solution
    int upstream_fds[100] = {0}, num_upstream_sockets = 0;

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
                int is_upstream = 0;

                // by upstream socket
                for(int k=0;k<num_upstream_sockets;k++) {
                    if(upstream_fds[k] == worker->event_buffers[j].data.fd) {
                        logg("CONNECTING TO UPSTREAM FAILED.");
                        is_upstream = 1;

                        // TODO: implement proper cleanup
                        epoll_ctl(worker->efd, EPOLL_CTL_DEL, worker->event_buffers[j].data.fd, NULL);
                        close(worker->event_buffers[j].data.fd);
                        // adjusting upstream socket list
                        int found = 0;
                        for(int k=0;k<num_upstream_sockets;k++) {
                            if(upstream_fds[k] == worker->event_buffers[j].data.fd) {
                                found=1;
                                num_upstream_sockets--;
                            }

                            if(found) upstream_fds[k] = upstream_fds[k+1];
                        }
                        break;
                    }
                }

                // by client
                if(is_upstream == 0) close_conn(worker, worker->event_buffers[j].data.fd);

                continue;
            }

            // client sent data, forward request to backend to process it
            if(worker->event_buffers[j].events & EPOLLIN) {
                int upstream_sockfd;
                if((upstream_sockfd = connect_to_upstream(upstream_servers[0])) == -1) {
                    logg("ERROR IN TRYING TO ESTABLISH A CONNECTION TO BACKEND.");
                    continue;
                }

                // nonblocking socket for upstream
                struct epoll_event ev = {
                    .events = EPOLLOUT | EPOLLIN | EPOLLET,
                    .data.fd = upstream_sockfd
                };
                epoll_ctl(worker->efd, EPOLL_CTL_ADD, upstream_sockfd, &ev);

                upstream_fds[num_upstream_sockets++] = upstream_sockfd;
            }

            // EPOLLOUT on upstream sockets
            if(worker->event_buffers[j].events & EPOLLOUT) {
                int err;
                socklen_t len = sizeof(err);

                getsockopt(worker->event_buffers[j].data.fd, SOL_SOCKET, SO_ERROR, &err, &len);

                // Connection succesfull
                if(err == 0) {
                    // TODO
                    logg("TODO HANDLE CLIENT REQUEST");
                } else {
                    logg("ERROR CONNECTING TO THE UPSTREAM SERVER");
                }

                // TODO: implement proper cleanup
                epoll_ctl(worker->efd, EPOLL_CTL_DEL, worker->event_buffers[j].data.fd, NULL);
                close(worker->event_buffers[j].data.fd);
                // adjusting upstream socket list
                int found = 0;
                for(int k=0;k<num_upstream_sockets;k++) {
                    if(upstream_fds[k] == worker->event_buffers[j].data.fd) {
                        found=1;
                        num_upstream_sockets--;
                    }

                    if(found) upstream_fds[k] = upstream_fds[k+1];
                }

                continue;
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