
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
	G_CoopNpcInit();
	G_CoopP2Init();
	G_CoopLifeInit();
	G_CoopClaimInit();
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

int G_CoopPartnerMode( void ) {
	return g_coopMode == COOP_MODE_HOST ? COOP_MODE_GUEST : COOP_MODE_HOST;
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
	// map entities spawn identically on both machines and belong to the host. Whatever a machine
	// allocates while playing (missiles, temp entities) is its own; replicas belong to the partner.
	if ( g_coopMode == COOP_MODE_SOLO || spawning ) {
		g_coopOwner[n] = COOP_MODE_HOST;
	} else if ( g_coopSpawningReplica ) {
		g_coopOwner[n] = (signed char)G_CoopPartnerMode();
	} else {
		g_coopOwner[n] = (signed char)g_coopMode;
	}
}

qboolean G_CoopIsPhysicalTrigger( const gentity_t *ent ) {
	const char *c = ent->classname;
	return (qboolean)( c && ( !Q_stricmp( c, "trigger_push" ) || !Q_stricmp( c, "trigger_teleport" ) || !Q_stricmp( c, "trigger_hurt" ) ) );
}

qboolean G_CoopRunsLocally( const gentity_t *ent ) {
	return (qboolean)( ( G_CoopOwnsEnt( ent ) && !G_CoopNpcGiving( ent ) ) || ( g_coopMode == COOP_MODE_GUEST && G_CoopIsPhysicalTrigger( ent ) ) );
}

qboolean G_CoopBlockUse( const gentity_t *self ) {
	return (qboolean)( g_coopMode == COOP_MODE_GUEST && !G_CoopOwnsEnt( self ) );
}

qboolean G_CoopBlockTouch( const gentity_t *self ) {
	return (qboolean)( g_coopMode == COOP_MODE_GUEST && !G_CoopRunsLocally( self ) );
}

qboolean G_CoopBlockDamage( const gentity_t *targ ) {
	return (qboolean)( g_coopMode != COOP_MODE_SOLO && ( !G_CoopOwnsEnt( targ ) || G_CoopNpcGiving( targ ) ) );
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
	gi.Printf( "COOP: entinfo %d owner %d class %s npc \"%s\" linked %d portal %d health %d origin %.0f %.0f %.0f\n",
		num, G_Coop_EntityOwner( num ), ex.inuse ? ex.classname : "(free)", ex.npcType, ex.linked, ex.areaPortalOpen, ex.health,
		ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] );
}

#define COOP_MAX_PORTAL_STEPS	8

void G_CoopSetPortalCount( gentity_t *ent, int count ) {
	const int n = G_CoopIndex( ent );
	int diff = count - g_coopPortal[n];
	if ( diff > COOP_MAX_PORTAL_STEPS || diff < -COOP_MAX_PORTAL_STEPS ) {
		gi.Printf( "COOP: areaportal of %d off by %d, clamped\n", n, diff );
		diff = diff > 0 ? COOP_MAX_PORTAL_STEPS : -COOP_MAX_PORTAL_STEPS;
	}
	for ( ; diff > 0; diff-- ) {
		G_CoopAdjustAreaPortal( ent, qtrue );
	}
	for ( ; diff < 0; diff++ ) {
		G_CoopAdjustAreaPortal( ent, qfalse );
	}
}

// ---------------------------------------------------------------- game commands between the two games
// G_CoopSend writes "<name> <args>" to the partner's game (reliable, in order); there the engine
// tokenizes it with argv(0) = "g" and calls G_Coop_GameCommand. Entity numbers go out in the
// partner's numbering: G_CoopWireNum swaps 0 <-> 1.

int G_CoopWireNum( int entNum ) {
	return entNum == 0 ? 1 : entNum == 1 ? 0 : entNum;
}

qboolean G_CoopPartnerInWorld( void ) {
	return (qboolean)( g_coopMode != COOP_MODE_SOLO && gi.Coop_PartnerInWorld && gi.Coop_PartnerInWorld() );
}

