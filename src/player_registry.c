#include "player_registry.h"
#include "csapp.h"
#include "debug.h"

typedef struct pmap {
	PLAYER *player;
	char *name;
} PMAP;


typedef struct player_registry {
	PMAP **buf;
	int length;
	int num_users;
	sem_t mutex;
} PLAYER_REGISTRY;

/*
 * Initialize a new player registry.
 *
 * @return the newly initialized PLAYER_REGISTRY, or NULL if initialization
 * fails.
 */
PLAYER_REGISTRY *preg_init(void){
	debug("Initializing player registry");
	PLAYER_REGISTRY *pr = (PLAYER_REGISTRY *) Malloc(sizeof(PLAYER_REGISTRY));
	pr->buf = NULL;
	pr->num_users = 0;
	pr->length = 0;
	Sem_init(&pr->mutex, 0 ,1);
	return pr;
}

/*
 * Finalize a player registry, freeing all associated resources.
 *
 * @param cr  The PLAYER_REGISTRY to be finalized, which must not
 * be referenced again.
 */
void preg_fini(PLAYER_REGISTRY *preg){
	if(preg != NULL){
		P(&preg->mutex);
	}
	for(int i = 0; i < preg->length; i++){
		if(preg->buf[i] != NULL){
			if(preg->buf[i]->player != NULL){
				player_unref(preg->buf[i]->player, "because player registry is being finalized");
			}
			if(preg->buf[i]->name != NULL){
				Free(preg->buf[i]->name);
			}
			if(preg->buf[i] != NULL){
				Free(preg->buf[i]);
			}
		}
	}
	if(preg->buf != NULL){
		Free(preg->buf);
	}
	if(preg != NULL){
		Free(preg);
	}
}

/*
 * Register a player with a specified user name.  If there is already
 * a player registered under that user name, then the existing registered
 * player is returned, otherwise a new player is created.
 * If an existing player is returned, then its reference count is increased
 * by one to account for the returned pointer.  If a new player is
 * created, then the returned player has reference count equal to two:
 * one count for the pointer retained by the registry and one count for
 * the pointer returned to the caller.
 *
 * @param name  The player's user name, which is copied by this function.
 * @return A pointer to a PLAYER object, in case of success, otherwise NULL.
 *
 */
PLAYER *preg_register(PLAYER_REGISTRY *preg, char *name){
	if(preg == NULL || name == NULL){
		return NULL;
	}
	P(&preg->mutex);

	// preg_register is adding a [player] with [name] to my (PMAP *) buffer
	if(preg->length == 0 || preg->buf == NULL){
		preg->buf = (PMAP **) Malloc(10*sizeof(PMAP *) + sizeof(PMAP **));
		if(preg->buf == NULL){
			return NULL;
		}
		for(int i = 0; i < 10; i++){
			preg->buf[i] = NULL;
		}
		preg->length = 10;
	}

	// search for name in preg
	for(int i=0; i<preg->num_users; i++){
		if(preg->buf[i] != NULL && preg->buf[i]->name != NULL && preg->buf[i]->player != NULL){
			if(strcmp(preg->buf[i]->name, name) == 0){
				debug("Player exists with that name");
				PLAYER *player = player_ref(preg->buf[i]->player, "for new reference to existing player");
				V(&preg->mutex);
				return player;
			}
		}
	}
	debug("Player with that name does not yet exist");

	// prepare to add to buffer
	if(preg->num_users >= preg->length){
		// need to expand list
		preg->buf = (PMAP **) Realloc(preg->buf, (preg->length + 10)*sizeof(PMAP *) + sizeof(PMAP **));

		// initialize extended space
		for(int i = preg->length; i < preg->length + 10; i++){
			preg->buf[i] = NULL;
		}
		preg->length += 10;
	}

	// create a new PMAP
	PMAP *pmap = (PMAP *) Malloc(sizeof(PMAP));
	pmap->player = NULL;
	pmap->name = NULL;

	// put player in
	pmap->player = player_create(name);
	if(pmap->player == NULL){
		if(pmap != NULL){
			Free(pmap);
		}
		V(&preg->mutex);
		return NULL;
	}

	// put name in
	char *pname = (char *) Malloc(strlen(name) + 1);
	strcpy(pname, name);
	pname[strlen(name)] = '\0';
	if(name == NULL){
		if(pmap != NULL){
			Free(pmap);
		}
		V(&preg->mutex);
		return NULL;
	}

	pmap->name = pname;

	printf("%p\n", preg->buf);
	preg->buf[preg->num_users] = pmap;

	preg->num_users++;

	player_ref(pmap->player, "for reference being retained by player registry");

	V(&preg->mutex);
	return pmap->player;
}

