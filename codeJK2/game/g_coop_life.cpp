#include "g_headers.h"
#include "g_local.h"
#include "g_functions.h"
#include "b_local.h"
#include "g_coop.h"

// Death and respawn, player 2's carried state, skipping levels (design 10, 13, 22-24).

extern vec3_t	playerMins, playerMaxs;
extern void		Player_RestoreFromPrevLevel( gentity_t *ent );
extern void		respawn( gentity_t *ent );
extern void		G_SetG2PlayerModel( gentity_t * const ent, const char *modelName, const char *customSkin, const char *surfOff, const char *surfOn );
extern void		target_level_change_use( gentity_t *self, gentity_t *other, gentity_t *activator );
extern void		WP_SaberInitBladeData( gentity_t *ent );

#define COOP_BOTH_DEAD_MS		3000
#define COOP_P2STATE_MS			5000

static cvar_t	*coop_respawnDelay;
static int		g_coopDeathTime;		// level.time our player died, 0 alive
static int		g_coopBothDeadTime;
static int		g_coopLastCountdown;
static int		g_coopNextP2State;
static qboolean	g_coopNeedSent;
static qboolean	g_coopArrived;
static qboolean	g_coopNeedPending;		// host: p2need waits for player 2's character (a loaded save gives Kyle his weapons late)
static qboolean	g_coopCameWithState;	// guest: playersave was set at InitGame (the host's level change), before the map-start autosave fills it

void G_CoopLifeInit( void ) {
	coop_respawnDelay = gi.cvar( "coop_respawnDelay", "10", CVAR_ARCHIVE );
	g_coopDeathTime = 0;
	g_coopBothDeadTime = 0;
	g_coopLastCountdown = -1;
	g_coopNextP2State = 0;
	g_coopNeedSent = qfalse;
	g_coopNeedPending = qfalse;
	g_coopArrived = qfalse;
	char s[16];
	gi.Cvar_VariableStringBuffer( "playersave", s, sizeof( s ) );
	g_coopCameWithState = (qboolean)( s[0] != 0 );
	// the host keeps the guest's last state (design 23); a fresh host starts without one
	gi.cvar( "coop_p2save", "", 0 );
	gi.cvar( "coop_p2ammo", "", 0 );
	gi.cvar( "coop_p2inv", "", 0 );
	gi.cvar( "coop_p2fplvl", "", 0 );
}

// ---------------------------------------------------------------- respawn next to the partner (design 10)

// the partner lives, so our death only waits for a respawn: no SP mission failed screen, scripts go on
qboolean G_CoopPartnerAlive( void ) {
	const gentity_t *partner = G_PlayerNum( 1 );
	return (qboolean)( G_CoopPartnerInWorld() && partner && partner->health > 0 );
}

static void G_CoopRevive( gentity_t *ent, const vec3_t near ) {
	gclient_t *cl = ent->client;
	if ( cl->dismembered || ( ent->flags & FL_DISINTEGRATED ) ) {
		gi.G2API_CleanGhoul2Models( ent->ghoul2 );
		ent->playerModel = -1;
		ent->weaponModel = -1;
		G_SetG2PlayerModel( ent, ent->NPC_type ? ent->NPC_type : "kyle", NULL, NULL, NULL );
		cl->dismembered = qfalse;
	}
	ent->flags &= ~FL_DISINTEGRATED;
	ent->health = cl->ps.stats[STAT_HEALTH] = 100;
	cl->ps.stats[STAT_ARMOR] = 0;
	cl->ps.pm_type = PM_NORMAL;
	cl->ps.eFlags &= ~EF_NODRAW;
	ent->takedamage = qtrue;
	ent->contents = CONTENTS_BODY;
	ent->e_DieFunc = dieF_player_die;
	VectorCopy( playerMins, ent->mins );
	VectorCopy( playerMaxs, ent->maxs );
	cl->ps.legsAnimTimer = cl->ps.torsoAnimTimer = 0;
	NPC_SetAnim( ent, SETANIM_BOTH, BOTH_STAND2, SETANIM_FLAG_OVERRIDE );
	G_CoopTeleportNear( ent, near );
	G_CoopCenterPrint( "" );
	gi.Printf( "COOP: respawned next to the partner\n" );
}

