#include "g_headers.h"
#include "g_local.h"
#include "b_local.h"
#include "g_functions.h"
#include "g_coop.h"

#define COOP_USE_HOLD_MS	300		// a tap of use is reported this long (G_Coop_GetEntityExtra)

extern gNPC_t	*New_NPC_t( void );
extern void		G_CreateG2AttachedWeaponModel( gentity_t *ent, const char *weaponModel );
extern int		WP_SetSaberModel( gclient_t *client, class_t npcClass );

// Replicas: entities another machine simulates. The engine (code/coop/coop_snapshot.cpp) calls these
// game exports with the owner's state; the game rebuilds what the engine cannot see.

int G_Coop_EntityOwner( int entNum ) {
	if ( entNum < 0 || entNum >= MAX_GENTITIES || !g_entities[entNum].inuse ) {
		return COOP_OWNER_NONE;
	}
	if ( g_coopMode == COOP_MODE_SOLO ) {
		return COOP_OWNER_HOST;
	}
	return G_CoopDropPaired( entNum ) ? COOP_OWNER_LOCAL : G_CoopOwner( &g_entities[entNum] );
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
	out->buttons = ent->client ? ent->client->usercmd.buttons : 0;
	if ( entNum == 0 ) {
		// a tap of use lasts one frame here, and the partner applies only its newest snapshot: the press
		// would often never reach the partner's triggers (switches with USE_BUTTON). Report it a while longer.
		static int lastUse;
		if ( out->buttons & BUTTON_USE ) {
			lastUse = level.time;
		} else if ( lastUse && level.time - lastUse < COOP_USE_HOLD_MS && level.time >= lastUse ) {
			out->buttons |= BUTTON_USE;
		}
	}
	out->itemTaken = G_CoopItemTakenBits( entNum );
	if ( ent->client && ent->client->ps.saberInFlight && ent->client->ps.saberEntityNum > 0
		&& ent->client->ps.saberEntityNum < ENTITYNUM_WORLD && g_entities[ent->client->ps.saberEntityNum].inuse ) {
		const gentity_t *saber = &g_entities[ent->client->ps.saberEntityNum];
		out->saberInFlight = 1;
		VectorCopy( saber->currentOrigin, out->saberOrigin );
		VectorCopy( saber->currentAngles, out->saberAngles );
	}
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

extern vec3_t	playerMins, playerMaxs;

// Entity 1: the partner's player (design 2). A client like ours, built from the partner's NPC type
// ("kyle", M8: "jan" / "luke") for model, animations and sounds; no ucmds, no think: every snapshot
// places it. NPC_ParseParms works without ent->NPC, so the puppet is no NPC for the AI.
static gentity_t *G_CoopSpawnPuppet( const coopEntExtra_t *ex ) {
	gentity_t *ent = &g_entities[1];
	G_InitGentity( ent );
	ent->client = &level.clients[1];
	memset( ent->client, 0, sizeof( *ent->client ) );
	ent->client->ps.clientNum = 1;
	ent->s.eType = ET_PLAYER;
	ent->classname = "player";
	const char *type = ex->npcType[0] ? ex->npcType : "kyle";
	ent->NPC_type = G_NewString( type );
	ent->weaponModel = -1;
	ent->playerModel = -1;
	if ( !NPC_ParseParms( ent->NPC_type, ent ) ) {
		gi.Printf( "COOP: cannot build the partner as %s\n", type );
		G_FreeEntity( ent );
		return NULL;
	}
	ent->targetname = ent->script_targetname = NULL;	// scripts name Kyle, not the partner (M8 names it)
	ent->client->playerTeam = TEAM_PLAYER;
	ent->client->enemyTeam = TEAM_ENEMY;
	ent->takedamage = qtrue;
	ent->flags &= ~FL_NOTARGET;
	ent->svFlags &= ~SVF_NPC;
	VectorCopy( playerMins, ent->mins );
	VectorCopy( playerMaxs, ent->maxs );
	ent->client->crouchheight = CROUCH_MAXS_2;
	ent->client->standheight = DEFAULT_MAXS_2;
	ent->client->renderInfo.lookTarget = ENTITYNUM_NONE;
	Q_strncpyz( ent->client->clientInfo.name, "partner", sizeof( ent->client->clientInfo.name ) );
	gi.Printf( "COOP: partner puppet ready (%s)\n", type );
	return ent;
}

// the partner's thrown saber (design 16): a local saber entity the puppet owns, placed every snapshot;
// it touches nothing here, the thrower's machine decides what it hits
static void G_CoopPuppetSaber( gentity_t *ent, const coopEntExtra_t *ex ) {
	gclient_t *cl = ent->client;
	if ( !cl ) {
		return;
	}
	const int n = cl->ps.saberEntityNum;
	gentity_t *saber = ( n > 0 && n < ENTITYNUM_WORLD && g_entities[n].inuse && g_entities[n].owner == ent ) ? &g_entities[n] : NULL;
	if ( !ex->saberInFlight ) {
		cl->ps.saberInFlight = qfalse;
		if ( saber && saber->linked ) {
			gi.unlinkentity( saber );
		}
		return;
	}
	if ( !saber ) {
		saber = G_Spawn();
		saber->classname = "lightsaber";
		saber->s.eType = ET_GENERAL;
		saber->svFlags = SVF_USE_CURRENT_ORIGIN;
		saber->s.weapon = WP_SABER;
		saber->owner = ent;
		saber->s.otherEntityNum = ent->s.number;
		VectorSet( saber->mins, -3.0f, -3.0f, -3.0f );
		VectorSet( saber->maxs, 3.0f, 3.0f, 3.0f );
		saber->s.modelindex = WP_SetSaberModel( cl, cl->NPC_class );
		gi.G2API_InitGhoul2Model( saber->ghoul2, cl->ps.saberModel, saber->s.modelindex, NULL_HANDLE, NULL_HANDLE, 0, 0 );
		gi.G2API_AddBolt( &saber->ghoul2[0], "*flash" );
		cl->ps.saberEntityNum = saber->s.number;
	}
	saber->contents = 0;
	saber->s.eFlags &= ~EF_NODRAW;
	saber->svFlags &= ~SVF_NOCLIENT;
	G_SetOrigin( saber, ex->saberOrigin );
	G_SetAngles( saber, ex->saberAngles );
	gi.linkentity( saber );
}

static void G_CoopFreePuppetSaber( gentity_t *ent ) {
	if ( !ent->client ) {
		return;
	}
	const int n = ent->client->ps.saberEntityNum;
	if ( n > 0 && n < ENTITYNUM_WORLD && g_entities[n].inuse && g_entities[n].owner == ent ) {
		G_FreeEntity( &g_entities[n] );
	}
}

static gentity_t *G_CoopSpawnReplica( int num, const coopEntExtra_t *ex ) {
	if ( num == 1 ) {
		return G_CoopSpawnPuppet( ex );
	}
	gentity_t *ent = G_SpawnAtNum( num );
	if ( !ent ) {
		return NULL;
	}
	ent->classname = G_NewString( ex->classname );
	if ( !ex->hasClient ) {
		return ent;
	}
	// an NPC: built like NPC_Spawn_Go does (NPC_spawn.cpp:1191-1266), without spawner, AI or scripts
	ent->client = (gclient_t *)G_Alloc( sizeof( gclient_t ) );
	ent->NPC = New_NPC_t();
	if ( !ent->client || !ent->NPC ) {
		G_FreeEntity( ent );
		return NULL;
	}
	memset( ent->client, 0, sizeof( *ent->client ) );
	ent->NPC_type = G_NewString( ex->npcType[0] ? ex->npcType : "stormtrooper" );
	ent->svFlags |= SVF_NPC;
	ent->s.eType = ET_PLAYER;
	if ( !NPC_ParseParms( ent->NPC_type, ent ) ) {
		gi.Printf( "COOP: cannot build NPC %s for entity %d\n", ent->NPC_type, num );
		G_FreeEntity( ent );
		return NULL;
	}
	ent->clipmask = MASK_NPCSOLID;	// as NPC_Begin: needed once this NPC runs here (design 6 migration)
	ent->mass = 10;
	ent->client->standheight = DEFAULT_MAXS_2;
	ent->client->crouchheight = CROUCH_MAXS_2;
	gi.Printf( "COOP: replica npc %d (%s)\n", num, ent->NPC_type );
	return ent;
}

// Ghoul2 animations are set by the game (PM_SetAnimFinal), not by copying numbers: restart what changed
static void G_CoopReplicaAnims( gentity_t *ent, int oldLegs, int oldTorso ) {
	playerState_t *ps = &ent->client->ps;
	const int legs = ps->legsAnim, torso = ps->torsoAnim;
	const int legsTimer = ps->legsAnimTimer, torsoTimer = ps->torsoAnimTimer;
	if ( legs != oldLegs ) {
		ps->legsAnim = oldLegs;
		ps->legsAnimTimer = 0;
		NPC_SetAnim( ent, SETANIM_LEGS, legs, SETANIM_FLAG_OVERRIDE );
	}
	if ( torso != oldTorso ) {
		ps->torsoAnim = oldTorso;
		ps->torsoAnimTimer = 0;
		NPC_SetAnim( ent, SETANIM_TORSO, torso, SETANIM_FLAG_OVERRIDE );
	}
	ps->legsAnim = legs;		// the host's values win over what NPC_SetAnim computed
	ps->torsoAnim = torso;
	ps->legsAnimTimer = legsTimer;
	ps->torsoAnimTimer = torsoTimer;
}

// the weapon in the hand is a Ghoul2 model bolted to the NPC (NPC_combat.cpp:820-838)
static void G_CoopReplicaWeaponModel( gentity_t *ent, int oldWeapon ) {
	const int weapon = ent->client->ps.weapon;
	if ( weapon == oldWeapon ) {
		return;
	}
	if ( ent->weaponModel >= 0 && ent->weaponModel != ent->playerModel ) {
		gi.G2API_RemoveGhoul2Model( ent->ghoul2, ent->weaponModel );
	}
	ent->weaponModel = -1;
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS || ent->playerModel < 0 ) {
		return;
	}
	if ( weapon == WP_SABER ) {
		if ( !ent->client->ps.saberModel ) {
			WP_SetSaberModel( ent->client, ent->client->NPC_class );
		}
		G_CreateG2AttachedWeaponModel( ent, ent->client->ps.saberModel );
	} else if ( weaponData[weapon].weaponMdl[0] ) {
		G_CreateG2AttachedWeaponModel( ent, weaponData[weapon].weaponMdl );
	}
}

