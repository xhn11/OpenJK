#include "coop_test.h"
#include "coop_entnum.h"

COOP_TEST( entnum_swap_players_only ) {
	CHECK_EQ( Coop_SwapEntNum( 0 ), 1 );
	CHECK_EQ( Coop_SwapEntNum( 1 ), 0 );
	CHECK_EQ( Coop_SwapEntNum( 2 ), 2 );
	CHECK_EQ( Coop_SwapEntNum( 767 ), 767 );
	CHECK_EQ( Coop_SwapEntNum( 1022 ), 1022 );
	CHECK_EQ( Coop_SwapEntNum( 1023 ), 1023 );
	CHECK_EQ( Coop_SwapEntNum( -1 ), -1 );
}
COOP_TEST( entnum_range_is_vanilla_for_everybody ) {
	for ( int role = 0 ; role <= 2 ; role++ ) {
		for ( int loading = 0 ; loading <= 1 ; loading++ ) {
			int f = -1, l = -1;
			Coop_DynamicRange( role, loading, &f, &l );
			CHECK_EQ( f, 2 ); CHECK_EQ( l, 1022 );
		}
	}
}
COOP_TEST( entnum_only_guest_after_load_spawns_top_down ) {
	CHECK_EQ( Coop_SpawnTopDown( 0, 0 ), 0 );
	CHECK_EQ( Coop_SpawnTopDown( 0, 1 ), 0 );
	CHECK_EQ( Coop_SpawnTopDown( 1, 0 ), 0 );
	CHECK_EQ( Coop_SpawnTopDown( 1, 1 ), 0 );
	CHECK_EQ( Coop_SpawnTopDown( 2, 1 ), 0 );	// map entities: same numbers as the host
	CHECK_EQ( Coop_SpawnTopDown( 2, 0 ), 1 );
}
