#include "g_headers.h"
#include "g_local.h"
#include "g_coop.h"
#include "../../code/coop/coop_entnum.h"

static_assert( COOP_ENT_MAX_CLIENTS == MAX_CLIENTS, "coop_entnum.h mirrors MAX_CLIENTS" );
static_assert( COOP_ENT_MAX_NORMAL == ENTITYNUM_MAX_NORMAL, "coop_entnum.h mirrors ENTITYNUM_MAX_NORMAL" );

extern qboolean	spawning;	// g_spawn.cpp: map entities are being spawned

int g_coopMode = COOP_MODE_SOLO;

void G_CoopInitGame( void ) {
	const cvar_t *role = gi.cvar( "coop_role", "0", CVAR_ROM );
	g_coopMode = ( role->integer == COOP_MODE_HOST || role->integer == COOP_MODE_GUEST ) ? role->integer : COOP_MODE_SOLO;
	if ( g_coopMode != COOP_MODE_SOLO ) {
		gi.Printf( "COOP: game mode %s\n", g_coopMode == COOP_MODE_HOST ? "host" : "guest" );
	}
}

void G_CoopSpawnRange( int *first, int *lastExclusive, qboolean *topDown ) {
	Coop_DynamicRange( g_coopMode, spawning ? 1 : 0, first, lastExclusive );
	*topDown = (qboolean)Coop_SpawnTopDown( g_coopMode, spawning ? 1 : 0 );
}
