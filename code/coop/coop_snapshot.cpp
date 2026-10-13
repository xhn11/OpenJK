#include "coop_local.h"
#include "coop_snap.h"
#include "coop_entnum.h"

// Snapshots of the entities each machine owns (design 2, 4): host -> guest the world and the host's
// player, guest -> host the guest's player and whatever else the guest owns (missiles, M6 NPCs).
//   coop_snapshot  long mapGen, short esSize, short psSize, short exSize, long viewOrigin[3] (float bits),
//                  long blobLength, blob (coop_snap.h)
//   coop_snapack   long mapGen, long frameNum            (-1: the next one must not be a delta)
// The sender swaps entity numbers 0 <-> 1 in every field, so the receiver uses them as they come.
// Each direction has its own ring: tx (frames we sent) and rx (frames we received).

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
static coopSnapRing_t	txRing, rxRing;
static qboolean			txRingReady, rxRingReady;

// sending
static int				txFrameNum;			// never reset, so frame numbers stay unique across maps
static int				txAckedFrame = -1;
static int				txLastBuiltTime = -1;
static int				txLastWritten, txLastBudget;	// logging (CoopSnapshot_CheckStall)
static qboolean			txStallLogged;
static int				rxLastApplied, rxLastReceived, rxLastResult, rxNobaseCount;
static qboolean			rxStallLogged;

// receiving
static byte				rxPending[COOP_MAX_MSGLEN];
static int				rxPendingLen;
static int				rxAppliedFrame = -1;
static int				rxAckFrame = -1;
static qboolean			rxSynced;			// a complete frame was applied in this map
static qboolean			rxNeedSweep;		// a frame was applied without a known previous one
static int				rxIncomingFrame;	// the frame being applied (logging)
static int				applyClients;

// guest time follows the host's frames
static int				guestServerTime;
static int				guestServerTimeAt;	// Sys_Milliseconds() when guestServerTime was applied
static qboolean			guestHaveTime;

// the partner's eye position (design 8): from the coop_snapshot header (and coop_view, older peers)
static vec3_t			peerView;
static qboolean			peerViewValid;
static qboolean			peerViewLogged;

// sender scratch
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

static qboolean CoopSnapshot_EnsureRing( coopSnapRing_t *ring, qboolean *ready ) {
	if ( !*ready ) {
		*ready = (qboolean)CoopSnap_Init( ring, sizeof( entityState_t ), sizeof( playerState_t ),
			sizeof( coopEntExtra_t ), COOP_SNAP_POOL_ENTS, COOP_SNAP_POOL_PS );
		if ( !*ready ) {
			Com_Printf( "COOP: cannot allocate the snapshot ring\n" );
		}
	}
	return *ready;
}

static coopOwner_t CoopSnapshot_Self( void ) {
	return coop.role == COOP_ROLE_HOST ? COOP_OWNER_HOST : COOP_OWNER_GUEST;
}

static coopOwner_t CoopSnapshot_Partner( void ) {
	return coop.role == COOP_ROLE_HOST ? COOP_OWNER_GUEST : COOP_OWNER_HOST;
}

// host: the guest left our map (or comes back into it): its puppet and everything else it owned goes.
// The guest keeps the host's world as it is until it leaves the map itself.
static void CoopSnapshot_FreeGuestEntities( void ) {
	if ( coop.role != COOP_ROLE_HOST || !ge || !Coop_LocalMapRunning() ) {
		return;
	}
	int freed = 0;
	for ( int n = 1; n < ge->num_entities; n++ ) {
		if ( ge->Coop_EntityOwner( n ) == COOP_OWNER_GUEST ) {
			if ( n == 1 ) {
				Com_Printf( "COOP: the guest's puppet goes with the guest (link %d, ready gen %d)\n", coop.state, Coop_MapGen() );
			}
			ge->Coop_FreeEntity( n );
			freed++;
		}
	}
	if ( freed ) {
		Com_Printf( "COOP: removed %d guest entities\n", freed );
	}
}

static void CoopSnapshot_ResetTx( void ) {
	txAckedFrame = -1;
	txLastBuiltTime = -1;
	if ( txRingReady ) {
		CoopSnap_Clear( &txRing );
	}
}

