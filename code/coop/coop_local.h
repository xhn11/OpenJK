#pragma once
#include "../server/exe_headers.h"
#include "../server/server.h"
#include "coop_public.h"
#include "coop_reliable.h"
#include "coop_netchan.h"

#define COOP_PROTOCOL_VERSION		8			// 6: coop_view, 7: snapshots both ways, game commands, 8: extra v2
#define COOP_DEFAULT_PORT			29170
#define COOP_CONNECT_RESEND_MS		1000
#define COOP_CONNECT_TIMEOUT_MS		15000
#define COOP_PACKET_INTERVAL_MS		33
#define COOP_MIN_TIMEOUT_SEC		5
#define COOP_LOAD_TIMEOUT_MS		120000		// link timeout while either side loads a map (SV_SpawnServer + cgame media)
// MSG_ReadString stops at MAX_STRING_CHARS-1 chars without consuming the NUL, so 1022 is the longest string that round-trips
#define COOP_MAX_WIRE_CMD_LEN		( MAX_STRING_CHARS - 2 )
// MSG_WriteBits refuses to write when fewer than 4 bytes are left (msg.cpp:113), so coop_eof needs 4
#define COOP_MSG_TAIL				4

typedef enum { COOP_LINK_IDLE, COOP_LINK_LISTENING, COOP_LINK_CONNECTING, COOP_LINK_ACTIVE } coopLinkState_t;

typedef struct {
	coopRole_t		role;
	coopLinkState_t	state;
	netadr_t		peer;
	coopNetchan_t	chan;
	int				challenge;
	int				connectStart;
	int				lastSent;
	int				lastReceived;
	char			peerName[64];
	coopReliable_t	rel;
	int				testPending;		// bytes of a coop_test payload to send in the next packet
	int				testRepeats;		// how many more packets carry the coop_test payload
	qboolean		localLoading;		// we sent "loading" (or are loading) and our client is not in the game yet
	qboolean		peerLoading;		// the peer said "loading": its timeout is paused until "loaded"
	int				peerLoadingSince;
	qboolean		onLinkPending;		// coop_onlink must run
	coopInQueue_t	inQueue;			// received reliable commands, executed by Coop_Frame
} coopLink_t;

extern coopLink_t	coop;
extern cvar_t		*coop_port, *coop_name, *coop_timeout, *coop_debug, *coop_autotest;

enum { coop_bad, coop_eof, coop_reliable, coop_snapshot, coop_snapack, coop_migrate /*M6*/,
	coop_test /*debug: fragmentation self-test*/, coop_cs, coop_csack, coop_view };

// Bytes a payload writer may still add to an outgoing packet (COOP_MSG_TAIL stays free for coop_eof).
static inline int Coop_MsgRoom( const msg_t *msg ) {
	return msg->maxsize - COOP_MSG_TAIL - msg->cursize;
}

void		Coop_FlushNow( void );						// sends the pending reliables right away (before a blocking load)
qboolean	Coop_LocalMapRunning( void );				// the local server runs a map (SS_GAME)
qboolean	Coop_LocalMapReady( void );					// ... and the local client is in the game (CS_ACTIVE)

void		Coop_OnLinkUp( void );
void		Coop_OnLinkDown( const char *reason );
void		Coop_WritePayload( msg_t *msg );			// write only what Coop_MsgRoom( msg ) allows
qboolean	Coop_ParsePayload( int cmd, msg_t *msg );
qboolean	Coop_ExecuteReliableCmd( const char *cmd );
// coop_session.cpp
void		Coop_SessionInit( void );
void		Coop_SessionFrame( void );					// Coop_Frame, after the inbound commands
void		Coop_SessionMapLoaded( void );				// Coop_MapLoadFinished
int			Coop_MapGen( void );						// host: running map's generation; guest: generation from coop_map
qboolean	Coop_GuestInWorld( void );					// guest: in the host's map, client in game, coop_ready sent
qboolean	Coop_HostGuestInWorld( void );				// host: the guest confirmed the running map
void		Coop_SessionMapLoading( void );				// Coop_MapLoadStarting

// coop_cs.cpp
void		CoopCS_MapLoading( void );					// host: the map's configstrings are rebuilt, versions restart
void		CoopCS_GuestReady( void );					// host: the guest entered the map, send everything
void		CoopCS_NewMap( void );						// guest: coop_map arrived, forget the old map's strings
void		CoopCS_LinkReset( void );
void		CoopCS_Write( msg_t *msg );					// host: coop_cs, guest: coop_csack
qboolean	CoopCS_Parse( msg_t *msg );					// guest, inside Com_EventLoop: only stores
qboolean	CoopCS_ParseAck( msg_t *msg );				// host
void		CoopCS_Frame( void );						// guest, Coop_Frame: applies stored strings in batches
qboolean	CoopCS_Synced( void );						// guest: the first full set is applied

// coop_snapshot.cpp
void		CoopSnapshot_Init( void );					// Coop_Init: cvars
void		CoopSnapshot_GuestReady( void );			// host: the guest entered the map, start against nothing
void		CoopSnapshot_NewMap( void );				// guest: coop_map arrived
void		CoopSnapshot_LinkReset( void );
void		CoopSnapshot_Write( msg_t *msg );			// host: coop_snapshot, guest: coop_snapack
qboolean	CoopSnapshot_Parse( msg_t *msg );			// guest, inside Com_EventLoop: only copies
qboolean	CoopSnapshot_ParseAck( msg_t *msg );		// host
void		CoopSnapshot_Frame( void );					// guest, Coop_Frame: decode and apply the newest snapshot
void		CoopView_Write( msg_t *msg );				// guest -> host: coop_view (design 8)
qboolean	CoopView_Parse( msg_t *msg );				// host
