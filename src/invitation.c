#include "csapp.h"
#include "client_registry.h"
#include "debug.h"

typedef struct invitation {
	INVITATION_STATE state;
	int refcnt;
	CLIENT *source;
	CLIENT *target;
	GAME_ROLE source_role;
	GAME_ROLE target_role;
	GAME *game;
	sem_t mutex;
} INVITATION;

/*
 * Create an INVITATION in the OPEN state, containing reference to
 * specified source and target CLIENTs, which cannot be the same CLIENT.
 * The reference counts of the source and target are incremented to reflect
 * the stored references.
 *
 * @param source  The CLIENT that is the source of this INVITATION.
 * @param target  The CLIENT that is the target of this INVITATION.
 * @param source_role  The GAME_ROLE to be played by the source of this INVITATION.
 * @param target_role  The GAME_ROLE to be played by the target of this INVITATION.
 * @return a reference to the newly created INVITATION, if initialization
 * was successful, otherwise NULL.
 */
INVITATION *inv_create(CLIENT *source, CLIENT *target, GAME_ROLE source_role, GAME_ROLE target_role){
	INVITATION *invitation = (INVITATION *) Malloc(sizeof(INVITATION));
	invitation->state = INV_OPEN_STATE;
	invitation->refcnt = 0; // should this be 0 or 1?
	invitation->source = client_ref(source, "as source of new invitation");
	invitation->target = client_ref(target, "as target of new invitation");
	invitation->source_role = source_role;
	invitation->target_role = target_role;
	invitation->game = NULL;
	Sem_init(&invitation->mutex, 0, 1);
	inv_ref(invitation, "for newly created invitation");
	return invitation;
}

/*
 * Increase the reference count on an invitation by one.
 *
 * @param inv  The INVITATION whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same INVITATION object that was passed as a parameter.
 */
INVITATION *inv_ref(INVITATION *inv, char *why){
	P(&inv->mutex);
	inv->refcnt++;
	debug("Increase reference count on invitation %p (%d -> %d) %s", inv, inv->refcnt - 1, inv->refcnt, why);
	V(&inv->mutex);
	return inv;
}

/*
 * Decrease the reference count on an invitation by one.
 * If after decrementing, the reference count has reached zero, then the
 * invitation and its contents are freed.
 *
 * @param inv  The INVITATION whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 *
 */
void inv_unref(INVITATION *inv, char *why){
	P(&inv->mutex);

	inv->refcnt--;
	debug("Decrease reference count on invitation %p (%d -> %d) %s", inv, inv->refcnt + 1, inv->refcnt, why);

	if(inv->refcnt == 0){
		debug("Free invitation %p", inv);
		client_unref(inv->source, "because invitation is being freed");
		client_unref(inv->target, "because invitation is being freed");
		if(inv->game != NULL){
			game_unref(inv->game, "because invitation is being freed");
		}
		if(inv != NULL){
			Free(inv);
		}
		return;
	}

	V(&inv->mutex);
}

/*
 * Get the CLIENT that is the source of an INVITATION.
 * The reference count of the returned CLIENT is NOT incremented,
 * so the CLIENT reference should only be regarded as valid as
 * long as the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the CLIENT that is the source of the INVITATION.
 */
CLIENT *inv_get_source(INVITATION *inv){
	return inv->source;
}

/*
 * Get the CLIENT that is the target of an INVITATION.
 * The reference count of the returned CLIENT is NOT incremented,
 * so the CLIENT reference should only be regarded as valid if
 * the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the CLIENT that is the target of the INVITATION.
 */
CLIENT *inv_get_target(INVITATION *inv){
	return inv->target;
}

/*
 * Get the GAME_ROLE to be played by the source of an INVITATION.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME_ROLE played by the source of the INVITATION.
 */
GAME_ROLE inv_get_source_role(INVITATION *inv){
	return inv->source_role;
}

/*
 * Get the GAME_ROLE to be played by the target of an INVITATION.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME_ROLE played by the target of the INVITATION.
 */
GAME_ROLE inv_get_target_role(INVITATION *inv){
	return inv->target_role;
}

/*
 * Get the GAME (if any) associated with an INVITATION.
 * The reference count of the returned GAME is NOT incremented,
 * so the GAME reference should only be regarded as valid as long
 * as the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME associated with the INVITATION, if there is one,
 * otherwise NULL.
 */
GAME *inv_get_game(INVITATION *inv){
	return inv->game;
}

/*
 * Accept an INVITATION, changing it from the OPEN to the
 * ACCEPTED state, and creating a new GAME.  If the INVITATION was
 * not previously in the the OPEN state then it is an error.
 *
 * @param inv  The INVITATION to be accepted.
 * @return 0 if the INVITATION was successfully accepted, otherwise -1.
 */
int inv_accept(INVITATION *inv){
	P(&inv->mutex);
	if(inv->state != INV_OPEN_STATE){
		debug("The game is not open in inv_accept()");
		V(&inv->mutex);
		return -1;
	}
	inv->state = INV_ACCEPTED_STATE;
	inv->game = game_create();
	if(inv->game == NULL){
		debug("Failed to create a game in inv_accept()");
		inv->state=INV_OPEN_STATE;
		V(&inv->mutex);
		return -1;
	}
	V(&inv->mutex);
	return 0;
}

/*
 * Close an INVITATION, changing it from either the OPEN state or the
 * ACCEPTED state to the CLOSED state.  If the INVITATION was not previously
 * in either the OPEN state or the ACCEPTED state, then it is an error.
 * If INVITATION that has a GAME in progress is closed, then the GAME
 * will be resigned by a specified player.
 *
 * @param inv  The INVITATION to be closed.
 * @param role  This parameter identifies the GAME_ROLE of the player that
 * should resign as a result of closing an INVITATION that has a game in
 * progress.  If NULL_ROLE is passed, then the invitation can only be
 * closed if there is no game in progress.
 * @return 0 if the INVITATION was successfully closed, otherwise -1.
 */
int inv_close(INVITATION *inv, GAME_ROLE role){
	P(&inv->mutex);
	if((inv->state != INV_OPEN_STATE) && (inv->state != INV_ACCEPTED_STATE) ){
		debug("invitation %p not open or accepted", inv);
		V(&inv->mutex);
		return -1;
	}
	if(inv->game != NULL){
		// there's game in progress
		if(role == NULL_ROLE){
			debug("NULL_ROLE when game is in progress in inv_close()");
			V(&inv->mutex);
			return -1;
		}
		inv->state = INV_CLOSED_STATE;
		if(game_resign(inv->game, role) != 0){
			debug("game_resign error in inv_close()");
			V(&inv->mutex);
			return -1;
		}
	} else {
		// there's no game in progress
		inv->state = INV_CLOSED_STATE;
	}
	V(&inv->mutex);
	return 0;
}






