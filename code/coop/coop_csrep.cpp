#include "coop_csrep.h"
#include <algorithm>
#include <cstring>

void CoopCs_Reset( coopCsRep_t *r ) {
	memset( r, 0, sizeof( *r ) );
}

void CoopCs_Touch( coopCsRep_t *r, int index ) {
	if ( index < 0 || index >= COOP_CS_MAX ) {
		return;
	}
	r->version[index] = ++r->seq;
}

int CoopCs_Select( const coopCsRep_t *r, int acked, int *out, int maxOut ) {
	int changed[COOP_CS_MAX];
	int count = 0;
	for ( int i = 0; i < COOP_CS_MAX; i++ ) {
		if ( r->version[i] > acked ) {
			changed[count++] = i;
		}
	}
	std::sort( changed, changed + count, [r]( int a, int b ) { return r->version[a] < r->version[b]; } );
	const int n = count < maxOut ? count : maxOut;
	memcpy( out, changed, n * sizeof( int ) );
	return n;
}
