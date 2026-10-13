#include "g_headers.h"
#include "g_local.h"
#include "g_functions.h"
#include "b_local.h"
#include "g_coop.h"
#include "wp_saber.h"

// Player 2 (design 11, 12, 15, 22): the guest plays Jan in the missions where Kyle fights without a
// saber and Luke once Kyle has one - decided by the host at level start from Kyle's weapons. Jan never
// has the Force or a saber; Luke has Kyle's Force levels (plus coop_lukeForceBonus).

extern void		G_SetG2PlayerModel( gentity_t * const ent, const char *modelName, const char *customSkin, const char *surfOff, const char *surfOn );
extern void		G_CreateG2AttachedWeaponModel( gentity_t *ent, const char *psWeaponModel );
extern void		WP_SaberInitBladeData( gentity_t *ent );
extern void		Q3_CoopMirrorSet( const char *type_name, const char *data );

int					g_coopP2Char = COOP_P2_NONE;	// both machines: who player 2 is in this level
static qboolean		g_coopP2Sent;
static int			g_coopP2ForceSent[NUM_FORCE_POWERS + 1];
static int			g_coopP2NextForceCheck;
static cvar_t		*coop_lukeForceBonus;
static int			g_coopStandIn = -1;			// the stand-in (bottom of this file)
static int			g_coopStandInContents;
static qboolean		g_coopStandInHidden;
static qboolean		g_coopStandInGod;

const char *G_CoopP2Name( void ) {
	return g_coopP2Char == COOP_P2_LUKE ? "luke" : "jan";
}

qboolean G_CoopP2HasForce( void ) {
	return (qboolean)( g_coopP2Char == COOP_P2_LUKE );
}

void G_CoopP2Init( void ) {
	g_coopP2Char = COOP_P2_NONE;
	g_coopP2Sent = qfalse;
	g_coopP2NextForceCheck = 0;
	memset( g_coopP2ForceSent, -1, sizeof( g_coopP2ForceSent ) );
	coop_lukeForceBonus = gi.cvar( "coop_lukeForceBonus", "0", CVAR_ARCHIVE );
	g_coopStandIn = -1;
	g_coopStandInHidden = qfalse;
	g_coopStandInGod = qfalse;
}

// ---------------------------------------------------------------- placing player 2 next to Kyle

static qboolean G_CoopSpotFree( gentity_t *ent, const vec3_t from, const vec3_t spot ) {
	trace_t tr;
	// from is inside the partner (entity 1): its body must not block the way out; ours is unlinked
	gi.trace( &tr, from, ent->mins, ent->maxs, spot, 1, MASK_PLAYERSOLID, G2_NOCOLLIDE, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction < 1.0f ) {
		return qfalse;
	}
	vec3_t down;
	VectorCopy( spot, down );
	down[2] -= 128;
	gi.trace( &tr, spot, ent->mins, ent->maxs, down, ent->s.number, MASK_PLAYERSOLID, G2_NOCOLLIDE, 0 );
	return (qboolean)( !tr.startsolid && tr.fraction < 1.0f );		// ground below, not a pit
}

// a free spot near org, qfalse when there is none
qboolean G_CoopFindSpotNear( gentity_t *ent, const vec3_t org, vec3_t out ) {
	static const float dists[] = { 64, 96, 128 };
	vec3_t from;
	VectorCopy( org, from );
	from[2] += 8;
	const qboolean linked = ent->linked;
	gi.unlinkentity( ent );
	qboolean found = qfalse;
	for ( size_t d = 0; d < ARRAY_LEN( dists ) && !found; d++ ) {
		for ( int a = 0; a < 8; a++ ) {
			const float yaw = DEG2RAD( a * 45.0f );
			vec3_t spot = { from[0] + cosf( yaw ) * dists[d], from[1] + sinf( yaw ) * dists[d], from[2] };
			if ( G_CoopSpotFree( ent, from, spot ) ) {
				VectorCopy( spot, out );
				found = qtrue;
				break;
			}
		}
	}
	if ( linked ) {
		gi.linkentity( ent );
	}
	return found;
}

