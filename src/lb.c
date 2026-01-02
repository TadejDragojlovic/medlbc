#include "utils.h"
#include "worker.h"
#include "server.h"

#define N_WORKERS 3
#define PORT "3456"

int main(int argc, char* argv[]) {
    int listenerfd;

    if((listenerfd = create_server_socket(PORT, 1)) == -1) exit(1);
    if(server_listen(listenerfd) == -1) exit(1);

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


/* TODO: STA RADITI SAD
1. algoritam za biranje upstream servera za konekciju sa klijentom (round robin load balancing)
2. client.c ?

VECI KORACI: 
- health check mehanizam

BONUS:
- citanje config fajla (kao nginx sto radi)
- graceful exit logika za signale (child procesi)
    * da li treba da koristim `kill(pid, signal_koji_zelim)` za workere?
*/
