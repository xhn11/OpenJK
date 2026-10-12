#include "g_headers.h"
#include "g_local.h"
#include "g_functions.h"
#include "g_coop.h"

// Interactions across the ownership boundary (design 7, 16-19). The machine where something happens
// to an entity it does not own sends it to the owner as a game command (g_coop.cpp G_CoopSend);
// the owner applies it with the partner's puppet or replica as the cause. Entity numbers on the wire
// are already in the receiver's numbering (G_CoopWireNum).

extern void		ForceThrowClient( gentity_t *self, gentity_t *target, qboolean pull );	// wp_saber.cpp
extern int		G_ItemPickupInto( gentity_t *item, gentity_t *other );					// g_items.cpp
extern void		G_TouchTriggers( gentity_t *ent );

static gentity_t *G_CoopLocalEnt( const char *arg, qboolean allowWorld ) {
	const int n = atoi( arg );
	if ( n == ENTITYNUM_WORLD ) {
		return allowWorld ? &g_entities[ENTITYNUM_WORLD] : NULL;
	}
	if ( n < 0 || n >= ENTITYNUM_WORLD || !g_entities[n].inuse ) {
		return NULL;
	}
	return &g_entities[n];
}

static int G_CoopWireEnt( const gentity_t *ent ) {
	return ent ? G_CoopWireNum( ent->s.number ) : ENTITYNUM_NONE;
}

// a cause we may report: ours, or none (world). A replica's damage is reported by its owner.
static qboolean g_coopRemoteHit;	// applying a partner's hit: passing it on (to the host, with the NPC) is fine

static qboolean G_CoopOurCause( const gentity_t *ent ) {
	return (qboolean)( g_coopRemoteHit || !ent || ent->s.number == ENTITYNUM_WORLD || G_CoopOwnsEnt( ent ) );
}

// ---------------------------------------------------------------- damage (design 7)

void G_CoopForwardDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, const vec3_t dir,
	const vec3_t point, int damage, int dflags, int mod, int hitLoc ) {
	if ( !targ->takedamage || !G_CoopOurCause( attacker ) || !G_CoopOurCause( inflictor ) ) {
		return;
	}
	vec3_t d, p;
	VectorClear( d );
	VectorClear( p );
	int has = 0;
	if ( dir ) {
		VectorCopy( dir, d );
		has |= 1;
	}
	if ( point ) {
		VectorCopy( point, p );
		has |= 2;
	}
	G_CoopSend( "hit %d %d %d %d %d %d %d %d %.3f %.3f %.3f %.1f %.1f %.1f",
		G_CoopWireEnt( targ ), G_CoopWireEnt( inflictor ), G_CoopWireEnt( attacker ), damage, dflags, mod, hitLoc, has,
		d[0], d[1], d[2], p[0], p[1], p[2] );
}

static void G_CoopCmd_Hit( void ) {
	gentity_t *targ = G_CoopLocalEnt( gi.argv( 2 ), qfalse );
	if ( !targ || !G_CoopOwnsEnt( targ ) ) {
		return;		// gone meanwhile, or ownership moved on (M6): the hit is lost
	}
	gentity_t *inflictor = G_CoopLocalEnt( gi.argv( 3 ), qtrue );
	gentity_t *attacker = G_CoopLocalEnt( gi.argv( 4 ), qtrue );
	const int damage = atoi( gi.argv( 5 ) );
	const int dflags = atoi( gi.argv( 6 ) );
	const int mod = atoi( gi.argv( 7 ) );
	const int hitLoc = atoi( gi.argv( 8 ) );
	const int has = atoi( gi.argv( 9 ) );
	vec3_t dir, point;
	for ( int i = 0; i < 3; i++ ) {
		dir[i] = atof( gi.argv( 10 + i ) );
		point[i] = atof( gi.argv( 13 + i ) );
	}
	if ( targ->s.number == 0 || g_coopMode == COOP_MODE_HOST ) {
		gi.Printf( "COOP: hit on %d (%s) for %d from the partner\n", targ->s.number, targ->classname, damage );
	}
	g_coopRemoteHit = qtrue;
	G_Damage( targ, inflictor, attacker, ( has & 1 ) ? dir : NULL, ( has & 2 ) ? point : NULL, damage, dflags, mod, hitLoc );
	g_coopRemoteHit = qfalse;
}

