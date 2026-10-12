
#include "g_headers.h"
#include "g_local.h"
#include "g_coop.h"
#include "../../code/coop/coop_entnum.h"

static_assert( COOP_ENT_MAX_CLIENTS == MAX_CLIENTS, "coop_entnum.h mirrors MAX_CLIENTS" );
static_assert( COOP_ENT_MAX_NORMAL == ENTITYNUM_MAX_NORMAL, "coop_entnum.h mirrors ENTITYNUM_MAX_NORMAL" );

extern qboolean	spawning;	// g_spawn.cpp: map entities are being spawned

int				g_coopMode = COOP_MODE_SOLO;
qboolean		g_coopSpawningReplica = qfalse;

static signed char	g_coopOwner[MAX_GENTITIES];
static int			g_coopPortal[MAX_GENTITIES];
static void			( *g_coopEngineAdjustAreaPortalState )( gentity_t *ent, qboolean open );

void G_CoopInitGame( void ) {
	const cvar_t *role = gi.cvar( "coop_role", "0", CVAR_ROM );
	g_coopMode = ( role->integer == COOP_MODE_HOST || role->integer == COOP_MODE_GUEST ) ? role->integer : COOP_MODE_SOLO;
	g_coopSpawningReplica = qfalse;
	memset( g_coopOwner, COOP_MODE_HOST, sizeof( g_coopOwner ) );
	memset( g_coopPortal, 0, sizeof( g_coopPortal ) );
	g_coopOwner[0] = (signed char)( g_coopMode == COOP_MODE_GUEST ? COOP_MODE_GUEST : COOP_MODE_HOST );
	g_coopOwner[1] = (signed char)( g_coopMode == COOP_MODE_GUEST ? COOP_MODE_HOST : COOP_MODE_GUEST );
	if ( g_coopMode != COOP_MODE_SOLO ) {
		gi.Printf( "COOP: game mode %s\n", g_coopMode == COOP_MODE_HOST ? "host" : "guest" );
	}
}

void G_CoopSpawnRange( int *first, int *lastExclusive, qboolean *topDown ) {
	Coop_DynamicRange( g_coopMode, spawning ? 1 : 0, first, lastExclusive );
	*topDown = (qboolean)Coop_SpawnTopDown( g_coopMode, spawning ? 1 : 0 );
}

static int G_CoopIndex( const gentity_t *ent ) {
	return (int)( ent - g_entities );
}

int G_CoopOwner( const gentity_t *ent ) {
	return g_coopOwner[G_CoopIndex( ent )];
}

qboolean G_CoopOwnsEnt( const gentity_t *ent ) {
	if ( g_coopMode == COOP_MODE_SOLO ) {
		return qtrue;
	}
	return (qboolean)( g_coopOwner[G_CoopIndex( ent )] == g_coopMode );
}

void G_CoopSetOwner( gentity_t *ent, int owner ) {
	g_coopOwner[G_CoopIndex( ent )] = (signed char)owner;
}

void G_CoopEntityInit( gentity_t *ent ) {
	const int n = G_CoopIndex( ent );
	g_coopPortal[n] = 0;
	if ( n < MAX_CLIENTS ) {
		return;	// set in G_CoopInitGame
	}
	// map entities spawn identically on both machines and belong to the host; so do replicas.
	// Whatever the guest allocates while playing (its missiles, temp entities) is its own.
	const qboolean guestOwn = (qboolean)( g_coopMode == COOP_MODE_GUEST && !spawning && !g_coopSpawningReplica );
	g_coopOwner[n] = (signed char)( guestOwn ? COOP_MODE_GUEST : COOP_MODE_HOST );
}

qboolean G_CoopIsPhysicalTrigger( const gentity_t *ent ) {
	const char *c = ent->classname;
	return (qboolean)( c && ( !Q_stricmp( c, "trigger_push" ) || !Q_stricmp( c, "trigger_teleport" ) || !Q_stricmp( c, "trigger_hurt" ) ) );
}

qboolean G_CoopRunsLocally( const gentity_t *ent ) {
	return (qboolean)( G_CoopOwnsEnt( ent ) || ( g_coopMode == COOP_MODE_GUEST && G_CoopIsPhysicalTrigger( ent ) ) );
}

qboolean G_CoopBlockUse( const gentity_t *self ) {
	return (qboolean)( g_coopMode == COOP_MODE_GUEST && !G_CoopOwnsEnt( self ) );
}

qboolean G_CoopBlockTouch( const gentity_t *self ) {
	return (qboolean)( g_coopMode == COOP_MODE_GUEST && !G_CoopRunsLocally( self ) );
}

qboolean G_CoopBlockDamage( const gentity_t *targ ) {
	return (qboolean)( g_coopMode != COOP_MODE_SOLO && !G_CoopOwnsEnt( targ ) );
}

void G_CoopAdjustAreaPortal( gentity_t *ent, qboolean open ) {
	g_coopPortal[G_CoopIndex( ent )] += open ? 1 : -1;
	g_coopEngineAdjustAreaPortalState( ent, open );
}

int G_CoopPortalCount( int entNum ) {
	return ( entNum >= 0 && entNum < MAX_GENTITIES ) ? g_coopPortal[entNum] : 0;
}

void G_CoopHookImports( void ) {
	g_coopEngineAdjustAreaPortalState = gi.AdjustAreaPortalState;
	gi.AdjustAreaPortalState = G_CoopAdjustAreaPortal;
}

void G_CoopEnts_f( void ) {
	int total = 0, ours = 0, clients = 0;
	for ( int i = 0; i < globals.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];
		if ( !ent->inuse ) {
			continue;
		}
		total++;
		if ( G_CoopOwnsEnt( ent ) ) {
			ours++;
		}
		if ( ent->client ) {
			clients++;
		}
	}
	gi.Printf( "COOP: entities %d (ours %d, other %d, clients %d)\n", total, ours, total - ours, clients );
}


gentity_t *G_SpawnAtNum( int num ) {
	if ( num < MAX_CLIENTS || num >= ENTITYNUM_MAX_NORMAL || g_entities[num].inuse ) {
		return NULL;
	}
	gentity_t *e = &g_entities[num];
	g_coopSpawningReplica = qtrue;
	G_InitGentity( e );		// owner: host (G_CoopEntityInit)
	g_coopSpawningReplica = qfalse;
	if ( num >= globals.num_entities ) {
		globals.num_entities = num + 1;
	}
	return e;
}

void G_CoopEntInfo_f( void ) {
	const int num = atoi( gi.argv( 1 ) );
	if ( gi.argc() != 2 || num < 0 || num >= MAX_GENTITIES ) {
		gi.Printf( "usage: coop_entinfo <entity number>\n" );
		return;
	}
	coopEntExtra_t ex;
	G_Coop_GetEntityExtra( num, &ex );
	const gentity_t *ent = &g_entities[num];
	gi.Printf( "COOP: entinfo %d owner %d class %s npc \"%s\" linked %d portal %d origin %.0f %.0f %.0f\n",
		num, G_Coop_EntityOwner( num ), ex.inuse ? ex.classname : "(free)", ex.npcType, ex.linked, ex.areaPortalOpen,
		ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] );
}