// ClientThink, our player is dead: qtrue when coop takes care of it (no SP "press fire to reload")
qboolean G_CoopRespawnThink( gentity_t *ent ) {
	if ( ent->s.number != 0 || !G_CoopPartnerInWorld() ) {
		g_coopDeathTime = 0;
		return qfalse;
	}
	gentity_t *partner = G_PlayerNum( 1 );
	const qboolean partnerAlive = (qboolean)( partner && partner->health > 0 );
	if ( !g_coopDeathTime ) {
		g_coopDeathTime = level.time;
		g_coopLastCountdown = -1;
		gi.Printf( "COOP: our player died, the partner %s (health %d)\n", partnerAlive ? "lives" : "is dead or missing",
			partner ? partner->health : -1 );
	}
	if ( !partnerAlive ) {
		// both dead (design 10): the host reloads the autosave, the guest follows the host's map
		if ( !g_coopBothDeadTime ) {
			g_coopBothDeadTime = level.time;
		}
		if ( g_coopMode == COOP_MODE_HOST && level.time - g_coopBothDeadTime > COOP_BOTH_DEAD_MS ) {
			g_coopBothDeadTime = 0;
			respawn( ent );
		}
		return qtrue;
	}
	g_coopBothDeadTime = 0;
	const int delay = coop_respawnDelay->integer * 1000;
	const int left = ( g_coopDeathTime + delay - level.time + 999 ) / 1000;
	if ( left > 0 ) {
		if ( left != g_coopLastCountdown ) {
			g_coopLastCountdown = left;
			G_CoopCenterPrint( va( "Respawn za %d", left ) );
		}
		return qtrue;
	}
	vec3_t spot;
	if ( !G_CoopFindSpotNear( ent, partner->currentOrigin, spot ) ) {
		if ( g_coopLastCountdown != 0 ) {
			g_coopLastCountdown = 0;
			G_CoopCenterPrint( "Ceka se, az bude partner na bezpecnem miste" );
		}
		return qtrue;
	}
	g_coopDeathTime = 0;
	G_CoopRevive( ent, partner->currentOrigin );
	return qtrue;
}

// ---------------------------------------------------------------- player 2's carried state (design 22, 23)
// The guest's own engine carries its player over level changes (maptransition, coop_session.cpp).
// Saves and fresh joins need the host's copy: the guest sends its state now and then (the
// playersave cvar format, SV_Player_EndOfLevelSave), the host keeps it in coop_p2* and its saves.

static void G_CoopFormatState( const playerState_t *ps, char *save, char *ammo, char *inv, char *fplvl, int size ) {
	Com_sprintf( save, size, "%i %i %i %i %i %i %i %f %f %f %i %i %i %i %i %i",
		ps->stats[STAT_HEALTH], ps->stats[STAT_ARMOR], ps->stats[STAT_WEAPONS], ps->stats[STAT_ITEMS],
		ps->weapon, ps->weaponstate, ps->batteryCharge, ps->viewangles[0], ps->viewangles[1], ps->viewangles[2],
		ps->forcePowersKnown, ps->forcePower, ps->saberActive, ps->saberAnimLevel, ps->saberLockEnemy, ps->saberLockTime );
	int len = 0;
	ammo[0] = inv[0] = fplvl[0] = 0;
	for ( int i = 0; i < AMMO_MAX; i++ ) {
		len += Com_sprintf( ammo + len, size - len, " %i", ps->ammo[i] );
	}
	len = 0;
	for ( int i = 0; i < INV_MAX; i++ ) {
		len += Com_sprintf( inv + len, size - len, " %i", ps->inventory[i] );
	}
	len = 0;
	for ( int i = 0; i < NUM_FORCE_POWERS; i++ ) {
		len += Com_sprintf( fplvl + len, size - len, " %i", ps->forcePowerLevel[i] );
	}
}

static void G_CoopSendState( const char *cmd, const playerState_t *ps, int health ) {
	char save[256], ammo[256], inv[256], fplvl[256];
	G_CoopFormatState( ps, save, ammo, inv, fplvl, sizeof( save ) );
	if ( health > 0 ) {		// a copy of Kyle's for a new player 2: full health, no shield
		Com_sprintf( save, sizeof( save ), "%i 0%s", health, strchr( strchr( save, ' ' ) + 1, ' ' ) );
	}
	G_CoopSend( "%s \"%s\" \"%s\" \"%s\" \"%s\"", cmd, save, ammo, inv, fplvl );
}

