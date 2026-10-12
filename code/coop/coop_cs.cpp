#include "coop_local.h"
#include "coop_csrep.h"

// Configstrings host -> guest as raw bytes: the reliable text channel mangles '%', quotes and bytes
// above 127, and a level's worth of strings would overflow it.
//   coop_cs     long mapGen, long base, long upTo, short count, { short index, short length, bytes }*
//               = every configstring that changed after version `base`, up to and including `upTo`
//   coop_csack  long mapGen, long version     (the guest has every change up to `version`)

static_assert( MAX_CONFIGSTRINGS <= COOP_CS_MAX, "coop_csrep.h: COOP_CS_MAX too small" );

#define COOP_CS_HEADER		( 1 + 4 + 4 + 4 + 2 )
#define COOP_CS_MAX_LEN		BIG_INFO_STRING

static coopCsRep_t	csRep;							// host: change versions of sv.configstrings
static int			csPeerAck;						// host: the guest has every change up to this version
static char			*csPending[MAX_CONFIGSTRINGS];	// guest: received, applied by CoopCS_Frame
static int			csPendingCount;
static int			csApplied;						// guest: highest version received completely
static int			csAppliedCount;
static qboolean		csSynced;

// configstrings that stay per machine
static qboolean CoopCS_Mirrored( int index ) {
	if ( index < RESERVED_CONFIGSTRINGS ) {
		return qfalse;		// serverinfo, systeminfo
	}
	if ( index == CS_AMBIENT_SET ) {
		return qfalse;		// set by triggers for the local player (g_trigger.cpp:69)
	}
	if ( index >= CS_PLAYERS && index < CS_PLAYERS + MAX_CLIENTS ) {
		return qfalse;		// player info: M4 maps 0 <-> 1
	}
	return qtrue;
}

static void CoopCS_ClearPending( void ) {
	for ( int i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( csPending[i] ) {
			Z_Free( csPending[i] );
			csPending[i] = NULL;
		}
	}
	csPendingCount = 0;
}

void Coop_ConfigstringChanged( int index ) {
	if ( index >= 0 && index < MAX_CONFIGSTRINGS ) {
		CoopCs_Touch( &csRep, index );
	}
}

void CoopCS_MapLoading( void ) {
	CoopCs_Reset( &csRep );
	csPeerAck = 0;
}

void CoopCS_GuestReady( void ) {
	csPeerAck = 0;
}

void CoopCS_NewMap( void ) {
	CoopCS_ClearPending();
	csApplied = 0;
	csAppliedCount = 0;
	csSynced = qfalse;
}

void CoopCS_LinkReset( void ) {
	csPeerAck = 0;
	CoopCS_NewMap();
}

qboolean CoopCS_Synced( void ) {
	return csSynced;
}

void CoopCS_Write( msg_t *msg ) {
	if ( coop.role == COOP_ROLE_GUEST ) {
		if ( Coop_MapGen() && Coop_MsgRoom( msg ) >= 9 ) {
			MSG_WriteByte( msg, coop_csack );
			MSG_WriteLong( msg, Coop_MapGen() );
			MSG_WriteLong( msg, csApplied );
		}
		return;
	}
	if ( !Coop_HostGuestInWorld() ) {
		return;
	}
	static int idx[COOP_CS_MAX];
	const int n = CoopCs_Select( &csRep, csPeerAck, idx, COOP_CS_MAX );
	int room = Coop_MsgRoom( msg ) - COOP_CS_HEADER;
	int covered = 0, written = 0;
	while ( covered < n ) {
		const int index = idx[covered];
		if ( CoopCS_Mirrored( index ) ) {
			const int need = 4 + (int)strlen( sv.configstrings[index] ? sv.configstrings[index] : "" );
			if ( need > room ) {
				break;		// the rest goes in the next packet
			}
			room -= need;
			written++;
		}
		covered++;
	}
	if ( !covered ) {
		return;
	}
	MSG_WriteByte( msg, coop_cs );
	MSG_WriteLong( msg, Coop_MapGen() );
	MSG_WriteLong( msg, csPeerAck );
	MSG_WriteLong( msg, csRep.version[idx[covered - 1]] );
	MSG_WriteShort( msg, written );
	for ( int i = 0; i < covered; i++ ) {
		if ( !CoopCS_Mirrored( idx[i] ) ) {
			continue;
		}
		const char *s = sv.configstrings[idx[i]] ? sv.configstrings[idx[i]] : "";
		const int len = (int)strlen( s );
		MSG_WriteShort( msg, idx[i] );
		MSG_WriteShort( msg, len );
		MSG_WriteData( msg, s, len );
	}
}