// ---------------------------------------------------------------- use (design 18)

qboolean G_CoopForwardUse( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	if ( g_coopMode != COOP_MODE_GUEST || G_CoopOwnsEnt( self ) ) {
		return qfalse;
	}
	if ( !G_CoopOurCause( activator ) ) {
		return qtrue;	// blocked, and not ours to report
	}
	G_CoopSend( "use %d %d %d", G_CoopWireEnt( self ), G_CoopWireEnt( other ), G_CoopWireEnt( activator ) );
	return qtrue;
}

static void G_CoopCmd_Use( void ) {
	gentity_t *self = G_CoopLocalEnt( gi.argv( 2 ), qfalse );
	if ( !self || !G_CoopOwnsEnt( self ) ) {
		return;
	}
	gentity_t *other = G_CoopLocalEnt( gi.argv( 3 ), qtrue );
	gentity_t *activator = G_CoopLocalEnt( gi.argv( 4 ), qtrue );
	gi.Printf( "COOP: partner used %d (%s)\n", self->s.number, self->classname );
	GEntity_UseFunc( self, other, activator );
}

// ---------------------------------------------------------------- items (design 19)
// The host decides: the puppet touches items on the host like the guest's player would. Weapons and
// keys go to both players (both would have to pick them up anyway), the rest to who touched it.

qboolean G_CoopSharedItem( const gitem_t *item ) {
	return (qboolean)( item->giType == IT_WEAPON
		|| ( item->giType == IT_HOLDABLE && ( item->giTag == INV_SECURITY_KEY || item->giTag == INV_GOODIE_KEY ) ) );
}

void G_CoopSendPickup( const gentity_t *item ) {
	G_CoopSend( "pickup %d %d %d %d \"%s\"", (int)( item->item - bg_itemlist ), item->count, item->flags & FL_DROPPED_ITEM,
		item->spawnflags, item->message ? item->message : "" );
}

static void G_CoopCmd_Pickup( void ) {
	const int index = atoi( gi.argv( 2 ) );
	gentity_t *player = &g_entities[0];
	if ( index <= 0 || index >= bg_numItems || !player->inuse || !player->client || player->health <= 0 ) {
		return;
	}
	static gentity_t tmp;	// stands in for the host's item entity
	memset( &tmp, 0, sizeof( tmp ) );
	tmp.item = &bg_itemlist[index];
	tmp.classname = tmp.item->classname;
	tmp.s.modelindex = index;
	tmp.s.number = ENTITYNUM_NONE;
	tmp.count = atoi( gi.argv( 3 ) );
	tmp.flags = atoi( gi.argv( 4 ) );
	tmp.spawnflags = atoi( gi.argv( 5 ) );
	tmp.message = gi.argv( 6 )[0] ? G_NewString( gi.argv( 6 ) ) : NULL;
	gi.Printf( "COOP: picked up %s (the host decided)\n", tmp.item->classname );
	G_ItemPickupInto( &tmp, player );
	G_CoopP2Restrict( player );
}

// ---------------------------------------------------------------- force push / pull (design 7)

qboolean G_CoopForwardThrow( gentity_t *self, gentity_t *target, qboolean pull ) {
	if ( g_coopMode == COOP_MODE_SOLO || G_CoopOwnsEnt( target ) ) {
		return qfalse;
	}
	G_CoopSend( "fthrow %d %d %d", G_CoopWireEnt( self ), G_CoopWireEnt( target ), pull ? 1 : 0 );
	return qtrue;
}

static void G_CoopCmd_Throw( void ) {
	gentity_t *self = G_CoopLocalEnt( gi.argv( 2 ), qfalse );
	gentity_t *target = G_CoopLocalEnt( gi.argv( 3 ), qfalse );
	if ( !self || !self->client || !target || !target->client || !G_CoopOwnsEnt( target ) ) {
		return;
	}
	ForceThrowClient( self, target, (qboolean)( atoi( gi.argv( 4 ) ) != 0 ) );
}

// ---------------------------------------------------------------- alerts (M7: the AI hears and sees both players)

