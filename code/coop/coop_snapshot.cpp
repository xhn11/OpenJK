#include "coop_local.h"
#include "coop_snap.h"
#include "coop_entnum.h"

// Snapshots of the entities the host owns, host -> guest (design 2, 4; M4 adds guest -> host).
//   coop_snapshot  long mapGen, short esSize, short psSize, short exSize, long viewOrigin[3] (float bits),
//                  long blobLength, blob (coop_snap.h)
//   coop_snapack   long mapGen, long frameNum            (-1: the next one must not be a delta)
// The sender swaps entity numbers 0 <-> 1 in every field, so the receiver uses them as they come.

#ifdef JK2_MODE

#ifndef EF_PERMANENT
#define EF_PERMANENT			0x00080000		// as in sv_snapshot.cpp: never sent to clients
#endif

static_assert( COOP_ENT_MAX_CLIENTS == MAX_CLIENTS, "coop_entnum.h mirrors MAX_CLIENTS" );
static_assert( COOP_ENT_MAX_NORMAL == ENTITYNUM_MAX_NORMAL, "coop_entnum.h mirrors ENTITYNUM_MAX_NORMAL" );
static_assert( COOP_SNAP_MAX_NUM == MAX_GENTITIES, "coop_snap.h entity numbers" );

#define COOP_SNAP_POOL_ENTS		( 16 * COOP_SNAP_MAX_NUM )
#define COOP_SNAP_POOL_PS		( 8 * COOP_SNAP_MAX_PS )
#define COOP_SNAP_WIRE_HEADER	( 1 + 4 + 3 * 2 + 3 * 4 + 4 )
#define COOP_EXTRAPOLATE_MS		250			// guest time runs at most this far past the newest snapshot
#define COOP_TIME_JUMP_MS		500			// further behind than this, the guest jumps to the host's time

static cvar_t			*coop_snapshotBytes;
static coopSnapRing_t	snapRing;			// host: frames sent; guest: frames received
static qboolean			snapRingReady;

// host
static int				hostFrameNum;		// never reset, so frame numbers stay unique across maps
static int				hostAckedFrame = -1;
static int				hostLastBuiltTime = -1;

// guest
static byte				guestPending[COOP_MAX_MSGLEN];
static int				guestPendingLen;
static int				guestAppliedFrame = -1;
static int				guestAckFrame = -1;
static int				guestServerTime;
static int				guestServerTimeAt;	// Sys_Milliseconds() when guestServerTime was applied
static qboolean			guestHaveTime;
static qboolean			guestSynced;
static qboolean			guestNeedSweep;		// a frame was applied without a known previous one
static int				applyNpcs;

// the partner's eye position (design 8): host <- coop_view, guest <- coop_snapshot header
static vec3_t			peerView;
static qboolean			peerViewValid;
static qboolean			peerViewLogged;

// host scratch
static entityState_t	gatherEs[COOP_SNAP_MAX_NUM];
static coopEntExtra_t	gatherEx[COOP_SNAP_MAX_NUM];
static playerState_t	gatherPs[COOP_SNAP_MAX_PS];
static int				gatherNums[COOP_SNAP_MAX_NUM];
static const uint8_t	*gatherEsP[COOP_SNAP_MAX_NUM], *gatherPsP[COOP_SNAP_MAX_NUM], *gatherExP[COOP_SNAP_MAX_NUM];
static byte				snapBlob[COOP_MAX_MSGLEN];
static byte				sweepPresent[MAX_GENTITIES];

void CoopSnapshot_Init( void ) {
	coop_snapshotBytes = Cvar_Get( "coop_snapshotBytes", "8000", 0 );	// snapshot bytes per packet (0: whatever fits)
}

static qboolean CoopSnapshot_EnsureRing( void ) {
	if ( !snapRingReady ) {
		snapRingReady = (qboolean)CoopSnap_Init( &snapRing, sizeof( entityState_t ), sizeof( playerState_t ),
			sizeof( coopEntExtra_t ), COOP_SNAP_POOL_ENTS, COOP_SNAP_POOL_PS );
		if ( !snapRingReady ) {
			Com_Printf( "COOP: cannot allocate the snapshot ring\n" );
		}
	}
	return snapRingReady;
}

