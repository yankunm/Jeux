#include "client_registry.h"
#include "jeux_globals.h"
#include "protocol.h"
#include "csapp.h"
#include "debug.h"

// mutex lock of network transmission of the system
static sem_t network;


typedef struct client {
	int connfd;
	int refcnt;
	PLAYER *player;
	INVITATION **invlist;
	int invlength;
	sem_t mutex; // client's mutex
} CLIENT;

static void init_network(void){
	Sem_init(&network, 0, 1);
}

/**************************** BASICS ************************************/
/*
 * Create a new CLIENT object with a specified file descriptor with which
 * to communicate with the client.  The returned CLIENT has a reference
 * count of one and is in the logged-out state.
 *
 * @param creg  The client registry in which to create the client.
 * @param fd  File descriptor of a socket to be used for communicating
 * with the client.
 * @return  The newly created CLIENT object, if creation is successful,
 * otherwise NULL.
 */
CLIENT *client_create(CLIENT_REGISTRY *creg, int fd){
	CLIENT *client = (CLIENT *) Malloc(sizeof(CLIENT));
	client->connfd = fd;
	client->refcnt = 0;
	client->player = NULL;
	client->invlist = NULL;
	client->invlength = 0; // length of invitations array
	Sem_init(&client->mutex, 0, 1);
	client_ref(client, "for newly created client");
	pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, init_network);
	return client;
}

/*
 * Increase the reference count on a CLIENT by one.
 *
 * @param client  The CLIENT whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same CLIENT that was passed as a parameter.
 */
CLIENT *client_ref(CLIENT *client, char *why){
	P(&client->mutex);
	client->refcnt++;
	debug("Increase reference count on client %p (%d -> %d) %s", client, client->refcnt - 1, client->refcnt, why);
	V(&client->mutex);
	return client;
}

/*
 * Decrease the reference count on a CLIENT by one.  If after
 * decrementing, the reference count has reached zero, then the CLIENT
 * and its contents are freed.
 *
 * @param client  The CLIENT whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 */
void client_unref(CLIENT *client, char *why){
	if(client != NULL){
		P(&client->mutex);
	}
	if(client != NULL){
		client->refcnt--;
		debug("Decrease reference count on client %p (%d -> %d) %s", client, client->refcnt + 1, client->refcnt, why);
	}
	if(client != NULL){
	if(client->refcnt == 0){
		debug("Free client %p", client);
		if(client->invlist != NULL){
			Free(client->invlist);
		}
		if(client != NULL){
			Free(client);
		}
		return;
	}
	}
	if(client != NULL){
		V(&client->mutex);
	}
}

/*
 * Get the PLAYER for the specified logged-in CLIENT.
 * The reference count on the returned PLAYER is NOT incremented,
 * so the returned reference should only be regarded as valid as long
 * as the CLIENT has not been freed.
 *
 * @param client  The CLIENT from which to get the PLAYER.
 * @return  The PLAYER that the CLIENT is currently logged in as,
 * otherwise NULL if the player is not currently logged in.
 */
PLAYER *client_get_player(CLIENT *client){
	return client->player;
}

/*
 * Get the file descriptor for the network connection associated with
 * this CLIENT.
 *
 * @param client  The CLIENT for which the file descriptor is to be
 * obtained.
 * @return the file descriptor.
 */
int client_get_fd(CLIENT *client){
	return client->connfd;
}

/**************************** COMMUNICATION ************************************/
/*
 * Send a packet to a client.  Exclusive access to the network connection
 * is obtained for the duration of this operation, to prevent concurrent
 * invocations from corrupting each other's transmissions.  To prevent
 * such interference, only this function should be used to send packets to
 * the client, rather than the lower-level proto_send_packet() function.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @param pkt  The header of the packet to be sent.
 * @param data  Data payload to be sent, or NULL if none.
 * @return 0 if transmission succeeds, -1 otherwise.
 */

// data is always Malloced, Free in caller
int client_send_packet(CLIENT *player, JEUX_PACKET_HEADER *pkt, void *data){
	// PKT already in Network Byte Order (210 server.c)
	P(&network);
	debug("Send packet (clientfd=%d, type=%d) for client %p", player->connfd, pkt->type, player);
	int i = proto_send_packet(player->connfd, pkt, data);
	V(&network);
	return i;
}

/*
 * Send an ACK packet to a client.  This is a convenience function that
 * streamlines a common case.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @param data  Pointer to the optional data payload for this packet,
 * or NULL if there is to be no payload.
 * @param datalen  Length of the data payload, or 0 if there is none.
 * @return 0 if transmission succeeds, -1 otherwise.
 */
int client_send_ack(CLIENT *client, void *data, size_t datalen){
	// client send ack
	static JEUX_PACKET_HEADER header;
	header.type = JEUX_ACK_PKT;
	header.id = 0;
	header.role = 0;
	header.size = htons(datalen);
	clockid_t clock_id = CLOCK_MONOTONIC;
	struct timespec tp;
	clock_gettime(clock_id, &tp);
	header.timestamp_sec = htonl(tp.tv_sec);
	header.timestamp_nsec = htonl(tp.tv_nsec);
	int i = client_send_packet(client, &header, data);
	return i;
}

/*
 * Send an NACK packet to a client.  This is a convenience function that
 * streamlines a common case.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @return 0 if transmission succeeds, -1 otherwise.
 */
int client_send_nack(CLIENT *client){
	// client send ack
	static JEUX_PACKET_HEADER header;
	header.type = JEUX_NACK_PKT;
	header.id = 0;
	header.role = 0;
	header.size = 0;
	clockid_t clock_id = CLOCK_MONOTONIC;
	struct timespec tp;
	clock_gettime(clock_id, &tp);
	header.timestamp_sec = htonl(tp.tv_sec);
	header.timestamp_nsec = htonl(tp.tv_nsec);
	int i = client_send_packet(client, &header, NULL);
	return i;
}