void G_CoopSend( const char *fmt, ... ) {
	if ( !G_CoopPartnerInWorld() ) {
		return;
	}
	va_list	argptr;
	char	text[1024];
	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	gi.Coop_SendGameCommand( text );
}

void G_CoopGTest_f( void ) {
	G_CoopSend( "echo \"%s\"", gi.argv( 1 ) );
}

// ---------------------------------------------------------------- two players (M7, design 6, 17, 20, 26)
// Entity 0 is this machine's player, entity 1 the partner's puppet. Code that meant "the player"
// asks these instead of comparing with g_entities[0].

qboolean G_IsPlayer( const gentity_t *ent ) {
	if ( !ent || !ent->client ) {
		return qfalse;
	}
	return (qboolean)( ent->s.number == 0 || ( ent->s.number == 1 && g_coopMode != COOP_MODE_SOLO && ent->inuse ) );
}

gentity_t *G_PlayerNum( int i ) {
	if ( i < 0 || i >= MAX_CLIENTS ) {
		return NULL;
	}
	gentity_t *ent = &g_entities[i];
	return ( ent->inuse && ent->client && ( i == 0 || g_coopMode != COOP_MODE_SOLO ) ) ? ent : NULL;
}

gentity_t *G_ClosestPlayer( const vec3_t org, qboolean aliveOnly ) {
	gentity_t *best = NULL;
	float bestDist = 0;
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *p = G_PlayerNum( i );
		if ( !p || ( aliveOnly && p->health <= 0 ) ) {
			continue;
		}
		const float d = DistanceSquared( org, p->currentOrigin );
		if ( !best || d < bestDist ) {
			best = p;
			bestDist = d;
		}
	}
	return best ? best : &g_entities[0];
}

qboolean G_AnyPlayerAlive( void ) {
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		const gentity_t *p = G_PlayerNum( i );
		if ( p && p->health > 0 ) {
			return qtrue;
		}
	}
	return qfalse;
}

// saves (host only, design 23): everything we own, and the NPCs the guest runs right now - they come
// back to the host with the save; never the puppet or the guest's missiles
qboolean G_CoopSaveEnt( const gentity_t *ent ) {
	if ( G_CoopOwnsEnt( ent ) ) {
		return qtrue;
	}
	return (qboolean)( ent->NPC && ent->client && ent->s.number >= MAX_CLIENTS );
}

// design 20: g_coopFriendlyFire 0 - neither player's attacks hurt the other one
qboolean G_CoopNoFriendlyFire( const gentity_t *targ, const gentity_t *attacker ) {
	static cvar_t *g_coopFriendlyFire;
	if ( g_coopMode == COOP_MODE_SOLO || targ == attacker || !G_IsPlayer( targ ) || !G_IsPlayer( attacker ) ) {
		return qfalse;
	}
	if ( !g_coopFriendlyFire ) {
		g_coopFriendlyFire = gi.cvar( "g_coopFriendlyFire", "0", CVAR_ARCHIVE );
	}
	return (qboolean)!g_coopFriendlyFire->integer;
}

// design 20: the allies follow Kyle; while he waits to respawn, they follow player 2
void G_CoopAlliesFollow( void ) {
	gentity_t *kyle = &g_entities[0];
	gentity_t *p2 = G_PlayerNum( 1 );
	if ( g_coopMode != COOP_MODE_HOST || !p2 ) {
		return;
	}
	const qboolean kyleDown = (qboolean)( kyle->health <= 0 && p2->health > 0 );
	gentity_t *from = kyleDown ? kyle : p2;
	gentity_t *to = kyleDown ? p2 : kyle;
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( ent->inuse && ent->client && ent->NPC && ent->client->leader == from && ent->client->playerTeam == TEAM_PLAYER ) {
			ent->client->leader = to;
		}
	}
}

// a centerprint on this machine only (gi.SendServerCommand "cp" from the host also goes to the guest)
void G_CoopCenterPrint( const char *text ) {
	extern void CG_CenterPrint( const char *str, int y );
	CG_CenterPrint( text, SCREEN_HEIGHT * 0.25 );
}
