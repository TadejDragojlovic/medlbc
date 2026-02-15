#include "utils.h"
#include "worker.h"
#include "server.h"

#include <sys/mman.h>

#define N_WORKERS 1
#define PORT "3456"

_Atomic uint64_t *rrindex = NULL;

int main(int argc, char* argv[]) {
    int listenerfd;

    if((listenerfd = create_server_socket(PORT, 1)) == -1) exit(1);
    if(server_listen(listenerfd) == -1) exit(1);

    // signal handling for master process
    if(setup_sigaction(SIGINT, master_sighandler, SA_RESTART) == -1 ||
       setup_sigaction(SIGTERM, master_sighandler, SA_RESTART) == -1) {
        exit(EXIT_FAILURE);
    }

    // setup shared memory for the round robin counter
    rrindex = mmap(NULL, sizeof(*rrindex), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(rrindex == MAP_FAILED) { perror("mmap rrindex"); exit(EXIT_FAILURE); }
    atomic_init(rrindex, 0);

    // 1. Master process starts up N_WORKERS
    WorkerProcess* worker_array = init_workers(listenerfd, N_WORKERS);
    if(!worker_array) {
        // TODO: cleanup
        exit(1);
    }

    // 2. Master process manages workers (monitoring)
    manage_workers(worker_array, N_WORKERS, listenerfd);

    // TODO: cleanup
    free(worker_array);

    return 0;
}


/* TODO:
1. client.c ?

VECI KORACI: 
- health check mehanizam

BONUS:
- citanje config fajla (kao nginx sto radi)
*/