/**************************** LOGIN/LOGOUT ************************************/
/*
 * Log in this CLIENT as a specified PLAYER.
 * The login fails if the CLIENT is already logged in or there is already
 * some other CLIENT that is logged in as the specified PLAYER.
 * Otherwise, the login is successful, the CLIENT is marked as "logged in"
 * and a reference to the PLAYER is retained by it.  In this case,
 * the reference count of the PLAYER is incremented to account for the
 * retained reference.
 *
 * @param CLIENT  The CLIENT that is to be logged in.
 * @param PLAYER  The PLAYER that the CLIENT is to be logged in as.
 * @return 0 if the login operation is successful, otherwise -1.
 */
int client_login(CLIENT *client, PLAYER *player){
	// if client already logged in then fail
	if(client->player != NULL){
		return -1;
	}
	// TODO: if there is already some other CLIENT Logged in as the PLAYER
	// if another client holds this PLAYER *
	if(creg_lookup(client_registry, player_get_name(player)) != NULL){
		return -1;
	}

	// lock client  -- retaining player reference
	P(&client->mutex);
	debug("Log in client %p as player %p [%s]", client, player, player_get_name(player));
	client->player = player_ref(player, "for reference being retained by client");

	V(&client->mutex);
	return 0;
}


/*
 * Log out this CLIENT.  If the client was not logged in, then it is
 * an error.  The reference to the PLAYER that the CLIENT was logged
 * in as is discarded, and its reference count is decremented.  Any
 * INVITATIONs in the client's list are revoked or declined, if
 * possible, any games in progress are resigned, and the invitations
 * are removed from the list of this CLIENT as well as its opponents'.
 *
 * @param client  The CLIENT that is to be logged out.
 * @return 0 if the client was logged in and has been successfully
 * logged out, otherwise -1.
 */
int client_logout(CLIENT *client){
	// check if client is logged in
	if(client->player == NULL){
		return -1;
	}
	// P(&ordered_logout);
	debug("Log out client %p", client);

	player_unref(client->player, "because client is logging out");
	client->player = NULL;

	// INVITATIONS
	// revoke -> for invitations that just sent
	// decline -> for invitations that just received
	// resign -> for ongoing games
	// loop through all invitations in this client's list
	for(int i = 0; i < client->invlength; i++){
		if(client->invlist[i] != NULL){ // there's an invitation there
			int j = 0;
			if(client == inv_get_source(client->invlist[i])){
				// V(&client->mutex);
				j = client_revoke_invitation(client, i);
				// P(&client->mutex);
			} else {
				// V(&client->mutex);
				j = client_decline_invitation(client, i);
				// P(&client->mutex);
			}
			if(j == -1){
				// V(&client->mutex);
				j = client_resign_game(client, i);
				// printf("IT GOT TO HERE\n");
				// P(&client->mutex);
			}
		}
	}
	return 0;
}


/**************************** INVITE ************************************/
/*
 * Add an INVITATION to the list of outstanding invitations for a
 * specified CLIENT.  A reference to the INVITATION is retained by
 * the CLIENT and the reference count of the INVITATION is
 * incremented.  The invitation is assigned an integer ID,
 * which the client subsequently uses to identify the invitation.
 *
 * @param client  The CLIENT to which the invitation is to be added.
 * @param inv  The INVITATION that is to be added.
 * @return  The ID assigned to the invitation, if the invitation
 * was successfully added, otherwise -1.
 */
int client_add_invitation(CLIENT *client, INVITATION *inv){
	// P(&client->invlist_lock);
	if(client == NULL || inv == NULL){
		return -1;
	}
	// P(&client->mutex);
	P(&client->mutex);
	// find an available spot in the invitations array to store the new invitation
	// P(&client->mutex);
	if(client->invlength == 0 || client->invlist == NULL){
		client->invlist = (INVITATION **) Realloc(client->invlist, (10 * sizeof(INVITATION *)) + sizeof(INVITATION **));
		memset(client->invlist, 0, 10*sizeof(INVITATION *));
		client->invlength = 10;
	}
	int index = -1;
	for(int i = 0; i < client->invlength; i++){
		if(client->invlist[i] == NULL){
			index = i;
			break;
		}
	}
	// if invitations array already full then reallocate it
	if(index == -1){
		client->invlist = (INVITATION **) Realloc(client->invlist, (client->invlength + 10) * sizeof(INVITATION *) + sizeof(INVITATION **));
		// initialize added memory space
		for(int i = client->invlength; i < client->invlength+10; i++){
			client->invlist[i] = NULL;
		}
		index = client->invlength; // add to end
		client->invlength += 10; // update the length of invlist array
	}
	client->invlist[index] = inv_ref(inv, "for invitation being added to client's list");

	V(&client->mutex);
	return index;
}

/*
 * Remove an invitation from the list of outstanding invitations
 * for a specified CLIENT.  The reference count of the invitation is
 * decremented to account for the discarded reference.
 *
 * @param client  The client from which the invitation is to be removed.
 * @param inv  The invitation that is to be removed.
 * @return the CLIENT's id for the INVITATION, if it was successfully
 * removed, otherwise -1.
 */
int client_remove_invitation(CLIENT *client, INVITATION *inv){
	// P(&client->mutex); //
	// loop through the invitations list
	// printf("IT GOT HERE!!");
	for(int i = 0; i < client->invlength; i++){
		if(client->invlist[i] == inv){
			client->invlist[i] = NULL;
			inv_unref(inv, "Because invitation is being released by client");
			V(&client->mutex);
			return i;
		}
	}
	debug("invitation not found inside client's list");
	// V(&client->mutex);
	return -1;
}

