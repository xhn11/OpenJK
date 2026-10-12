#include "coop_test.h"
#include "coop_reliable.h"
#include <cstring>
#include <string>
#include <memory>

static std::unique_ptr<coopReliable_t> NewRel() {
	std::unique_ptr<coopReliable_t> r( new coopReliable_t );
	CoopRel_Init( r.get() );
	return r;
}

COOP_TEST( rel_pending_in_order ) {
	auto r = NewRel();
	CHECK( CoopRel_Add( r.get(), "a" ) );
	CHECK( CoopRel_Add( r.get(), "b" ) );
	CHECK_EQ( CoopRel_PendingCount( r.get() ), 2 );
	int seq = 0;
	CHECK( !strcmp( CoopRel_Pending( r.get(), 0, &seq ), "a" ) ); CHECK_EQ( seq, 1 );
	CHECK( !strcmp( CoopRel_Pending( r.get(), 1, &seq ), "b" ) ); CHECK_EQ( seq, 2 );
	CHECK( CoopRel_Pending( r.get(), 2, &seq ) == nullptr );
}
COOP_TEST( rel_ack_removes_pending_and_ignores_bogus ) {
	auto r = NewRel();
	CoopRel_Add( r.get(), "a" );
	CoopRel_Add( r.get(), "b" );
	CoopRel_Ack( r.get(), 1 );
	CHECK_EQ( CoopRel_PendingCount( r.get() ), 1 );
	CoopRel_Ack( r.get(), 5 );	// future ack
	CoopRel_Ack( r.get(), 0 );	// stale ack
	CHECK_EQ( CoopRel_PendingCount( r.get() ), 1 );
	int seq = 0;
	CHECK( !strcmp( CoopRel_Pending( r.get(), 0, &seq ), "b" ) ); CHECK_EQ( seq, 2 );
}
COOP_TEST( rel_accept_only_next_sequence ) {
	auto r = NewRel();
	CHECK( CoopRel_Accept( r.get(), 1 ) );
	CHECK( !CoopRel_Accept( r.get(), 1 ) );	// duplicate
	CHECK( !CoopRel_Accept( r.get(), 3 ) );	// gap
	CHECK( CoopRel_Accept( r.get(), 2 ) );
}
COOP_TEST( rel_overflow_returns_false ) {
	auto r = NewRel();
	for ( int i = 0; i < COOP_MAX_RELIABLE; i++ ) CHECK( CoopRel_Add( r.get(), "x" ) );
	CHECK( !CoopRel_Add( r.get(), "x" ) );
	CoopRel_Ack( r.get(), 1 );
	CHECK( CoopRel_Add( r.get(), "y" ) );
}
COOP_TEST( rel_rejects_too_long_command ) {
	auto r = NewRel();
	std::string longCmd( COOP_MAX_RELIABLE_LEN, 'x' );
	CHECK( !CoopRel_Add( r.get(), longCmd.c_str() ) );
	CHECK_EQ( CoopRel_PendingCount( r.get() ), 0 );
}