void CoopSnapshot_GuestReady( void ) {
	hostAckedFrame = -1;
	hostLastBuiltTime = -1;
	if ( snapRingReady ) {
		CoopSnap_Clear( &snapRing );
	}
	peerViewValid = qfalse;
	peerViewLogged = qfalse;
}

void CoopSnapshot_NewMap( void ) {
	guestPendingLen = 0;
	guestAppliedFrame = -1;
	guestAckFrame = -1;
	guestHaveTime = qfalse;
	guestSynced = qfalse;
	guestNeedSweep = qfalse;
	if ( snapRingReady ) {
		CoopSnap_Clear( &snapRing );
	}
	peerViewValid = qfalse;
	peerViewLogged = qfalse;
}

void CoopSnapshot_LinkReset( void ) {
	CoopSnapshot_GuestReady();
	CoopSnapshot_NewMap();
}

// ---------------------------------------------------------------- host

static void CoopSnapshot_SwapEntityState( entityState_t *s ) {
	s->number = Coop_SwapEntNum( s->number );
	s->otherEntityNum = Coop_SwapEntNum( s->otherEntityNum );
	s->otherEntityNum2 = Coop_SwapEntNum( s->otherEntityNum2 );
	s->groundEntityNum = Coop_SwapEntNum( s->groundEntityNum );
	s->clientNum = Coop_SwapEntNum( s->clientNum );
}

static void CoopSnapshot_SwapPlayerState( playerState_t *ps ) {
	ps->clientNum = Coop_SwapEntNum( ps->clientNum );
	ps->groundEntityNum = Coop_SwapEntNum( ps->groundEntityNum );
	ps->viewEntity = Coop_SwapEntNum( ps->viewEntity );
	ps->saberEntityNum = Coop_SwapEntNum( ps->saberEntityNum );
	ps->saberLockEnemy = Coop_SwapEntNum( ps->saberLockEnemy );
	ps->forceGripEntityNum = Coop_SwapEntNum( ps->forceGripEntityNum );
	ps->saberModel = NULL;		// a pointer into our memory; the receiver keeps its own (plan-M3 R1)
}

static int CoopSnapshot_Gather( void ) {
	const gentity_t *player = SV_GentityNum( 0 );
	const int playerSaber = ( player->inuse && player->client ) ? player->client->saberEntityNum : ENTITYNUM_NONE;
	const int limit = ge->num_entities < COOP_SNAP_MAX_NUM ? ge->num_entities : COOP_SNAP_MAX_NUM;
	int count = 0, psCount = 0;
	for ( int wire = 0; wire < limit; wire++ ) {
		const int n = Coop_SwapEntNum( wire );	// ascending wire numbers keep the input sorted
		if ( n < MAX_CLIENTS || n >= limit ) {
			continue;	// M3: the players are not replicated (the partner puppet comes in M4)
		}
		gentity_t *ent = SV_GentityNum( n );
		if ( !ent->inuse || n == playerSaber || ( ent->s.eFlags & EF_PERMANENT ) ) {
			continue;
		}
		const qboolean visible = (qboolean)( ent->linked && !( ent->svFlags & SVF_NOCLIENT ) );
		if ( !visible && !ent->client ) {
			continue;
		}
		if ( ge->Coop_EntityOwner( n ) != COOP_OWNER_HOST ) {
			continue;
		}
		if ( ent->client && psCount >= COOP_SNAP_MAX_PS ) {
			continue;
		}
		gatherEs[count] = ent->s;
		CoopSnapshot_SwapEntityState( &gatherEs[count] );
		ge->Coop_GetEntityExtra( n, &gatherEx[count] );
		gatherEx[count].ownerNum = Coop_SwapEntNum( gatherEx[count].ownerNum );
		gatherEx[count].enemyNum = Coop_SwapEntNum( gatherEx[count].enemyNum );
		gatherPsP[count] = NULL;
		if ( ent->client ) {
			gatherPs[psCount] = *ent->client;
			CoopSnapshot_SwapPlayerState( &gatherPs[psCount] );
			gatherPsP[count] = (const uint8_t *)&gatherPs[psCount];
			psCount++;
		}
		gatherNums[count] = wire;
		gatherEsP[count] = (const uint8_t *)&gatherEs[count];
		gatherExP[count] = (const uint8_t *)&gatherEx[count];
		count++;
	}
	return count;
}

