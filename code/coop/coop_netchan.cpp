// Adapted from qcommon/net_chan.cpp (Id Software / Raven Software / OpenJK contributors, GPLv2).
// See coop_netchan.h for what differs from the engine netchan.
#include "coop_netchan.h"

#define	FRAGMENT_BIT		( 1 << 31 )
#define COOP_PACKET_HEADER	12			// sequence, ack, fragment start, fragment length

extern cvar_t	*showpackets, *showdrop;	// net_chan.cpp

static const char *netsrcString[2] = { "client", "server" };

void CoopNetchan_Setup( netsrc_t sock, coopNetchan_t *chan, netadr_t adr ) {
	memset( chan, 0, sizeof( *chan ) );
	chan->sock = sock;
	chan->remoteAddress = adr;
	chan->incomingSequence = 0;
	chan->outgoingSequence = 1;
}

static void CoopNetchan_TransmitFragments( coopNetchan_t *chan, int length, const byte *data ) {
	byte	send_buf[COOP_FRAGMENT_SIZE + COOP_PACKET_HEADER];
	msg_t	send;
	int		fragmentStart = 0, fragmentLength;

	do {
		MSG_Init( &send, send_buf, sizeof( send_buf ) );
		MSG_WriteLong( &send, chan->outgoingSequence | FRAGMENT_BIT );
		MSG_WriteLong( &send, chan->incomingSequence );

		fragmentLength = COOP_FRAGMENT_SIZE;
		if ( fragmentStart + fragmentLength > length ) {
			fragmentLength = length - fragmentStart;
		}
		MSG_WriteShort( &send, fragmentStart );
		MSG_WriteShort( &send, fragmentLength );
		MSG_WriteData( &send, data + fragmentStart, fragmentLength );

		NET_SendPacket( chan->sock, send.cursize, send.data, chan->remoteAddress );

		if ( showpackets->integer ) {
			Com_Printf( "coop %s send %4i : s=%i ack=%i fragment=%i,%i\n", netsrcString[chan->sock],
				send.cursize, chan->outgoingSequence, chan->incomingSequence, fragmentStart, fragmentLength );
		}

		fragmentStart += fragmentLength;
		// a message that is an exact multiple of the fragment size still needs a trailing
		// zero-length fragment so the other side can tell there aren't more to follow
	} while ( fragmentStart != length || fragmentLength == COOP_FRAGMENT_SIZE );

	chan->outgoingSequence++;
}

/*
Sends a message to the peer, fragmenting if necessary. A 0 length still generates a packet.
*/
void CoopNetchan_Transmit( coopNetchan_t *chan, int length, const byte *data ) {
	if ( length < 0 || length > COOP_MAX_PAYLOAD ) {
		Com_Printf( "CoopNetchan_Transmit: bad length %i, message dropped\n", length );
		return;
	}
	if ( length >= COOP_FRAGMENT_SIZE ) {
		CoopNetchan_TransmitFragments( chan, length, data );
		return;
	}

	byte	send_buf[COOP_FRAGMENT_SIZE + COOP_PACKET_HEADER];
	msg_t	send;
	MSG_Init( &send, send_buf, sizeof( send_buf ) );
	MSG_WriteLong( &send, chan->outgoingSequence );
	MSG_WriteLong( &send, chan->incomingSequence );
	chan->outgoingSequence++;
	MSG_WriteData( &send, data, length );

	NET_SendPacket( chan->sock, send.cursize, send.data, chan->remoteAddress );

	if ( showpackets->integer ) {
		Com_Printf( "coop %s send %4i : s=%i ack=%i\n", netsrcString[chan->sock],
			send.cursize, chan->outgoingSequence - 1, chan->incomingSequence );
	}
}