/*
 * Make a new invitation from a specified "source" CLIENT to a specified
 * target CLIENT.  The invitation represents an offer to the target to
 * engage in a game with the source.  The invitation is added to both the
 * source's list of invitations and the target's list of invitations and
 * the invitation's reference count is appropriately increased.
 * An `INVITED` packet is sent to the target of the invitation.
 *
 * @param source  The CLIENT that is the source of the INVITATION.
 * @param target  The CLIENT that is the target of the INVITATION.
 * @param source_role  The GAME_ROLE to be played by the source of the INVITATION.
 * @param target_role  The GAME_ROLE to be played by the target of the INVITATION.
 * @return the ID assigned by the source to the INVITATION, if the operation
 * is successful, otherwise -1.
 */
int client_make_invitation(CLIENT *source, CLIENT *target,
			   GAME_ROLE source_role, GAME_ROLE target_role){
	debug("[%d] Make an invitation", source->connfd);
	INVITATION *invitation = inv_create(source, target, source_role, target_role);

	// P(&source->mutex);
	debug("[%d] add invitation as source", source->connfd);
	int sourceid = client_add_invitation(source, invitation);
	if(sourceid == -1){
		debug("client_add_invitation #1 failed in client_make_invitation");
		return -1;
	}
	// V(&source->mutex);

	// P(&target->mutex);
	debug("[%d] add invitation as target", target->connfd);
	int targetid = client_add_invitation(target, invitation);
	if(targetid == -1){
		debug("client_add_invitation #2 failed in client_make_invitation");
		return -1;
	}
	// V(&target->mutex);

	// char *name = player_get_name(target->player);
	// printf("the length of name is: %ld\n", strlen(name));
	// printf("the length of player_get_name is: %ld\n", strlen(player_get_name(target->player)));
	// send invited packet to target
	static JEUX_PACKET_HEADER header;
	header.type = JEUX_INVITED_PKT;
	header.id = targetid;
	header.role = target_role;
	header.size = htons(strlen(player_get_name(source->player)));
	clockid_t clock_id = CLOCK_MONOTONIC;
	struct timespec tp;
	clock_gettime(clock_id, &tp);
	header.timestamp_sec = htonl(tp.tv_sec);
	header.timestamp_nsec = htonl(tp.tv_nsec);
	int i = client_send_packet(target, &header, player_get_name(source->player));
	if(i == -1){
		return -1;
	}

	inv_unref(invitation, "because pointer to invitation is being discarded");

	return sourceid;
}

/**************************** REVOKE ************************************/
/*
 * Revoke an invitation for which the specified CLIENT is the source.
 * The invitation is removed from the lists of invitations of its source
 * and target CLIENT's and the reference counts are appropriately
 * decreased.  It is an error if the specified CLIENT is not the source
 * of the INVITATION, or the INVITATION does not exist in the source or
 * target CLIENT's list.  It is also an error if the INVITATION being
 * revoked is in a state other than the "open" state.  If the invitation
 * is successfully revoked, then the target is sent a REVOKED packet
 * containing the target's ID of the revoked invitation.
 *
 * @param client  The CLIENT that is the source of the invitation to be
 * revoked.
 * @param id  The ID assigned by the CLIENT to the invitation to be
 * revoked.
 * @return 0 if the invitation is successfully revoked, otherwise -1.
 */
int client_revoke_invitation(CLIENT *client, int id){
	debug("[%d] Revoke invitation %d", client->connfd, id);
	P(&client->mutex);
	// find invitation in this client's (should be the source of it) list based on id
	if(id >= client->invlength){
		debug("invalid invitation id client->invlength = %d, invitationid=%d", client->invlength, id);
		V(&client->mutex);
		return -1;
	}
	if(client->invlist[id] == NULL){
		debug("invitation not found inside source's list");
		V(&client->mutex);
		return -1;
	}
	// check if client is the source
	if(client != inv_get_source(client->invlist[id])){
		debug("client is not the source of the invitation");
		V(&client->mutex);
		return -1;
	}

	if(inv_get_game(client->invlist[id]) != NULL){
		debug("invitation is not in OPEN state");
		V(&client->mutex);
		return -1;
	}
	// P(&client->mutex);  // LOCK THIS CLIENT

	INVITATION *inv = inv_ref(client->invlist[id], "for pointer to invitation copied from source client's list");

	// remove invitation from source and target
	debug("[%d] Remove invitation %p", client->connfd, inv);
	int sourceid = client_remove_invitation(client, inv);
	if(sourceid == -1){
		inv_unref(inv, "because pointer to invitation is now being discarded");
		V(&client->mutex);
		// V(&client->mutex); // UNLOCK THIS CLIENT BEFORE RETURN
		return -1;
	}

	// remove invitation from source and target
	debug("[%d] Remove invitation %p", inv_get_target(inv)->connfd, inv);
	int targetid = client_remove_invitation(inv_get_target(inv), inv);
	if(targetid == -1){
		inv_unref(inv, "because pointer to invitation is now being discarded");
		V(&client->mutex);
		// V(&client->mutex); // UNLOCK THIS CLIENT BEFORE RETURN
		return -1;
	}

	// send invited packet to target
	static JEUX_PACKET_HEADER header;
	header.type = JEUX_REVOKED_PKT;
	header.id = targetid;
	header.role = 0;
	header.size = 0;
	clockid_t clock_id = CLOCK_MONOTONIC;
	struct timespec tp;
	clock_gettime(clock_id, &tp);
	header.timestamp_sec = htonl(tp.tv_sec);
	header.timestamp_nsec = htonl(tp.tv_nsec);
	int i = client_send_packet(inv_get_target(inv), &header, NULL);

	inv_unref(inv, "because pointer to invitation is now being discarded");


	// V(&client->mutex); // UNLOCK THIS CLIENT BEFORE RETURN
	V(&client->mutex);
	return i;
}

