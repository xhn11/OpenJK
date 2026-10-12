#include "coop_test.h"
#include "coop_delta.h"
#include <cstring>
#include <cstdint>

struct DeltaBlob { uint32_t w[70]; };		// 280 bytes = 3 blocks of 32 words

static DeltaBlob MakeBlob( uint32_t seed ) {
	DeltaBlob b;
	for ( int i = 0; i < 70; i++ ) {
		b.w[i] = seed * 2654435761u + (uint32_t)i;
	}
	return b;
}

COOP_TEST( delta_identical_is_bitmap_only ) {
	const DeltaBlob a = MakeBlob( 1 ), b = a;
	uint8_t buf[512];
	const int n = CoopDelta_Encode( &a, &b, sizeof( DeltaBlob ), buf, sizeof( buf ) );
	CHECK_EQ( n, 1 );						// 3 blocks -> 1 bitmap byte
	CHECK_EQ( buf[0], 0 );
	DeltaBlob c;
	memset( &c, 0xcd, sizeof( c ) );
	CHECK_EQ( CoopDelta_Decode( &a, &c, sizeof( DeltaBlob ), buf, n ), 1 );
	CHECK( !memcmp( &a, &c, sizeof( a ) ) );
}
COOP_TEST( delta_roundtrip_changes_in_several_blocks ) {
	const DeltaBlob a = MakeBlob( 1 );
	DeltaBlob b = a;
	b.w[0] ^= 1;
	b.w[31] = 7;
	b.w[40] = 0;
	b.w[69] = 0xdeadbeefu;
	uint8_t buf[512];
	const int n = CoopDelta_Encode( &a, &b, sizeof( DeltaBlob ), buf, sizeof( buf ) );
	CHECK_EQ( n, 1 + ( 4 + 2 * 4 ) + ( 4 + 4 ) + ( 4 + 4 ) );	// bitmap + block 0 (2 words) + block 1 + block 2
	DeltaBlob c;
	CHECK_EQ( CoopDelta_Decode( &a, &c, sizeof( DeltaBlob ), buf, n ), n );
	CHECK( !memcmp( &b, &c, sizeof( b ) ) );
}
COOP_TEST( delta_from_null_means_zeros ) {
	DeltaBlob b;
	memset( &b, 0, sizeof( b ) );
	b.w[5] = 9;
	uint8_t buf[512];
	const int n = CoopDelta_Encode( nullptr, &b, sizeof( DeltaBlob ), buf, sizeof( buf ) );
	CHECK_EQ( n, 1 + 4 + 4 );
	DeltaBlob c;
	memset( &c, 0x11, sizeof( c ) );
	CHECK_EQ( CoopDelta_Decode( nullptr, &c, sizeof( DeltaBlob ), buf, n ), n );
	CHECK( !memcmp( &b, &c, sizeof( b ) ) );
}
COOP_TEST( delta_decodes_in_place ) {
	const DeltaBlob a = MakeBlob( 3 );
	DeltaBlob b = a;
	b.w[33] = 1;
	uint8_t buf[512];
	const int n = CoopDelta_Encode( &a, &b, sizeof( DeltaBlob ), buf, sizeof( buf ) );
	DeltaBlob c = a;
	CHECK_EQ( CoopDelta_Decode( &c, &c, sizeof( DeltaBlob ), buf, n ), n );
	CHECK( !memcmp( &b, &c, sizeof( b ) ) );
}
COOP_TEST( delta_truncated_input_fails ) {
	const DeltaBlob a = MakeBlob( 1 );
	DeltaBlob b = a;
	b.w[10] = 0; b.w[50] = 0;
	uint8_t buf[512];
	const int n = CoopDelta_Encode( &a, &b, sizeof( DeltaBlob ), buf, sizeof( buf ) );
	DeltaBlob c;
	for ( int len = 0; len < n; len++ ) {
		CHECK_EQ( CoopDelta_Decode( &a, &c, sizeof( DeltaBlob ), buf, len ), -1 );
	}
}
COOP_TEST( delta_small_output_fails ) {
	const DeltaBlob a = MakeBlob( 1 );
	DeltaBlob b = a;
	b.w[10] = 0;
	uint8_t buf[512];
	const int n = CoopDelta_Encode( &a, &b, sizeof( DeltaBlob ), buf, sizeof( buf ) );
	CHECK_EQ( CoopDelta_Encode( &a, &b, sizeof( DeltaBlob ), buf, n - 1 ), -1 );
	CHECK_EQ( CoopDelta_Encode( &a, &b, sizeof( DeltaBlob ), buf, 0 ), -1 );
}
COOP_TEST( delta_mask_beyond_size_fails ) {
	const uint32_t from[2] = { 0, 0 };
	uint32_t to[2];
	const uint8_t in[] = { 0x01, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00 };	// block 0, word 2 of a 2-word struct
	CHECK_EQ( CoopDelta_Decode( from, to, sizeof( to ), in, sizeof( in ) ), -1 );
}
COOP_TEST( delta_bitmap_beyond_blocks_fails ) {
	uint32_t to[2];
	const uint8_t in[] = { 0x02 };													// block 1 does not exist
	CHECK_EQ( CoopDelta_Decode( nullptr, to, sizeof( to ), in, sizeof( in ) ), -1 );
}
COOP_TEST( delta_empty_mask_fails ) {
	uint32_t to[2];
	const uint8_t in[] = { 0x01, 0x00, 0x00, 0x00, 0x00 };							// marked block with no words
	CHECK_EQ( CoopDelta_Decode( nullptr, to, sizeof( to ), in, sizeof( in ) ), -1 );
}
COOP_TEST( delta_bad_size_fails ) {
	uint8_t buf[64];
	const uint32_t x[2] = { 1, 2 };
	CHECK_EQ( CoopDelta_Encode( nullptr, x, 6, buf, sizeof( buf ) ), -1 );
	CHECK_EQ( CoopDelta_Encode( nullptr, x, 0, buf, sizeof( buf ) ), -1 );
}
COOP_TEST( delta_max_size_bounds_encode ) {
	const DeltaBlob a = MakeBlob( 1 ), b = MakeBlob( 2 );
	uint8_t buf[512];
	const int n = CoopDelta_Encode( &a, &b, sizeof( DeltaBlob ), buf, sizeof( buf ) );
	CHECK_EQ( n, CoopDelta_MaxSize( sizeof( DeltaBlob ) ) );
	CHECK_EQ( CoopDelta_MaxSize( sizeof( DeltaBlob ) ), 1 + 3 * 4 + 70 * 4 );
}
