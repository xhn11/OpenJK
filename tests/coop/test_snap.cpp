#include "coop_test.h"
#include "coop_snap.h"
#include <cstring>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

const int ES = 8, PS = 8, EX = 4;

struct TestEnt {
	int			num;
	uint32_t	es[2];
	bool		hasPs;
	uint32_t	ps[2];
	uint32_t	ex;
};

// keeps the pointer arrays coopSnapInput_t needs alive; ents must outlive it
struct TestInput {
	std::vector<int>				nums;
	std::vector<const uint8_t *>	es, ps, ex;
	coopSnapInput_t					in;
	explicit TestInput( const std::vector<TestEnt> &ents ) {
		for ( const TestEnt &e : ents ) {
			nums.push_back( e.num );
			es.push_back( (const uint8_t *)e.es );
			ps.push_back( e.hasPs ? (const uint8_t *)e.ps : nullptr );
			ex.push_back( (const uint8_t *)&e.ex );
		}
		in.count = (int)nums.size();
		in.nums = nums.data();
		in.es = es.data();
		in.ps = ps.data();
		in.ex = ex.data();
	}
};

struct TestRing {
	coopSnapRing_t r;
	TestRing() { CHECK( CoopSnap_Init( &r, ES, PS, EX, 2 * COOP_SNAP_MAX_NUM, 2 * COOP_SNAP_MAX_PS ) ); }
	~TestRing() { CoopSnap_Shutdown( &r ); }
};

struct Change {
	int			num, kind;
	bool		hasPs;
	uint32_t	es1, ps0, ex;
};

void Collect( void *ctx, int num, int kind, const uint8_t *es, const uint8_t *ps, const uint8_t *ex ) {
	Change c = { num, kind, ps != nullptr, 0, 0, 0 };
	if ( es ) memcpy( &c.es1, es + 4, 4 );
	if ( ps ) memcpy( &c.ps0, ps, 4 );
	if ( ex ) memcpy( &c.ex, ex, 4 );
	( (std::vector<Change> *)ctx )->push_back( c );
}

std::vector<TestEnt> ThreeEnts() {
	return {
		{ 2, { 10, 11 }, false, { 0, 0 }, 5 },
		{ 7, { 20, 21 }, true, { 30, 31 }, 6 },
		{ 900, { 40, 41 }, false, { 0, 0 }, 7 },
	};
}

} // namespace

COOP_TEST( snap_full_frame_roundtrip ) {
	TestRing host, guest;
	const std::vector<TestEnt> ents = ThreeEnts();
	TestInput in( ents );
	uint8_t buf[4096];
	coopSnapWriteInfo_t info;
	const int n = CoopSnap_Write( &host.r, 1, -1, 5000, &in.in, buf, sizeof( buf ), &info );
	CHECK( n > COOP_SNAP_HEADER );
	CHECK_EQ( info.records, 3 );
	CHECK_EQ( info.deferred, 0 );
	CHECK_EQ( info.base, -1 );
	CHECK_EQ( info.entities, 3 );
	int frame = 0;
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	CHECK_EQ( frame, 1 );
	const coopSnapFrame_t *f = CoopSnap_Frame( &guest.r, 1 );
	CHECK( f != nullptr );
	if ( f ) {
		CHECK_EQ( f->serverTime, 5000 );
		CHECK_EQ( f->flags, COOP_SNAPF_COMPLETE );
		CHECK_EQ( f->numEnts, 3 );
	}
	std::vector<Change> ch;
	CHECK_EQ( CoopSnap_Diff( &guest.r, -1, 1, Collect, &ch ), 1 );
	CHECK_EQ( (int)ch.size(), 3 );
	if ( ch.size() == 3 ) {
		CHECK_EQ( ch[0].num, 2 );   CHECK_EQ( ch[0].kind, COOP_SNAPCHG_ADDED ); CHECK_EQ( ch[0].es1, 11 ); CHECK( !ch[0].hasPs );
		CHECK_EQ( ch[1].num, 7 );   CHECK( ch[1].hasPs ); CHECK_EQ( ch[1].ps0, 30 );
		CHECK_EQ( ch[2].num, 900 ); CHECK_EQ( ch[2].ex, 7 );
	}
}