/**************************** DECLINE ************************************/
/*
 * Decline an invitation previously made with the specified CLIENT as target.
 * The invitation is removed from the lists of invitations of its source
 * and target CLIENT's and the reference counts are appropriately
 * decreased.  It is an error if the specified CLIENT is not the target
 * of the INVITATION, or the INVITATION does not exist in the source or
 * target CLIENT's list.  It is also an error if the INVITATION being
 * declined is in a state other than the "open" state.  If the invitation
 * is successfully declined, then the source is sent a DECLINED packet
 * containing the source's ID of the declined invitation.
 *
 * @param client  The CLIENT that is the target of the invitation to be
 * declined.
 * @param id  The ID assigned by the CLIENT to the invitation to be
 * declined.
 * @return 0 if the invitation is successfully declined, otherwise -1.
 */
int client_decline_invitation(CLIENT *client, int id){
	debug("[%d] Decline invitation %d", client->connfd, id);
	P(&client->mutex);
	// find invitation in this client's (should be the target of it) list based on id
	if(id >= client->invlength){
		debug("invalid invitation id client->invlength = %d, invitationid=%d", client->invlength, id);
		V(&client->mutex);
		return -1;
	}
	if(client->invlist[id] == NULL){
		debug("invitation not found inside source's list");
		V(&client->mutex);
		return -1;
	}
	// check if client is the source
	if(client != inv_get_target(client->invlist[id])){
		debug("client is not the target of the invitation");
		V(&client->mutex);
		return -1;
	}

	if(inv_get_game(client->invlist[id]) != NULL){
		debug("invitation is not in OPEN state");
		V(&client->mutex);
		return -1;
	}
	// P(&client->mutex);  // LOCK THIS CLIENT

	INVITATION *inv = inv_ref(client->invlist[id], "for pointer to invitation copied from source client's list");

	if(client == NULL){
		inv_unref(inv, "because pointer to invitation is now being discarded");
		V(&client->mutex);
		return -1;
	}
	// remove invitation from source and target
	debug("[%d] Remove invitation %p", client->connfd, inv);
	int targetid = client_remove_invitation(client, inv);
	if(targetid == -1){
		inv_unref(inv, "because pointer to invitation is now being discarded");
		V(&client->mutex);
		return -1;
	}

	if(inv_get_source(inv) == NULL){
		inv_unref(inv, "because pointer to invitation is now being discarded");
		V(&client->mutex);
		return -1;
	}
	// remove invitation from source and target
	debug("[%d] Remove invitation %p", inv_get_source(inv)->connfd, inv);
	int sourceid = client_remove_invitation(inv_get_source(inv), inv);
	if(sourceid == -1){
		inv_unref(inv, "because pointer to invitation is now being discarded");
		V(&client->mutex);
		return -1;
	}

	// send revoked packet to target
	static JEUX_PACKET_HEADER header;
	header.type = JEUX_DECLINED_PKT;
	header.id = sourceid;
	header.role = 0;
	header.size = 0;
	clockid_t clock_id = CLOCK_MONOTONIC;
	struct timespec tp;
	clock_gettime(clock_id, &tp);
	header.timestamp_sec = htonl(tp.tv_sec);
	header.timestamp_nsec = htonl(tp.tv_nsec);
	int i = client_send_packet(inv_get_source(inv), &header, NULL);

	inv_unref(inv, "because pointer to invitation is now being discarded");
	V(&client->mutex);
	return i;
}

/**************************** ACCEPT ************************************/
/*
 * Accept an INVITATION previously made with the specified CLIENT as
 * the target.  A new GAME is created and a reference to it is saved
 * in the INVITATION.  If the invitation is successfully accepted,
 * the source is sent an ACCEPTED packet containing the source's ID
 * of the accepted INVITATION.  If the source is to play the role of
 * the first player, then the payload of the ACCEPTED packet contains
 * a string describing the initial game state.  A reference to the
 * new GAME (with its reference count incremented) is returned to the
 * caller.
 *
 * @param client  The CLIENT that is the target of the INVITATION to be
 * accepted.
 * @param id  The ID assigned by the target to the INVITATION.
 * @param strp  Pointer to a variable into which will be stored either
 * NULL, if the accepting client is not the first player to move,
 * or a malloc'ed string that describes the initial game state,
 * if the accepting client is the first player to move.
 * If non-NULL, this string should be used as the payload of the `ACK`
 * message to be sent to the accepting client.  The caller must free
 * the string after use.
 * @return 0 if the INVITATION is successfully accepted, otherwise -1.
 */