#define COOP_ALERT_INTERVAL_MS	100

void G_CoopForwardAlert( int type, const gentity_t *owner, const vec3_t pos, float radius, int alertLevel, float extra ) {
	static int lastSent[2];
	if ( owner != &g_entities[0] || !G_CoopPartnerInWorld() || type < 0 || type > 1 ) {
		return;
	}
	if ( alertLevel < AEL_DANGER && level.time - lastSent[type] < COOP_ALERT_INTERVAL_MS && level.time >= lastSent[type] ) {
		return;		// footsteps and the like: a few per second is enough
	}
	lastSent[type] = level.time;
	G_CoopSend( "alert %d %.0f %.0f %.0f %.0f %d %.2f", type, pos[0], pos[1], pos[2], radius, alertLevel, extra );
}

extern void AddSoundEvent( gentity_t *owner, vec3_t position, float radius, alertEventLevel_e alertLevel, qboolean needLOS );
extern void AddSightEvent( gentity_t *owner, vec3_t position, float radius, alertEventLevel_e alertLevel, float addLight );

static void G_CoopCmd_Alert( void ) {
	gentity_t *puppet = G_PlayerNum( 1 );
	if ( !puppet ) {
		return;
	}
	vec3_t pos;
	for ( int i = 0; i < 3; i++ ) {
		pos[i] = atof( gi.argv( 3 + i ) );
	}
	const float radius = atof( gi.argv( 6 ) );
	const alertEventLevel_e alertLevel = (alertEventLevel_e)atoi( gi.argv( 7 ) );
	const float extra = atof( gi.argv( 8 ) );
	if ( atoi( gi.argv( 2 ) ) == AET_SOUND ) {
		AddSoundEvent( puppet, pos, radius, alertLevel, (qboolean)( extra != 0.0f ) );
	} else {
		AddSightEvent( puppet, pos, radius, alertLevel, extra );
	}
}

// ---------------------------------------------------------------- the partner in our world

// host: logical triggers react to the guest's puppet like to a player (design 17); the guest runs
// the physical ones (push, teleport, hurt) for its own player
void G_CoopRunPuppet( gentity_t *ent ) {
	if ( g_coopMode != COOP_MODE_HOST || ent->s.number != 1 || !ent->client || ent->health <= 0 ) {
		return;
	}
	G_TouchTriggers( ent );
}

// ---------------------------------------------------------------- dispatcher

typedef struct {
	const char	*name;
	void		( *fn )( void );
} coopGameCmd_t;

static void G_CoopCmd_Claimed( void );
static void G_CoopCmd_MindTrick( void );
static void G_CoopCmd_Busy( void );
static void G_CoopCmd_Release( void );

static void G_CoopCmd_Echo( void ) {
	gi.Printf( "COOP: game command echo %s\n", gi.argv( 2 ) );
}

static void G_CoopCmd_Cam( void ) {
	G_CoopCamCommand( gi.argv( 1 ) );
}

static void G_CoopCmd_Life( void ) {
	G_CoopLifeCommand( gi.argv( 1 ) );
}

static void G_CoopCmd_P2( void ) {
	G_CoopP2Command( gi.argv( 1 ) );
}

static void G_CoopCmd_Npc( void ) {
	G_CoopNpcCommand( gi.argv( 1 ) );
}

static const coopGameCmd_t coopGameCmds[] = {
	{ "echo",	G_CoopCmd_Echo },
	{ "hit",	G_CoopCmd_Hit },
	{ "use",	G_CoopCmd_Use },
	{ "pickup",	G_CoopCmd_Pickup },
	{ "fthrow",	G_CoopCmd_Throw },
	{ "claimed",	G_CoopCmd_Claimed },
	{ "mtrick",	G_CoopCmd_MindTrick },
	{ "busy",	G_CoopCmd_Busy },
	{ "release",	G_CoopCmd_Release },
	{ "alert",	G_CoopCmd_Alert },
	{ "cam",	G_CoopCmd_Cam },
	{ "skipreq",	G_CoopCmd_Cam },
	{ "p2state",	G_CoopCmd_Life },
	{ "p2need",	G_CoopCmd_Life },
	{ "p2restore",	G_CoopCmd_Life },
	{ "near",	G_CoopCmd_Life },
	{ "skiplevel",	G_CoopCmd_Life },
	{ "p2char",	G_CoopCmd_P2 },
	{ "p2force",	G_CoopCmd_P2 },
	{ "kset",	G_CoopCmd_P2 },
	{ "npcst",	G_CoopCmd_Npc },
	{ "npcgive",	G_CoopCmd_Npc },
	{ "npctaken",	G_CoopCmd_Npc },
	{ "npcreq",	G_CoopCmd_Npc },
};

