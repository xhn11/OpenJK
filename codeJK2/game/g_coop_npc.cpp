#include "g_headers.h"
#include "g_local.h"
#include "g_functions.h"
#include "b_local.h"
#include "g_icarus.h"
#include "g_coop.h"
#include "../../code/coop/coop_owner.h"

// NPC authority migration (design 6, 9): an NPC runs (AI and physics) on the machine of the player
// it has as enemy. The host decides (CoopOwner_Decide) for every NPC; the owner gives the NPC away:
//   giver:    G_CoopGiveNpc -> frozen (G_CoopRunsLocally false), still in its snapshots,
//             "npcst" parts + "npcgive" to the partner
//   receiver: "npcgive" -> applies the state, owns it, runs the AI, answers "npctaken"
//   giver:    "npctaken" -> the partner owns it, its snapshots now carry it
// The guest -> host direction starts with the host asking ("npcreq"). An NPC keeps its number (design 9).
// State: the AI-relevant gentity_t fields, gNPC_t and some gclient_t fields with pointers as entity
// numbers (like the savegame code, g_savegame.cpp savefields_gNPC / savefields_gClient), the entity's
// timers; position, animation, health and playerState_t come with the snapshots anyway.

extern qboolean	PM_SaberInAttack( int move );

#define COOP_NPC_PART_BYTES		480			// hex encoded in one reliable command (<= COOP_MAX_WIRE_CMD_LEN)
#define COOP_NPC_DECIDE_MS		200
#define COOP_MIG_NONE			0
#define COOP_MIG_GIVING			1

typedef struct {
	int		thinkFunc, painFunc, dieFunc, touchFunc, useFunc;
	int		nextthink;
	int		enemy, lastEnemy;
	int		painDebounceTime, attackDebounceTime;
	int		takedamage;
	int		clipmask, mass;			// physics: a replica has neither until it runs here
} coopNpcEnt_t;

typedef struct {
	int		playerTeam, enemyTeam;
	int		leader, teamLeader, follower, numFollowers, formationGoal, nextFormGoal;
	int		npcClass;
	float	hiddenDist, hiddenDir[3];
	int		fireDelay;
	float	pushVec[3];
	int		pushVecTime, poisonDamage, poisonTime;
	int		standheight, crouchheight;
} coopNpcClient_t;

typedef struct {
	coopNpcEnt_t	ent;
	coopNpcClient_t	cl;
	gNPC_t			npc;		// pointers hold entity numbers (intptr_t), group an index into level.groups
} coopNpcState_t;

static_assert( std::is_trivially_copyable<gNPC_t>::value, "gNPC_t is migrated raw" );

static byte		g_coopMig[MAX_GENTITIES];
static int		g_coopMigTime[MAX_GENTITIES];
static int		g_coopNextDecide;
static qboolean	g_coopPullAll;
static qboolean	g_coopHadPartner;

// receiving
static byte		g_coopRxState[sizeof( coopNpcState_t )];
static int		g_coopRxNum = -1, g_coopRxLen;

void G_CoopNpcInit( void ) {
	memset( g_coopMig, 0, sizeof( g_coopMig ) );
	memset( g_coopMigTime, 0, sizeof( g_coopMigTime ) );
	g_coopNextDecide = 0;
	g_coopPullAll = qfalse;
	g_coopHadPartner = qfalse;
	g_coopRxNum = -1;
	g_coopRxLen = 0;
}

qboolean G_CoopNpcGiving( const gentity_t *ent ) {
	return (qboolean)( g_coopMig[ent - g_entities] == COOP_MIG_GIVING );
}

static int G_CoopEntNumOf( const gentity_t *ent ) {
	return ( ent && ent->inuse ) ? G_CoopWireNum( ent->s.number ) : -1;
}

static gentity_t *G_CoopEntFromNum( intptr_t n ) {
	return ( n >= 0 && n < ENTITYNUM_WORLD && g_entities[n].inuse ) ? &g_entities[n] : NULL;
}

// ---------------------------------------------------------------- state

