#include "game.h"
#include "csapp.h"
#include "debug.h"

static GAME_ROLE check(GAME_ROLE *board);
static char role_to_xo(GAME_ROLE role);
static void fill_string(char *string, GAME_ROLE *board, GAME_ROLE nextmover);

/*
 * The GAME type is a structure type that defines the state of a game.
 * You will have to give a complete structure definition in game.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.
 */
typedef struct game {
	int refcnt;
	int winner;
	GAME_ROLE nextmover;
	GAME_ROLE board[9]; // [9xboard spots]
	sem_t mutex;
} GAME;

/*
 * The GAME_MOVE type is a structure type that defines a move in a game.
 * The details are up to you.  A GAME_MOVE is immutable.
 */
typedef struct game_move {
	int spot;
	GAME_ROLE role;
} GAME_MOVE;

/*
 * Create a new game in an initial state.  The returned game has a
 * reference count of one.
 *
 * @return the newly created GAME, if initialization was successful,
 * otherwise NULL.
 */
GAME *game_create(void){
	GAME *game = (GAME *) Malloc(sizeof(GAME));
	game->refcnt = 0;
	memset(game->board, 0, sizeof(game->board));
	game->winner = -1; // -1 indicating game not ended
	game -> nextmover = 1;
	Sem_init(&game->mutex, 0, 1);
	game_ref(game, "for newly created game");
	return game;
}

/*
 * Increase the reference count on a game by one.
 *
 * @param game  The GAME whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same GAME object that was passed as a parameter.
 */
GAME *game_ref(GAME *game, char *why){
	P(&game->mutex);
	game->refcnt++;
	debug("Increase reference count on game %p (%d -> %d) %s", game, game->refcnt - 1, game->refcnt, why);
	V(&game->mutex);
	return game;
}

/*
 * Decrease the reference count on a game by one.  If after
 * decrementing, the reference count has reached zero, then the
 * GAME and its contents are freed.
 *
 * @param game  The GAME whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 */
void game_unref(GAME *game, char *why){
	P(&game->mutex);

	game->refcnt--;
	debug("Decrease reference count on game %p (%d -> %d) %s", game, game->refcnt + 1, game->refcnt, why);

	if(game->refcnt == 0){
		debug("Free game %p", game);
		if(game != NULL){
			Free(game);
		}
		return;
	}

	V(&game->mutex);
}

/*
 * Apply a GAME_MOVE to a GAME.
 * If the move is illegal in the current GAME state, then it is an error.
 *
 * @param game  The GAME to which the move is to be applied.
 * @param move  The GAME_MOVE to be applied to the game.
 * @return 0 if application of the move was successful, otherwise -1.
 */
int game_apply_move(GAME *game, GAME_MOVE *move){
	// check if game is NULL or move is NULL
	if(game == NULL || move == NULL){
		return -1;
	}
	// check if move has all valid values
	if(move->spot < 0 || move->spot > 8){
		return -1;
	}
	if(move->role < 1 || move->role > 2){
		return -1;
	}
	P(&game->mutex);
	// check if game has all valid values
	if(game->winner != -1){ // then game has ended
		V(&game->mutex);
		return -1;
	}
	// check if move is illegal
	if(game->board[move->spot] != 0){
		V(&game->mutex);
		return -1;
	}
 	// debug("Apply move %s to game %p", game_unparse_move(move), game);
	// Passed all checks
	game->board[move->spot] = move->role;

	// update game winner based on new move
	if(game->board != NULL){
		game->winner = check(game->board);
     debug("Game is over, %c wins", role_to_xo(game->winner));
	}
	if(game->nextmover == 1){
		game->nextmover = 2;
	} else {
		game->nextmover = 1;
	}

	V(&game->mutex);
	return 0;
}

/*
 * Submit the resignation of the GAME by the player in a specified
 * GAME_ROLE. It is an error if the game has already terminated.
 *
 * @param game  The GAME to be resigned.
 * @param role  The GAME_ROLE of the player making the resignation.
 * @return 0 if resignation was successful, otherwise -1.
 */
int game_resign(GAME *game, GAME_ROLE role){
	if(game == NULL || role == NULL_ROLE){
		return -1;
	}
	if(role != 1 && role != 2){
		return -1;
	}
	P(&game->mutex);
	if(game->winner != -1){ // game already terminiated
		V(&game->mutex);
		return -1;
	}
	if(role == 1){
		game->winner = SECOND_PLAYER_ROLE;
     debug("Game is over, %c wins", role_to_xo(game->winner));
	} else {
		game->winner = FIRST_PLAYER_ROLE;
     debug("Game is over, %c wins", role_to_xo(game->winner));
	}

	V(&game->mutex);
	return 0;

}

/*
 * Get a string that describes the current GAME state, in a format
 * appropriate for human users.  The returned string is in malloc'ed
 * storage, which the caller is responsible for freeing when the string
 * is no longer required.
 *
 * @param game  The GAME for which the state description is to be
 * obtained.
 * @return  A string that describes the current GAME state.
 */
char *game_unparse_state(GAME *game){
	if(game == NULL){
		return NULL;
	}
	P(&game->mutex);

	char *string = (char *) Malloc(41*sizeof(char));
	if(string == NULL){
		V(&game->mutex);
		return NULL;
	}
	fill_string(string, game->board, game->nextmover);

	V(&game->mutex);
	return string;
}

/*
 * Determine if a specifed GAME has terminated.
 *
 * @param game  The GAME to be queried.
 * @return 1 if the game is over, 0 otherwise.
 */