qboolean CoopCS_Parse( msg_t *msg ) {
	const int gen = MSG_ReadLong( msg );
	const int base = MSG_ReadLong( msg );
	const int upTo = MSG_ReadLong( msg );
	const int count = MSG_ReadShort( msg );
	if ( count < 0 ) {
		return qfalse;
	}
	const qboolean current = (qboolean)( coop.role == COOP_ROLE_GUEST && gen == Coop_MapGen() );
	static char value[COOP_CS_MAX_LEN];
	for ( int i = 0; i < count; i++ ) {
		const int index = MSG_ReadShort( msg );
		const int len = MSG_ReadShort( msg );
		if ( index < 0 || index >= MAX_CONFIGSTRINGS || len < 0 || len >= COOP_CS_MAX_LEN || msg->readcount + len > msg->cursize ) {
			return qfalse;
		}
		if ( current ) {
			memcpy( value, msg->data + msg->readcount, len );
			value[len] = 0;
			if ( csPending[index] ) {
				Z_Free( csPending[index] );
			} else {
				csPendingCount++;
			}
			csPending[index] = CopyString( value );
		}
		msg->readcount += len;
	}
	if ( current && base <= csApplied && upTo > csApplied ) {
		csApplied = upTo;
	}
	return qtrue;
}

qboolean CoopCS_ParseAck( msg_t *msg ) {
	const int gen = MSG_ReadLong( msg );
	const int version = MSG_ReadLong( msg );
	if ( coop.role == COOP_ROLE_HOST && gen == Coop_MapGen() && version > csPeerAck && version <= csRep.seq ) {
		csPeerAck = version;
	}
	return qtrue;
}

static void CoopCS_CheckConflict( int index, const char *value ) {
	const char *old = sv.configstrings[index];
	if ( !old || !old[0] || !strcmp( old, value ) ) {
		return;
	}
	if ( ( index >= CS_MODELS && index < CS_MODELS + MAX_MODELS ) || ( index >= CS_SOUNDS && index < CS_SOUNDS + MAX_SOUNDS )
		|| ( index >= CS_EFFECTS && index < CS_EFFECTS + MAX_FX ) || ( index >= CS_CHARSKINS && index < CS_CHARSKINS + MAX_CHARSKINS ) ) {
		Com_Printf( "COOP: configstring %d conflict (\"%s\" -> \"%s\")\n", index, old, value );
	}
}

void CoopCS_Frame( void ) {
	if ( coop.role != COOP_ROLE_GUEST || !Coop_GuestInWorld() ) {
		return;
	}
	if ( csPendingCount ) {
		// every applied string becomes a "cs" command for our own client, which is dropped after
		// MAX_RELIABLE_COMMANDS unacknowledged ones (sv_main.cpp:101): apply in batches
		const client_t *cl = &svs.clients[0];
		int budget = cl->state < CS_PRIMED ? MAX_CONFIGSTRINGS
			: MAX_RELIABLE_COMMANDS / 2 - ( cl->reliableSequence - cl->reliableAcknowledge );
		for ( int i = 0; i < MAX_CONFIGSTRINGS && budget > 0 && csPendingCount; i++ ) {
			if ( !csPending[i] ) {
				continue;
			}
			if ( CoopCS_Mirrored( i ) ) {
				CoopCS_CheckConflict( i, csPending[i] );
				SV_SetConfigstring( i, csPending[i] );
				csAppliedCount++;
				budget--;
			}
			Z_Free( csPending[i] );
			csPending[i] = NULL;
			csPendingCount--;
		}
	}
	if ( !csSynced && !csPendingCount && csApplied > 0 ) {
		csSynced = qtrue;
		Com_Printf( "COOP: configstrings synced (%d strings)\n", csAppliedCount );
	}
}
