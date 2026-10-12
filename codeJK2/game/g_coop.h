
#pragma once
// Coop (2 players) in the JK2 game DLL. The engine side lives in code/coop.

#define COOP_MODE_SOLO		0
#define COOP_MODE_HOST		1		// same values as the engine's coopRole_t and coopOwner_t
#define COOP_MODE_GUEST		2

extern int		g_coopMode;				// latched in InitGame from the engine cvar coop_role, fixed for the level
extern qboolean	g_coopSpawningReplica;	// G_SpawnAtNum is building a replica of a host entity

void		G_CoopInitGame( void );									// InitGame, before any entity spawns
void		G_CoopSpawnRange( int *first, int *lastExclusive, qboolean *topDown );	// G_Spawn: where and in which direction
void		G_CoopHookImports( void );								// GetGameAPI, after gi = *import

// ownership (design 2, 3, 6): the owner simulates an entity, the other machine only replays it
int			G_CoopOwner( const gentity_t *ent );					// COOP_MODE_HOST or COOP_MODE_GUEST
qboolean	G_CoopOwnsEnt( const gentity_t *ent );					// solo: always qtrue
void		G_CoopSetOwner( gentity_t *ent, int owner );
void		G_CoopEntityInit( gentity_t *ent );						// G_InitGentity: owner by who allocates, portal count 0

// what the guest may do to host entities (M5 turns the blocked cases into messages to the host)
qboolean	G_CoopIsPhysicalTrigger( const gentity_t *ent );		// design 17: each machine runs these for its own player
qboolean	G_CoopRunsLocally( const gentity_t *ent );				// G_RunFrame: think/move here
qboolean	G_CoopBlockUse( const gentity_t *self );
qboolean	G_CoopBlockTouch( const gentity_t *self );
qboolean	G_CoopBlockDamage( const gentity_t *targ );

// areaportals: net count of open calls per entity (the engine keeps reference counts)
void		G_CoopAdjustAreaPortal( gentity_t *ent, qboolean open );
int			G_CoopPortalCount( int entNum );

void		G_CoopEnts_f( void );									// console: coop_ents

