#include "player.h"
#include "csapp.h"
#include "debug.h"
#include "math.h"

/*
 * The PLAYER type is a structure type that defines the state of a player.
 * You will have to give a complete structure definition in player.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.
 */
typedef struct player {
	int refcnt;
	char *username;
	int rating;
	sem_t mutex;
} PLAYER;

/*
 * Create a new PLAYER with a specified username.  A private copy is
 * made of the username that is passed.  The newly created PLAYER has
 * a reference count of one, corresponding to the reference that is
 * returned from this function.
 *
 * @param name  The username of the PLAYER.
 * @return  A reference to the newly created PLAYER, if initialization
 * was successful, otherwise NULL.
 */
PLAYER *player_create(char *name){
	PLAYER *player = (PLAYER *) Malloc(sizeof(PLAYER));
	if(player == NULL){return NULL; }
	player->refcnt = 0;
	player->username = (char *) Malloc(strlen(name)+1);
	if(player->username == NULL){
		Free(player);
		return NULL;
	}
	strcpy(player->username, name);
	player->username[strlen(name)] = '\0';
	player->rating = PLAYER_INITIAL_RATING;
	Sem_init(&player->mutex, 0, 1);
	player_ref(player, "for newly created player");
	return player;
}

/*
 * Increase the reference count on a player by one.
 *
 * @param player  The PLAYER whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same PLAYER object that was passed as a parameter.
 */
PLAYER *player_ref(PLAYER *player, char *why){
	P(&player->mutex);
	player->refcnt++;
	debug("Increase reference count on invitation %p (%d -> %d) %s", player, player->refcnt - 1, player->refcnt, why);
	V(&player->mutex);
	return player;
}

/*
 * Decrease the reference count on a PLAYER by one.
 * If after decrementing, the reference count has reached zero, then the
 * PLAYER and its contents are freed.
 *
 * @param player  The PLAYER whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 *
 */
void player_unref(PLAYER *player, char *why){
	P(&player->mutex);
	player->refcnt--;
	debug("Decrease reference count on player %p (%d -> %d) %s", player, player->refcnt + 1, player->refcnt, why);
	if(player->refcnt == 0){
		debug("Free player %p", player);
		if(player->username != NULL){
			Free(player->username);
		}
		V(&player->mutex);
		if(player != NULL){
			Free(player);
		}
		return;
	}
	V(&player->mutex);
}


/*
 * Get the username of a player.
 *
 * @param player  The PLAYER that is to be queried.
 * @return the username of the player.
 */
char *player_get_name(PLAYER *player){
	return player->username;
}

/*
 * Get the rating of a player.
 *
 * @param player  The PLAYER that is to be queried.
 * @return the rating of the player.
 */
int player_get_rating(PLAYER *player){
	return player->rating;
}

/*
 * Post the result of a game between two players.
 * To update ratings, we use a system of a type devised by Arpad Elo,
 * similar to that used by the US Chess Federation.
 * The player's ratings are updated as follows:
 * Assign each player a score of 0, 0.5, or 1, according to whether that
 * player lost, drew, or won the game.
 * Let S1 and S2 be the scores achieved by player1 and player2, respectively.
 * Let R1 and R2 be the current ratings of player1 and player2, respectively.
 * Let E1 = 1/(1 + 10**((R2-R1)/400)), and
 *     E2 = 1/(1 + 10**((R1-R2)/400))
 * Update the players ratings to R1' and R2' using the formula:
 *     R1' = R1 + 32*(S1-E1)
 *     R2' = R2 + 32*(S2-E2)
 *
 * @param player1  One of the PLAYERs that is to be updated.
 * @param player2  The other PLAYER that is to be updated.
 * @param result   0 if draw, 1 if player1 won, 2 if player2 won.
 */
void player_post_result(PLAYER *player1, PLAYER *player2, int result){
	if(player1 != NULL && player2 != NULL){
		double S1, S2;
		if(result == 0){
			S1 = 0.5;
			S2 = 0.5;
		} else if(result == 1){
			S1 = 1;
			S2 = 0;
		} else if(result == 2){
			S2 = 1;
			S1 = 0;
		} else {
			return;
		}

		int R1 = player1->rating;
		int R2 = player2->rating;

		double E1 = 1/(1 + pow(10, (R2-R1)/400));
		double E2 = 1/(1 + pow(10, (R1-R2)/400));

		R1 += (int)(32*(S1-E1));
		R2 += (int)(32*(S2-E2));

		debug("Post result(%s, %s, %d)", player_get_name(player1), player_get_name(player2), result);

		P(&player1->mutex);
		player1->rating = R1;
		V(&player1->mutex);
		P(&player2->mutex);
		player2->rating = R2;
		V(&player2->mutex);
	}
}