int client_accept_invitation(CLIENT *client, int id, char **strp){
	P(&client->mutex);
	if(id >= client->invlength){
		debug("Invalid id of invitation");
		V(&client->mutex);
		return -1;
	}
	if(client->invlist[id] == NULL){
		debug("invitation is not in client's list");
		V(&client->mutex);
		return -1;
	}
	if(client != inv_get_target(client->invlist[id])){
		debug("Client is not the TARGET");
		V(&client->mutex);
		return -1;
	}
	if(inv_get_game(client->invlist[id]) != NULL){
		debug("Invitation already been accepted");
		V(&client->mutex);
		return -1;
	}
	int in_target_list = 0;
	int index = 0;
	for(int i = 0; i < (inv_get_source(client->invlist[id]))->invlength; i++ ){
		if(inv_get_source(client->invlist[id])->invlist[i] == client->invlist[id]){
			index = i;
			in_target_list = 1;
			break;
		}
	}
	if(!in_target_list){
		debug("invitation not in target's list");
		V(&client->mutex);
		return -1;
	}
	INVITATION *inv = inv_ref(client->invlist[id], "for pointer to invitation copied from target client's list");
	if(inv_accept(inv) == -1){
		inv_unref(inv, "because pointer to invitation is now being discarded");
		V(&client->mutex);
		return -1;
	}
	// successful

	char *state = NULL; // Malloced storage
	// send accepted packet to source
	static JEUX_PACKET_HEADER header;
	header.type = JEUX_ACCEPTED_PKT;
	header.id = index;
	header.role = 0;
	header.size = 0;
	clockid_t clock_id = CLOCK_MONOTONIC;
	struct timespec tp;
	clock_gettime(clock_id, &tp);
	header.timestamp_sec = htonl(tp.tv_sec);
	header.timestamp_nsec = htonl(tp.tv_nsec);

	if(inv_get_source_role(inv) == 1){
		// SOURCE IS FIRST ONE TO MOVE
		state = game_unparse_state(inv_get_game(inv));
		header.size = htons(strlen(state));
		if(client_send_packet(inv_get_source(inv), &header, state) == -1){
			inv_unref(inv, "because pointer to invitation is now being discarded 11111");
			V(&client->mutex);
			return -1;
		}
		*strp = NULL;
	} else {
		if(client_send_packet(inv_get_source(inv), &header, NULL) == -1){
			inv_unref(inv, "because pointer to invitation is now being discarded");
			V(&client->mutex);
			return -1;
		}
		*strp = game_unparse_state(inv_get_game(inv));
	}

	// char *board = " | | \n-----\n | | \n-----\n | | \n";
	// char *str = NULL;
	// // int i;
	// str = (char *) Malloc(strlen(board) + 1);
	// strcpy(str, board);
	// str[strlen(board)] = '\0';
	// printf("%s", str);
	// exit(0);

	// if source if 1, then send initial game state along with ACCEPTED
	// ELSE
	// the initial game state is stored inside strp

	// if(inv_get_source_role(inv) != 1){
	// 	// printf("this code is activated.\n");
	// 	i = client_send_packet(inv_get_source(inv), &header, str);
	// } else {
	// 	*strp = str;
	// client_send_packet(inv_get_source(inv), &header, NULL);
	// }

	inv_unref(inv, "because pointer to invitation is now being discarded");

	if(state != NULL){
		Free(state);
	}
	V(&client->mutex);
	return 0;

}

/**************************** RESIGN ************************************/
/*
 * Resign a game in progress.  This function may be called by a CLIENT
 * that is either source or the target of the INVITATION containing the
 * GAME that is to be resigned.  It is an error if the INVITATION containing
 * the GAME is not in the ACCEPTED state.  If the game is successfully
 * resigned, the INVITATION is set to the CLOSED state, it is removed
 * from the lists of both the source and target, and a RESIGNED packet
 * containing the opponent's ID for the INVITATION is sent to the opponent
 * of the CLIENT that has resigned.
 *
 * @param client  The CLIENT that is resigning.
 * @param id  The ID assigned by the CLIENT to the INVITATION that contains
 * the GAME to be resigned.
 * @return 0 if the game is successfully resigned, otherwise -1.
 */