static void CoopSnapshot_WriteFloat( msg_t *msg, float f ) {
	int bits;
	memcpy( &bits, &f, 4 );
	MSG_WriteLong( msg, bits );
}

static void CoopSnapshot_WriteHost( msg_t *msg ) {
	if ( !Coop_HostGuestInWorld() || sv.time == hostLastBuiltTime || !CoopSnapshot_EnsureRing() ) {
		return;	// one snapshot per game frame
	}
	int budget = Coop_MsgRoom( msg ) - COOP_SNAP_WIRE_HEADER;
	if ( coop_snapshotBytes->integer > 0 && budget > coop_snapshotBytes->integer ) {
		budget = coop_snapshotBytes->integer;
	}
	if ( budget < COOP_SNAP_HEADER + 64 ) {
		return;
	}
	coopSnapInput_t in;
	in.count = CoopSnapshot_Gather();
	in.nums = gatherNums;
	in.es = gatherEsP;
	in.ps = gatherPsP;
	in.ex = gatherExP;
	coopSnapWriteInfo_t info;
	const int frameNum = ++hostFrameNum;
	const int len = CoopSnap_Write( &snapRing, frameNum, hostAckedFrame, sv.time, &in, snapBlob, budget, &info );
	if ( len < 0 ) {
		Com_Printf( "COOP: snapshot %d not written (%d)\n", frameNum, len );
		return;
	}
	hostLastBuiltTime = sv.time;
	const gentity_t *player = SV_GentityNum( 0 );
	MSG_WriteByte( msg, coop_snapshot );
	MSG_WriteLong( msg, Coop_MapGen() );
	MSG_WriteShort( msg, (int)sizeof( entityState_t ) );
	MSG_WriteShort( msg, (int)sizeof( playerState_t ) );
	MSG_WriteShort( msg, (int)sizeof( coopEntExtra_t ) );
	for ( int i = 0; i < 3; i++ ) {
		CoopSnapshot_WriteFloat( msg, ( player->inuse && player->client ) ? player->client->serverViewOrg[i] : 0.0f );
	}
	MSG_WriteLong( msg, len );
	MSG_WriteData( msg, snapBlob, len );
	if ( info.deferred && coop_debug->integer ) {
		Com_Printf( "COOP: snapshot %d: %d bytes, %d changes deferred\n", frameNum, len, info.deferred );
	}
}

qboolean CoopSnapshot_ParseAck( msg_t *msg ) {
	const int gen = MSG_ReadLong( msg );
	const int frame = MSG_ReadLong( msg );
	if ( coop.role == COOP_ROLE_HOST && gen == Coop_MapGen() ) {
		if ( frame < 0 ) {
			hostAckedFrame = -1;
		} else if ( frame > hostAckedFrame && frame <= hostFrameNum ) {
			hostAckedFrame = frame;
		}
	}
	return qtrue;
}

// ---------------------------------------------------------------- guest

static void CoopSnapshot_WriteGuestAck( msg_t *msg ) {
	if ( !Coop_MapGen() || Coop_MsgRoom( msg ) < 9 ) {
		return;
	}
	MSG_WriteByte( msg, coop_snapack );
	MSG_WriteLong( msg, Coop_MapGen() );
	MSG_WriteLong( msg, guestAckFrame );
}

void CoopSnapshot_Write( msg_t *msg ) {
	if ( coop.role == COOP_ROLE_HOST ) {
		CoopSnapshot_WriteHost( msg );
	} else if ( coop.role == COOP_ROLE_GUEST ) {
		CoopSnapshot_WriteGuestAck( msg );
	}
}