static void G_CoopNpcPack( gentity_t *ent, coopNpcState_t *st ) {
	memset( st, 0, sizeof( *st ) );
	st->ent.thinkFunc = ent->e_ThinkFunc;
	st->ent.painFunc = ent->e_PainFunc;
	st->ent.dieFunc = ent->e_DieFunc;
	st->ent.touchFunc = ent->e_TouchFunc;
	st->ent.useFunc = ent->e_UseFunc;
	st->ent.nextthink = ent->nextthink;
	st->ent.enemy = G_CoopEntNumOf( ent->enemy );
	st->ent.lastEnemy = G_CoopEntNumOf( ent->lastEnemy );
	st->ent.painDebounceTime = ent->painDebounceTime;
	st->ent.attackDebounceTime = ent->attackDebounceTime;
	st->ent.takedamage = ent->takedamage;
	st->ent.clipmask = ent->clipmask;
	st->ent.mass = ent->mass;
	gclient_t *cl = ent->client;
	st->cl.playerTeam = cl->playerTeam;
	st->cl.enemyTeam = cl->enemyTeam;
	st->cl.leader = G_CoopEntNumOf( cl->leader );
	st->cl.teamLeader = G_CoopEntNumOf( cl->team_leader );
	st->cl.follower = G_CoopEntNumOf( cl->follower );
	st->cl.numFollowers = cl->numFollowers;
	st->cl.formationGoal = G_CoopEntNumOf( cl->formationGoal );
	st->cl.nextFormGoal = cl->nextFormGoal;
	st->cl.npcClass = cl->NPC_class;
	st->cl.hiddenDist = cl->hiddenDist;
	VectorCopy( cl->hiddenDir, st->cl.hiddenDir );
	st->cl.fireDelay = cl->fireDelay;
	VectorCopy( cl->pushVec, st->cl.pushVec );
	st->cl.pushVecTime = cl->pushVecTime;
	st->cl.poisonDamage = cl->poisonDamage;
	st->cl.poisonTime = cl->poisonTime;
	st->cl.standheight = cl->standheight;
	st->cl.crouchheight = cl->crouchheight;
	memcpy( &st->npc, ent->NPC, sizeof( gNPC_t ) );
	gNPC_t *n = &st->npc;
	gentity_t **ptrs[] = { &n->touchedByPlayer, &n->aimingBeam, &n->eventOwner, &n->coverTarg, &n->tempGoal, &n->goalEntity,
		&n->lastGoalEntity, &n->eventualGoal, &n->captureGoal, &n->defendEnt, &n->greetEnt, &n->watchTarget };
	for ( size_t i = 0; i < ARRAY_LEN( ptrs ); i++ ) {
		const intptr_t num = G_CoopEntNumOf( *ptrs[i] );
		memcpy( ptrs[i], &num, sizeof( num ) );
	}
	const intptr_t group = -1;	// AI groups are per machine: the receiver regroups
	memcpy( &n->group, &group, sizeof( group ) );
}

static gentity_t *G_CoopNpcPtr( gentity_t *const *field ) {
	intptr_t num;
	memcpy( &num, field, sizeof( num ) );
	return G_CoopEntFromNum( num );
}

