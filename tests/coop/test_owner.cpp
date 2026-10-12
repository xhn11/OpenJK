#include "coop_test.h"
#include "coop_owner.h"

static coopOwnerInput_t OwnerInput( int enemyPlayer, int currentOwner ) {
	coopOwnerInput_t in = {};
	in.enemyPlayer = enemyPlayer;
	in.forcedPlayer = -1;
	in.currentOwner = currentOwner;
	in.lastTransferTime = 0;
	in.now = 100000;
	return in;
}

COOP_TEST( owner_follows_the_enemy_player ) {
	coopOwnerInput_t in = OwnerInput( 1, 1 );
	CHECK_EQ( CoopOwner_Decide( &in ), 2 );
	in = OwnerInput( 0, 2 );
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
	in = OwnerInput( 0, 1 );
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
}
COOP_TEST( owner_without_player_enemy_is_host ) {
	coopOwnerInput_t in = OwnerInput( -1, 2 );
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
	in = OwnerInput( -1, 1 );
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
}
COOP_TEST( owner_scripted_and_allies_stay_host ) {
	coopOwnerInput_t in = OwnerInput( 1, 1 );
	in.scripted = 1;
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
	in = OwnerInput( 1, 1 );
	in.ally = 1;
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
	in = OwnerInput( 1, 2 );	// a script starts on a guest NPC: back at once, no hysteresis
	in.scripted = 1;
	in.lastTransferTime = in.now - 10;
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
}
COOP_TEST( owner_hysteresis_and_attacks_delay_changes ) {
	coopOwnerInput_t in = OwnerInput( 1, 1 );
	in.lastTransferTime = in.now - ( COOP_OWNER_HYSTERESIS_MS - 1 );
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
	in.lastTransferTime = in.now - COOP_OWNER_HYSTERESIS_MS;
	CHECK_EQ( CoopOwner_Decide( &in ), 2 );
	in.inAttack = 1;
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
}
COOP_TEST( owner_pull_all_and_death_go_home_at_once ) {
	coopOwnerInput_t in = OwnerInput( 1, 2 );
	in.pullAll = 1;
	in.inAttack = 1;
	in.lastTransferTime = in.now;
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
	in = OwnerInput( 1, 2 );
	in.dead = 1;
	in.lastTransferTime = in.now;
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
}
COOP_TEST( owner_forced_player_wins_over_enemy ) {
	coopOwnerInput_t in = OwnerInput( 0, 1 );
	in.forcedPlayer = 1;
	CHECK_EQ( CoopOwner_Decide( &in ), 2 );
	in = OwnerInput( 1, 2 );
	in.forcedPlayer = 0;
	CHECK_EQ( CoopOwner_Decide( &in ), 1 );
}
