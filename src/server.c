#include "jeux_globals.h"
#include "server.h"
#include "csapp.h"
#include "debug.h"

// static char *form_txtstring(char **strings, int *ints, int size);

// static sem_t logout_in_progress;

static int logout_in_progress = 0;
static sem_t mutex1;
// static int unregister_in_progress = 0;
// static sem_t mutex2;

static void mutex_init(void){
	Sem_init(&mutex1, 0, 1);
	// Sem_init(&mutex2, 0, 1);
}
/*
 * Thread function for the thread that handles a particular client.
 *
 * @param  Pointer to a variable that holds the file descriptor for
 * the client connection.  This pointer must be freed once the file
 * descriptor has been retrieved.
 * @return  NULL
 *
 * This function executes a "service loop" that receives packets from
 * the client and dispatches to appropriate functions to carry out
 * the client's requests.  It also maintains information about whether
 * the client has logged in or not.  Until the client has logged in,
 * only LOGIN packets will be honored.  Once a client has logged in,
 * LOGIN packets will no longer be honored, but other packets will be.
 * The service loop ends when the network connection shuts down and
 * EOF is seen.  This could occur either as a result of the client
 * explicitly closing the connection, a timeout in the network causing
 * the connection to be closed, or the main thread of the server shutting
 * down the connection as part of graceful termination.
 */
