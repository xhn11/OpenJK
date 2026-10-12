#include "coop_entnum.h"

int Coop_SwapEntNum( int n ) {
	if ( n == 0 ) {
		return 1;
	}
	if ( n == 1 ) {
		return 0;
	}
	return n;
}

void Coop_DynamicRange( int role, int loading, int *first, int *lastExclusive ) {
	(void)role;
	(void)loading;
	*first = COOP_ENT_MAX_CLIENTS;
	*lastExclusive = COOP_ENT_MAX_NORMAL;
}

int Coop_SpawnTopDown( int role, int loading ) {
	return role == 2 && !loading;
}
