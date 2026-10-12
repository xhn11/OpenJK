#include "coop_owner.h"

int CoopOwner_Decide( const coopOwnerInput_t *in ) {
	int want;
	if ( in->pullAll || in->scripted || in->ally || in->dead ) {
		want = 1;	// the host keeps scripts, allies and deaths (death scripts, mission counters)
	} else if ( in->forcedPlayer == 0 || in->forcedPlayer == 1 ) {
		want = in->forcedPlayer == 1 ? 2 : 1;
	} else if ( in->enemyPlayer == 1 ) {
		want = 2;
	} else {
		want = 1;	// the host's player, no player or no enemy at all
	}
	if ( want == in->currentOwner ) {
		return want;
	}
	if ( in->pullAll || in->scripted || in->dead ) {
		return want;	// these do not wait
	}
	if ( in->inAttack || in->now - in->lastTransferTime < COOP_OWNER_HYSTERESIS_MS ) {
		return in->currentOwner;
	}
	return want;
}