int client_resign_game(CLIENT *client, int id){
	debug("[%d] Resign game %d", client->connfd, id);
	P(&client->mutex); // LOCK THIS CLIENT

	if(id >= client->invlength){
		debug("invalid invitation id client->invlength = %d, invitationid=%d", client->invlength, id);
		V(&client->mutex);
		return -1;
	}
	if(client->invlist[id] == NULL){
		debug("invitation not found inside source's list");
		V(&client->mutex);
		return -1;
	}
	if(inv_get_game(client->invlist[id]) == NULL){
		debug("invitation is not in ACCEPTED state");
		V(&client->mutex);
		return -1;
	}
	// P(&client->mutex); // LOCK THIS CLIENT
	INVITATION *inv = inv_ref(client->invlist[id], "for pointer to invitation copied from source client's list");

	// resignation process
	int i = 0;
	int sourceid;
	int targetid;
	if(client == inv_get_source(inv)){
		// source resigned TARGET WON!!!!!
		// check if inv is in target's list
		int in_target_list = 0;
		int targetid = 0;
		for(int i = 0; i < (inv_get_target(inv))->invlength; i++ ){
			if(inv_get_target(inv)->invlist[i] == inv){
				targetid = i;
				in_target_list = 1;
				break;
			}
		}
		if(!in_target_list){
			debug("invitation not in opponent's list 1");
			inv_unref(inv, "because pointer to invitation is now being discarded 1");
			V(&client->mutex);
			return -1;
		}


		// client resigning is the source
		i = inv_close(inv, inv_get_source_role(inv));
		// check return value
		if(i == -1){
			debug("inv_close() error in client_resign_game()");
			inv_unref(inv, "because pointer to invitation is now being discarded 2");
			V(&client->mutex);
			return -1;
		}

		// send resigned packet to target
		static JEUX_PACKET_HEADER header;
		header.type = JEUX_RESIGNED_PKT;
		header.id = targetid;
		header.role = 0;
		header.size = 0;
		clockid_t clock_id = CLOCK_MONOTONIC;
		struct timespec tp;
		clock_gettime(clock_id, &tp);
		header.timestamp_sec = htonl(tp.tv_sec);
		header.timestamp_nsec = htonl(tp.tv_nsec);
		if(client_send_packet(inv_get_target(inv), &header, NULL) == -1){
			inv_unref(inv, "because pointer to invitation is now being discarded 19");
			V(&client->mutex);
			return -1;
		}

		GAME *game = inv_get_game(inv);
		// GAME IS OVER WHEN CLIENT IS THE SOURCE
		if(game_is_over(game)){
			// game terminated
			// send ended packet to client
			static JEUX_PACKET_HEADER header1;
			header1.type = JEUX_ENDED_PKT;
			header1.id = id;
			header1.role = inv_get_target_role(inv);
			header1.size = 0;
			clockid_t clock_id = CLOCK_MONOTONIC;
			struct timespec tp;
			clock_gettime(clock_id, &tp);
			header1.timestamp_sec = htonl(tp.tv_sec);
			header1.timestamp_nsec = htonl(tp.tv_nsec);
			if(client_send_packet(client, &header1, NULL) == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 3");
				V(&client->mutex);
				return -1;
			}

			// send ended packet to target
			static JEUX_PACKET_HEADER header2;
			header2.type = JEUX_ENDED_PKT;
			header2.id = targetid;
			header2.role = inv_get_target_role(inv);
			header2.size = 0;
			// clockid_t clock_id = CLOCK_MONOTONIC;
			// struct timespec tp;
			clock_gettime(clock_id, &tp);
			header2.timestamp_sec = htonl(tp.tv_sec);
			header2.timestamp_nsec = htonl(tp.tv_nsec);
			if(client_send_packet(inv_get_target(inv), &header2, NULL) == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 4");
				V(&client->mutex);
				return -1;
			}
			// remove invitation from source and target
			debug("[%d] Remove invitation %p", client->connfd, inv);
			sourceid = client_remove_invitation(client, inv);
			if(sourceid == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 5");
				V(&client->mutex);
				return -1;
			}

			// remove invitation from source and target
			debug("[%d] Remove invitation %p", inv_get_target(inv)->connfd, inv);
			targetid = client_remove_invitation(inv_get_target(inv), inv);
			if(targetid == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 6");
				V(&client->mutex);
				return -1;
			}
			// post final results
			// printf("!!!!!!!!!!!!!!!!!%p\n", inv_get_source(inv));
			// printf("!!!!!!!!!!!!!!!!!%p\n", inv_get_target(inv));
			player_post_result(client_get_player(inv_get_target(inv)), client_get_player(inv_get_source(inv)), game_get_winner(game));
		}

	} else {
		// client is the target SOURCE HAS WON!!!
		// check if inv is in source's list
		int in_source_list = 0;
		int sourceid = 0;
		for(int i = 0; i < (inv_get_source(inv))->invlength; i++ ){
			if(inv_get_source(inv)->invlist[i] == inv){
				sourceid = i;
				in_source_list = 1;
				break;
			}
		}
		if(!in_source_list){
			debug("invitation not in opponent's list 1");
			inv_unref(inv, "because pointer to invitation is now being discarded 7");
			V(&client->mutex);
			return -1;
		}

		// client resigning is the TARGET
		i = inv_close(inv, inv_get_target_role(inv));;
		// check return value
		if(i == -1){
			debug("inv_close() error in client_resign_game()");
			inv_unref(inv, "because pointer to invitation is now being discarded 8");
			V(&client->mutex);
			return -1;
		}

		// send resigned packet to target
		static JEUX_PACKET_HEADER header;
		header.type = JEUX_RESIGNED_PKT;
		header.id = sourceid;
		header.role = 0;
		header.size = 0;
		clockid_t clock_id = CLOCK_MONOTONIC;
		struct timespec tp;
		clock_gettime(clock_id, &tp);
		header.timestamp_sec = htonl(tp.tv_sec);
		header.timestamp_nsec = htonl(tp.tv_nsec);
		if(client_send_packet(inv_get_source(inv), &header, NULL) == -1){
			inv_unref(inv, "because pointer to invitation is now being discarded 9");
			V(&client->mutex);
			return -1;
		}

		GAME *game = inv_get_game(inv);
		// GAME IS OVER WHEN CLIENT IS THE TARGET
		if(game_is_over(inv_get_game(inv))){
			// game terminated
			// send ended packet to client
			static JEUX_PACKET_HEADER header1;
			header1.type = JEUX_ENDED_PKT;
			header1.id = id;
			header1.role = inv_get_source_role(inv);
			header1.size = 0;
			clockid_t clock_id = CLOCK_MONOTONIC;
			struct timespec tp;
			clock_gettime(clock_id, &tp);
			header1.timestamp_sec = htonl(tp.tv_sec);
			header1.timestamp_nsec = htonl(tp.tv_nsec);
			if(client_send_packet(client, &header1, NULL) == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 10");
				V(&client->mutex);
				return -1;
			}

			// send ended packet to target
			static JEUX_PACKET_HEADER header2;
			header2.type = JEUX_ENDED_PKT;
			header2.id = sourceid;
			header2.role = inv_get_source_role(inv);
			header2.size = 0;
			// clockid_t clock_id = CLOCK_MONOTONIC;
			// struct timespec tp;
			clock_gettime(clock_id, &tp);
			header2.timestamp_sec = htonl(tp.tv_sec);
			header2.timestamp_nsec = htonl(tp.tv_nsec);
			if(client_send_packet(inv_get_source(inv), &header2, NULL) == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 11");
				V(&client->mutex);
				return -1;
			}

			// remove invitation from source and target
			debug("[%d] Remove invitation %p", client->connfd, inv);
			sourceid = client_remove_invitation(client, inv);
			if(sourceid == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 12");
				V(&client->mutex);
				return -1;
			}

			// remove invitation from source and target
			debug("[%d] Remove invitation %p", inv_get_source(inv)->connfd, inv);
			targetid = client_remove_invitation(inv_get_source(inv), inv);
			if(targetid == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 13");
				V(&client->mutex);
				return -1;
			}

			// post final results
			player_post_result(client_get_player(inv_get_target(inv)), client_get_player(inv_get_source(inv)), game_get_winner(game));
		}
	}

	inv_unref(inv, "because pointer to invitation is now being discarded 15");

	// resignation process
	V(&client->mutex);
	return 0;
}



