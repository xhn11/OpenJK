#include "g_headers.h"
#include "g_local.h"
#include "b_local.h"
#include "g_functions.h"
#include "g_coop.h"

// Replicas: entities another machine simulates. The engine (code/coop/coop_snapshot.cpp) calls these
// game exports with the owner's state; the game rebuilds what the engine cannot see.

int G_Coop_EntityOwner( int entNum ) {
	if ( entNum < 0 || entNum >= MAX_GENTITIES || !g_entities[entNum].inuse ) {
		return COOP_OWNER_NONE;
	}
	return g_coopMode == COOP_MODE_SOLO ? COOP_OWNER_HOST : G_CoopOwner( &g_entities[entNum] );
}

void G_Coop_GetEntityExtra( int entNum, coopEntExtra_t *out ) {
	memset( out, 0, sizeof( *out ) );
	out->ownerNum = ENTITYNUM_NONE;
	out->enemyNum = ENTITYNUM_NONE;
	if ( entNum < 0 || entNum >= MAX_GENTITIES ) {
		return;
	}
	const gentity_t *ent = &g_entities[entNum];
	if ( !ent->inuse ) {
		return;
	}
	out->inuse = 1;
	Q_strncpyz( out->classname, ent->classname ? ent->classname : "", sizeof( out->classname ) );
	if ( ent->client && ent->NPC_type ) {
		Q_strncpyz( out->npcType, ent->NPC_type, sizeof( out->npcType ) );
	}
	out->spawnflags = ent->spawnflags;
	out->health = ent->health;
	out->maxHealth = ent->max_health;
	if ( ent->owner && ent->owner->inuse ) {
		out->ownerNum = ent->owner->s.number;
	}
	if ( ent->enemy && ent->enemy->inuse ) {
		out->enemyNum = ent->enemy->s.number;
	}
	out->flags = ent->flags;
	out->svFlags = ent->svFlags;
	out->contents = ent->contents;
	out->hasClient = ent->client ? 1 : 0;
	out->linked = ent->linked ? 1 : 0;
	VectorCopy( ent->mins, out->mins );
	VectorCopy( ent->maxs, out->maxs );
	out->areaPortalOpen = G_CoopPortalCount( entNum );
	out->triggerEnabled = ( ent->svFlags & SVF_INACTIVE ) ? 0 : 1;
}

static gentity_t *G_CoopEntityPtr( int num ) {
	return ( num >= 0 && num < ENTITYNUM_NONE && g_entities[num].inuse ) ? &g_entities[num] : NULL;
}

static qboolean G_CoopReplicaMismatch( const gentity_t *ent, const coopEntExtra_t *ex ) {
	if ( Q_stricmpn( ent->classname ? ent->classname : "", ex->classname, sizeof( ex->classname ) - 1 ) ) {
		return qtrue;
	}
	if ( ( ent->client != NULL ) != ( ex->hasClient != 0 ) ) {
		return qtrue;
	}
	if ( ex->npcType[0] && Q_stricmpn( ent->NPC_type ? ent->NPC_type : "", ex->npcType, sizeof( ex->npcType ) - 1 ) ) {
		return qtrue;
	}
	return qfalse;
}

static gentity_t *G_CoopSpawnReplica( int num, const coopEntExtra_t *ex ) {
	gentity_t *ent = G_SpawnAtNum( num );
	if ( !ent ) {
		return NULL;
	}
	ent->classname = G_NewString( ex->classname );
	return ent;
}

void G_Coop_ApplyEntity( int entNum, const entityState_t *es, const playerState_t *ps, const coopEntExtra_t *ex ) {
	if ( g_coopMode != COOP_MODE_GUEST || entNum < MAX_CLIENTS || entNum >= ENTITYNUM_MAX_NORMAL ) {
		return;		// M3: only the guest replays, and not the players (M4)
	}
	gentity_t *ent = &g_entities[entNum];
	if ( ent->inuse && G_CoopOwnsEnt( ent ) ) {
		// the guest allocates its own entities top-down (code/coop/coop_entnum.h); when the map is nearly
		// full the host can reach the same slot. The host's entity wins, the guest's local one goes.
		gi.Printf( "COOP: entity %d taken over by the host (%s -> %s)\n", entNum, ent->classname, ex->classname );
		G_FreeEntity( ent );
	}
	if ( ent->inuse && G_CoopReplicaMismatch( ent, ex ) ) {
		gi.Printf( "COOP: entity %d mismatch (%s -> %s)\n", entNum, ent->classname, ex->classname );
		G_FreeEntity( ent );
	}
	if ( ex->hasClient && !ent->inuse ) {
		return;		// NPC replicas are built from Task 12 on
	}
	if ( !ent->inuse ) {
		ent = G_CoopSpawnReplica( entNum, ex );
		if ( !ent ) {
			return;
		}
	}
	ent->s = *es;
	ent->s.number = entNum;
	if ( ent->client && ps ) {
		char *saberModel = ent->client->ps.saberModel;	// a pointer into this machine's memory (plan-M3 R1)
		ent->client->ps = *ps;
		ent->client->ps.saberModel = saberModel;
	}
	ent->spawnflags = ex->spawnflags;
	ent->health = ex->health;
	ent->max_health = ex->maxHealth;
	ent->flags = ex->flags;
	ent->svFlags = ex->svFlags;
	ent->contents = ex->contents;
	VectorCopy( ex->mins, ent->mins );
	VectorCopy( ex->maxs, ent->maxs );
	ent->owner = G_CoopEntityPtr( ex->ownerNum );
	ent->enemy = G_CoopEntityPtr( ex->enemyNum );
	if ( ent->client ) {
		VectorCopy( ent->client->ps.origin, ent->currentOrigin );
		VectorCopy( ent->s.apos.trBase, ent->currentAngles );
	} else if ( ent->s.eType != ET_MOVER ) {
		EvaluateTrajectory( &ent->s.pos, level.time, ent->currentOrigin );
		EvaluateTrajectory( &ent->s.apos, level.time, ent->currentAngles );
	}
	if ( ex->linked ) {
		gi.linkentity( ent );
	} else if ( ent->linked ) {
		gi.unlinkentity( ent );
	}
}

void G_Coop_FreeEntity( int entNum ) {
	if ( g_coopMode != COOP_MODE_GUEST || entNum < MAX_CLIENTS || entNum >= ENTITYNUM_MAX_NORMAL ) {
		return;
	}
	gentity_t *ent = &g_entities[entNum];
	if ( ent->inuse && !G_CoopOwnsEnt( ent ) ) {
		G_FreeEntity( ent );
	}
}
