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
qboolean	Coop_GuestAdvanceTime( int frameMsec );			// SV_Frame: guest time follows the host's snapshots; qfalse = vanilla
void		Coop_ForwardServerCommand( int clientNum, const char *cmd );	// SV_GameSendServerCommand: host -> guest too
qboolean	Coop_PeerViewOrigin( vec3_t org );				// SV_BuildClientSnapshot: the partner's eyes (design 8)
void		Coop_SendGameCommand( const char *text );		// game import: reliable "g <text>" to the partner's game
int			Coop_PartnerInWorld( void );					// game import: the partner plays in our map
int			Coop_WorldSynced( void );						// game import: a complete partner snapshot was applied in this map
void		Coop_NoteTransition( const char *spawnTarget );		// SV_MapTransition_f: the next map is a level change
qboolean	Coop_GuestSaveRequest( const char *name );		// SV_SaveGame_f: qtrue = the guest asked the host instead
qboolean	Coop_GuestLoadBlocked( void );					// SV_LoadGame_f: qtrue = the guest may not load
