#pragma once
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

typedef enum { COOP_ROLE_NONE, COOP_ROLE_HOST, COOP_ROLE_GUEST } coopRole_t;

void		Coop_Init( void );
void		Coop_Shutdown( void );
void		Coop_Frame( void );								// call once per frame after SV_Frame
void		Coop_ReceivePackets( void );					// drains the coop UDP socket, called from Com_EventLoop
qboolean	Coop_IsLinked( void );							// ACTIVE
coopRole_t	Coop_Role( void );
qboolean	Coop_AddReliable( const char *cmd );			// qfalse: not linked, too long (dropped) or the queue overflowed (link closed)
int			Coop_ReliableFree( void );						// free slots in the outgoing reliable queue, 0 when not linked
void		Coop_MapLoadStarting( void );					// SV_SpawnServer start: tell the peer, the load blocks all packets
void		Coop_MapLoadFinished( void );					// SV_SpawnServer end
void		Coop_ConfigstringChanged( int index );			// SV_SetConfigstring: index got a new value
