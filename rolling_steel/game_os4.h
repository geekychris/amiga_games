/*
 * Force-included before game.c (the 3DO code, unchanged): its static kill()
 * clashes with the POSIX kill() newlib's headers declare, so those headers
 * are read first and game.c's kill is renamed after them.
 */
#include <string.h>
#include <signal.h>
#define kill rs_kill_player