static void G_CoopNpcUnpack( gentity_t *ent, const coopNpcState_t *st ) {
	ent->e_ThinkFunc = (thinkFunc_t)st->ent.thinkFunc;
	ent->e_PainFunc = (painFunc_t)st->ent.painFunc;
	ent->e_DieFunc = (dieFunc_t)st->ent.dieFunc;
	ent->e_TouchFunc = (touchFunc_t)st->ent.touchFunc;
	ent->e_UseFunc = (useFunc_t)st->ent.useFunc;
	ent->nextthink = st->ent.nextthink > level.time ? st->ent.nextthink : level.time + FRAMETIME;
	ent->enemy = G_CoopEntFromNum( st->ent.enemy );
	ent->lastEnemy = G_CoopEntFromNum( st->ent.lastEnemy );
	ent->painDebounceTime = st->ent.painDebounceTime;
	ent->attackDebounceTime = st->ent.attackDebounceTime;
	ent->takedamage = (qboolean)st->ent.takedamage;
	ent->clipmask = st->ent.clipmask ? st->ent.clipmask : MASK_NPCSOLID;
	ent->mass = st->ent.mass;
	gclient_t *cl = ent->client;
	cl->playerTeam = (team_t)st->cl.playerTeam;
	cl->enemyTeam = (team_t)st->cl.enemyTeam;
	cl->leader = G_CoopEntFromNum( st->cl.leader );
	cl->team_leader = G_CoopEntFromNum( st->cl.teamLeader );
	cl->follower = G_CoopEntFromNum( st->cl.follower );
	cl->numFollowers = st->cl.numFollowers;
	cl->formationGoal = G_CoopEntFromNum( st->cl.formationGoal );
	cl->nextFormGoal = st->cl.nextFormGoal;
	cl->NPC_class = (class_t)st->cl.npcClass;
	cl->hiddenDist = st->cl.hiddenDist;
	VectorCopy( st->cl.hiddenDir, cl->hiddenDir );
	cl->fireDelay = st->cl.fireDelay;
	VectorCopy( st->cl.pushVec, cl->pushVec );
	cl->pushVecTime = st->cl.pushVecTime;
	cl->poisonDamage = st->cl.poisonDamage;
	cl->poisonTime = st->cl.poisonTime;
	cl->standheight = st->cl.standheight ? st->cl.standheight : DEFAULT_MAXS_2;
	cl->crouchheight = st->cl.crouchheight ? st->cl.crouchheight : CROUCH_MAXS_2;

	gentity_t *ownGoal = ent->NPC->tempGoal;	// a per-NPC helper entity on each machine, never replicated
	memcpy( ent->NPC, &st->npc, sizeof( gNPC_t ) );
	gNPC_t *n = ent->NPC;
	gentity_t **ptrs[] = { &n->touchedByPlayer, &n->aimingBeam, &n->eventOwner, &n->coverTarg, &n->tempGoal, &n->goalEntity,
		&n->lastGoalEntity, &n->eventualGoal, &n->captureGoal, &n->defendEnt, &n->greetEnt, &n->watchTarget };
	gentity_t *const senderGoal = G_CoopNpcPtr( &n->tempGoal );
	for ( size_t i = 0; i < ARRAY_LEN( ptrs ); i++ ) {
		*ptrs[i] = G_CoopNpcPtr( ptrs[i] );
	}
	n->group = NULL;
	if ( !ownGoal ) {
		ownGoal = G_Spawn();
		ownGoal->classname = "NPC_goal";
		ownGoal->owner = ent;
		ownGoal->svFlags |= SVF_NOCLIENT;
	}
	n->tempGoal = ownGoal;
	if ( senderGoal ) {
		// the sender's goal entity is not ours: the goals that pointed at it point at our own one
		gentity_t **goals[] = { &n->goalEntity, &n->lastGoalEntity, &n->eventualGoal };
		for ( size_t i = 0; i < ARRAY_LEN( goals ); i++ ) {
			if ( *goals[i] == senderGoal ) {
				*goals[i] = ownGoal;
			}
		}
	}
	ent->client->ps.eFlags |= EF_NPC;
	ent->svFlags |= SVF_NPC;
}

// ---------------------------------------------------------------- protocol

static void G_CoopSendHex( int num, const byte *data, int len ) {
	static const char hex[] = "0123456789abcdef";
	char text[COOP_NPC_PART_BYTES * 2 + 1];
	for ( int part = 0, off = 0; off < len; part++, off += COOP_NPC_PART_BYTES ) {
		const int n = len - off < COOP_NPC_PART_BYTES ? len - off : COOP_NPC_PART_BYTES;
		for ( int i = 0; i < n; i++ ) {
			text[i * 2] = hex[data[off + i] >> 4];
			text[i * 2 + 1] = hex[data[off + i] & 15];
		}
		text[n * 2] = 0;
		G_CoopSend( "npcst %d %d %s", G_CoopWireNum( num ), off, text );
	}
}

// the owner hands an NPC to the partner; it stays frozen here until "npctaken"
void G_CoopGiveNpc( gentity_t *ent ) {
	const int num = ent->s.number;
	if ( !G_CoopPartnerInWorld() || !G_CoopOwnsEnt( ent ) || g_coopMig[num] != COOP_MIG_NONE || !ent->NPC || !ent->client ) {
		return;
	}
	static coopNpcState_t st;
	G_CoopNpcPack( ent, &st );
	G_CoopSendHex( num, (const byte *)&st, sizeof( st ) );
	char timers[900];
	TIMER_Export( num, timers, sizeof( timers ) );
	G_CoopSend( "npcgive %d %d \"%s\"", G_CoopWireNum( num ), (int)sizeof( st ), timers );
	g_coopMig[num] = COOP_MIG_GIVING;
	g_coopMigTime[num] = level.time;
}