qboolean CoopSnapshot_Parse( msg_t *msg ) {
	const int gen = MSG_ReadLong( msg );
	const int esSize = MSG_ReadShort( msg );
	const int psSize = MSG_ReadShort( msg );
	const int exSize = MSG_ReadShort( msg );
	vec3_t view;
	for ( int i = 0; i < 3; i++ ) {
		const int bits = MSG_ReadLong( msg );
		memcpy( &view[i], &bits, 4 );
	}
	const int len = MSG_ReadLong( msg );
	if ( len < 0 || len > msg->cursize - msg->readcount ) {
		return qfalse;
	}
	const byte *blob = msg->data + msg->readcount;
	msg->readcount += len;
	if ( coop.role != COOP_ROLE_GUEST || gen != Coop_MapGen() ) {
		return qtrue;	// another map: ignore
	}
	if ( esSize != (int)sizeof( entityState_t ) || psSize != (int)sizeof( playerState_t ) || exSize != (int)sizeof( coopEntExtra_t ) ) {
		static qboolean warned;
		if ( !warned ) {
			warned = qtrue;
			Com_Printf( "COOP: snapshot layout differs from this build (%d/%d/%d), host and guest must run the same build\n",
				esSize, psSize, exSize );
		}
		return qtrue;
	}
	memcpy( guestPending, blob, len );	// Coop_Frame applies it; only the newest one matters
	guestPendingLen = len;
	VectorCopy( view, peerView );
	peerViewValid = qtrue;
	return qtrue;
}

static void CoopSnapshot_ApplyChange( void *, int num, int change, const uint8_t *es, const uint8_t *ps, const uint8_t *ex ) {
	if ( change == COOP_SNAPCHG_REMOVED ) {
		ge->Coop_FreeEntity( num );
		return;
	}
	static entityState_t	s;
	static playerState_t	p;
	static coopEntExtra_t	x;
	memcpy( &s, es, sizeof( s ) );
	memcpy( &x, ex, sizeof( x ) );
	if ( ps ) {
		memcpy( &p, ps, sizeof( p ) );
	}
	ge->Coop_ApplyEntity( num, &s, ps ? &p : NULL, &x );
}

static void CoopSnapshot_MarkPresent( void *, int num, int, const uint8_t *, const uint8_t *ps, const uint8_t * ) {
	if ( num >= 0 && num < MAX_GENTITIES ) {
		sweepPresent[num] = 1;
	}
	if ( ps ) {
		applyNpcs++;
	}
}

// host entities that the guest has from its own map load but the host no longer has
static void CoopSnapshot_Sweep( int frameNum ) {
	memset( sweepPresent, 0, sizeof( sweepPresent ) );
	applyNpcs = 0;
	CoopSnap_ForEach( &snapRing, frameNum, CoopSnapshot_MarkPresent, NULL );
	const int limit = ge->num_entities;	// the guest's own entities are skipped by the owner check below
	int removed = 0;
	for ( int n = MAX_CLIENTS; n < limit; n++ ) {
		gentity_t *ent = SV_GentityNum( n );
		if ( !ent->inuse || sweepPresent[n] || !ent->linked || ( ent->svFlags & SVF_NOCLIENT ) || ( ent->s.eFlags & EF_PERMANENT ) ) {
			continue;	// unseen kinds (triggers, path corners...) are never in snapshots: keep them
		}
		if ( ge->Coop_EntityOwner( n ) != COOP_OWNER_HOST ) {
			continue;
		}
		ge->Coop_FreeEntity( n );
		removed++;
	}
	if ( removed ) {
		Com_Printf( "COOP: removed %d entities the host does not have\n", removed );
	}
}

void CoopSnapshot_Frame( void ) {
	if ( coop.role != COOP_ROLE_GUEST || !guestPendingLen ) {
		return;
	}
	if ( !Coop_GuestInWorld() || !CoopCS_Synced() || !CoopSnapshot_EnsureRing() ) {
		return;	// keep it until the world can take it (configstrings first: plan-M3 R3)
	}
	int frameNum = -1;
	const int result = CoopSnap_Read( &snapRing, guestPending, guestPendingLen, &frameNum );
	guestPendingLen = 0;
	if ( result == COOP_SNAP_NOBASE ) {
		guestAckFrame = -1;
		return;
	}
	if ( result != COOP_SNAP_OK ) {
		Com_Printf( "COOP: bad snapshot (%d), asking for a full one\n", result );
		guestAckFrame = -1;
		return;
	}
	if ( CoopSnap_Diff( &snapRing, guestAppliedFrame, frameNum, CoopSnapshot_ApplyChange, NULL ) == 1 ) {
		guestNeedSweep = qtrue;
	}
	const coopSnapFrame_t *f = CoopSnap_Frame( &snapRing, frameNum );
	const qboolean complete = (qboolean)( f && ( f->flags & COOP_SNAPF_COMPLETE ) );
	if ( guestNeedSweep && complete ) {
		guestNeedSweep = qfalse;
		CoopSnapshot_Sweep( frameNum );
	}
	guestAppliedFrame = frameNum;
	guestAckFrame = frameNum;
	if ( f ) {
		guestServerTime = f->serverTime;
		guestServerTimeAt = Sys_Milliseconds();
		guestHaveTime = qtrue;
	}
	if ( !guestSynced && complete ) {
		guestSynced = qtrue;
		memset( sweepPresent, 0, sizeof( sweepPresent ) );
		applyNpcs = 0;
		const int ents = CoopSnap_ForEach( &snapRing, frameNum, CoopSnapshot_MarkPresent, NULL );
		Com_Printf( "COOP: world synced (%d entities, %d npcs)\n", ents, applyNpcs );
	}
}

