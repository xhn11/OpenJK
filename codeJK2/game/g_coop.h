#pragma once
// Coop (2 players) in the JK2 game DLL. The engine side lives in code/coop.

#define COOP_MODE_SOLO		0
#define COOP_MODE_HOST		1		// same values as the engine's coopRole_t
#define COOP_MODE_GUEST		2

extern int	g_coopMode;				// latched in InitGame from the engine cvar coop_role, fixed for the level

void		G_CoopInitGame( void );									// InitGame, before any entity spawns
void		G_CoopSpawnRange( int *first, int *lastExclusive, qboolean *topDown );	// G_Spawn: where and in which direction
