#pragma once
// Delta codec for fixed-size structures, word by word. Pure module (unit tested).
//   bitmap: one bit per block of 32 words, ceil(blocks / 8) bytes
//   for every block with its bit set: uint32 mask (bit i = word i of the block changed, little endian),
//   then the changed words (little endian), in order
// from == nullptr means a structure of zeros. size must be a multiple of 4.
#include <cstdint>

int		CoopDelta_MaxSize( int size );
int		CoopDelta_Encode( const void *from, const void *to, int size, uint8_t *out, int outMax );	// bytes written, -1 = no room or bad size
int		CoopDelta_Decode( const void *from, void *to, int size, const uint8_t *in, int inLen );	// bytes consumed, -1 = malformed (to is then undefined)