/**************************** MOVE ************************************/
/*
 * Make a move in a game currently in progress, in which the specified
 * CLIENT is a participant.  The GAME in which the move is to be made is
 * specified by passing the ID assigned by the CLIENT to the INVITATION
 * that contains the game.  The move to be made is specified as a string
 * that describes the move in a game-dependent format.  It is an error
 * if the ID does not refer to an INVITATION containing a GAME in progress,
 * if the move cannot be parsed, or if the move is not legal in the current
 * GAME state.  If the move is successfully made, then a MOVED packet is
 * sent to the opponent of the CLIENT making the move.  In addition, if
 * the move that has been made results in the game being over, then an
 * ENDED packet containing the appropriate game ID and the game result
 * is sent to each of the players participating in the game, and the
 * INVITATION containing the now-terminated game is removed from the lists
 * of both the source and target.  The result of the game is posted in
 * order to update both players' ratings.
 *
 * @param client  The CLIENT that is making the move.
 * @param id  The ID assigned by the CLIENT to the GAME in which the move
 * is to be made.
 * @param move  A string that describes the move to be made.
 * @return 0 if the move was made successfully, -1 otherwise.
 */
int client_make_move(CLIENT *client, int id, char *move){
	debug("[%d] Make move '%s' in game %d", client->connfd, move, id);
	P(&client->mutex);

	//CHECKS

	if(id >= client->invlength){
		debug("invalid invitation id client->invlength = %d, invitationid=%d", client->invlength, id);
		V(&client->mutex);
		return -1;
	}
	if(client->invlist[id] == NULL){
		debug("invitation not found inside source's list");
		V(&client->mutex);
		return -1;
	}
	GAME *game;
	if((game = inv_get_game(client->invlist[id])) == NULL){
		debug("invitation is not in ACCEPTED state");
		V(&client->mutex);
		return -1;
	}
	INVITATION *inv = inv_ref(client ->invlist[id], "for pointer to invitation copied from client's list");
	int client_is_source = 0;
	if(client == inv_get_source(inv)){
		client_is_source = 1;
	}
	// parse game move
	GAME_MOVE *pmove; // free this when no longer needed
	int in_source_list = 0;
	int sourceid = 0;
	int in_target_list = 0;
	int targetid = 0;
	char *state = NULL;

	if(client_is_source){
		// check if invitation is inside target's list
		in_source_list = 1;
		sourceid = id;
		// check if inv is in target's list
		for(int i = 0; i < (inv_get_target(inv))->invlength; i++ ){
			if(inv_get_target(inv)->invlist[i] == inv){
				targetid = i;
				in_target_list = 1;
				break;
			}
		}
		if(!in_target_list){
			debug("invitation not in opponent's list 1");
			inv_unref(inv, "because pointer to invitation is now being discarded 1");
			V(&client->mutex);
			return -1;
		}


		if((pmove = game_parse_move(game, inv_get_source_role(inv), move)) == NULL){
			inv_unref(inv, "discarding 1");
			V(&client->mutex);
			return -1;
		}

		// apply move
		if(game_apply_move(game, pmove) == -1){
			inv_unref(inv, "discarding 3");
			V(&client->mutex);
			return -1;
		}

		if(pmove != NULL){
			Free(pmove);
		}


		// send moved packet to target
		// id = targetid
		// payload = game state after move
		state = game_unparse_state(game);

		// send resigned packet to target
		static JEUX_PACKET_HEADER header;
		header.type = JEUX_MOVED_PKT;
		header.id = targetid;
		header.role = 0;
		header.size = htons(strlen(state));
		clockid_t clock_id = CLOCK_MONOTONIC;
		struct timespec tp;
		clock_gettime(clock_id, &tp);
		header.timestamp_sec = htonl(tp.tv_sec);
		header.timestamp_nsec = htonl(tp.tv_nsec);
		if(client_send_packet(inv_get_target(inv), &header, state) == -1){
			inv_unref(inv, "because pointer to invitation is now being discarded 19");
			if(state != NULL){ Free(state); }
			V(&client->mutex);
			return -1;
		}


		// GAME IS OVER WHEN CLIENT IS THE SOURCE
		if(game_is_over(inv_get_game(inv))){
			// game terminated
			// send ended packet to client
			static JEUX_PACKET_HEADER header1;
			header1.type = JEUX_ENDED_PKT;
			header1.id = id;
			header1.role = game_get_winner(game);
			header1.size = 0;
			clockid_t clock_id = CLOCK_MONOTONIC;
			struct timespec tp;
			clock_gettime(clock_id, &tp);
			header1.timestamp_sec = htonl(tp.tv_sec);
			header1.timestamp_nsec = htonl(tp.tv_nsec);
			if(client_send_packet(client, &header1, NULL) == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 3");
				if(state != NULL){ Free(state); }
				V(&client->mutex);
				return -1;
			}

			// send ended packet to target
			static JEUX_PACKET_HEADER header2;
			header2.type = JEUX_ENDED_PKT;
			header2.id = targetid;
			header2.role = game_get_winner(game);
			header2.size = 0;
			// clockid_t clock_id = CLOCK_MONOTONIC;
			// struct timespec tp;
			clock_gettime(clock_id, &tp);
			header2.timestamp_sec = htonl(tp.tv_sec);
			header2.timestamp_nsec = htonl(tp.tv_nsec);
			if(client_send_packet(inv_get_target(inv), &header2, NULL) == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 4");
				if(state != NULL){ Free(state); }
				V(&client->mutex);
				return -1;
			}
			// remove invitation from source and target
			debug("[%d] Remove invitation %p", client->connfd, inv);
			sourceid = client_remove_invitation(client, inv);
			if(sourceid == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 5");
				if(state != NULL){ Free(state); }
				V(&client->mutex);
				return -1;
			}

			// remove invitation from source and target
			debug("[%d] Remove invitation %p", inv_get_target(inv)->connfd, inv);
			targetid = client_remove_invitation(inv_get_target(inv), inv);
			if(targetid == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 6");
				if(state != NULL){ Free(state); }
				V(&client->mutex);
				return -1;
			}
			// post final results
			// printf("!!!!!!!!!!!!!!!!!%p\n", inv_get_source(inv));
			// printf("!!!!!!!!!!!!!!!!!%p\n", inv_get_target(inv));
			player_post_result(client_get_player(inv_get_target(inv)), client_get_player(inv_get_source(inv)), game_get_winner(game));
		}


	} else {
		// check if invitation is inside source's list
		in_target_list = 1;
		targetid = id;
		for(int i = 0; i < (inv_get_source(inv))->invlength; i++ ){
			if(inv_get_source(inv)->invlist[i] == inv){
				targetid = i;
				in_source_list = 1;
				break;
			}
		}
		if(!in_source_list){
			debug("invitation not in opponent's list 2");
			inv_unref(inv, "because pointer to invitation is now being discarded 2");
			V(&client->mutex);
			return -1;
		}

		if((pmove = game_parse_move(game, inv_get_target_role(inv), move)) == NULL){
			inv_unref(inv, "discarding 2");
			V(&client->mutex);
			return -1;
		}

		// apply move
		if(game_apply_move(game, pmove) == -1){
			inv_unref(inv, "discarding 3");
			V(&client->mutex);
			return -1;
		}

		if(pmove != NULL){
			Free(pmove);
		}


		// send moved packet
		// id = invitationid
		// payload = game state after move
		state = game_unparse_state(game);

		// send moved packet to source
		static JEUX_PACKET_HEADER header;
		header.type = JEUX_MOVED_PKT;
		header.id = sourceid;
		header.role = 0;
		header.size = htons(strlen(state));
		clockid_t clock_id = CLOCK_MONOTONIC;
		struct timespec tp;
		clock_gettime(clock_id, &tp);
		header.timestamp_sec = htonl(tp.tv_sec);
		header.timestamp_nsec = htonl(tp.tv_nsec);
		if(client_send_packet(inv_get_source(inv), &header, state) == -1){
			inv_unref(inv, "because pointer to invitation is now being discarded 9");
			if(state != NULL){ Free(state); }
			V(&client->mutex);
			return -1;
		}

		// GAME IS OVER WHEN CLIENT IS THE TARGET
		if(game_is_over(inv_get_game(inv))){
			// game terminated
			// send ended packet to client
			static JEUX_PACKET_HEADER header1;
			header1.type = JEUX_ENDED_PKT;
			header1.id = id;
			header1.role = game_get_winner(game);
			header1.size = 0;
			clockid_t clock_id = CLOCK_MONOTONIC;
			struct timespec tp;
			clock_gettime(clock_id, &tp);
			header1.timestamp_sec = htonl(tp.tv_sec);
			header1.timestamp_nsec = htonl(tp.tv_nsec);
			if(client_send_packet(client, &header1, NULL) == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 10");
				if(state != NULL){ Free(state); }
				V(&client->mutex);
				return -1;
			}

			// send ended packet to target
			static JEUX_PACKET_HEADER header2;
			header2.type = JEUX_ENDED_PKT;
			header2.id = sourceid;
			header2.role = game_get_winner(game);
			header2.size = 0;
			// clockid_t clock_id = CLOCK_MONOTONIC;
			// struct timespec tp;
			clock_gettime(clock_id, &tp);
			header2.timestamp_sec = htonl(tp.tv_sec);
			header2.timestamp_nsec = htonl(tp.tv_nsec);
			if(client_send_packet(inv_get_source(inv), &header2, NULL) == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 11");
				if(state != NULL){ Free(state); }
				V(&client->mutex);
				return -1;
			}

			// remove invitation from source and target
			debug("[%d] Remove invitation %p", client->connfd, inv);
			sourceid = client_remove_invitation(client, inv);
			if(sourceid == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 12");
				if(state != NULL){ Free(state); }
				V(&client->mutex);
				return -1;
			}

			// remove invitation from source and target
			debug("[%d] Remove invitation %p", inv_get_source(inv)->connfd, inv);
			targetid = client_remove_invitation(inv_get_source(inv), inv);
			if(targetid == -1){
				inv_unref(inv, "because pointer to invitation is now being discarded 13");
				if(state != NULL){ Free(state); }
				V(&client->mutex);
				return -1;
			}

			// post final results
			player_post_result(client_get_player(inv_get_target(inv)), client_get_player(inv_get_source(inv)), game_get_winner(game));
		}
	}
	inv_unref(inv, "client_make_move() ended");

	if(state != NULL){ Free(state); }
	V(&client->mutex);
	return 0;
}


