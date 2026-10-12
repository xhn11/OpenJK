#include "coop_local.h"

// Shared map (design 2, 22): the host tells the guest which map runs (coop_map), the guest loads it
// with the same cheat state and answers coop_ready. Map generations tell a reload of the same map
// apart. Like every reliable handler, these run from Coop_Frame, never inside Com_EventLoop.

typedef struct {
	int			mapGen;					// guest: generation from the last coop_map
	char		mapName[MAX_QPATH];		// guest: the host's map
	int			cheats;					// guest: the host runs it with devmap
	qboolean	loadPending;			// guest: coop_map received, map command not issued yet
	qboolean	readySent;				// guest: coop_ready sent for mapGen
	int			guestReadyGen;			// host: generation the guest confirmed, 0 = none
	qboolean	onWorldPending;			// coop_onworld must run
} coopSession_t;

static int				coopHostMapGen;	// bumped by every finished map load on this machine (the host uses it)
static coopSession_t	sess;
static cvar_t			*coop_onworld;

void SV_GameSendServerCommand( int clientNum, const char *fmt, ... );	// sv_game.cpp:96

static char				coopCpTest[MAX_STRING_CHARS];	// coop_cptest: centerprint sent once the guest is in the world

static qboolean Coop_ForwardedCommand( const char *cmd ) {
	static const char *forwarded[] = { "cp", "ct", "cts", "st" };	// shared by both players (cg_servercmds.cpp:265-273)
	char name[16];
	int i = 0;
	while ( cmd[i] && cmd[i] != ' ' && i < (int)sizeof( name ) - 1 ) {
		name[i] = cmd[i];
		i++;
	}
	name[i] = 0;
	for ( size_t k = 0; k < ARRAY_LEN( forwarded ); k++ ) {
		if ( !Q_stricmp( name, forwarded[k] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

void Coop_ForwardServerCommand( int clientNum, const char *cmd ) {
	if ( ( clientNum != -1 && clientNum != 0 ) || !Coop_HostGuestInWorld() || !Coop_ForwardedCommand( cmd ) ) {
		return;
	}
	if ( (int)strlen( cmd ) + 6 > COOP_MAX_WIRE_CMD_LEN || Coop_ReliableFree() < 8 ) {
		Com_Printf( "COOP: server command dropped (reliable queue full): %s\n", cmd );
		return;
	}
	Coop_AddReliable( va( "svcmd %s", cmd ) );
}

static void Coop_CpTest_f( void ) {
	Q_strncpyz( coopCpTest, Cmd_Args(), sizeof( coopCpTest ) );
}

void Coop_SessionInit( void ) {
	coop_onworld = Cvar_Get( "coop_onworld", "", 0 );	// console commands run once when the guest is in the host's map (testing)
	Cmd_AddCommand( "coop_cptest", Coop_CpTest_f );
	memset( &sess, 0, sizeof( sess ) );
}

int Coop_MapGen( void ) {
	return coop.role == COOP_ROLE_HOST ? coopHostMapGen : sess.mapGen;
}

qboolean Coop_GuestInWorld( void ) {
	return (qboolean)( coop.role == COOP_ROLE_GUEST && coop.state == COOP_LINK_ACTIVE && sess.readySent
		&& Coop_LocalMapReady() && !Q_stricmp( sv_mapname->string, sess.mapName ) );
}

qboolean Coop_HostGuestInWorld( void ) {
	return (qboolean)( coop.role == COOP_ROLE_HOST && coop.state == COOP_LINK_ACTIVE && sess.guestReadyGen != 0
		&& sess.guestReadyGen == coopHostMapGen && Coop_LocalMapRunning() );
}

static void Coop_SendMap( void ) {
	sess.guestReadyGen = 0;
	Coop_AddReliable( va( "coop_map %d %s %d", coopHostMapGen, sv_mapname->string,
		Cvar_VariableIntegerValue( "helpUsObi" ) ? 1 : 0 ) );
	Com_Printf( "COOP: sent map %s to the guest\n", sv_mapname->string );
}

void Coop_OnLinkUp( void ) {
	memset( &sess, 0, sizeof( sess ) );
	CoopCS_LinkReset();
	CoopSnapshot_LinkReset();
	if ( coop.role == COOP_ROLE_HOST && Coop_LocalMapRunning() ) {
		Coop_SendMap();
	}
}

void Coop_OnLinkDown( const char *reason ) {
	if ( coop.role == COOP_ROLE_GUEST && sess.mapGen ) {
		// design 23: without the host the guest's world stops; leave instead of playing on with frozen replicas
		Com_Printf( "COOP: host left (%s), leaving the game\n", reason );
		Cbuf_AddText( "disconnect\n" );
	}
	CoopCS_LinkReset();
	CoopSnapshot_LinkReset();
	memset( &sess, 0, sizeof( sess ) );
}

void Coop_SessionMapLoading( void ) {
	CoopCS_MapLoading();
}

void Coop_SessionMapLoaded( void ) {
	coopHostMapGen++;
	if ( coop.role == COOP_ROLE_HOST && coop.state == COOP_LINK_ACTIVE ) {
		Coop_SendMap();
	}
}

void Coop_SessionFrame( void ) {
	if ( coopCpTest[0] && Coop_HostGuestInWorld() ) {
		SV_GameSendServerCommand( -1, "cp \"%s\"", coopCpTest );
		coopCpTest[0] = 0;
	}
	if ( coop.role == COOP_ROLE_GUEST ) {
		if ( sess.loadPending ) {
			sess.loadPending = qfalse;
			Com_Printf( "COOP: loading %s from host\n", sess.mapName );
			Cbuf_AddText( va( "%s %s\n", sess.cheats ? "devmap" : "map", sess.mapName ) );
			return;
		}
		if ( sess.mapGen && !sess.readySent && Coop_LocalMapReady() && !Q_stricmp( sv_mapname->string, sess.mapName ) ) {
			sess.readySent = qtrue;
			sess.onWorldPending = qtrue;
			Coop_AddReliable( va( "coop_ready %d", sess.mapGen ) );
			Com_Printf( "COOP: in %s, waiting for the host's world\n", sess.mapName );
		}
	}
	if ( sess.onWorldPending ) {
		sess.onWorldPending = qfalse;
		if ( coop_onworld->string[0] ) {
			Cbuf_AddText( va( "%s\n", coop_onworld->string ) );
		}
	}
	CoopCS_Frame();
	CoopSnapshot_Frame();
}

qboolean Coop_ExecuteReliableCmd( const char *cmd ) {
	const char *c = Cmd_Argv( 0 );	// Coop_ExecuteReliable tokenized cmd
	if ( !Q_stricmp( c, "coop_map" ) ) {
		if ( coop.role != COOP_ROLE_GUEST ) {
			return qtrue;
		}
		const int gen = atoi( Cmd_Argv( 1 ) );
		const char *map = Cmd_Argv( 2 );
		coop.peerLoading = qfalse;	// the host is in its map again
		if ( gen <= 0 || !map[0] || gen == sess.mapGen ) {
			return qtrue;			// malformed, or the map we already load / run
		}
		sess.mapGen = gen;
		Q_strncpyz( sess.mapName, map, sizeof( sess.mapName ) );
		sess.cheats = atoi( Cmd_Argv( 3 ) );
		sess.loadPending = qtrue;
		sess.readySent = qfalse;
		CoopCS_NewMap();
		CoopSnapshot_NewMap();
		return qtrue;
	}
	if ( !Q_stricmp( c, "coop_ready" ) ) {
		if ( coop.role != COOP_ROLE_HOST ) {
			return qtrue;
		}
		coop.peerLoading = qfalse;
		if ( !Coop_LocalMapRunning() ) {
			return qtrue;			// we are loading ourselves: Coop_SessionMapLoaded sends the new map
		}
		if ( atoi( Cmd_Argv( 1 ) ) != coopHostMapGen ) {
			Coop_SendMap();			// the guest is in an old map
			return qtrue;
		}
		sess.guestReadyGen = coopHostMapGen;
		CoopCS_GuestReady();
		CoopSnapshot_GuestReady();
		sess.onWorldPending = qtrue;
		Com_Printf( "COOP: guest entered %s\n", sv_mapname->string );
		return qtrue;
	}
	if ( !Q_stricmp( c, "svcmd" ) ) {
		if ( coop.role == COOP_ROLE_GUEST && Coop_GuestInWorld() ) {
			const char *text = cmd + 6;		// raw text after "svcmd ": quotes stay as the host wrote them
			Com_Printf( "COOP: server command from host: %s\n", text );
			SV_SendServerCommand( NULL, "%s", text );
		}
		return qtrue;
	}
	(void)cmd;		// the raw text is needed from Task 14 (svcmd)
	return qfalse;	// Coop_ExecuteReliable logs unknown commands with coop_debug 1
}

// payloads: configstrings (Task 8), snapshots from Task 11
void Coop_WritePayload( msg_t *msg ) {
	CoopCS_Write( msg );		// configstrings first: snapshots refer to their indices
	CoopSnapshot_Write( msg );
}

qboolean Coop_ParsePayload( int cmd, msg_t *msg ) {
	switch ( cmd ) {
	case coop_cs:		return CoopCS_Parse( msg );
	case coop_csack:	return CoopCS_ParseAck( msg );
	case coop_snapshot:	return CoopSnapshot_Parse( msg );
	case coop_snapack:	return CoopSnapshot_ParseAck( msg );
	default:			return qfalse;	// unknown payload: the link closes
	}
}