// TeleportPlayer without G_KillBox: never telefrag the partner standing at org
void G_CoopTeleportNear( gentity_t *ent, const vec3_t org ) {
	vec3_t spot;
	if ( !ent->client || !G_CoopFindSpotNear( ent, org, spot ) ) {
		return;
	}
	gi.unlinkentity( ent );
	VectorCopy( spot, ent->client->ps.origin );
	VectorCopy( spot, ent->currentOrigin );
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	PlayerStateToEntityState( &ent->client->ps, &ent->s );
	gi.linkentity( ent );
	gi.Printf( "COOP: moved next to the partner (%.0f %.0f %.0f)\n", spot[0], spot[1], spot[2] );
}

// ---------------------------------------------------------------- character

// guest: our own player becomes Jan or Luke (the host builds its puppet from our NPC_type)
static void G_CoopBecome( int which ) {
	gentity_t *ent = &g_entities[0];
	g_coopP2Char = which;
	if ( !ent->inuse || !ent->client ) {
		return;
	}
	const char *name = G_CoopP2Name();
	if ( !ent->NPC_type || Q_stricmp( ent->NPC_type, name ) ) {
		gi.G2API_CleanGhoul2Models( ent->ghoul2 );
		ent->playerModel = -1;
		ent->weaponModel = -1;
		G_SetG2PlayerModel( ent, name, NULL, NULL, NULL );
		ent->NPC_type = G_NewString( name );
		ent->client->NPC_class = which == COOP_P2_LUKE ? CLASS_LUKE : CLASS_JAN;
		const int weapon = ent->client->ps.weapon;
		if ( weapon == WP_SABER && ent->client->ps.saberModel ) {
			G_CreateG2AttachedWeaponModel( ent, ent->client->ps.saberModel );
		} else if ( weapon > WP_NONE && weapon < WP_NUM_WEAPONS && weaponData[weapon].weaponMdl[0] ) {
			G_CreateG2AttachedWeaponModel( ent, weaponData[weapon].weaponMdl );
		}
	}
	G_CoopP2Restrict( ent );
	gi.Printf( "COOP: player 2 is %s\n", name );
}

// Jan: no saber, no Force (design 15)
void G_CoopP2Restrict( gentity_t *ent ) {
	if ( g_coopMode != COOP_MODE_GUEST || g_coopP2Char != COOP_P2_JAN || !ent->client ) {
		return;
	}
	playerState_t *ps = &ent->client->ps;
	ps->stats[STAT_WEAPONS] &= ~( 1 << WP_SABER );
	if ( ps->weapon == WP_SABER ) {
		ps->weapon = WP_BRYAR_PISTOL;
		ps->stats[STAT_WEAPONS] |= ( 1 << WP_BRYAR_PISTOL );
	}
	ps->saberActive = qfalse;
	ps->forcePowersKnown = 0;
	for ( int i = 0; i < NUM_FORCE_POWERS; i++ ) {
		ps->forcePowerLevel[i] = 0;
	}
	ps->forcePowersActive = 0;
}

static void G_CoopCmd_P2Char( void ) {
	const int which = !Q_stricmp( gi.argv( 2 ), "luke" ) ? COOP_P2_LUKE : COOP_P2_JAN;
	if ( g_coopMode == COOP_MODE_GUEST ) {
		G_CoopBecome( which );
	}
}