static void G_CoopCmd_NpcState( void ) {
	const int num = atoi( gi.argv( 2 ) );
	const int off = atoi( gi.argv( 3 ) );
	const char *text = gi.argv( 4 );
	if ( off == 0 ) {
		g_coopRxNum = num;
		g_coopRxLen = 0;
	}
	if ( num != g_coopRxNum || off != g_coopRxLen ) {
		g_coopRxNum = -1;	// out of order: cannot happen on the reliable channel, drop the transfer
		return;
	}
	for ( int i = 0; text[i] && text[i + 1] && g_coopRxLen < (int)sizeof( g_coopRxState ); i += 2 ) {
		const char hi = text[i], lo = text[i + 1];
		const int h = hi <= '9' ? hi - '0' : hi - 'a' + 10;
		const int l = lo <= '9' ? lo - '0' : lo - 'a' + 10;
		g_coopRxState[g_coopRxLen++] = (byte)( ( h << 4 ) | l );
	}
}

static void G_CoopCmd_NpcGive( void ) {
	const int num = atoi( gi.argv( 2 ) );
	const int len = atoi( gi.argv( 3 ) );
	gentity_t *ent = G_CoopEntFromNum( num );
	if ( num != g_coopRxNum || len != (int)sizeof( coopNpcState_t ) || g_coopRxLen != len
		|| !ent || !ent->client || !ent->NPC || num < MAX_CLIENTS ) {
		gi.Printf( "COOP: npc %d transfer failed (%d of %d bytes)\n", num, g_coopRxLen, len );
		G_CoopSend( "npctaken %d 0", G_CoopWireNum( num ) );
		return;
	}
	g_coopRxNum = -1;
	G_CoopNpcUnpack( ent, (const coopNpcState_t *)g_coopRxState );
	TIMER_Import( ent, gi.argv( 4 ) );
	G_CoopSetOwner( ent, g_coopMode );
	g_coopMig[num] = COOP_MIG_NONE;
	g_coopMigTime[num] = level.time;
	gi.Printf( "COOP: npc %d (%s) -> %s\n", num, ent->NPC_type, g_coopMode == COOP_MODE_HOST ? "host" : "guest" );
	G_CoopSend( "npctaken %d 1", G_CoopWireNum( num ) );
}

static void G_CoopCmd_NpcTaken( void ) {
	const int num = atoi( gi.argv( 2 ) );
	gentity_t *ent = G_CoopEntFromNum( num );
	if ( !ent || g_coopMig[num] != COOP_MIG_GIVING ) {
		return;
	}
	g_coopMig[num] = COOP_MIG_NONE;
	g_coopMigTime[num] = level.time;
	if ( atoi( gi.argv( 3 ) ) ) {
		G_CoopSetOwner( ent, G_CoopPartnerMode() );		// from now on a replica here
	}
}

static void G_CoopCmd_NpcRequest( void ) {
	gentity_t *ent = G_CoopEntFromNum( atoi( gi.argv( 2 ) ) );
	if ( ent && g_coopMode == COOP_MODE_GUEST ) {
		G_CoopGiveNpc( ent );
	}
}

// ---------------------------------------------------------------- decisions (host)

static qboolean G_CoopNpcScripted( const gentity_t *ent ) {
	return (qboolean)( ent->taskManager && ent->taskManager->IsRunning() );
}

static int G_CoopPlayerIndex( const gentity_t *ent ) {
	if ( ent == &g_entities[0] ) {
		return 0;
	}
	if ( ent == &g_entities[1] ) {
		return 1;
	}
	return -1;
}

// host, link gone: the guest's NPCs come home with their last state and forget their target (design 9)
static void G_CoopReclaimAll( void ) {
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse ) {
			continue;
		}
		g_coopMig[i] = COOP_MIG_NONE;
		if ( G_CoopOwner( ent ) == COOP_MODE_GUEST && ent->NPC ) {
			G_CoopSetOwner( ent, COOP_MODE_HOST );
			ent->enemy = NULL;
			ent->NPC->goalEntity = NULL;
			ent->nextthink = level.time + FRAMETIME;
			gi.Printf( "COOP: npc %d (%s) back on the host\n", i, ent->NPC_type );
		}
	}
}