int game_is_over(GAME *game){
	if(game->winner != -1){
		return 1;
	}
	return 0;
}

/*
 * Get the GAME_ROLE of the player who has won the game.
 *
 * @param game  The GAME for which the winner is to be obtained.
 * @return  The GAME_ROLE of the winning player, if there is one.
 * If the game is not over, or there is no winner because the game
 * is drawn, then NULL_PLAYER is returned.
 */
GAME_ROLE game_get_winner(GAME *game){
	return game->winner;
}

/*
 * Attempt to interpret a string as a move in the specified GAME.
 * If successful, a GAME_MOVE object representing the move is returned,
 * otherwise NULL is returned.  The caller is responsible for freeing
 * the returned GAME_MOVE when it is no longer needed.
 * Refer to the assignment handout for the syntax that should be used
 * to specify a move.
 *
 * @param game  The GAME for which the move is to be parsed.
 * @param role  The GAME_ROLE of the player making the move.
 * If this is not NULL_ROLE, then it must agree with the role that is
 * currently on the move in the game.
 * @param str  The string that is to be interpreted as a move.
 * @return  A GAME_MOVE described by the given string, if the string can
 * in fact be interpreted as a move, otherwise NULL.
 */
GAME_MOVE *game_parse_move(GAME *game, GAME_ROLE role, char *str){
	if(str == NULL || game == NULL || role < 0 || role > 2){
		return NULL;
	}
	if(role == NULL_ROLE && role != game->nextmover){
		return NULL;
	}
	GAME_MOVE *move = (GAME_MOVE *) Malloc(sizeof(GAME_MOVE));
	if(move == NULL){
		return NULL;
	}
	move->role = role;
	char nstr[2];
	nstr[0] = str[0];
	nstr[1] = '\0';
	move->spot = atoi(nstr) - 1;
	return move;
}

/*
 * Get a string that describes a specified GAME_MOVE, in a format
 * appropriate to be shown to human users.  The returned string should
 * be in a format from which the GAME_MOVE can be recovered by applying
 * game_parse_move() to it.  The returned string is in malloc'ed storage,
 * which it is the responsibility of the caller to free when it is no
 * longer needed.
 *
 * @param move  The GAME_MOVE whose description is to be obtained.
 * @return  A string describing the specified GAME_MOVE.
 */
char *game_unparse_move(GAME_MOVE *move){
	if(move == NULL){
		return NULL;
	}
	char xo = role_to_xo(move->role);
	if(xo == 0){
		return NULL;
	}
	char *string = (char *) Malloc(5*sizeof(char));
	if(string == NULL){
		return NULL;
	}
	string[0] = (move->spot) + '1';
	string[1] = '<';
	string[2] = '-';
	string[3] = xo;
	string[4] = '\0';
	return string;
}


/*
 * Fills a string of length 41 exactly, based on board and nextmover.
 */
static void fill_string(char *string, GAME_ROLE *board, GAME_ROLE nextmover){
	int index = 0;
	int j = 0;
	for(int i = 0; i < 2; i++){
		string[index++] = role_to_xo(board[j++]);
		string[index++] = '|';
		string[index++] = role_to_xo(board[j++]);
		string[index++] = '|';
		string[index++] = role_to_xo(board[j++]);
		string[index++] = '\n';
		for(int k=0; k<5; k++){
			string[index++] = '-';
		}
		string[index++] = '\n';
	}
	string[index++] = role_to_xo(board[j++]);
	string[index++] = '|';
	string[index++] = role_to_xo(board[j++]);
	string[index++] = '|';
	string[index++] = role_to_xo(board[j++]);
	string[index++] = '\n';
	string[index++] = role_to_xo(nextmover);
	char *stringp = &string[index];
	strcpy(stringp, " to move\n");
	string[40] = '\0';
}


/*
 * Run through the board, return the GAME_ROLE of the player who won
 * if any. Requires exclusive access to board.
 * RETURN: player role of winner, 0 if tied, -1 if no winner found
 */
static GAME_ROLE check(GAME_ROLE *board){
	// check if tied
	int zero_found = 0;
	for(int i = 0; i < 9; i++){
		if(board[i] == 0){
			zero_found = 1;
			break;
		}
	}
	if(!zero_found){ // tied
		return 0;
	}
	// eight winning patterns
	if( (board[2] == board[4]) && (board[2] == board[6]) && (board[2] != 0)){ // 357
		return board[2];
	}
	if( (board[0] == board[4]) && (board[0] == board[8]) && (board[0] != 0)){ // 159
		return board[0];
	}
	if( (board[2] == board[5]) && (board[2] == board[8]) && (board[2] != 0)){ // 369
		return board[2];
	}
	if( (board[1] == board[4]) && (board[1] == board[7]) && (board[1] != 0)){ // 258
		return board[1];
	}
	if( (board[0] == board[3]) && (board[0] == board[6]) && (board[0] != 0)){ // 147
		return board[0];
	}
	if( (board[6] == board[7]) && (board[6] == board[8]) && (board[6] != 0)){ // 789
		return board[6];
	}
	if( (board[3] == board[4]) && (board[3] == board[5]) && (board[3] != 0)){ // 456
		return board[3];
	}
	if( (board[0] == board[1]) && (board[0] == board[2]) && (board[0] != 0)){ // 123
		return board[0];
	}

	// no winning patterns found
	return -1;
}


static char role_to_xo(GAME_ROLE role){
	if(role == 1){
		return 'X';
	} else if(role == 2){
		return 'O';
	} else if(role == 0){
		return ' ';
	} else {
		return 0;
	}
}




