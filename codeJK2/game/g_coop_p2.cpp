#include "g_headers.h"
#include "g_local.h"
#include "g_functions.h"
#include "b_local.h"
#include "g_coop.h"
#include "wp_saber.h"
#include "Q3_Interface.h"

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
static int			g_coopStandInScanTime;		// a cutscene actor stands in: look for the companion once a frame

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
	g_coopStandInScanTime = -1;
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

// guest: our Jan / Luke is built before the host's configstrings arrive, and the sync can overwrite its
// skin slot (CS_CHARSKINS) with one of the host's skins, which cgame then puts on our model every frame
static void G_CoopP2KeepSkin( void ) {
	gentity_t *ent = &g_entities[0];
	if ( g_coopP2Char == COOP_P2_NONE || !ent->inuse || !ent->client || !ent->NPC_type
		|| ent->playerModel < 0 || ent->playerModel >= ent->ghoul2.size() || ent->ghoul2[ent->playerModel].mCustomSkin <= 0 ) {
		return;
	}
	CGhoul2Info *g2 = &ent->ghoul2[ent->playerModel];
	char name[MAX_QPATH], slot[MAX_QPATH];
	Com_sprintf( name, sizeof( name ), "models/players/%s/model_default.skin", ent->NPC_type );
	gi.GetConfigstring( CS_CHARSKINS + g2->mCustomSkin, slot, sizeof( slot ) );
	if ( !Q_stricmp( slot, name ) ) {
		return;
	}
	const int skin = gi.RE_RegisterSkin( name );
	if ( !skin ) {
		return;
	}
	const int index = G_SkinIndex( name );
	gi.Printf( "COOP: our skin slot %d became \"%s\", %s is slot %d now\n", g2->mCustomSkin, slot, name, index );
	gi.G2API_SetSkin( g2, index, skin );
}

