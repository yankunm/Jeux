#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>

#include "csapp.h"

#include "debug.h"
#include "protocol.h"
#include "server.h"
#include "client_registry.h"
#include "player_registry.h"
#include "jeux_globals.h"

#ifdef DEBUG
int _debug_packets_ = 1;
#endif

static int listenfd, *connfdp = NULL;
// static pthread_attr_t attr;

static void terminate(int status);

void handler(int signum){
    Close(listenfd);
    if(connfdp != NULL){
        Free(connfdp);
    }
    // pthread_attr_destroy(&attr);
    terminate(EXIT_SUCCESS);
}


// // what happens if we register too many? nothing?
// void *thread(void *vargp){
//     int i, connfd;
//     connfd = *((int *) vargp);
//     CLIENT *cp = client_create(client_registry, connfd);
//     printf("Client: %d\n", client_get_fd(cp));
//     for(i = 0; i < 70; i++){
//         creg_register(client_registry, connfd);
//         printf("Just registered it in client_registry.\n");
//         connfd++;
//         // int i = creg_unregister(client_registry, cp); // If this succeeds, then it suggests that
//         // // "client_create" returns a pointer to the created CLIENT
//         // // then "creg_register" returns a pointer to the location of the CLIENT In client_registry,
//         // // which are not the same. I think client_registry is "indexed" using file descriptors.

//         // //Experiment:
//         // // when I call creg_register using connfd, I am storing connfd inside client_registry
//         // //  creg_register calls client_create(client_registry, connfd) to create a client object itself
//         // //  so the one I created previously in service thread (which is at a different address), did not work.

//         // //Conclusion:
//         // // client_create(client_registry, connfd) creates an instance of client
//         // // creg_register(client_registry, connfd) creates an instance of client then
//         // // puts that into client_registry.
//         // printf("Just unregistered it from client_registry.\n");
//     }
//     return 0;
// }
/*
 * "Jeux" game server.
 *
 * Usage: jeux <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    if(argc < 3){ // if -p exists and is valid, argc must be at least 3
        exit(0);
    }
    char *port_number = NULL; // port number we take from the CLI
    int index = 1; // index in argv array
    while(index < argc){
        if(strcmp(argv[index], "-p") == 0){
            // found an -p, if it is not the last argument then we have found
            if(index < argc - 1){
                port_number = argv[index + 1];
                break;
            }
        }
        index++;
    }
    if(port_number == NULL){ // if port if empty then terminate (depends on whether port number can be 0)
        exit(0);
    }


    // Perform required initializations of the client_registry and
    // player_registry.
    client_registry = creg_init();
    player_registry = preg_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function jeux_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.
    struct sigaction act, oldact;
    sigset_t block_mask;
    sigemptyset(&block_mask);
    act.sa_mask = block_mask;
    act.sa_handler = handler;
    act.sa_flags = 0;

    if(sigaction(SIGHUP, &act, &oldact) < 0){
        terminate(EXIT_FAILURE);
    }
    // if(sigaction(SIGINT, &act, &oldact) < 0){ // JUST FOR MY CONVENIENCE
    //     terminate(EXIT_FAILURE);
    // }

    // Set up listenfd
    struct sockaddr_storage clientaddr; socklen_t clientlen;
    pthread_t tid;

    listenfd = Open_listenfd(argv[2]); /* Pass in Port Number */ // TODO: free listenfd
    debug("Jeux server listening on port %s", argv[2]);

    // _______

    // pthread_attr_init(&attr);
    // pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    // ______
    // Master thread while loop
    while(1){
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen); // TODO: free connfd

        // Create a thread to handle this Request
        Pthread_create(&tid, NULL, jeux_client_service, connfdp);
        // Pthread_create(&tid, NULL, thread, connfdp);
    }

    // fprintf(stderr, "You have to finish implementing main() "
	//     "before the Jeux server will function.\n");

    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    // Shutdown all client connections.
    // This will trigger the eventual termination of service threads.
    creg_shutdown_all(client_registry);

    debug("%ld: Waiting for service threads to terminate...", pthread_self());
    creg_wait_for_empty(client_registry);
    debug("%ld: All service threads terminated.", pthread_self());

    // Finalize modules.
    creg_fini(client_registry);
    preg_fini(player_registry);

    debug("%ld: Jeux server terminating", pthread_self());
    exit(status);
}