// Luke: Kyle's Force levels, all the time (design 15)
static void G_CoopCmd_P2Force( void ) {
	gentity_t *ent = &g_entities[0];
	if ( g_coopMode != COOP_MODE_GUEST || g_coopP2Char != COOP_P2_LUKE || !ent->client ) {
		return;
	}
	playerState_t *ps = &ent->client->ps;
	ps->forcePowersKnown = atoi( gi.argv( 2 ) );
	const int bonus = coop_lukeForceBonus ? coop_lukeForceBonus->integer : 0;
	for ( int i = 0; i < NUM_FORCE_POWERS; i++ ) {
		int level = atoi( gi.argv( 3 + i ) );
		if ( level > 0 && bonus > 0 ) {
			level = level + bonus > FORCE_LEVEL_3 ? FORCE_LEVEL_3 : level + bonus;
		}
		ps->forcePowerLevel[i] = level;
	}
	if ( !( ps->stats[STAT_WEAPONS] & ( 1 << WP_SABER ) ) ) {
		ps->stats[STAT_WEAPONS] |= ( 1 << WP_SABER );
		WP_SaberInitBladeData( ent );
	}
}

static void G_CoopCmd_KSet( void ) {
	if ( g_coopMode == COOP_MODE_GUEST ) {
		Q3_CoopMirrorSet( gi.argv( 2 ), gi.argv( 3 ) );
	}
}

void G_CoopP2Command( const char *name ) {
	if ( !Q_stricmp( name, "p2char" ) ) {
		G_CoopCmd_P2Char();
	} else if ( !Q_stricmp( name, "p2force" ) ) {
		G_CoopCmd_P2Force();
	} else if ( !Q_stricmp( name, "kset" ) ) {
		G_CoopCmd_KSet();
	}
}

// host, every frame: decide the character once Kyle is in, tell the guest, keep Luke's Force in step
void G_CoopP2Frame( void ) {
	if ( g_coopMode != COOP_MODE_HOST ) {
		return;
	}
	gentity_t *kyle = &g_entities[0];
	if ( !kyle->inuse || !kyle->client ) {
		return;
	}
	if ( g_coopP2Char == COOP_P2_NONE ) {
		g_coopP2Char = ( kyle->client->ps.stats[STAT_WEAPONS] & ( 1 << WP_SABER ) ) ? COOP_P2_LUKE : COOP_P2_JAN;
	}
	if ( !G_CoopPartnerInWorld() ) {
		g_coopP2Sent = qfalse;
		memset( g_coopP2ForceSent, -1, sizeof( g_coopP2ForceSent ) );
		return;
	}
	if ( !g_coopP2Sent ) {
		g_coopP2Sent = qtrue;
		G_CoopSend( "p2char %s", G_CoopP2Name() );
	}
	if ( g_coopP2Char != COOP_P2_LUKE || level.time < g_coopP2NextForceCheck ) {
		return;
	}
	g_coopP2NextForceCheck = level.time + 500;
	const playerState_t *ps = &kyle->client->ps;
	qboolean changed = (qboolean)( g_coopP2ForceSent[NUM_FORCE_POWERS] != ps->forcePowersKnown );
	for ( int i = 0; i < NUM_FORCE_POWERS; i++ ) {
		changed = (qboolean)( changed || g_coopP2ForceSent[i] != ps->forcePowerLevel[i] );
	}
	if ( !changed ) {
		return;
	}
	char text[256];
	int len = Com_sprintf( text, sizeof( text ), "p2force %d", ps->forcePowersKnown );
	for ( int i = 0; i < NUM_FORCE_POWERS; i++ ) {
		len += Com_sprintf( text + len, sizeof( text ) - len, " %d", ps->forcePowerLevel[i] );
		g_coopP2ForceSent[i] = ps->forcePowerLevel[i];
	}
	g_coopP2ForceSent[NUM_FORCE_POWERS] = ps->forcePowersKnown;
	G_CoopSend( "%s", text );
}

// ---------------------------------------------------------------- the stand-in (design 11, 14)
// The mission's NPC of player 2's character stays in the game, so every script that talks to "jan" or
// "luke" runs exactly as in SP. Outside cutscenes it is invisible, not solid, never targeted, never
// hurt and fights nobody: player 2 is that character. In cutscenes it plays the role, player 2 is
// hidden, and afterwards player 2 appears where the script left the stand-in.

