#include "csapp.h"
#include "client_registry.h"
#include "debug.h"

typedef struct client_registry{
	CLIENT *buf[MAX_CLIENTS];
	int count;
	sem_t mutex;
	sem_t empty;
} CLIENT_REGISTRY;

// CLIENT_REGISTRY cr;

/*
 * Initialize a new client registry.
 *
 * @return  the newly initialized client registry, or NULL if initialization
 * fails.
 */
CLIENT_REGISTRY *creg_init(){
	debug("Initializing client registry.");
	CLIENT_REGISTRY *cr;
	if((cr = (CLIENT_REGISTRY *) malloc(sizeof(CLIENT_REGISTRY))) == NULL){
		debug("malloc Error when Initializing CLIENT_REGISTRY");
		return NULL; // Initialization fails
	}
	memset(&cr->buf, 0, sizeof(CLIENT *)*MAX_CLIENTS);
	cr->count = 0;
	Sem_init(&cr->mutex, 0, 1);
	Sem_init(&cr->empty, 0 ,1);
	return cr;
}

/*
 * Finalize a client registry, freeing all associated resources.
 * This method should not be called unless there are no currently
 * registered clients.
 *
 * @param cr  The client registry to be finalized, which must not
 * be referenced again.
 */
void creg_fini(CLIENT_REGISTRY *cr){
	// for(int i = 0; i < MAX_CLIENTS; i++){
	// 	if(cr->buf[i] != NULL){
	// 		Close(client_get_fd(cr->buf[i]));
	// 	}
	// }
	if(cr != NULL){
		Free(cr);
	}
}

/*
 * Register a client file descriptor.
 * If successful, returns a reference to the the newly registered CLIENT,
 * otherwise NULL.  The returned CLIENT has a reference count of one.
 *
 * @param cr  The client registry.
 * @param fd  The file descriptor to be registered.
 * @return a reference to the newly registered CLIENT, if registration
 * is successful, otherwise NULL.
 */
CLIENT *creg_register(CLIENT_REGISTRY *cr, int fd){
	CLIENT *cp = client_create(cr, fd); // increase reference count
	P(&cr->mutex);
	// Insert fd into a NULL spot in array
	for(int i = 0; i < MAX_CLIENTS; i++){
		if(cr->buf[i] == NULL){
			cr->buf[i] = cp;
			if(cr->count == 0){
				P(&cr->empty);
			}
			cr->count++;
			debug("Register client fd %d (total connected: %d)", fd, cr->count);
			V(&cr->mutex);
			return cp;
		}
	}
	debug("Failed to register client fd %d (total connected: %d)", fd, cr->count);
	V(&cr->mutex);
	return NULL;
}


/*
 * Unregister a CLIENT, removing it from the registry.
 * The client reference count is decreased by one to account for the
 * pointer discarded by the client registry.  If the number of registered
 * clients is now zero, then any threads that are blocked in
 * creg_wait_for_empty() waiting for this situation to occur are allowed
 * to proceed.  It is an error if the CLIENT is not currently registered
 * when this function is called.
 *
 * @param cr  The client registry.
 * @param client  The CLIENT to be unregistered.
 * @return 0  if unregistration succeeds, otherwise -1.
 */
int creg_unregister(CLIENT_REGISTRY *cr, CLIENT *client){
	P(&cr->mutex);
	for(int i=0; i<MAX_CLIENTS; i++){
		if(cr->buf[i] == client){
			debug("Unregister client fd %d (total connected %d)", client_get_fd(client), cr->count);
			client_unref(client, "because client is being unregistered.");
			cr->buf[i] = NULL;
			cr->count--;
			if(cr->count == 0){
				V(&cr->empty);
			}
			V(&cr->mutex);
			return 0;
		}
	}
	V(&cr->mutex);
	return -1;
}

/*
 * Given a username, return the CLIENT that is logged in under that
 * username.  The reference count of the returned CLIENT is
 * incremented by one to account for the reference returned.
 *
 * @param cr  The registry in which the lookup is to be performed.
 * @param user  The username that is to be looked up.
 * @return the CLIENT currently registered under the specified
 * username, if there is one, otherwise NULL.
 */
CLIENT *creg_lookup(CLIENT_REGISTRY *cr, char *user){
	P(&cr->mutex);
	CLIENT *client = NULL;

	// iterate through all logged in clients and check their usernames
	for(int i=0; i<MAX_CLIENTS; i++){
		if(cr->buf[i] != NULL){ // if there is a client pointer there
			if( client_get_player(cr->buf[i]) != NULL ){ // If that client is logged in
				int r = strcmp(user, player_get_name(client_get_player(cr->buf[i])));
				if(r == 0){ // the usernames are equal
					client = client_ref(cr->buf[i], "for reference being returned by creg_lookup()");
					V(&cr->mutex);
					return client;
				}
			}
		}
	}
	V(&cr->mutex);
	return client;
}

/*
 * Return a list of all currently logged in players.  The result is
 * returned as a malloc'ed array of PLAYER pointers, with a NULL
 * pointer marking the end of the array.  It is the caller's
 * responsibility to decrement the reference count of each of the
 * entries and to free the array when it is no longer needed.
 *
 * @param cr  The registry for which the set of usernames is to be
 * obtained.
 * @return the list of players as a NULL-terminated array of pointers.
 */
PLAYER **creg_all_players(CLIENT_REGISTRY *cr){
	P(&cr->mutex);

	// Count the number of players inside cr
	int count = 0;
	for(int i=0; i < MAX_CLIENTS; i++){
		PLAYER *player;
		if(cr->buf[i] != NULL){
			if((player = client_get_player(cr->buf[i])) != NULL )
				count++;
		}
	}

	// Calloc Result Array
	PLAYER **result = calloc(count + 1, sizeof(PLAYER *));
	if(result == NULL){
		debug("calloc error.");
		return NULL;
	}

	// Insert all players into result array
	int index = 0;
	for(int i=0; i < MAX_CLIENTS; i++){
		if(cr->buf[i] != NULL){
			if( (client_get_player(cr->buf[i])) != NULL ){
				result[index] = client_get_player(cr->buf[i]);
				player_ref(result[index], "for reference being added to players list");
				index++;
			}
		}
	}
	result[count] = NULL; // NULL terminator

	V(&cr->mutex);
	return result;
}

/*
 * A thread calling this function will block in the call until
 * the number of registered clients has reached zero, at which
 * point the function will return.  Note that this function may be
 * called concurrently by an arbitrary number of threads.
 *
 * @param cr  The client registry.
 */
void creg_wait_for_empty(CLIENT_REGISTRY *cr){
	P(&cr->empty);
}

/*
 * Shut down (using shutdown(2)) all the sockets for connections
 * to currently registered clients.  The clients are not unregistered
 * by this function.  It is intended that the clients will be
 * unregistered by the threads servicing their connections, once
 * those server threads have recognized the EOF on the connection
 * that has resulted from the socket shutdown.
 *
 * @param cr  The client registry.
 */
void creg_shutdown_all(CLIENT_REGISTRY *cr){
	P(&cr->mutex);
	for(int i = 0; i < MAX_CLIENTS; i++){
		if(cr->buf[i] != NULL){
			debug("Shutting down client %d", client_get_fd(cr->buf[i]));
			shutdown(client_get_fd(cr->buf[i]),SHUT_RD);
		}
	}
	V(&cr->mutex);
}