static void CoopSnapshot_ResetRx( void ) {
	rxPendingLen = 0;
	rxAppliedFrame = -1;
	rxAckFrame = -1;
	rxSynced = qfalse;
	rxNeedSweep = qfalse;
	if ( rxRingReady ) {
		CoopSnap_Clear( &rxRing );
	}
	peerViewValid = qfalse;
	peerViewLogged = qfalse;
}

void CoopSnapshot_GuestReady( void ) {	// host: the guest (re)entered our map, both directions start over
	CoopSnapshot_FreeGuestEntities();
	CoopSnapshot_ResetTx();
	CoopSnapshot_ResetRx();
}

void CoopSnapshot_NewMap( void ) {		// guest: coop_map arrived
	guestHaveTime = qfalse;
	CoopSnapshot_ResetTx();
	CoopSnapshot_ResetRx();
}

void CoopSnapshot_LinkReset( void ) {
	CoopSnapshot_FreeGuestEntities();
	guestHaveTime = qfalse;
	CoopSnapshot_ResetTx();
	CoopSnapshot_ResetRx();
}

static qboolean CoopSnapshot_InWorld( void ) {	// both machines play the same map
	return coop.role == COOP_ROLE_HOST ? Coop_HostGuestInWorld() : Coop_GuestInWorld();
}