void G_Coop_GameCommand( void ) {
	const char *name = gi.argv( 1 );
	if ( g_coopMode == COOP_MODE_SOLO ) {
		return;
	}
	for ( size_t i = 0; i < ARRAY_LEN( coopGameCmds ); i++ ) {
		if ( !Q_stricmp( name, coopGameCmds[i].name ) ) {
			coopGameCmds[i].fn();
			return;
		}
	}
	gi.Printf( "COOP: unknown game command %s\n", name );
}

// ---------------------------------------------------------------- testing

// console: coop_hurt <entity> <damage> - our player damages an entity (a partner's one goes as a hit)
void G_CoopHurt_f( void ) {
	gentity_t *targ = G_CoopLocalEnt( gi.argv( 1 ), qfalse );
	if ( gi.argc() != 3 || !targ ) {
		gi.Printf( "usage: coop_hurt <entity> <damage>\n" );
		return;
	}
	G_Damage( targ, &g_entities[0], &g_entities[0], NULL, NULL, atoi( gi.argv( 2 ) ), DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN, HL_NONE );
}

// console: coop_use <entity> - our player uses an entity (a host one from the guest goes to the host)
void G_CoopUse_f( void ) {
	gentity_t *ent = G_CoopLocalEnt( gi.argv( 1 ), qfalse );
	if ( gi.argc() != 2 || !ent ) {
		gi.Printf( "usage: coop_use <entity>\n" );
		return;
	}
	GEntity_UseFunc( ent, &g_entities[0], &g_entities[0] );
}

// ---------------------------------------------------------------- emplaced guns, turrets, cameras (design 26)
// Whoever uses one owns it while using it, so aiming and firing run without delay on that machine; the
// other player gets "occupied". The host hands it over when the guest's use arrives (directly or
// through a panel's targets) and takes it back when the guest lets go.

static byte g_coopClaimed[MAX_GENTITIES];

void G_CoopClaimInit( void ) {
	memset( g_coopClaimed, 0, sizeof( g_coopClaimed ) );
}

static qboolean G_CoopClaimable( const gentity_t *ent ) {
	const char *c = ent->classname;
	return (qboolean)( c && ( !Q_stricmp( c, "emplaced_gun" ) || !Q_stricmp( c, "misc_panel_turret" ) || !Q_stricmp( c, "misc_camera" ) ) );
}

// the local player is using it
static qboolean G_CoopUsing( const gentity_t *ent, const gentity_t *player ) {
	if ( !player || !player->client ) {
		return qfalse;
	}
	if ( !Q_stricmp( ent->classname, "emplaced_gun" ) ) {
		return (qboolean)( ent->activator == player );
	}
	return (qboolean)( player->client->ps.viewEntity == ent->s.number );
}

// GEntity_UseFunc on the host: qtrue = coop took care of this use
qboolean G_CoopClaimUse( gentity_t *self, gentity_t *activator ) {
	if ( g_coopMode != COOP_MODE_HOST || !G_CoopClaimable( self ) || !activator ) {
		return qfalse;
	}
	const int n = self->s.number;
	if ( activator->s.number == 1 ) {		// the guest wants it
		if ( G_CoopUsing( self, &g_entities[0] ) || !G_CoopOwnsEnt( self ) ) {
			G_CoopSend( "busy" );
			return qtrue;
		}
		G_CoopSetOwner( self, COOP_MODE_GUEST );
		g_coopClaimed[n] = 1;
		G_CoopSend( "claimed %d %d %d", n, self->count, self->health );
		return qtrue;
	}
	if ( activator->s.number == 0 && !G_CoopOwnsEnt( self ) ) {
		gi.SendServerCommand( 0, "cp \"Occupied by the partner\"" );
		return qtrue;
	}
	return qfalse;
}