COOP_TEST( snap_delta_changed_removed_added ) {
	TestRing host, guest;
	std::vector<TestEnt> ents = ThreeEnts();
	uint8_t buf[4096];
	int frame = 0;
	{
		TestInput in( ents );
		const int n = CoopSnap_Write( &host.r, 1, -1, 5000, &in.in, buf, sizeof( buf ), nullptr );
		CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	}
	ents[1].es[1] = 99;							// 7 changed
	ents.pop_back();							// 900 removed
	ents.push_back( { 10, { 1, 2 }, false, { 0, 0 }, 3 } );	// 10 added; input is 2, 7, 10 (ascending)
	coopSnapWriteInfo_t info;
	TestInput in2( ents );
	const int n2 = CoopSnap_Write( &host.r, 2, 1, 5050, &in2.in, buf, sizeof( buf ), &info );
	CHECK( n2 > 0 );
	CHECK_EQ( info.base, 1 );
	CHECK_EQ( info.records, 3 );				// 7 changed, 10 added, 900 removed
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n2, &frame ), COOP_SNAP_OK );
	CHECK_EQ( frame, 2 );
	std::vector<Change> ch;
	CHECK_EQ( CoopSnap_Diff( &guest.r, 1, 2, Collect, &ch ), 0 );
	CHECK_EQ( (int)ch.size(), 3 );
	if ( ch.size() == 3 ) {
		CHECK_EQ( ch[0].num, 7 );   CHECK_EQ( ch[0].kind, COOP_SNAPCHG_CHANGED ); CHECK_EQ( ch[0].es1, 99 );
		CHECK_EQ( ch[1].num, 10 );  CHECK_EQ( ch[1].kind, COOP_SNAPCHG_ADDED );
		CHECK_EQ( ch[2].num, 900 ); CHECK_EQ( ch[2].kind, COOP_SNAPCHG_REMOVED );
	}
}

COOP_TEST( snap_unchanged_frame_is_header_and_end ) {
	TestRing host, guest;
	const std::vector<TestEnt> ents = ThreeEnts();
	TestInput in( ents );
	uint8_t buf[4096];
	int frame = 0;
	const int n = CoopSnap_Write( &host.r, 1, -1, 0, &in.in, buf, sizeof( buf ), nullptr );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	coopSnapWriteInfo_t info;
	const int n2 = CoopSnap_Write( &host.r, 2, 1, 50, &in.in, buf, sizeof( buf ), &info );
	CHECK_EQ( n2, COOP_SNAP_HEADER + 2 );
	CHECK_EQ( info.records, 0 );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n2, &frame ), COOP_SNAP_OK );
	std::vector<Change> ch;
	CHECK_EQ( CoopSnap_Diff( &guest.r, 1, 2, Collect, &ch ), 0 );
	CHECK_EQ( (int)ch.size(), 0 );
}

COOP_TEST( snap_budget_defers_and_catches_up ) {
	TestRing host, guest;
	const std::vector<TestEnt> ents = ThreeEnts();
	TestInput in( ents );
	uint8_t buf[4096];
	int frame = 0;
	// entity 2: 3 + es delta (1 + 4 + 2*4) + ex delta (1 + 4 + 4) = 25 bytes
	coopSnapWriteInfo_t info;
	const int n = CoopSnap_Write( &host.r, 1, -1, 0, &in.in, buf, COOP_SNAP_HEADER + 25 + 2, &info );
	CHECK_EQ( n, COOP_SNAP_HEADER + 25 + 2 );
	CHECK_EQ( info.records, 1 );
	CHECK_EQ( info.deferred, 2 );
	CHECK_EQ( info.entities, 1 );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	const coopSnapFrame_t *f = CoopSnap_Frame( &guest.r, 1 );
	CHECK( f && f->numEnts == 1 && f->flags == 0 );
	const int n2 = CoopSnap_Write( &host.r, 2, 1, 50, &in.in, buf, sizeof( buf ), &info );
	CHECK_EQ( info.records, 2 );
	CHECK_EQ( info.deferred, 0 );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n2, &frame ), COOP_SNAP_OK );
	f = CoopSnap_Frame( &guest.r, 2 );
	CHECK( f && f->numEnts == 3 && f->flags == COOP_SNAPF_COMPLETE );
	std::vector<Change> ch;
	CoopSnap_Diff( &guest.r, 1, 2, Collect, &ch );
	CHECK_EQ( (int)ch.size(), 2 );
	if ( ch.size() == 2 ) {
		CHECK_EQ( ch[0].num, 7 );   CHECK_EQ( ch[0].kind, COOP_SNAPCHG_ADDED );
		CHECK_EQ( ch[1].num, 900 ); CHECK_EQ( ch[1].kind, COOP_SNAPCHG_ADDED );
	}
}

COOP_TEST( snap_removal_deferred_without_room ) {
	TestRing host, guest;
	std::vector<TestEnt> ents = ThreeEnts();
	uint8_t buf[4096];
	int frame = 0;
	TestInput in( ents );
	int n = CoopSnap_Write( &host.r, 1, -1, 0, &in.in, buf, sizeof( buf ), nullptr );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	ents.pop_back();								// 900 removed
	TestInput in2( ents );
	coopSnapWriteInfo_t info;
	n = CoopSnap_Write( &host.r, 2, 1, 50, &in2.in, buf, COOP_SNAP_HEADER + 2, &info );	// no room for the 3-byte record
	CHECK_EQ( n, COOP_SNAP_HEADER + 2 );
	CHECK_EQ( info.deferred, 1 );
	CHECK_EQ( info.entities, 3 );					// the guest still has 900
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	n = CoopSnap_Write( &host.r, 3, 2, 100, &in2.in, buf, sizeof( buf ), &info );
	CHECK_EQ( info.records, 1 );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	std::vector<Change> ch;
	CoopSnap_Diff( &guest.r, 2, 3, Collect, &ch );
	CHECK_EQ( (int)ch.size(), 1 );
	if ( ch.size() == 1 ) {
		CHECK_EQ( ch[0].num, 900 ); CHECK_EQ( ch[0].kind, COOP_SNAPCHG_REMOVED );
	}
}

