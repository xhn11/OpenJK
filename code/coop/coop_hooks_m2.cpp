#include "coop_local.h"
void		Coop_OnLinkUp( void ) {}
void		Coop_OnLinkDown( const char * ) {}
void		Coop_WritePayload( msg_t * ) {}
qboolean	Coop_ParsePayload( int, msg_t * ) { return qfalse; }
qboolean	Coop_ExecuteReliableCmd( const char * ) { return qfalse; }
