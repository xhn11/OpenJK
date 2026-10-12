#include "coop_test.h"
#include "coop_csrep.h"
#include <memory>

static std::unique_ptr<coopCsRep_t> NewRep() {
	std::unique_ptr<coopCsRep_t> r( new coopCsRep_t );
	CoopCs_Reset( r.get() );
	return r;
}

COOP_TEST( csrep_nothing_after_reset ) {
	auto r = NewRep();
	int out[8];
	CHECK_EQ( CoopCs_Select( r.get(), 0, out, 8 ), 0 );
}
COOP_TEST( csrep_oldest_change_first ) {
	auto r = NewRep();
	CoopCs_Touch( r.get(), 5 );		// version 1
	CoopCs_Touch( r.get(), 3 );		// version 2
	CoopCs_Touch( r.get(), 5 );		// version 3 (5 changed again)
	int out[8];
	CHECK_EQ( CoopCs_Select( r.get(), 0, out, 8 ), 2 );
	CHECK_EQ( out[0], 3 );
	CHECK_EQ( out[1], 5 );
	CHECK_EQ( r->version[3], 2 );
	CHECK_EQ( r->version[5], 3 );
}
COOP_TEST( csrep_select_after_ack ) {
	auto r = NewRep();
	CoopCs_Touch( r.get(), 10 );
	CoopCs_Touch( r.get(), 20 );
	CoopCs_Touch( r.get(), 30 );
	int out[8];
	CHECK_EQ( CoopCs_Select( r.get(), 2, out, 8 ), 1 );
	CHECK_EQ( out[0], 30 );
	CHECK_EQ( CoopCs_Select( r.get(), 3, out, 8 ), 0 );
}
COOP_TEST( csrep_select_respects_max ) {
	auto r = NewRep();
	for ( int i = 0; i < 10; i++ ) CoopCs_Touch( r.get(), 100 + i );
	int out[3];
	CHECK_EQ( CoopCs_Select( r.get(), 0, out, 3 ), 3 );
	CHECK_EQ( out[0], 100 );
	CHECK_EQ( out[2], 102 );
}
COOP_TEST( csrep_ignores_bad_index ) {
	auto r = NewRep();
	CoopCs_Touch( r.get(), -1 );
	CoopCs_Touch( r.get(), COOP_CS_MAX );
	int out[8];
	CHECK_EQ( CoopCs_Select( r.get(), 0, out, 8 ), 0 );
	CHECK_EQ( r->seq, 0 );
}
