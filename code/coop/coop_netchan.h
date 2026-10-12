#pragma once
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

// Coop-owned copy of the engine netchan (qcommon/net_chan.cpp), which stays untouched for the
// loopback client/server. Differences: larger messages, MTU-sized fragments, no qport field
// (the coop link identifies its peer by address + challenge) and two reassembly fixes.
//
// packet header
//   4  outgoing sequence, high bit set for a fragment
//   4  acknowledge sequence
//  [2  fragment start  (unsigned)]
//  [2  fragment length (unsigned), < COOP_FRAGMENT_SIZE marks the last fragment]

#define COOP_MAX_MSGLEN			65536
#define COOP_FRAGMENT_SIZE		1200							// + 12 header + 28 IP/UDP fits an MTU of 1280 (Tailscale, ZeroTier)
#define COOP_MAX_PAYLOAD		( COOP_MAX_MSGLEN - 4 )			// reassembly prepends the 4-byte sequence

typedef struct {
	netsrc_t	sock;
	int			dropped;				// between last packet and previous
	netadr_t	remoteAddress;

	int			incomingSequence;
	int			incomingAcknowledged;
	int			outgoingSequence;

	int			fragmentSequence;		// incoming fragment assembly
	int			fragmentLength;
	byte		fragmentBuffer[COOP_MAX_MSGLEN];
} coopNetchan_t;

void		CoopNetchan_Setup( netsrc_t sock, coopNetchan_t *chan, netadr_t adr );
void		CoopNetchan_Transmit( coopNetchan_t *chan, int length, const byte *data );	// length <= COOP_MAX_PAYLOAD
qboolean	CoopNetchan_Process( coopNetchan_t *chan, msg_t *msg );						// msg->maxsize must be COOP_MAX_MSGLEN
