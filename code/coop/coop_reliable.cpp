#include "coop_reliable.h"
#include <cstring>

void CoopRel_Init( coopReliable_t *r ) {
	memset( r, 0, sizeof( *r ) );
}

bool CoopRel_Add( coopReliable_t *r, const char *cmd ) {
	const size_t len = strlen( cmd );
	if ( len >= COOP_MAX_RELIABLE_LEN || r->outSequence - r->outAcknowledged >= COOP_MAX_RELIABLE ) {
		return false;
	}
	r->outSequence++;
	memcpy( r->outCmds[r->outSequence % COOP_MAX_RELIABLE], cmd, len + 1 );
	return true;
}

void CoopRel_Ack( coopReliable_t *r, int ack ) {
	if ( ack > r->outAcknowledged && ack <= r->outSequence ) {
		r->outAcknowledged = ack;
	}
}

int CoopRel_PendingCount( const coopReliable_t *r ) {
	return r->outSequence - r->outAcknowledged;
}

const char *CoopRel_Pending( const coopReliable_t *r, int index, int *seq ) {
	const int s = r->outAcknowledged + 1 + index;
	if ( index < 0 || s > r->outSequence ) {
		return nullptr;
	}
	if ( seq ) {
		*seq = s;
	}
	return r->outCmds[s % COOP_MAX_RELIABLE];
}

bool CoopRel_Accept( coopReliable_t *r, int seq ) {
	if ( seq != r->inSequence + 1 ) {
		return false;
	}
	r->inSequence = seq;
	return true;
}

int CoopRel_FreeSlots( const coopReliable_t *r ) {
	return COOP_MAX_RELIABLE - ( r->outSequence - r->outAcknowledged );
}

void CoopInQ_Init( coopInQueue_t *q ) {
	q->head = 0;
	q->count = 0;
}

bool CoopInQ_Full( const coopInQueue_t *q ) {
	return q->count >= COOP_MAX_INBOUND;
}

bool CoopInQ_Push( coopInQueue_t *q, const char *cmd ) {
	const size_t len = strlen( cmd );
	if ( CoopInQ_Full( q ) || len >= COOP_MAX_RELIABLE_LEN ) {
		return false;
	}
	memcpy( q->cmds[( q->head + q->count ) % COOP_MAX_INBOUND], cmd, len + 1 );
	q->count++;
	return true;
}

const char *CoopInQ_Front( const coopInQueue_t *q ) {
	return q->count ? q->cmds[q->head] : nullptr;
}

void CoopInQ_Pop( coopInQueue_t *q ) {
	if ( q->count ) {
		q->head = ( q->head + 1 ) % COOP_MAX_INBOUND;
		q->count--;
	}
}