// ---------------------------------------------------------------- sending

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
	const gentity_t *puppet = SV_GentityNum( 1 );	// its thrown saber is our local copy of the partner's (never sent)
	const int puppetSaber = ( puppet->inuse && puppet->client ) ? puppet->client->saberEntityNum : ENTITYNUM_NONE;
	const int limit = ge->num_entities < COOP_SNAP_MAX_NUM ? ge->num_entities : COOP_SNAP_MAX_NUM;
	const coopOwner_t self = CoopSnapshot_Self();
	int count = 0, psCount = 0;
	if ( !player->inuse || !player->client || ge->Coop_EntityOwner( 0 ) != self ) {
		static int logged;
		if ( logged++ < 5 ) {
			Com_Printf( "COOP: own player not sent (inuse %d client %d owner %d)\n", player->inuse, player->client != NULL, ge->Coop_EntityOwner( 0 ) );
		}
	}
	for ( int wire = 0; wire < limit; wire++ ) {
		const int n = Coop_SwapEntNum( wire );	// ascending wire numbers keep the input sorted
		if ( n == 1 || n >= limit ) {
			continue;	// entity 1 is the partner's puppet, never ours
		}
		gentity_t *ent = SV_GentityNum( n );
		if ( !ent->inuse || ( n == playerSaber && n != 0 ) || ( n == puppetSaber && n != 0 ) || ( ent->s.eFlags & EF_PERMANENT ) ) {	// no saber: 0
			continue;
		}
		const qboolean visible = (qboolean)( ent->linked && !( ent->svFlags & SVF_NOCLIENT ) );
		if ( !visible && !ent->client ) {
			continue;
		}
		if ( ge->Coop_EntityOwner( n ) != self ) {
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

static void CoopSnapshot_WriteFrame( msg_t *msg ) {
	if ( !CoopSnapshot_InWorld() || sv.time == txLastBuiltTime || !CoopSnapshot_EnsureRing( &txRing, &txRingReady ) ) {
		return;	// one snapshot per game frame
	}
	if ( coop.role == COOP_ROLE_GUEST && !rxSynced ) {
		return;	// the guest sends its own entities once it has the host's world
	}
	int budget = Coop_MsgRoom( msg ) - COOP_SNAP_WIRE_HEADER;
	if ( coop_snapshotBytes->integer > 0 && budget > coop_snapshotBytes->integer ) {
		budget = coop_snapshotBytes->integer;
	}
	txLastBudget = budget;
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
	const int frameNum = ++txFrameNum;
	const int len = CoopSnap_Write( &txRing, frameNum, txAckedFrame, sv.time, &in, snapBlob, budget, &info );
	if ( len < 0 ) {
		Com_Printf( "COOP: snapshot %d not written (%d)\n", frameNum, len );
		return;
	}
	txLastBuiltTime = sv.time;
	txLastWritten = Sys_Milliseconds();
	if ( txStallLogged ) {
		txStallLogged = qfalse;
		Com_Printf( "COOP: snapshots sent again (frame %d)\n", frameNum );
	}
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
	if ( gen == Coop_MapGen() ) {
		if ( frame < 0 ) {
			txAckedFrame = -1;
		} else if ( frame > txAckedFrame && frame <= txFrameNum ) {
			txAckedFrame = frame;
		}
	}
	return qtrue;
}

// ---------------------------------------------------------------- receiving

static void CoopSnapshot_WriteAck( msg_t *msg ) {
	if ( !CoopSnapshot_InWorld() || Coop_MsgRoom( msg ) < 9 ) {
		return;
	}
	MSG_WriteByte( msg, coop_snapack );
	MSG_WriteLong( msg, Coop_MapGen() );
	MSG_WriteLong( msg, rxAckFrame );
}

void CoopSnapshot_Write( msg_t *msg ) {
	if ( coop.role == COOP_ROLE_HOST || coop.role == COOP_ROLE_GUEST ) {
		CoopSnapshot_WriteAck( msg );		// 9 bytes, before the snapshot takes the rest of the budget
		CoopSnapshot_WriteFrame( msg );
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
	if ( gen != Coop_MapGen() || !CoopSnapshot_InWorld() ) {
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
	memcpy( rxPending, blob, len );	// Coop_Frame applies it; only the newest one matters
	rxPendingLen = len;
	rxLastReceived = Sys_Milliseconds();
	VectorCopy( view, peerView );
	peerViewValid = qtrue;
	return qtrue;
}

static void CoopSnapshot_ApplyChange( void *, int num, int change, const uint8_t *es, const uint8_t *ps, const uint8_t *ex ) {
	if ( change == COOP_SNAPCHG_REMOVED ) {
		if ( num == 1 ) {
			// the partner's player only goes when the partner leaves our map (link reset, coop_ready, map change);
			// a frame without it is a fault on the other side, see its "own player not sent" log line
			static int logged;
			if ( logged++ < 5 ) {
				Com_Printf( "COOP: the partner's frame %d no longer has its player (applied %d), puppet kept\n", rxIncomingFrame, rxAppliedFrame );
			}
			return;
		}
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
		applyClients++;
	}
}

// partner entities we have (the guest: from its own map load) that the partner no longer has
static void CoopSnapshot_Sweep( int frameNum ) {
	memset( sweepPresent, 0, sizeof( sweepPresent ) );
	applyClients = 0;
	CoopSnap_ForEach( &rxRing, frameNum, CoopSnapshot_MarkPresent, NULL );
	const coopOwner_t partner = CoopSnapshot_Partner();
	const int limit = ge->num_entities;	// our own entities are skipped by the owner check below
	int removed = 0;
	for ( int n = 1; n < limit; n++ ) {
		gentity_t *ent = SV_GentityNum( n );
		if ( !ent->inuse || sweepPresent[n] || ( ent->s.eFlags & EF_PERMANENT ) ) {
			continue;
		}
		if ( !ent->client && ( !ent->linked || ( ent->svFlags & SVF_NOCLIENT ) ) ) {
			continue;	// unseen kinds (triggers, path corners...) are never in snapshots: keep them
		}
		if ( ge->Coop_EntityOwner( n ) != partner ) {
			continue;
		}
		if ( n == 1 ) {
			Com_Printf( "COOP: sweep keeps the partner's player\n" );
			continue;
		}
		ge->Coop_FreeEntity( n );
		removed++;
	}
	if ( removed ) {
		Com_Printf( "COOP: removed %d entities the partner does not have\n", removed );
	}
}

// logging: frames from the partner stopped being applied (the M4+ manual sessions lost the guest's player)


static void CoopSnapshot_CheckStall( void ) {
	const int now = Sys_Milliseconds();
	if ( !CoopSnapshot_InWorld() || !rxSynced ) {
		return;
	}
	if ( now - rxLastApplied > 2000 && !rxStallLogged ) {
		rxStallLogged = qtrue;
		Com_Printf( "COOP: no partner snapshot applied for %d ms (last received %d ms ago, pending %d, last result %d, nobase %d, ack %d)\n",
			now - rxLastApplied, now - rxLastReceived, rxPendingLen, rxLastResult, rxNobaseCount, rxAckFrame );
	}
	if ( txLastBuiltTime >= 0 && now - txLastWritten > 2000 && !txStallLogged ) {	// after our first frame
		txStallLogged = qtrue;
		Com_Printf( "COOP: no snapshot sent for %d ms (sv.time %d, last built %d, budget %d, acked %d)\n",
			now - txLastWritten, sv.time, txLastBuiltTime, txLastBudget, txAckedFrame );
	}
}

void CoopSnapshot_Frame( void ) {
	CoopSnapshot_CheckStall();
	if ( !rxPendingLen ) {
		return;
	}
	if ( !CoopSnapshot_InWorld() || !CoopSnapshot_EnsureRing( &rxRing, &rxRingReady ) ) {
		return;
	}
	if ( coop.role == COOP_ROLE_GUEST && !CoopCS_Synced() ) {
		return;	// keep it until the world can take it (configstrings first: plan-M3 R3)
	}
	int frameNum = -1;
	const int result = CoopSnap_Read( &rxRing, rxPending, rxPendingLen, &frameNum );
	rxPendingLen = 0;
	rxLastResult = result;
	if ( result == COOP_SNAP_NOBASE ) {
		rxNobaseCount++;
		rxAckFrame = -1;
		return;
	}
	if ( result != COOP_SNAP_OK ) {
		Com_Printf( "COOP: bad snapshot (%d), asking for a full one\n", result );
		rxAckFrame = -1;
		return;
	}
	rxIncomingFrame = frameNum;
	if ( CoopSnap_Diff( &rxRing, rxAppliedFrame, frameNum, CoopSnapshot_ApplyChange, NULL ) == 1 ) {
		rxNeedSweep = qtrue;
	}
	const coopSnapFrame_t *f = CoopSnap_Frame( &rxRing, frameNum );
	const qboolean complete = (qboolean)( f && ( f->flags & COOP_SNAPF_COMPLETE ) );
	if ( rxNeedSweep && complete ) {
		rxNeedSweep = qfalse;
		CoopSnapshot_Sweep( frameNum );
	}
	rxAppliedFrame = frameNum;
	rxAckFrame = frameNum;
	rxLastApplied = Sys_Milliseconds();
	if ( rxStallLogged ) {
		rxStallLogged = qfalse;
		Com_Printf( "COOP: partner snapshots applied again (frame %d)\n", frameNum );
	}
	if ( rxSynced && !SV_GentityNum( 1 )->inuse ) {
		// the partner is in our map but its player is not here: ask for a full frame, it brings the player back
		static int lastAsk;
		if ( Sys_Milliseconds() - lastAsk > 1000 ) {
			lastAsk = Sys_Milliseconds();
			Com_Printf( "COOP: the partner's player is missing, asking for a full snapshot\n" );
			rxAckFrame = -1;
			rxAppliedFrame = -1;
		}
	}
	if ( f && coop.role == COOP_ROLE_GUEST ) {
		guestServerTime = f->serverTime;
		guestServerTimeAt = Sys_Milliseconds();
		guestHaveTime = qtrue;
	}
	if ( !rxSynced && complete ) {
		rxSynced = qtrue;
		memset( sweepPresent, 0, sizeof( sweepPresent ) );
		applyClients = 0;
		const int ents = CoopSnap_ForEach( &rxRing, frameNum, CoopSnapshot_MarkPresent, NULL );
		if ( coop.role == COOP_ROLE_GUEST ) {
			Com_Printf( "COOP: world synced (%d entities, %d npcs)\n", ents, applyClients );
		} else {
			Com_Printf( "COOP: guest synced (%d entities, %d clients)\n", ents, applyClients );
		}
	}
}

void CoopView_Write( msg_t * ) {
	// the partner's view travels in the coop_snapshot header both ways now (M4)
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
	if ( !CoopSnapshot_InWorld() || !rxSynced || !peerViewValid ) {
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