void *jeux_client_service(void *arg){
	int connfd, n, login=0;
	// retrieve connfd, free arg
	connfd = *((int *) arg);
	Free(arg);

	// detach for automatic reaping
	Pthread_detach(pthread_self());

	CLIENT *client = NULL;
	PLAYER *player = NULL;
	JEUX_PACKET_HEADER header;
	pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, mutex_init);


	char *payload;
	char *result = Malloc(sizeof(char));
	*result = '\0';
	char *board = Malloc(sizeof(char));
	*board = '\0';
	clockid_t clock_id = CLOCK_MONOTONIC;
	struct timespec tp;

	debug("[%d] Starting client service", connfd);

	// register with client registry
	client = creg_register(client_registry, connfd);

	// failed to register client (client_registry is full)
	if(client == NULL){
		debug("client registry full.");
		if(result != NULL){
			Free(result);
		}
		Close(connfd);
		return 0;
	}

	// Service Loop
	while(!(n = proto_recv_packet(connfd, &header, (void **) &payload))){
		uint8_t type = header.type;
		uint8_t id = header.id;
		uint8_t role = header.role; // role of the target packet - invite
		uint16_t size = ntohs(header.size);
		// uint32_t timesec = ntohl(header.timestamp_sec);
		// uint32_t timensec = ntohl(header.timestamp_nsec);

		// set server clock
		// tp.tv_sec = timesec;
		// tp.tv_nsec = timensec;
		// clock_settime(clock_id, &tp);


		if(type == JEUX_LOGIN_PKT){ // LOGIN ---------------------------------
			debug("[%d] LOGIN packet received", connfd);
			if(login){
				debug("[%d] Already logged in", connfd);
				client_send_nack(client);
			} else{
				// move payload to my temporary storage (add a null terminator)
				char p[size+1];
				for(int i = 0; i< size; i++){
					p[i] = payload[i];
				}
				p[size] = '\0';
				// Free(payload);

				debug("Login '%s'", p);

				player = preg_register(player_registry, p);
				// debug("player %p registered", player);

				if(player == NULL){
					debug("preg_register error while processing LOGIN packet");
					client_send_nack(client);
				} else {
					// create player (logged in client)
					int i = client_login(client, player);
					if(i == 0){
						// client login successful
						client_send_ack(client, NULL, 0);
						login = 1;
					} else {
						// client login unsuccessful
						client_send_nack(client);
					}
				}
			}
		} else if(!login){  // HAVEN'T LOGGEDIN ---------------------------------
			debug("[%d] Please log in first then you can do that", connfd);
			client_send_nack(client);
							// LOGGEDIN -----------------------------------------

		} else if(type == JEUX_USERS_PKT){ // USERS -----------------------------
			debug("[%d] USERS packet received", connfd);

			// get all the logged in players (NULL terminated)
			PLAYER **players = creg_all_players(client_registry);

			// count the total number of players
			PLAYER *iter;
			int np = 0; // total number of players
			while((iter=players[np]) != NULL){
				np++;
			}

			// printf("!!!!!!!!!!!!!!!!!!!!!!!!!%d\n", np);

			// get usernames and ratings
			char *usernames[np];
			int ratings[np];
			for(int i = 0; i < np; i++){
				usernames[i] = player_get_name(players[i]);
				ratings[i] = player_get_rating(players[i]);
			}

			// form textstring
			// Calculate total length of all strings
			int total_length = 0;
			for(int i = 0; i < np; i++){
				total_length += strlen(usernames[i]) + 1; // for tab character
				total_length += snprintf(NULL, 0, "%d", ratings[i]) + 1; // for next line character
			}

			result = Realloc(result, total_length + 1);

			// Copy individual strings to concatenated string
			int index = 0;
			for(int i = 0; i < np; i++){
				strcpy(result + index, usernames[i]);
				index += strlen(usernames[i]);
				result[index++] = '\t';
				index += snprintf(result + index, snprintf(NULL, 0, "%d", ratings[i])+1, "%d", ratings[i]);
				result[index++] = '\n';
			}
			result[total_length] = '\0';
			// printf("The length of result: %d\n", total_length);

			// decrement refcnt of every player (incremented in creg_all_players)
			for(int i = 0; i < np; i++){
				player_unref(players[i], "player removed from players list");
			}
			Free(players);
			client_send_ack(client, result, strlen(result));

		} else if(type == JEUX_INVITE_PKT){ // INVITE -----------------------------
			debug("[%d] INVITE packet received", connfd);
			// move payload to my temporary storage (add a null terminator)
			char p[size+1];
			for(int i = 0; i< size; i++){
				p[i] = payload[i];
			}
			p[size] = '\0';
			debug("[%d] Invite '%s'", connfd, p);

			// create target client pointer
			CLIENT *target = creg_lookup(client_registry, p);
			if(target == NULL){ // this target does not exist
				debug("this target username (%s) does not exist", p);
				client_send_nack(client);
			} else {

				int source_role;
				if(role == 1){
					source_role = 2;
				} else {
					source_role = 1;
				}
				// client make invitation
				int source_id = client_make_invitation(client, target, source_role, role);
				if(source_id == -1){ // this target does not exist
					debug("client_make_invitation failed while processing invite packet");
					client_send_nack(client);
				} else {

					// decrement references if needed
					client_unref(target, "after invitation attempt");

					// client send ack
					header.type = JEUX_ACK_PKT;
					header.id = source_id;
					header.role = 0;
					header.size = 0;
					clock_gettime(clock_id, &tp);
					header.timestamp_sec = htonl(tp.tv_sec);
					header.timestamp_nsec = htonl(tp.tv_nsec);

					if(client_send_packet(client, &header, NULL) != 0){
						debug("send packet failed after processing invite packet");
						client_send_nack(client);
					}
				}
			}

		} else if(type == JEUX_REVOKE_PKT){ // REVOKE -----------------------------
			debug("[%d] REVOKE packet received", connfd);
			int revokeid = id;
			debug("[%d] Revoke '%d'", connfd, revokeid);
			if(client_revoke_invitation(client, revokeid) != 0){
				debug("client_revoke_invitation() error while processing REVOKE packet");
				client_send_nack(client);
			} else {
				client_send_ack(client, NULL, 0);
			}

		} else if(type == JEUX_DECLINE_PKT){ // DECLINE -----------------------------
			debug("[%d] DECLINE packet received", connfd);
			int declineid = id;
			debug("[%d] Decline '%d'", connfd, declineid);
			if(client_decline_invitation(client, declineid) != 0){
				debug("client_decline_invitation() error while processing DECLINE packet");
				client_send_nack(client);
			} else {
				client_send_ack(client, NULL, 0);
			}

		} else if(type == JEUX_ACCEPT_PKT){ // ACCEPT -------------------------------------
			debug("[%d] ACCEPT packet received", connfd);
			int acceptid = id;
			debug("[%d] Accept'%d'", connfd, acceptid);
			char *str;
			if(client_accept_invitation(client, acceptid, &str) != 0){
				debug("client_accept_invitation() error while processing ACCEPT packet");
				client_send_nack(client);
			} else {
				// client_accept_invitation successful
				if(str==NULL){
					// printf("PROGRAM REACHED HERE 1\n");
					header.type = JEUX_ACK_PKT;
					header.id = id;
					header.role = 0;
					header.size = 0;
					clockid_t clock_id = CLOCK_MONOTONIC;
					struct timespec tp;
					clock_gettime(clock_id, &tp);
					header.timestamp_sec = htonl(tp.tv_sec);
					header.timestamp_nsec = htonl(tp.tv_nsec);
					if(client_send_packet(client, &header, NULL) == -1){
						debug("client_send_packet() error while processing ACCEPT packet");
						client_send_nack(client);
					}
				} else {
					// printf("PROGRAM REACHED HERE 2 str=%s\n", str);
					// str is not null
					// printf("!!!!!!!!!!ID=%d\n", id);
					board = Realloc(board, strlen(str)+1);
					board[strlen(str)] = '\0';
					strcpy(board, str);
					header.type = JEUX_ACK_PKT;
					header.id = id;
					header.role = 0;
					header.size = htons(strlen(str));
					clockid_t clock_id = CLOCK_MONOTONIC;
					struct timespec tp;
					clock_gettime(clock_id, &tp);
					header.timestamp_sec = htonl(tp.tv_sec);
					header.timestamp_nsec = htonl(tp.tv_nsec);
					if(client_send_packet(client, &header, str) == -1){
						debug("client_send_packet() error while processing ACCEPT packet");
						client_send_nack(client);
					}
					// client_send_ack(client, board, strlen(str));
					Free(str);
				}
			}

		} else if(type == JEUX_MOVE_PKT){ // MOVE -------------------------------------
			debug("[%d] MOVE packet received", connfd);
			int gameid = id;

			// move payload to my temporary storage (add a null terminator)
			char p[size+1];
			for(int i = 0; i< size; i++){
				p[i] = payload[i];
			}
			p[size] = '\0';

			debug("[%d] Move'%d' (%s)", connfd, gameid, p);

			if(client_make_move(client, gameid, p) != 0){
				debug("client_make_move() error while processing MOVE packet");
				client_send_nack(client);
			} else {
				client_send_ack(client, NULL, 0);
			}

		} else if(type == JEUX_RESIGN_PKT){ // RESIGN -----------------------------------------------
			debug("[%d] RESIGN packet received", connfd);
			int gameid = id;
			debug("[%d] Decline '%d'", connfd, gameid);
			if(client_resign_game(client, gameid) != 0){
				debug("client_resign_game() error while processing RESIGN packet");
				client_send_nack(client);
			} else {
				client_send_ack(client, NULL, 0);
			}

		} else {	// OTHERS ----------------------------------------------------------
			debug("I don't know what this is");
			// printf("Packet received: %d.%d type=%d id=%d role=%d size=%d", timesec, timensec, type, id, role, size);
			client_send_nack(client);
		}
		// Free payload after each packet
		if(payload != NULL){
			Free(payload);
		}
	}

	// EOF encountered
	// debug("EOF encountered");
	if(player != NULL){
		player_unref(player, "because server thread is discarding reference to logged in player");
	}
	if(login){
		P(&mutex1);
		logout_in_progress++;
		V(&mutex1);
		debug("[%d] Logging out client", connfd);
		if(client_logout(client) != 0){
			debug("client_logout failed");
		}
		P(&mutex1);
		logout_in_progress--;
		V(&mutex1);
	}
	if(result != NULL){
		Free(result);
	}
	if(board != NULL){
		Free(board);
	}
	// P(&mutex2);
	// logout_in_progress++;
	// V(&mutex2);
	while(logout_in_progress){;}
	if((creg_unregister(client_registry, client) != 0)){
		debug("creg_unregister failed");
	}
	// P(&mutex2);
	// logout_in_progress--;
	// V(&mutex2);
	debug("[%d] Ending client service", connfd);
	Close(connfd);
	return 0;
}

// /*
//  * Caller has reponsibility of Freeing txtstring
//  */
// static char *form_txtstring(char **strings, int *ints, int size, char *result){
// 	// Calculate total length of all strings
// 	int total_length;
// 	for(int i = 0; i < size; i++){
// 		total_length += strlen(strings[i]) + 2 + 1;
// 		total_length += snprintf(NULL, 0, "%d", ints[i]);
// 	}

// 	// Allocate memory for concatenated string
// 	char *result = (char *) malloc((total_length + 1) * sizeof(char));
// 	if(result == NULL){
// 		fprintf(stderr, "malloc error.");
// 		return NULL;
// 	}
// 	// Copy individual strings to concatenated string
// 	int index = 0;
// 	for(int i = 0; i < size; i++){
// 		strcpy(result + index, strings[i]);
// 		index += strlen(strings[i]);
// 		result[index++] = '\t';
// 		index += snprintf(result + index, total_length - index + 1, "%d", ints[i]);
// 		result[index++] = '\n';
// 	}
// 	result[total_length] = '\0';
// 	// Add null terminator to end of concatenated string
// 	return result;
// }