void G_CoopNpcFrame( void ) {
	if ( g_coopMode != COOP_MODE_HOST ) {
		return;
	}
	const qboolean partner = G_CoopPartnerInWorld();
	if ( !partner ) {
		if ( g_coopHadPartner ) {
			g_coopHadPartner = qfalse;
			G_CoopReclaimAll();
		}
		return;
	}
	g_coopHadPartner = qtrue;
	if ( level.time < g_coopNextDecide ) {
		return;
	}
	g_coopNextDecide = level.time + COOP_NPC_DECIDE_MS;
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || !ent->NPC || g_coopMig[i] != COOP_MIG_NONE ) {
			continue;
		}
		coopOwnerInput_t in;
		in.enemyPlayer = G_CoopPlayerIndex( ent->enemy );
		in.scripted = G_CoopNpcScripted( ent );
		in.ally = ent->client->playerTeam == TEAM_PLAYER;
		in.forcedPlayer = -1;
		if ( ent->client->ps.saberLockTime > level.time ) {
			in.forcedPlayer = G_CoopPlayerIndex( G_CoopEntFromNum( ent->client->ps.saberLockEnemy ) );
		}
		in.pullAll = g_coopPullAll;
		in.currentOwner = G_CoopOwner( ent );
		in.lastTransferTime = g_coopMigTime[i];
		in.now = level.time;
		in.inAttack = ent->client->ps.weapon == WP_SABER && PM_SaberInAttack( ent->client->ps.saberMove );
		in.dead = ent->health <= 0;
		const int want = CoopOwner_Decide( &in );
		if ( want == in.currentOwner ) {
			continue;
		}
		if ( in.currentOwner == COOP_MODE_HOST ) {
			G_CoopGiveNpc( ent );
		} else {
			G_CoopSend( "npcreq %d", G_CoopWireNum( i ) );
			g_coopMigTime[i] = level.time;		// ask again only after the hysteresis
		}
	}
}

// host: before a save, a cutscene or a level change (design 9); qtrue when no NPC is on the guest
qboolean G_CoopPullAllToHost( qboolean pull ) {
	g_coopPullAll = pull;
	if ( !pull || g_coopMode != COOP_MODE_HOST ) {
		return qtrue;
	}
	g_coopNextDecide = 0;
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];
		if ( ent->inuse && ent->NPC && ( G_CoopOwner( ent ) == COOP_MODE_GUEST || g_coopMig[i] != COOP_MIG_NONE ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// guest: a killing blow on an NPC we run goes to the host with the NPC (deaths run there, design 6)
void G_CoopNpcBeforeDamage( gentity_t *targ, int damage ) {
	if ( g_coopMode == COOP_MODE_GUEST && targ->NPC && targ->client && targ->s.number >= MAX_CLIENTS
		&& G_CoopOwnsEnt( targ ) && g_coopMig[targ->s.number] == COOP_MIG_NONE && damage >= targ->health ) {
		G_CoopGiveNpc( targ );
	}
}

// the host freed / reclaims a guest NPC instead of losing it (G_Coop_FreeEntity)
qboolean G_CoopNpcKeep( gentity_t *ent ) {
	if ( g_coopMode != COOP_MODE_HOST || !ent->NPC || ent->s.number < MAX_CLIENTS ) {
		return qfalse;
	}
	G_CoopSetOwner( ent, COOP_MODE_HOST );
	g_coopMig[ent->s.number] = COOP_MIG_NONE;
	ent->enemy = NULL;
	ent->NPC->goalEntity = NULL;
	ent->nextthink = level.time + FRAMETIME;
	return qtrue;
}

// console: coop_npcs - who runs which NPC
void G_CoopNpcs_f( void ) {
	int host = 0, guest = 0, giving = 0;
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->NPC || !ent->client ) {
			continue;
		}
		if ( g_coopMig[i] != COOP_MIG_NONE ) {
			giving++;
		} else if ( G_CoopOwner( ent ) == COOP_MODE_GUEST ) {
			guest++;
		} else {
			host++;
		}
	}
	gi.Printf( "COOP: npcs host %d guest %d in transfer %d\n", host, guest, giving );
}