/*
Returns qfalse if the message should not be processed due to being out of order, duplicated,
malformed or a non-final fragment. When the last fragment arrives the whole message is copied
over msg (which must be able to hold it) and reading continues after the sequence number.
*/
qboolean CoopNetchan_Process( coopNetchan_t *chan, msg_t *msg ) {
	int			fragmentStart = 0, fragmentLength = 0;
	qboolean	fragmented = qfalse;

	MSG_BeginReading( msg );
	int sequence = MSG_ReadLong( msg );
	const int sequence_ack = MSG_ReadLong( msg );

	if ( sequence & FRAGMENT_BIT ) {
		sequence &= ~FRAGMENT_BIT;
		fragmented = qtrue;
		fragmentStart = MSG_ReadShort( msg );	// 0..65535, -1 if truncated
		fragmentLength = MSG_ReadShort( msg );
	}

	if ( showpackets->integer ) {
		if ( fragmented ) {
			Com_Printf( "coop %s recv %4i : s=%i ack=%i fragment=%i,%i\n", netsrcString[chan->sock],
				msg->cursize, sequence, sequence_ack, fragmentStart, fragmentLength );
		} else {
			Com_Printf( "coop %s recv %4i : s=%i ack=%i\n", netsrcString[chan->sock],
				msg->cursize, sequence, sequence_ack );
		}
	}

	// discard out of order or duplicated packets
	if ( sequence <= chan->incomingSequence ) {
		if ( showdrop->integer || showpackets->integer ) {
			Com_Printf( "%s:Out of order coop packet %i at %i\n",
				NET_AdrToString( chan->remoteAddress ), sequence, chan->incomingSequence );
		}
		return qfalse;
	}

	// dropped packets don't keep the message from being used
	chan->dropped = sequence - ( chan->incomingSequence + 1 );
	if ( chan->dropped > 0 && ( showdrop->integer || showpackets->integer ) ) {
		Com_Printf( "%s:Dropped %i coop packets at %i\n",
			NET_AdrToString( chan->remoteAddress ), chan->dropped, sequence );
	}

	if ( fragmented ) {
		if ( sequence != chan->fragmentSequence ) {
			chan->fragmentSequence = sequence;
			chan->fragmentLength = 0;
		}

		// if we missed a fragment, dump the message (keep the part we have so far)
		if ( fragmentStart != chan->fragmentLength ) {
			if ( showdrop->integer || showpackets->integer ) {
				Com_Printf( "%s:Dropped a coop message fragment %i\n", NET_AdrToString( chan->remoteAddress ), sequence );
			}
			return qfalse;
		}

		if ( fragmentLength < 0 || msg->readcount + fragmentLength > msg->cursize ||
			chan->fragmentLength + fragmentLength > (int)sizeof( chan->fragmentBuffer ) ) {
			if ( showdrop->integer || showpackets->integer ) {
				Com_Printf( "%s:illegal coop fragment length\n", NET_AdrToString( chan->remoteAddress ) );
			}
			return qfalse;
		}

		memcpy( chan->fragmentBuffer + chan->fragmentLength, msg->data + msg->readcount, fragmentLength );
		chan->fragmentLength += fragmentLength;

		// if this wasn't the last fragment, don't process anything
		if ( fragmentLength == COOP_FRAGMENT_SIZE ) {
			return qfalse;
		}

		// the 4-byte sequence goes in front of the reassembled message (vanilla checks only
		// fragmentLength > maxsize, which lets a peer overflow msg by 4 bytes)
		if ( chan->fragmentLength + 4 > msg->maxsize ) {
			Com_Printf( "%s:coop fragmentLength %i > msg->maxsize\n", NET_AdrToString( chan->remoteAddress ), chan->fragmentLength );
			chan->fragmentLength = 0;
			return qfalse;
		}

		*(int *)msg->data = LittleLong( sequence );
		memcpy( msg->data + 4, chan->fragmentBuffer, chan->fragmentLength );
		msg->cursize = chan->fragmentLength + 4;
		msg->readcount = 4;	// past the sequence number
		msg->bit = 0;
		chan->fragmentLength = 0;
	}

	// vanilla skips this for reassembled messages, so stale packets would be accepted afterwards
	chan->incomingSequence = sequence;
	chan->incomingAcknowledged = sequence_ack;
	return qtrue;
}