void CoopView_Write( msg_t *msg ) {
	if ( coop.role != COOP_ROLE_GUEST || !Coop_GuestInWorld() || Coop_MsgRoom( msg ) < 17 ) {
		return;
	}
	const gentity_t *player = SV_GentityNum( 0 );
	if ( !player->inuse || !player->client ) {
		return;
	}
	MSG_WriteByte( msg, coop_view );
	MSG_WriteLong( msg, Coop_MapGen() );
	for ( int i = 0; i < 3; i++ ) {
		CoopSnapshot_WriteFloat( msg, player->client->serverViewOrg[i] );
	}
}

qboolean CoopView_Parse( msg_t *msg ) {
	const int gen = MSG_ReadLong( msg );
	vec3_t view;
	for ( int i = 0; i < 3; i++ ) {
		const int bits = MSG_ReadLong( msg );
		memcpy( &view[i], &bits, 4 );
	}
	if ( coop.role == COOP_ROLE_HOST && gen == Coop_MapGen() ) {
		VectorCopy( view, peerView );
		peerViewValid = qtrue;
	}
	return qtrue;
}

qboolean Coop_PeerViewOrigin( vec3_t org ) {
	const qboolean inWorld = coop.role == COOP_ROLE_HOST ? Coop_HostGuestInWorld()
		: (qboolean)( coop.role == COOP_ROLE_GUEST && Coop_GuestInWorld() && guestSynced );
	if ( !inWorld || !peerViewValid ) {
		return qfalse;
	}
	if ( !peerViewLogged ) {
		peerViewLogged = qtrue;
		Com_Printf( "COOP: partner view added to snapshots\n" );
	}
	VectorCopy( peerView, org );
	return qtrue;
}

qboolean Coop_GuestAdvanceTime( int frameMsec ) {
	if ( coop.role != COOP_ROLE_GUEST || !guestHaveTime || !Coop_GuestInWorld() ) {
		return qfalse;
	}
	int elapsed = Sys_Milliseconds() - guestServerTimeAt;
	if ( elapsed > COOP_EXTRAPOLATE_MS ) {
		elapsed = COOP_EXTRAPOLATE_MS;
	}
	const int target = guestServerTime + elapsed;
	if ( target - sv.time > COOP_TIME_JUMP_MS ) {
		// first snapshot or a long stall: jump instead of running hundreds of frames
		sv.time = target - frameMsec;
		re.G2API_SetTime( sv.time, G2T_SV_TIME );
	}
	sv.timeResidual = target > sv.time ? target - sv.time : 0;	// ahead of the host: wait
	sv.timeResidualFraction = 0;
	return qtrue;
}

#else	// !JK2_MODE: the JA engine has no coop game exports

void		CoopSnapshot_Init( void ) {}
void		CoopSnapshot_GuestReady( void ) {}
void		CoopSnapshot_NewMap( void ) {}
void		CoopSnapshot_LinkReset( void ) {}
void		CoopSnapshot_Write( msg_t * ) {}
qboolean	CoopSnapshot_Parse( msg_t * ) { return qfalse; }
qboolean	CoopSnapshot_ParseAck( msg_t * ) { return qfalse; }
void		CoopSnapshot_Frame( void ) {}
qboolean	Coop_GuestAdvanceTime( int ) { return qfalse; }
void		CoopView_Write( msg_t * ) {}
qboolean	CoopView_Parse( msg_t * ) { return qfalse; }
qboolean	Coop_PeerViewOrigin( vec3_t ) { return qfalse; }

#endif
