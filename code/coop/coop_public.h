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
void		Coop_AddReliable( const char *cmd );			// disconnects on overflow