// console: coop_npcgive <entity> - give one of our NPCs to the partner now (testing)
void G_CoopNpcGive_f( void ) {
	gentity_t *ent = G_CoopEntFromNum( atoi( gi.argv( 1 ) ) );
	if ( atoi( gi.argv( 1 ) ) < 0 ) {	// -1: the first enemy NPC we run
		ent = NULL;
		for ( int i = MAX_CLIENTS; i < globals.num_entities && !ent; i++ ) {
			gentity_t *e = &g_entities[i];
			if ( e->inuse && e->NPC && e->client && e->health > 0 && e->client->playerTeam == TEAM_ENEMY && G_CoopOwnsEnt( e ) ) {
				ent = e;
			}
		}
	}
	if ( !ent || !ent->NPC ) {
		gi.Printf( "usage: coop_npcgive <npc entity>\n" );
		return;
	}
	g_coopMigTime[ent->s.number] = level.time;	// the decision would take it back otherwise... for a while
	G_CoopGiveNpc( ent );
}

void G_CoopNpcCommand( const char *name ) {
	if ( !Q_stricmp( name, "npcst" ) ) {
		G_CoopCmd_NpcState();
	} else if ( !Q_stricmp( name, "npcgive" ) ) {
		G_CoopCmd_NpcGive();
	} else if ( !Q_stricmp( name, "npctaken" ) ) {
		G_CoopCmd_NpcTaken();
	} else if ( !Q_stricmp( name, "npcreq" ) ) {
		G_CoopCmd_NpcRequest();
	}
}

// console: coop_npcinfo - every live NPC's enemy and state, and how the partner looks to them (debugging)
void G_CoopNpcInfo_f( void ) {
	extern bool in_camera;
	const gentity_t *p = &g_entities[1];
	gi.Printf( "COOP: in_camera %d, player 0 health %d flags 0x%x origin %.0f %.0f %.0f\n", in_camera ? 1 : 0, g_entities[0].health,
		g_entities[0].flags, g_entities[0].currentOrigin[0], g_entities[0].currentOrigin[1], g_entities[0].currentOrigin[2] );
	if ( p->inuse && p->client ) {
		gi.Printf( "COOP: light partner %.1f player0 %.1f\n", p->lightLevel, g_entities[0].lightLevel );
		gi.Printf( "COOP: partner health %d team %d flags 0x%x svFlags 0x%x contents 0x%x linked %d takedamage %d eFlags 0x%x origin %.0f %.0f %.0f eye %.0f %.0f %.0f\n",
			p->health, p->client->playerTeam, p->flags, p->svFlags, p->contents, p->linked, p->takedamage, p->s.eFlags,
			p->currentOrigin[0], p->currentOrigin[1], p->currentOrigin[2],
			p->client->renderInfo.eyePoint[0], p->client->renderInfo.eyePoint[1], p->client->renderInfo.eyePoint[2] );
	} else {
		gi.Printf( "COOP: no partner puppet\n" );
	}
	for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ ) {
		const gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->NPC || !ent->client || ent->health <= 0 ) {
			continue;
		}
		gi.Printf( "COOP: npc %d %s owner %d team %d enemyTeam %d enemy %d bState %d scriptFlags 0x%x dist0 %.0f dist1 %.0f at %.0f %.0f %.0f\n",
			i, ent->NPC_type, G_CoopOwner( ent ), ent->client->playerTeam, ent->client->enemyTeam,
			ent->enemy ? ent->enemy->s.number : -1, ent->NPC->behaviorState, ent->NPC->scriptFlags,
			Distance( ent->currentOrigin, g_entities[0].currentOrigin ), p->inuse ? Distance( ent->currentOrigin, p->currentOrigin ) : -1.0f,
			ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] );
	}
}

// console: coop_droppuppet - free the partner's puppet here (testing the snapshot self-repair)
void G_CoopDropPuppet_f( void ) {
	if ( g_entities[1].inuse && !G_CoopOwnsEnt( &g_entities[1] ) ) {
		G_FreeEntity( &g_entities[1] );
		gi.Printf( "COOP: puppet dropped (test)\n" );
	}
}