static void G_CoopCmd_P2State( void ) {		// host
	if ( g_coopMode != COOP_MODE_HOST ) {
		return;
	}
	gi.cvar_set( "coop_p2save", gi.argv( 2 ) );
	gi.cvar_set( "coop_p2ammo", gi.argv( 3 ) );
	gi.cvar_set( "coop_p2inv", gi.argv( 4 ) );
	gi.cvar_set( "coop_p2fplvl", gi.argv( 5 ) );
}

static void G_CoopAnswerNeed( void ) {		// host
	char save[256], ammo[256], inv[256], fplvl[256];
	gi.Cvar_VariableStringBuffer( "coop_p2save", save, sizeof( save ) );
	gi.Cvar_VariableStringBuffer( "coop_p2ammo", ammo, sizeof( ammo ) );
	gi.Cvar_VariableStringBuffer( "coop_p2inv", inv, sizeof( inv ) );
	gi.Cvar_VariableStringBuffer( "coop_p2fplvl", fplvl, sizeof( fplvl ) );
	if ( save[0] ) {
		gi.Printf( "COOP: player 2 gets the saved state\n" );
		G_CoopSend( "p2restore \"%s\" \"%s\" \"%s\" \"%s\"", save, ammo, inv, fplvl );
	} else if ( g_entities[0].client ) {
		gi.Printf( "COOP: player 2 gets a copy of Kyle's weapons\n" );	// design 22
		G_CoopSendState( "p2restore", &g_entities[0].client->ps, 100 );
	}
}

static void G_CoopCmd_P2Need( void ) {		// host: the guest came in without its state
	if ( g_coopMode != COOP_MODE_HOST ) {
		return;
	}
	if ( !G_CoopP2SendChar() ) {
		g_coopNeedPending = qtrue;
		return;
	}
	G_CoopAnswerNeed();
}

static void G_CoopCmd_P2Restore( void ) {	// guest
	gentity_t *ent = &g_entities[0];
	if ( g_coopMode != COOP_MODE_GUEST || !ent->client ) {
		return;
	}
	gi.cvar_set( "playersave", gi.argv( 2 ) );
	gi.cvar_set( "playerammo", gi.argv( 3 ) );
	gi.cvar_set( "playerinv", gi.argv( 4 ) );
	gi.cvar_set( "playerfplvl", gi.argv( 5 ) );
	Player_RestoreFromPrevLevel( ent );
	G_CoopP2Restrict( ent );	// Jan: Kyle's saber goes before it gets a saber entity
	if ( ent->client->ps.stats[STAT_WEAPONS] & ( 1 << WP_SABER ) ) {
		WP_SaberInitBladeData( ent );
	}
	gi.Printf( "COOP: state restored from the host\n" );
}

static void G_CoopCmd_Near( void ) {		// guest: just came in, go stand next to Kyle
	if ( g_coopMode != COOP_MODE_GUEST || !g_entities[0].client ) {
		return;
	}
	const vec3_t kyle = { (float)atof( gi.argv( 2 ) ), (float)atof( gi.argv( 3 ) ), (float)atof( gi.argv( 4 ) ) };
	const float dist = Distance( g_entities[0].currentOrigin, kyle );
	if ( dist < 48 || dist > 512 ) {	// on top of each other at the spawn point, or far away
		G_CoopTeleportNear( &g_entities[0], kyle );
	}
}