static qboolean G_CoopSameMove( const trajectory_t *a, const trajectory_t *b ) {
	return (qboolean)( a->trType == b->trType && a->trDuration == b->trDuration
		&& VectorCompare( a->trBase, b->trBase ) && VectorCompare( a->trDelta, b->trDelta ) );
}

// a mover runs its moves here by itself (design 4): the host's trajectory counts only when a new move starts.
// The same move with a shifted start (the host's mover was blocked a moment) or the host arriving a little
// earlier than our clock would only jerk ours.
static qboolean G_CoopKeepLocalMove( const trajectory_t *local, const trajectory_t *host ) {
	if ( G_CoopSameMove( local, host ) ) {
		return qtrue;	// at most trTime differs
	}
	if ( host->trType == TR_STATIONARY && ( local->trType == TR_LINEAR_STOP || local->trType == TR_NONLINEAR_STOP ) ) {
		vec3_t end;
		EvaluateTrajectory( local, local->trTime + local->trDuration, end );
		return (qboolean)( Distance( end, host->trBase ) < 1.0f );	// ours gets there itself
	}
	return qfalse;
}

void G_Coop_ApplyEntity( int entNum, const entityState_t *es, const playerState_t *ps, const coopEntExtra_t *ex ) {
	if ( g_coopMode == COOP_MODE_SOLO || entNum < 1 || entNum >= ENTITYNUM_MAX_NORMAL ) {
		return;		// entity 0 is always our own player
	}
	gentity_t *ent = &g_entities[entNum];
	if ( entNum >= MAX_CLIENTS && ent->inuse && G_CoopOwnsEnt( ent ) && ent->client ) {
		return;		// an NPC we run (migrated, design 9): the partner's frames from before the transfer are stale
	}
	if ( entNum >= MAX_CLIENTS && ent->inuse && G_CoopOwnsEnt( ent ) ) {
		// the guest allocates its own entities top-down (code/coop/coop_entnum.h); when the map is nearly
		// full both can reach the same slot. The host's entity wins, the guest's local one goes.
		if ( g_coopMode == COOP_MODE_HOST ) {
			return;
		}
		gi.Printf( "COOP: entity %d taken over by the host (%s -> %s)\n", entNum, ent->classname, ex->classname );
		G_FreeEntity( ent );
	}
	if ( entNum < MAX_CLIENTS && ent->inuse && ex->npcType[0] && Q_stricmp( ent->NPC_type ? ent->NPC_type : "", ex->npcType ) ) {
		gi.Printf( "COOP: partner changed to %s\n", ex->npcType );		// M8: Jan <-> Luke
		G_FreeEntity( ent );
	}
	if ( ent->inuse && G_CoopReplicaMismatch( ent, ex ) ) {
		gi.Printf( "COOP: entity %d mismatch (%s -> %s)\n", entNum, ent->classname, ex->classname );
		G_FreeEntity( ent );
	}
	qboolean fresh = qfalse;
	if ( !ent->inuse ) {
		ent = G_CoopSpawnReplica( entNum, ex );
		if ( !ent ) {
			return;
		}
		fresh = qtrue;
	}
	const int oldWeapon = ( ent->client && !fresh ) ? ent->client->ps.weapon : WP_NONE;
	const int oldLegs = ( ent->client && !fresh ) ? ent->client->ps.legsAnim : -1;		// -1: no animation yet, set it
	const int oldTorso = ( ent->client && !fresh ) ? ent->client->ps.torsoAnim : -1;
	const trajectory_t localPos = ent->s.pos, localApos = ent->s.apos;
	ent->s = *es;
	ent->s.number = entNum;
	// the sender swaps 0 <-> 1 in every entity number field, also where 0 is no entity: an effect attached
	// to nothing would follow the partner's player, a speaker without "random" would play all the time
	const int event = es->eType > ET_EVENTS ? es->eType - ET_EVENTS : ( es->event & ~EV_EVENT_BITS );
	if ( event == EV_PLAY_EFFECT && ent->s.otherEntityNum == 1 ) {
		ent->s.otherEntityNum = 0;
	}
	if ( es->eType == ET_SPEAKER && ( es->clientNum == 0 || es->clientNum == 1 ) ) {
		ent->s.clientNum = 1 - es->clientNum;	// random * 10, no entity
	}
	if ( !ent->client && !ent->bmodel && es->solid == SOLID_BMODEL && es->modelindex > 0 ) {
		// a map brush entity we freed while the host hid it, rebuilt from its classname: without its inline
		// model cgame draws model_draw[modelindex] (nothing or a wrong model) and linking makes it a box
		gi.SetBrushModel( ent, va( "*%d", es->modelindex ) );
	}
	if ( !fresh && es->eType == ET_MOVER ) {
		if ( G_CoopKeepLocalMove( &localPos, &es->pos ) ) {
			ent->s.pos = localPos;
		}
		if ( G_CoopKeepLocalMove( &localApos, &es->apos ) ) {
			ent->s.apos = localApos;
		}
	}
	if ( ent->client && ps ) {
		char *saberModel = ent->client->ps.saberModel;	// a pointer into this machine's memory (plan-M3 R1)
		const int saberEntityNum = ent->client->ps.saberEntityNum;	// the puppet's saber entity is ours
		if ( entNum == 1 ) {
			// the AI's stealth check counts turning from lastAngles, which only ClientThink sets: from the
			// previous snapshot's view, not from 0 0 0 (the puppet's yaw itself would count as turning)
			VectorCopy( fresh ? ps->viewangles : ent->client->ps.viewangles, ent->lastAngles );
		}
		ent->client->ps = *ps;
		ent->client->ps.saberModel = saberModel;
		if ( entNum == 1 ) {
			ent->client->ps.saberEntityNum = saberEntityNum;
			ent->client->usercmd.upmove = ( ps->pm_flags & PMF_DUCKED ) ? -127 : 0;	// the AI sees crouching here
		}
		ent->client->usercmd.buttons = ex->buttons;
	}
	ent->spawnflags = ex->spawnflags;
	G_CoopItemSetTakenBits( entNum, ex->itemTaken );
	ent->health = ex->health;
	if ( ent->client && entNum >= MAX_CLIENTS ) {
		// shots and blows hit a living partner NPC here (G_MissileImpacted & co. test takedamage); the hit goes
		// to its owner, whose G_Damage decides (invincible, undying...)
		ent->takedamage = (qboolean)( ent->health > 0 );
	}
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
	} else if ( ent->s.eType != ET_MOVER || fresh ) {
		EvaluateTrajectory( &ent->s.pos, level.time, ent->currentOrigin );
		EvaluateTrajectory( &ent->s.apos, level.time, ent->currentAngles );
	}
	// the effect's axis: cgame takes it from the game-only pos3/pos4 (cg_event.cpp EV_PLAY_EFFECT)
	if ( !ent->client && ent->classname && !Q_stricmp( ent->classname, "fx_runner" ) ) {
		vec3_t temp;	// as fx_runner_think
		AngleVectors( ent->currentAngles, ent->pos3, NULL, NULL );
		MakeNormalVectors( ent->pos3, ent->pos4, temp );
	} else if ( es->eType == ET_EVENTS + EV_PLAY_EFFECT && VectorLengthSquared( ent->pos3 ) == 0 ) {
		vec3_t temp;	// G_PlayEffect's direction is not in entityState_t: up
		VectorSet( ent->pos3, 0, 0, 1 );
		MakeNormalVectors( ent->pos3, ent->pos4, temp );
	}
	if ( ex->linked ) {
		gi.linkentity( ent );
	} else if ( ent->linked ) {
		gi.unlinkentity( ent );
	}
	if ( ent->linked ) {
		G_CoopSetPortalCount( ent, ex->areaPortalOpen );	// after linking: the engine needs the entity's areas
	}
	if ( ent->client ) {
		G_CoopReplicaAnims( ent, oldLegs, oldTorso );
		G_CoopReplicaWeaponModel( ent, oldWeapon );
	}
	if ( entNum == 1 ) {
		G_CoopPuppetSaber( ent, ex );
	}
}

