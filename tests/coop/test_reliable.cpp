#include "coop_test.h"
#include "coop_reliable.h"
#include <cstring>
#include <cstdio>
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

COOP_TEST( rel_free_slots_track_pending ) {
	auto r = NewRel();
	CHECK_EQ( CoopRel_FreeSlots( r.get() ), COOP_MAX_RELIABLE );
	CoopRel_Add( r.get(), "a" );
	CoopRel_Add( r.get(), "b" );
	CHECK_EQ( CoopRel_FreeSlots( r.get() ), COOP_MAX_RELIABLE - 2 );
	CoopRel_Ack( r.get(), 2 );
	CHECK_EQ( CoopRel_FreeSlots( r.get() ), COOP_MAX_RELIABLE );
}

static std::unique_ptr<coopInQueue_t> NewInQ() {
	std::unique_ptr<coopInQueue_t> q( new coopInQueue_t );
	CoopInQ_Init( q.get() );
	return q;
}

COOP_TEST( inq_fifo_order ) {
	auto q = NewInQ();
	CHECK( CoopInQ_Front( q.get() ) == nullptr );
	CHECK( CoopInQ_Push( q.get(), "a" ) );
	CHECK( CoopInQ_Push( q.get(), "b" ) );
	CHECK( !strcmp( CoopInQ_Front( q.get() ), "a" ) );
	CoopInQ_Pop( q.get() );
	CHECK( !strcmp( CoopInQ_Front( q.get() ), "b" ) );
	CoopInQ_Pop( q.get() );
	CHECK( CoopInQ_Front( q.get() ) == nullptr );
	CoopInQ_Pop( q.get() );	// popping an empty queue is harmless
	CHECK( CoopInQ_Front( q.get() ) == nullptr );
}
COOP_TEST( inq_full_rejects_until_popped ) {
	auto q = NewInQ();
	for ( int i = 0; i < COOP_MAX_INBOUND; i++ ) CHECK( CoopInQ_Push( q.get(), "x" ) );
	CHECK( CoopInQ_Full( q.get() ) );
	CHECK( !CoopInQ_Push( q.get(), "y" ) );
	CoopInQ_Pop( q.get() );
	CHECK( !CoopInQ_Full( q.get() ) );
	CHECK( CoopInQ_Push( q.get(), "y" ) );
}
COOP_TEST( inq_wraps_around ) {
	auto q = NewInQ();
	char text[16];
	for ( int i = 0; i < 3 * COOP_MAX_INBOUND; i++ ) {
		snprintf( text, sizeof( text ), "c%d", i );
		CHECK( CoopInQ_Push( q.get(), text ) );
		CHECK( !strcmp( CoopInQ_Front( q.get() ), text ) );
		CoopInQ_Pop( q.get() );
	}
}
COOP_TEST( inq_rejects_too_long ) {
	auto q = NewInQ();
	std::string longCmd( COOP_MAX_RELIABLE_LEN, 'x' );
	CHECK( !CoopInQ_Push( q.get(), longCmd.c_str() ) );
	CHECK( CoopInQ_Front( q.get() ) == nullptr );
}