void G_CoopLifeFrame( void ) {
	gentity_t *self = &g_entities[0];
	if ( !G_CoopPartnerInWorld() ) {
		g_coopArrived = qfalse;
		g_coopNeedSent = qfalse;
		g_coopNeedPending = qfalse;
		return;
	}
	if ( !self->inuse || !self->client ) {
		return;
	}
	if ( !g_coopArrived ) {
		g_coopArrived = qtrue;
		if ( g_coopMode == COOP_MODE_HOST ) {
			G_CoopSend( "near %.0f %.0f %.0f", self->currentOrigin[0], self->currentOrigin[1], self->currentOrigin[2] );
		}
	}
	if ( g_coopNeedPending && G_CoopP2SendChar() ) {
		g_coopNeedPending = qfalse;
		G_CoopAnswerNeed();
	}
	if ( g_coopMode != COOP_MODE_GUEST ) {
		return;
	}
	if ( !g_coopNeedSent ) {
		g_coopNeedSent = qtrue;
		if ( !g_coopCameWithState ) {
			g_coopCameWithState = qtrue;	// once per level
			gi.Printf( "COOP: came without a carried state, asking the host for it\n" );
			G_CoopSend( "p2need" );		// we came with "map", not with the host's level change
		}
	}
	if ( level.time >= g_coopNextP2State && self->health > 0 ) {
		g_coopNextP2State = level.time + COOP_P2STATE_MS;
		G_CoopSendState( "p2state", &self->client->ps, 0 );
	}
}

// ---------------------------------------------------------------- skipping a level (design 13)

typedef struct {
	const char	*map;			// the level being skipped
	qboolean	saber;
	int			force[NUM_FORCE_POWERS];	// minimum levels after it
} coopSkipReward_t;

// what Kyle would have had after these levels (yavin_temple / yavin_trial scripts give the saber and
// the first Force powers); applied to Kyle, player 2 follows through the usual mirroring
static const coopSkipReward_t coopSkipRewards[] = {
	{ "yavin_temple",	qfalse,	{ 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 } },	// heal levitation speed push pull | mind grip lightning | throw defense offense
	{ "yavin_trial",	qtrue,	{ 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1 } },
};

static void G_CoopApplySkipReward( const char *map ) {
	gentity_t *kyle = &g_entities[0];
	if ( !kyle->client ) {
		return;
	}
	for ( size_t i = 0; i < ARRAY_LEN( coopSkipRewards ); i++ ) {
		const coopSkipReward_t *r = &coopSkipRewards[i];
		if ( Q_stricmp( map, r->map ) ) {
			continue;
		}
		playerState_t *ps = &kyle->client->ps;
		if ( r->saber && !( ps->stats[STAT_WEAPONS] & ( 1 << WP_SABER ) ) ) {
			ps->stats[STAT_WEAPONS] |= ( 1 << WP_SABER );
			WP_SaberInitBladeData( kyle );
		}
		for ( int f = 0; f < NUM_FORCE_POWERS; f++ ) {
			if ( r->force[f] > ps->forcePowerLevel[f] ) {
				ps->forcePowerLevel[f] = r->force[f];
				ps->forcePowersKnown |= ( 1 << f );
			}
		}
		gi.Printf( "COOP: rewards of %s applied\n", map );
	}
}

// console (host): coop_skiplevel - leave through this map's own target_level_change
void G_CoopSkipLevel_f( void ) {
	if ( g_coopMode == COOP_MODE_GUEST ) {
		G_CoopSend( "skiplevel" );
		gi.Printf( "COOP: asked the host to skip this level\n" );
		return;
	}
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->classname || Q_stricmp( ent->classname, "target_level_change" ) || !ent->message
			|| !Q_stricmp( ent->message, "disconnect" ) ) {
			continue;
		}
		gi.Printf( "COOP: skipping %s -> %s\n", level.mapname, ent->message );
		G_CoopApplySkipReward( level.mapname );
		target_level_change_use( ent, &g_entities[0], &g_entities[0] );
		return;
	}
	gi.Printf( "COOP: %s has no level change to skip through\n", level.mapname );
}

static void G_CoopCmd_SkipLevel( void ) {	// host: the guest asks
	if ( g_coopMode == COOP_MODE_HOST ) {
		G_CoopCenterPrint( "Partner chce preskocit level (coop_skiplevel)" );
	}
}

void G_CoopLifeCommand( const char *name ) {
	if ( !Q_stricmp( name, "p2state" ) ) {
		G_CoopCmd_P2State();
	} else if ( !Q_stricmp( name, "p2need" ) ) {
		G_CoopCmd_P2Need();
	} else if ( !Q_stricmp( name, "p2restore" ) ) {
		G_CoopCmd_P2Restore();
	} else if ( !Q_stricmp( name, "near" ) ) {
		G_CoopCmd_Near();
	} else if ( !Q_stricmp( name, "skiplevel" ) ) {
		G_CoopCmd_SkipLevel();
	}
}