// host, every frame: decide the character once Kyle is in, tell the guest, keep Luke's Force in step (guest: our skin)
void G_CoopP2Frame( void ) {
	if ( g_coopMode == COOP_MODE_GUEST ) {
		G_CoopP2KeepSkin();
		return;
	}
	if ( g_coopMode != COOP_MODE_HOST ) {
		return;
	}
	gentity_t *kyle = &g_entities[0];
	if ( !kyle->inuse || !kyle->client ) {
		return;
	}
	if ( g_coopP2Char == COOP_P2_NONE ) {
		if ( !( kyle->client->ps.stats[STAT_WEAPONS] & ( 1 << WP_NONE ) ) ) {
			return;		// a loaded save gives Kyle his weapons only in ClientSpawn, a spawned player always has WP_NONE
		}
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


// the gameplay companion that affect("jan") / affect("luke") and NPC_targetname "jan" mean, not a cinematicNN_* actor
static qboolean G_CoopStandInNamed( const gentity_t *ent ) {
	return (qboolean)( ent->script_targetname && !Q_stricmp( ent->script_targetname, G_CoopP2Name() ) );
}

// spawned and its spawnscript started (NPC_Begin ran): until then its contents and place are not its own yet
static qboolean G_CoopStandInCandidate( const gentity_t *ent ) {
	return (qboolean)( ent->inuse && ent->NPC && ent->client && ent->NPC_type && !Q_stricmp( ent->NPC_type, G_CoopP2Name() )
		&& ent->e_ThinkFunc != thinkF_NPC_Begin );
}

// the stand-in plays by the SP rules again: shown, solid, fights
static void G_CoopStandInShow( gentity_t *ent ) {
	g_coopStandInHidden = qfalse;
	ent->s.eFlags &= ~EF_NODRAW;
	ent->client->ps.eFlags &= ~EF_NODRAW;
	ent->contents = g_coopStandInContents;
	ent->flags &= ~FL_NOTARGET;
	ent->svFlags &= ~SVF_IGNORE_ENEMIES;
	ent->NPC->scriptFlags &= ~SCF_DONT_FIRE;
}

gentity_t *G_CoopStandIn( void ) {
	gentity_t *cur = NULL;
	if ( g_coopStandIn >= 0 ) {
		gentity_t *ent = &g_entities[g_coopStandIn];
		if ( ent->inuse && ent->NPC && ent->NPC_type && !Q_stricmp( ent->NPC_type, G_CoopP2Name() ) ) {
			if ( G_CoopStandInNamed( ent ) ) {
				return ent;
			}
			cur = ent;		// a cutscene actor: the companion takes over as soon as it is there (cairn_bay: luke)
		} else {
			g_coopStandIn = -1;
		}
	}
	if ( g_coopP2Char == COOP_P2_NONE || ( cur && g_coopStandInScanTime == level.time ) ) {
		return cur;
	}
	g_coopStandInScanTime = level.time;
	gentity_t *pick = NULL;
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( !G_CoopStandInCandidate( ent ) ) {
			continue;
		}
		if ( G_CoopStandInNamed( ent ) ) {
			pick = ent;
			break;
		}
		if ( !pick && !cur ) {
			pick = ent;
		}
	}
	if ( !pick || pick == cur ) {
		return cur;
	}
	if ( cur ) {
		if ( g_coopStandInHidden ) {
			G_CoopStandInShow( cur );
		}
		if ( g_coopStandInGod ) {
			cur->flags &= ~FL_GODMODE;
			g_coopStandInGod = qfalse;
		}
		gi.Printf( "COOP: %s (%d) no longer stands in for player 2\n", cur->targetname ? cur->targetname : cur->NPC_type, cur->s.number );
	}
	g_coopStandIn = pick->s.number;
	g_coopStandInContents = pick->contents;
	g_coopStandInHidden = qfalse;
	gi.Printf( "COOP: %s (%d, %s) stands in for player 2\n", pick->NPC_type, pick->s.number, pick->targetname ? pick->targetname : "-" );
	return pick;
}

// host: the hidden stand-in when a trigger waits for it (NPC_targetname "jan"), else NULL. In SP the
// companion followed Kyle, so a player walking in was enough (kejim_post: join_jan opens the monitor room)
gentity_t *G_CoopHiddenStandIn( const char *npcTargetname ) {
	if ( g_coopMode != COOP_MODE_HOST || !npcTargetname || !npcTargetname[0] ) {
		return NULL;
	}
	gentity_t *ent = G_CoopStandIn();
	if ( !ent || !g_coopStandInHidden || !ent->script_targetname || Q_stricmp( ent->script_targetname, npcTargetname ) ) {
		return NULL;
	}
	return ent;
}

// a script sends the hidden stand-in somewhere (kejim_post: Jan into the lift, its NPC trigger starts
// the lift): nobody sees it walk, so it is there at once and the script goes on
static void G_CoopStandInToGoal( gentity_t *ent, const vec3_t goal ) {
	trace_t	tr;
	vec3_t	start, end;
	static const float ups[] = { 48, 24, 8, 1 };	// navgoals sit at the floor or a bit above it, under low ceilings too
	VectorSet( end, goal[0], goal[1], goal[2] - 64 );
	for ( size_t i = 0; ; i++ ) {
		if ( i == ARRAY_LEN( ups ) ) {
			return;
		}
		VectorSet( start, goal[0], goal[1], goal[2] + ups[i] - ent->mins[2] );
		// bodies do not count: it is not solid, and a player on the navgoal would leave it standing on his head
		gi.trace( &tr, start, ent->mins, ent->maxs, end, ent->s.number, MASK_NPCSOLID & ~CONTENTS_BODY, G2_NOCOLLIDE, 0 );
		if ( !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f ) {
			break;
		}
	}
	if ( Distance( tr.endpos, ent->currentOrigin ) < 1.0f ) {
		return;
	}
	gi.Printf( "COOP: hidden %s goes straight to its navgoal (%.0f %.0f %.0f)\n", ent->NPC_type, tr.endpos[0], tr.endpos[1], tr.endpos[2] );
	G_SetOrigin( ent, tr.endpos );
	VectorCopy( tr.endpos, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	gi.linkentity( ent );
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
		const gentity_t *p2 = G_PlayerNum( 1 );
		qboolean busy = (qboolean)( ent->taskManager && ent->taskManager->IsRunning() );
		for ( int t = 0; t < NUM_TIDS && !busy; t++ ) {
			busy = Q3_TaskIDPending( ent, (taskID_t)t );	// a script waits for it (IsRunning is false meanwhile)
		}
		if ( ent->NPC->goalEntity && Q3_TaskIDPending( ent, TID_MOVE_NAV ) ) {
			G_CoopStandInToGoal( ent, ent->NPC->goalEntity->currentOrigin );
		} else if ( p2 && !busy && G_CoopStandInNamed( ent ) ) {
			// player 2 is this character: it stands where player 2 is, so a script that takes it
			// (kejim_post: Jan shoots the big door) starts there, not where Kyle left it. Cutscene actors
			// stay where the map put them (cairn_bay: cinematic24_luke must walk to its mark from there)
			G_SetOrigin( ent, p2->currentOrigin );
			VectorCopy( p2->currentOrigin, ent->client->ps.origin );
			VectorClear( ent->client->ps.velocity );
			gi.linkentity( ent );
		}
	} else if ( g_coopStandInHidden ) {
		G_CoopStandInShow( ent );
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