COOP_TEST( snap_missing_base_reports_nobase ) {
	TestRing host, guest;
	const std::vector<TestEnt> ents = ThreeEnts();
	TestInput in( ents );
	uint8_t buf[4096];
	int frame = 0;
	CoopSnap_Write( &host.r, 1, -1, 0, &in.in, buf, sizeof( buf ), nullptr );	// never delivered
	const int n = CoopSnap_Write( &host.r, 2, 1, 50, &in.in, buf, sizeof( buf ), nullptr );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_NOBASE );
}

COOP_TEST( snap_rejects_malformed ) {
	TestRing host, guest;
	const std::vector<TestEnt> ents = ThreeEnts();
	TestInput in( ents );
	uint8_t buf[4096];
	int frame = 0;
	const int n = CoopSnap_Write( &host.r, 1, -1, 0, &in.in, buf, sizeof( buf ) - 1, nullptr );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n - 1, &frame ), COOP_SNAP_BAD );		// truncated
	buf[n] = 0;
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n + 1, &frame ), COOP_SNAP_BAD );		// trailing byte
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, COOP_SNAP_HEADER, &frame ), COOP_SNAP_BAD );
	buf[COOP_SNAP_HEADER] = 0xff;													// first record number 0x??ff beyond the limit
	buf[COOP_SNAP_HEADER + 1] = 0x7f;
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_BAD );
}

COOP_TEST( snap_input_must_ascend ) {
	TestRing host;
	std::vector<TestEnt> ents = ThreeEnts();
	std::swap( ents[0], ents[1] );
	TestInput in( ents );
	uint8_t buf[4096];
	CHECK_EQ( CoopSnap_Write( &host.r, 1, -1, 0, &in.in, buf, sizeof( buf ), nullptr ), COOP_SNAP_BAD );
}

COOP_TEST( snap_entity_gains_playerstate ) {
	TestRing host, guest;
	std::vector<TestEnt> ents = ThreeEnts();
	uint8_t buf[4096];
	int frame = 0;
	TestInput in( ents );
	int n = CoopSnap_Write( &host.r, 1, -1, 0, &in.in, buf, sizeof( buf ), nullptr );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	ents[0].hasPs = true;
	ents[0].ps[0] = 77;
	TestInput in2( ents );
	n = CoopSnap_Write( &host.r, 2, 1, 50, &in2.in, buf, sizeof( buf ), nullptr );
	CHECK_EQ( CoopSnap_Read( &guest.r, buf, n, &frame ), COOP_SNAP_OK );
	std::vector<Change> ch;
	CoopSnap_Diff( &guest.r, 1, 2, Collect, &ch );
	CHECK_EQ( (int)ch.size(), 1 );
	if ( ch.size() == 1 ) {
		CHECK_EQ( ch[0].num, 2 ); CHECK_EQ( ch[0].kind, COOP_SNAPCHG_CHANGED ); CHECK( ch[0].hasPs ); CHECK_EQ( ch[0].ps0, 77 );
	}
}

COOP_TEST( snap_old_base_falls_back_to_full ) {
	TestRing host;
	const std::vector<TestEnt> ents = ThreeEnts();
	TestInput in( ents );
	uint8_t buf[4096];
	coopSnapWriteInfo_t info;
	for ( int f = 1; f <= COOP_FRAME_BACKUP + 1; f++ ) {
		CoopSnap_Write( &host.r, f, 1, f * 50, &in.in, buf, sizeof( buf ), &info );
	}
	CHECK_EQ( info.base, -1 );					// frame 33 cannot delta from frame 1
	CHECK_EQ( info.records, 3 );
}

COOP_TEST( snap_foreach_lists_frame ) {
	TestRing host;
	const std::vector<TestEnt> ents = ThreeEnts();
	TestInput in( ents );
	uint8_t buf[4096];
	CoopSnap_Write( &host.r, 4, -1, 0, &in.in, buf, sizeof( buf ), nullptr );
	std::vector<Change> ch;
	CHECK_EQ( CoopSnap_ForEach( &host.r, 4, Collect, &ch ), 3 );
	CHECK_EQ( (int)ch.size(), 3 );
	CHECK_EQ( CoopSnap_ForEach( &host.r, 5, Collect, &ch ), -1 );
	CoopSnap_Clear( &host.r );
	CHECK( !CoopSnap_HasFrame( &host.r, 4 ) );
}