static void G_CoopCmd_Claimed( void ) {		// guest
	const int n = atoi( gi.argv( 2 ) );
	gentity_t *ent = G_CoopLocalEnt( gi.argv( 2 ), qfalse );
	if ( g_coopMode != COOP_MODE_GUEST || !ent || !g_entities[0].client ) {
		return;
	}
	G_CoopSetOwner( ent, COOP_MODE_GUEST );
	g_coopClaimed[n] = 1;
	ent->count = atoi( gi.argv( 3 ) );
	ent->health = atoi( gi.argv( 4 ) );
	GEntity_UseFunc( ent, &g_entities[0], &g_entities[0] );
	gi.Printf( "COOP: using %s (%d)\n", ent->classname, n );
}

static void G_CoopCmd_Busy( void ) {		// guest
	gi.SendServerCommand( 0, "cp \"Occupied by the partner\"" );
}

static void G_CoopCmd_Release( void ) {		// host
	const int n = atoi( gi.argv( 2 ) );
	gentity_t *ent = G_CoopLocalEnt( gi.argv( 2 ), qfalse );
	if ( g_coopMode != COOP_MODE_HOST || !ent || !g_coopClaimed[n] ) {
		return;
	}
	g_coopClaimed[n] = 0;
	G_CoopSetOwner( ent, COOP_MODE_HOST );
	ent->count = atoi( gi.argv( 3 ) );
	ent->health = atoi( gi.argv( 4 ) );
	ent->activator = NULL;
}

// guest: let go of what we no longer use; host: everything comes back when the guest leaves
void G_CoopClaimFrame( void ) {
	static qboolean hadPartner;
	const qboolean partner = G_CoopPartnerInWorld();
	for ( int n = MAX_CLIENTS; n < globals.num_entities; n++ ) {
		if ( !g_coopClaimed[n] ) {
			continue;
		}
		gentity_t *ent = &g_entities[n];
		if ( g_coopMode == COOP_MODE_HOST ) {
			if ( !partner && hadPartner ) {
				g_coopClaimed[n] = 0;
				G_CoopSetOwner( ent, COOP_MODE_HOST );
				ent->activator = NULL;
			}
			continue;
		}
		if ( !ent->inuse || !G_CoopUsing( ent, &g_entities[0] ) ) {
			g_coopClaimed[n] = 0;
			G_CoopSetOwner( ent, COOP_MODE_HOST );
			G_CoopSend( "release %d %d %d", n, ent->count, ent->health );
		}
	}
	hadPartner = partner;
}

// the host keeps a claimed entity the guest's snapshots stop carrying (G_Coop_FreeEntity)
qboolean G_CoopClaimKeep( gentity_t *ent ) {
	const int n = ent->s.number;
	if ( g_coopMode != COOP_MODE_HOST || n < MAX_CLIENTS || !G_CoopClaimable( ent ) ) {
		return qfalse;
	}
	return qtrue;	// a map entity: never ours to free because the guest's frame lost it
}

// ---------------------------------------------------------------- mind trick (design 26)

extern void WP_CoopApplyMindTrick( gentity_t *self, gentity_t *traceEnt, int level );

qboolean G_CoopForwardMindTrick( gentity_t *self, gentity_t *target ) {
	if ( g_coopMode == COOP_MODE_SOLO || G_CoopOwnsEnt( target ) || !self->client ) {
		return qfalse;
	}
	G_CoopSend( "mtrick %d %d %d", G_CoopWireEnt( self ), G_CoopWireEnt( target ), self->client->ps.forcePowerLevel[FP_TELEPATHY] );
	return qtrue;
}

static void G_CoopCmd_MindTrick( void ) {
	gentity_t *self = G_CoopLocalEnt( gi.argv( 2 ), qfalse );
	gentity_t *target = G_CoopLocalEnt( gi.argv( 3 ), qfalse );
	if ( self && self->client && target && G_CoopOwnsEnt( target ) ) {
		WP_CoopApplyMindTrick( self, target, atoi( gi.argv( 4 ) ) );
	}
}
