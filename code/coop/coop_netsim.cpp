#include "coop_netsim.h"
#include <cstring>

void CoopSim_Init( coopNetSim_t *s, unsigned seed ) {
	s->head = 0;
	s->count = 0;
	s->rng = seed;
}

bool CoopSim_Drop( coopNetSim_t *s, int lossPercent ) {
	s->rng = s->rng * 1103515245u + 12345u;
	return (int)( ( s->rng >> 16 ) % 100u ) < lossPercent;
}

bool CoopSim_Push( coopNetSim_t *s, int releaseTime, const void *data, int length, const void *addr, int addrSize ) {
	if ( s->count >= COOP_SIM_MAX_PACKETS || length < 0 || length > COOP_SIM_MAX_BYTES
		|| addrSize < 0 || addrSize > COOP_SIM_ADDR_BYTES ) {
		return false;
	}
	coopSimPacket_t *p = &s->queue[( s->head + s->count ) % COOP_SIM_MAX_PACKETS];
	p->releaseTime = releaseTime;
	p->length = length;
	memcpy( p->data, data, length );
	memset( p->addr, 0, sizeof( p->addr ) );
	memcpy( p->addr, addr, addrSize );
	s->count++;
	return true;
}

const coopSimPacket_t *CoopSim_Due( const coopNetSim_t *s, int now ) {
	if ( !s->count ) {
		return nullptr;
	}
	const coopSimPacket_t *p = &s->queue[s->head];
	return now - p->releaseTime >= 0 ? p : nullptr;
}

void CoopSim_Pop( coopNetSim_t *s ) {
	if ( s->count ) {
		s->head = ( s->head + 1 ) % COOP_SIM_MAX_PACKETS;
		s->count--;
	}
}
