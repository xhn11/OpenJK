#include "coop_test.h"
#include "coop_netsim.h"
#include <cstring>
#include <memory>

static std::unique_ptr<coopNetSim_t> NewSim() {
	std::unique_ptr<coopNetSim_t> s( new coopNetSim_t );
	CoopSim_Init( s.get(), 1 );
	return s;
}

COOP_TEST( sim_drop_extremes ) {
	auto s = NewSim();
	for ( int i = 0; i < 1000; i++ ) {
		CHECK( !CoopSim_Drop( s.get(), 0 ) );
		CHECK( CoopSim_Drop( s.get(), 100 ) );
	}
}
COOP_TEST( sim_drop_rate_is_close ) {
	auto s = NewSim();
	int dropped = 0;
	for ( int i = 0; i < 10000; i++ ) {
		if ( CoopSim_Drop( s.get(), 10 ) ) {
			dropped++;
		}
	}
	CHECK( dropped > 800 && dropped < 1200 );
}
COOP_TEST( sim_releases_in_order_when_due ) {
	auto s = NewSim();
	const char a[] = "first", b[] = "second";
	const int addr = 42;
	CHECK( CoopSim_Push( s.get(), 100, a, sizeof( a ), &addr, sizeof( addr ) ) );
	CHECK( CoopSim_Push( s.get(), 150, b, sizeof( b ), &addr, sizeof( addr ) ) );
	CHECK( CoopSim_Due( s.get(), 99 ) == nullptr );
	const coopSimPacket_t *p = CoopSim_Due( s.get(), 100 );
	CHECK( p != nullptr );
	if ( p ) {
		CHECK_EQ( p->length, (int)sizeof( a ) );
		CHECK( !memcmp( p->data, a, sizeof( a ) ) );
		int got = 0;
		memcpy( &got, p->addr, sizeof( got ) );
		CHECK_EQ( got, 42 );
	}
	CoopSim_Pop( s.get() );
	CHECK( CoopSim_Due( s.get(), 120 ) == nullptr );
	p = CoopSim_Due( s.get(), 150 );
	CHECK( p != nullptr );
	if ( p ) {
		CHECK( !memcmp( p->data, b, sizeof( b ) ) );
	}
	CoopSim_Pop( s.get() );
	CHECK( CoopSim_Due( s.get(), 1000 ) == nullptr );
}
COOP_TEST( sim_rejects_full_and_oversize ) {
	auto s = NewSim();
	static char big[COOP_SIM_MAX_BYTES + 1];
	const int addr = 0;
	CHECK( !CoopSim_Push( s.get(), 0, big, (int)sizeof( big ), &addr, sizeof( addr ) ) );
	for ( int i = 0; i < COOP_SIM_MAX_PACKETS; i++ ) {
		CHECK( CoopSim_Push( s.get(), i, big, 10, &addr, sizeof( addr ) ) );
	}
	CHECK( !CoopSim_Push( s.get(), 0, big, 10, &addr, sizeof( addr ) ) );
	CoopSim_Pop( s.get() );
	CHECK( CoopSim_Push( s.get(), 0, big, 10, &addr, sizeof( addr ) ) );
}