void G_Coop_FreeEntity( int entNum ) {
	if ( g_coopMode == COOP_MODE_SOLO || entNum < 1 || entNum >= ENTITYNUM_MAX_NORMAL ) {
		return;
	}
	gentity_t *ent = &g_entities[entNum];
	if ( ent->inuse && !G_CoopOwnsEnt( ent ) ) {
		if ( entNum == 1 ) {
			gi.Printf( "COOP: partner puppet removed\n" );
			G_CoopFreePuppetSaber( ent );
		} else if ( G_CoopNpcKeep( ent ) || G_CoopClaimKeep( ent ) ) {
			return;
		}
		G_FreeEntity( ent );
	}
}

void G_CoopRunReplica( gentity_t *ent ) {
	if ( ent->client ) {
		G_CoopRunPuppet( ent );	// NPCs: every snapshot places them (ps.origin), nothing to integrate
		return;
	}
	if ( ent->s.eType == ET_MOVER ) {
		G_CoopRunReplicaMover( ent );
		return;
	}
	if ( !ent->linked || ( ent->s.pos.trType == TR_STATIONARY && ent->s.apos.trType == TR_STATIONARY ) ) {
		return;
	}
	// missiles, debris, dropped items: follow the trajectory, no collisions (the owner decides hits)
	EvaluateTrajectory( &ent->s.pos, level.time, ent->currentOrigin );
	EvaluateTrajectory( &ent->s.apos, level.time, ent->currentAngles );
	gi.linkentity( ent );
}
