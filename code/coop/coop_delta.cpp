#include "coop_delta.h"
#include <cstring>

static uint32_t Coop_ReadLE( const uint8_t *p ) {
	return (uint32_t)p[0] | ( (uint32_t)p[1] << 8 ) | ( (uint32_t)p[2] << 16 ) | ( (uint32_t)p[3] << 24 );
}

static void Coop_WriteLE( uint8_t *p, uint32_t v ) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)( v >> 8 );
	p[2] = (uint8_t)( v >> 16 );
	p[3] = (uint8_t)( v >> 24 );
}

static uint32_t Coop_Word( const void *base, int index ) {
	if ( !base ) {
		return 0;
	}
	uint32_t w;
	memcpy( &w, (const uint8_t *)base + index * 4, 4 );
	return w;
}

static int Coop_BitCount( uint32_t v ) {
	int n = 0;
	for ( ; v; v &= v - 1 ) {
		n++;
	}
	return n;
}

int CoopDelta_MaxSize( int size ) {
	const int words = size / 4, blocks = ( words + 31 ) / 32;
	return ( blocks + 7 ) / 8 + blocks * 4 + words * 4;
}

int CoopDelta_Encode( const void *from, const void *to, int size, uint8_t *out, int outMax ) {
	if ( size <= 0 || size % 4 || !to || !out ) {
		return -1;
	}
	const int words = size / 4, blocks = ( words + 31 ) / 32, bitmapBytes = ( blocks + 7 ) / 8;
	if ( outMax < bitmapBytes ) {
		return -1;
	}
	memset( out, 0, bitmapBytes );
	int pos = bitmapBytes;
	for ( int b = 0; b < blocks; b++ ) {
		const int first = b * 32;
		const int count = words - first < 32 ? words - first : 32;
		uint32_t mask = 0;
		for ( int i = 0; i < count; i++ ) {
			if ( Coop_Word( from, first + i ) != Coop_Word( to, first + i ) ) {
				mask |= 1u << i;
			}
		}
		if ( !mask ) {
			continue;
		}
		if ( pos + 4 + 4 * Coop_BitCount( mask ) > outMax ) {
			return -1;
		}
		out[b >> 3] |= (uint8_t)( 1u << ( b & 7 ) );
		Coop_WriteLE( out + pos, mask );
		pos += 4;
		for ( int i = 0; i < count; i++ ) {
			if ( mask & ( 1u << i ) ) {
				Coop_WriteLE( out + pos, Coop_Word( to, first + i ) );
				pos += 4;
			}
		}
	}
	return pos;
}

int CoopDelta_Decode( const void *from, void *to, int size, const uint8_t *in, int inLen ) {
	if ( size <= 0 || size % 4 || !to || !in ) {
		return -1;
	}
	const int words = size / 4, blocks = ( words + 31 ) / 32, bitmapBytes = ( blocks + 7 ) / 8;
	if ( inLen < bitmapBytes ) {
		return -1;
	}
	for ( int b = blocks; b < bitmapBytes * 8; b++ ) {
		if ( in[b >> 3] & ( 1u << ( b & 7 ) ) ) {
			return -1;	// a block past the end of the structure
		}
	}
	if ( !from ) {
		memset( to, 0, size );
	} else if ( from != to ) {
		memmove( to, from, size );
	}
	int pos = bitmapBytes;
	for ( int b = 0; b < blocks; b++ ) {
		if ( !( in[b >> 3] & ( 1u << ( b & 7 ) ) ) ) {
			continue;
		}
		if ( pos + 4 > inLen ) {
			return -1;
		}
		const uint32_t mask = Coop_ReadLE( in + pos );
		pos += 4;
		const int first = b * 32;
		const int count = words - first < 32 ? words - first : 32;
		if ( !mask || ( count < 32 && ( mask >> count ) ) ) {
			return -1;	// the encoder never writes an empty mask or words past the end
		}
		for ( int i = 0; i < count; i++ ) {
			if ( !( mask & ( 1u << i ) ) ) {
				continue;
			}
			if ( pos + 4 > inLen ) {
				return -1;
			}
			const uint32_t w = Coop_ReadLE( in + pos );
			memcpy( (uint8_t *)to + ( first + i ) * 4, &w, 4 );
			pos += 4;
		}
	}
	return pos;
}