extern bool		in_camera;


gentity_t *G_CoopStandIn( void ) {
	if ( g_coopStandIn >= 0 ) {
		gentity_t *ent = &g_entities[g_coopStandIn];
		if ( ent->inuse && ent->NPC && ent->NPC_type && !Q_stricmp( ent->NPC_type, G_CoopP2Name() ) ) {
			return ent;
		}
		g_coopStandIn = -1;
	}
	if ( g_coopP2Char == COOP_P2_NONE ) {
		return NULL;
	}
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( ent->inuse && ent->NPC && ent->client && ent->NPC_type && !Q_stricmp( ent->NPC_type, G_CoopP2Name() ) ) {
			g_coopStandIn = i;
			g_coopStandInContents = ent->contents;
			g_coopStandInHidden = qfalse;
			gi.Printf( "COOP: %s (%d) stands in for player 2\n", ent->NPC_type, i );
			return ent;
		}
	}
	return NULL;
}

static void G_CoopStandInFrame( void ) {
	gentity_t *ent = G_CoopStandIn();
	if ( !ent ) {
		return;
	}
	const qboolean partner = G_CoopPartnerInWorld();
	const qboolean hide = (qboolean)( partner && !in_camera );
	if ( partner && !( ent->flags & FL_GODMODE ) ) {
		ent->flags |= FL_GODMODE;	// never dies while player 2 plays it (design 12: not a mission failure)
		g_coopStandInGod = qtrue;
	} else if ( !partner && g_coopStandInGod ) {
		ent->flags &= ~FL_GODMODE;	// without a guest the SP rules apply
		g_coopStandInGod = qfalse;
	}
	if ( hide ) {
		if ( !g_coopStandInHidden ) {
			g_coopStandInContents = ent->contents;
			g_coopStandInHidden = qtrue;
		}
		ent->s.eFlags |= EF_NODRAW;
		ent->client->ps.eFlags |= EF_NODRAW;
		ent->contents = 0;
		ent->flags |= FL_NOTARGET;
		ent->svFlags |= SVF_IGNORE_ENEMIES;
		ent->NPC->scriptFlags |= SCF_DONT_FIRE;		// an invisible ally must not start fights (its targets turn on Kyle)
		ent->NPC->confusionTime = level.time + 500;	// and picks no enemies at all (G_SetEnemy, NPC_FindEnemy)
		if ( ent->enemy ) {
			G_ClearEnemy( ent );
		}
	} else if ( g_coopStandInHidden ) {
		g_coopStandInHidden = qfalse;
		ent->s.eFlags &= ~EF_NODRAW;
		ent->client->ps.eFlags &= ~EF_NODRAW;
		ent->contents = g_coopStandInContents;
		ent->flags &= ~FL_NOTARGET;
		ent->svFlags &= ~SVF_IGNORE_ENEMIES;
		ent->NPC->scriptFlags &= ~SCF_DONT_FIRE;
	}
}

// guest: hidden in cutscenes, the stand-in plays the role
static void G_CoopHideInCutscenes( void ) {
	static qboolean hidden;
	gentity_t *self = &g_entities[0];
	if ( g_coopMode != COOP_MODE_GUEST || !self->client ) {
		return;
	}
	if ( in_camera ) {
		self->client->ps.eFlags |= EF_NODRAW;
		hidden = qtrue;
	} else if ( hidden ) {
		self->client->ps.eFlags &= ~EF_NODRAW;
		hidden = qfalse;
	}
}

void G_CoopStandInRunFrame( void ) {
	if ( g_coopMode == COOP_MODE_HOST ) {
		G_CoopStandInFrame();
	} else {
		G_CoopHideInCutscenes();
	}
}
