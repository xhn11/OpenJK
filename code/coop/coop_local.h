#pragma once
#include "../server/exe_headers.h"
#include "../server/server.h"
#include "coop_public.h"
#include "coop_reliable.h"

#define COOP_PROTOCOL_VERSION		1
#define COOP_DEFAULT_PORT			29170
#define COOP_CONNECT_RESEND_MS		1000
#define COOP_CONNECT_TIMEOUT_MS		15000
#define COOP_PACKET_INTERVAL_MS		33
// MSG_ReadString stops at MAX_STRING_CHARS-1 chars without consuming the NUL, so 1022 is the longest string that round-trips
#define COOP_MAX_WIRE_CMD_LEN		( MAX_STRING_CHARS - 2 )

typedef enum { COOP_LINK_IDLE, COOP_LINK_LISTENING, COOP_LINK_CONNECTING, COOP_LINK_ACTIVE } coopLinkState_t;

typedef struct {
	coopRole_t		role;
	coopLinkState_t	state;
	netadr_t		peer;
	netchan_t		chan;
	int				challenge;
	int				qport;
	int				connectStart;
	int				lastSent;
	int				lastReceived;
	char			peerName[64];
	coopReliable_t	rel;
} coopLink_t;

extern coopLink_t	coop;
extern cvar_t		*coop_port, *coop_name, *coop_timeout, *coop_debug;

enum { coop_bad, coop_eof, coop_reliable, coop_snapshot /*M3*/, coop_snapack /*M3*/, coop_migrate /*M6*/ };

void		Coop_OnLinkUp( void );
void		Coop_OnLinkDown( const char *reason );
void		Coop_WritePayload( msg_t *msg );
qboolean	Coop_ParsePayload( int cmd, msg_t *msg );
qboolean	Coop_ExecuteReliableCmd( const char *cmd );
