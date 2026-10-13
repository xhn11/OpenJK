#include "coop_local.h"


coopLink_t	coop;
cvar_t		*coop_port, *coop_name, *coop_timeout, *coop_debug, *coop_autotest;
static cvar_t *coop_role;
static cvar_t *coop_simLoss, *coop_simLatency, *coop_autotestCount;
static cvar_t *coop_onlink;
static cvar_t *coop_password;
static cvar_t *coop_checkPaks;
#define COOP_BUILD_ID	__DATE__ " " __TIME__	// coop_link.cpp is rebuilt with the engine: same binary, same id

coopRole_t	Coop_Role( void )		{ return coop.role; }
qboolean	Coop_IsLinked( void )	{ return (qboolean)( coop.state == COOP_LINK_ACTIVE ); }

static void Coop_Reset( coopRole_t role, coopLinkState_t state ) {
	memset( &coop, 0, sizeof( coop ) );
	CoopRel_Init( &coop.rel );
	CoopInQ_Init( &coop.inQueue );
	coop.role = role;
	coop.state = state;
	Cvar_Set( "coop_role", va( "%i", (int)role ) );
}

static void Coop_CloseLink( const char *reason, qboolean notifyPeer ) {
	if ( coop.state == COOP_LINK_ACTIVE ) {
		if ( notifyPeer ) {
			NET_OutOfBandPrint( NS_SERVER, coop.peer, "coopDisconnect \"%s\"", reason );
		}
		Coop_OnLinkDown( reason );
	}
	Com_Printf( "COOP: link closed: %s\n", reason );
	if ( coop.role == COOP_ROLE_HOST ) {
		Coop_Reset( COOP_ROLE_HOST, COOP_LINK_LISTENING );	// keep the port open for a reconnect
	} else {
		NET_IP_Close();
		Coop_Reset( COOP_ROLE_NONE, COOP_LINK_IDLE );
	}
}

qboolean Coop_AddReliable( const char *cmd ) {
	if ( coop.state != COOP_LINK_ACTIVE ) {
		return qfalse;
	}
	if ( (int)strlen( cmd ) > COOP_MAX_WIRE_CMD_LEN ) {
		Com_Printf( "COOP: reliable command too long (%d chars), dropped\n", (int)strlen( cmd ) );
		return qfalse;
	}
	if ( !CoopRel_Add( &coop.rel, cmd ) ) {
		Coop_CloseLink( "reliable command overflow", qtrue );
		return qfalse;
	}
	return qtrue;
}

int Coop_ReliableFree( void ) {
	return coop.state == COOP_LINK_ACTIVE ? CoopRel_FreeSlots( &coop.rel ) : 0;
}

// ---------------------------------------------------------------- fragmentation self-test

#define COOP_TEST_MAX_BYTES		( COOP_MAX_MSGLEN - 1024 )

static int Coop_TestClamp( int bytes ) {
	return bytes < 1 ? 1 : ( bytes > COOP_TEST_MAX_BYTES ? COOP_TEST_MAX_BYTES : bytes );
}

static byte Coop_TestByte( int i ) {
	return (byte)( ( i * 31 + 7 ) & 0xff );
}

static unsigned Coop_TestMix( unsigned sum, byte b ) {
	return sum * 33 + b;
}

// coop_test: long length, long checksum, length bytes of a fixed pattern
static void Coop_WriteTest( msg_t *msg ) {
	const int length = coop.testPending;
	if ( !length || Coop_MsgRoom( msg ) < 9 + length ) {
		return;	// nothing to send, or no room next to the reliables: try the next packet
	}
	unsigned sum = 5381;
	for ( int i = 0; i < length; i++ ) {
		sum = Coop_TestMix( sum, Coop_TestByte( i ) );
	}
	MSG_WriteByte( msg, coop_test );
	MSG_WriteLong( msg, length );
	MSG_WriteLong( msg, (int)sum );
	byte *data = (byte *)MSG_GetSpace( msg, length );
	for ( int i = 0; i < length; i++ ) {
		data[i] = Coop_TestByte( i );
	}
	if ( --coop.testRepeats <= 0 ) {
		coop.testPending = 0;	// otherwise the same payload goes out again in the next packet (loss test)
	}
	Com_Printf( "COOP: test payload %d bytes sent\n", length );
}

// returns qfalse if the rest of the packet cannot be parsed
static qboolean Coop_ReadTest( msg_t *msg ) {
	const int length = MSG_ReadLong( msg );
	const unsigned checksum = (unsigned)MSG_ReadLong( msg );
	if ( length < 0 || length > msg->cursize - msg->readcount ) {
		Com_Printf( "COOP: test payload corrupt (%d bytes)\n", length );
		return qfalse;
	}
	const byte *data = msg->data + msg->readcount;
	msg->readcount += length;
	unsigned sum = 5381;
	qboolean ok = qtrue;
	for ( int i = 0; i < length; i++ ) {
		if ( data[i] != Coop_TestByte( i ) ) {
			ok = qfalse;
		}
		sum = Coop_TestMix( sum, data[i] );
	}
	if ( ok && sum == checksum ) {
		Com_Printf( "COOP: test payload %d bytes ok\n", length );
	} else {
		Com_Printf( "COOP: test payload corrupt (%d bytes)\n", length );
	}
	return qtrue;
}

qboolean Coop_LocalMapRunning( void ) {
	return (qboolean)( com_sv_running && com_sv_running->integer && sv.state == SS_GAME );
}

qboolean Coop_LocalMapReady( void ) {
	return (qboolean)( Coop_LocalMapRunning() && svs.clients && svs.clients[0].state == CS_ACTIVE );
}

static void Coop_LinkUp( netadr_t from ) {
	Cvar_Set( "coop_peername", coop.peerName );	// cgame shows it over the partner (design 27)
	CoopNetchan_Setup( coop.role == COOP_ROLE_HOST ? NS_SERVER : NS_CLIENT, &coop.chan, from );
	coop.peer = from;
	coop.state = COOP_LINK_ACTIVE;
	coop.lastReceived = Sys_Milliseconds();
	coop.lastSent = 0;
	Com_Printf( "COOP: link up (%s, peer \"%s\")\n", NET_AdrToString( from ), coop.peerName );
	Coop_AddReliable( va( "hello \"%s\"", coop_name->string ) );
	if ( coop_autotest->integer > 0 ) {
		coop.testPending = Coop_TestClamp( coop_autotest->integer );
		coop.testRepeats = coop_autotestCount->integer < 1 ? 1 : coop_autotestCount->integer;
	}
	if ( coop.localLoading ) {
		Coop_AddReliable( "loading" );	// the peer must not time us out while our client finishes loading
	}
	coop.onLinkPending = (qboolean)( coop_onlink->string[0] != 0 );
	Coop_OnLinkUp();
}

// ---------------------------------------------------------------- commands

static void Coop_ReloadMap_f( void ) {
	if ( !Coop_LocalMapRunning() ) {
		Com_Printf( "COOP: no map is running\n" );
		return;
	}
	Cbuf_AddText( va( "%s %s\n", Cvar_VariableIntegerValue( "helpUsObi" ) ? "devmap" : "map", sv_mapname->string ) );
}

static void Coop_Host_f( void ) {
	if ( coop.state != COOP_LINK_IDLE ) {
		Com_Printf( "COOP: already active, use coop_disconnect first\n" );
		return;
	}
	if ( coop_port->integer < 1 || coop_port->integer > 65535 ) {
		Com_Printf( "COOP: invalid coop_port %d (use 1..65535)\n", coop_port->integer );
		return;
	}
	if ( !NET_IP_Open( coop_port->integer ) ) {
		Com_Printf( "COOP: cannot open UDP port %d\n", coop_port->integer );
		return;
	}
	Coop_Reset( COOP_ROLE_HOST, COOP_LINK_LISTENING );
	Com_Printf( "COOP: hosting on UDP port %d\n", NET_IP_LocalPort() );
}

// LAN discovery (design 28): coop_find broadcasts "coopDiscover", hosts answer "coopInfo"; the first
// host found becomes coop_connectaddr (the Coop menu's address field)
#define COOP_FIND_MS	1500
static int	coopFindUntil;
static int	coopFound;

static void Coop_Find_f( void ) {
	if ( coop.state != COOP_LINK_IDLE ) {
		Com_Printf( "COOP: already active, use coop_disconnect first\n" );
		return;
	}
	if ( !coopFindUntil && !NET_IP_Open( PORT_ANY ) ) {
		Com_Printf( "COOP: cannot open a UDP socket\n" );
		return;
	}
	netadr_t adr;
	memset( &adr, 0, sizeof( adr ) );
	adr.type = NA_IP;
	adr.ip[0] = adr.ip[1] = adr.ip[2] = adr.ip[3] = 255;
	adr.port = BigShort( (short)( coop_port->integer > 0 ? coop_port->integer : COOP_DEFAULT_PORT ) );
	NET_OutOfBandPrint( NS_CLIENT, adr, "coopDiscover %d", COOP_PROTOCOL_VERSION );
	coopFindUntil = Sys_Milliseconds() + COOP_FIND_MS;
	coopFound = 0;
	Com_Printf( "COOP: looking for games on the LAN...\n" );
}

static void Coop_FindFrame( int now ) {
	if ( !coopFindUntil || now < coopFindUntil ) {
		return;
	}
	coopFindUntil = 0;
	Com_Printf( "COOP: %d game(s) found\n", coopFound );
	if ( coop.state == COOP_LINK_IDLE ) {
		NET_IP_Close();
	}
}

static void Coop_Connect_f( void ) {
	// no argument: the address typed in the Coop menu (coop_connectaddr)
	const char *target = Cmd_Argc() == 2 ? Cmd_Argv( 1 ) : Cvar_VariableString( "coop_connectaddr" );
	if ( Cmd_Argc() > 2 || !target[0] ) {
		Com_Printf( "usage: coop_connect <address[:port]>\n" );
		return;
	}
	char targetBuf[MAX_QPATH];
	Q_strncpyz( targetBuf, target, sizeof( targetBuf ) );
	Cvar_Set( "coop_connectaddr", targetBuf );	// remembered for the menu
	Cmd_TokenizeString( va( "coop_connect %s", targetBuf ) );
	if ( coop.state != COOP_LINK_IDLE ) {
		Com_Printf( "COOP: already active, use coop_disconnect first\n" );
		return;
	}
	netadr_t adr;
	if ( !NET_StringToAdr( Cmd_Argv( 1 ), &adr ) || adr.type != NA_IP ) {
		Com_Printf( "COOP: bad address \"%s\" (use an IP, not localhost)\n", Cmd_Argv( 1 ) );
		return;
	}
	if ( !adr.port ) {
		if ( coop_port->integer < 1 || coop_port->integer > 65535 ) {
			Com_Printf( "COOP: invalid coop_port %d (use 1..65535)\n", coop_port->integer );
			return;
		}
		adr.port = BigShort( (short)coop_port->integer );
	}
	if ( !NET_IP_Open( PORT_ANY ) ) {
		Com_Printf( "COOP: cannot open a UDP socket\n" );
		return;
	}
	Coop_Reset( COOP_ROLE_GUEST, COOP_LINK_CONNECTING );
	coop.peer = adr;
	coop.challenge = ( ( rand() << 16 ) ^ rand() ^ Sys_Milliseconds() ) & 0x7fffffff;
	coop.connectStart = Sys_Milliseconds();
	Com_Printf( "COOP: connecting to %s\n", NET_AdrToString( adr ) );
}

static void Coop_Disconnect_f( void ) {
	if ( coop.state == COOP_LINK_IDLE ) {
		return;
	}
	if ( coop.state == COOP_LINK_ACTIVE ) {
		Coop_CloseLink( "user disconnected", qtrue );
	}
	NET_IP_Close();
	Coop_Reset( COOP_ROLE_NONE, COOP_LINK_IDLE );
	Com_Printf( "COOP: stopped\n" );
}

// bandwidth for coop_status: bytes over the last full second
static int	coopBytesOut, coopBytesIn, coopRateStart, coopKbpsOut, coopKbpsIn;

static void Coop_CountBytes( int out, int in ) {
	const int now = Sys_Milliseconds();
	coopBytesOut += out;
	coopBytesIn += in;
	if ( now - coopRateStart >= 1000 ) {
		coopKbpsOut = coopBytesOut * 8 / ( now - coopRateStart );
		coopKbpsIn = coopBytesIn * 8 / ( now - coopRateStart );
		coopBytesOut = coopBytesIn = 0;
		coopRateStart = now;
	}
}

static void Coop_Status_f( void ) {
	static const char *states[] = { "idle", "listening", "connecting", "active" };
	Com_Printf( "role %d, state %s, port %d\n", coop.role, states[coop.state], NET_IP_LocalPort() );
	if ( coop.state == COOP_LINK_ACTIVE ) {
		Com_Printf( "peer %s \"%s\", last packet %d ms ago, reliable out %d pending, in %d\n",
			NET_AdrToString( coop.peer ), coop.peerName, Sys_Milliseconds() - coop.lastReceived,
			CoopRel_PendingCount( &coop.rel ), coop.rel.inSequence );
		Com_Printf( "bandwidth out %d kbit/s, in %d kbit/s\n", coopKbpsOut, coopKbpsIn );
	}
}

static void Coop_Say_f( void ) {
	if ( coop.state != COOP_LINK_ACTIVE ) {
		Com_Printf( "COOP: not connected\n" );
		return;
	}
	Coop_AddReliable( va( "say \"%s\"", Cmd_Args() ) );
}

static void Coop_SendTest_f( void ) {
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: coop_sendtest <bytes>\n" );
		return;
	}
	if ( coop.state != COOP_LINK_ACTIVE ) {
		Com_Printf( "COOP: not connected\n" );
		return;
	}
	coop.testPending = Coop_TestClamp( atoi( Cmd_Argv( 1 ) ) );
	coop.testRepeats = 1;
}

// ---------------------------------------------------------------- packets

static void Coop_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	MSG_BeginReading( msg );
	MSG_ReadLong( msg );	// -1
	Cmd_TokenizeString( MSG_ReadStringLine( msg ) );
	const char *c = Cmd_Argv( 0 );

	if ( coop.role == COOP_ROLE_HOST && !Q_stricmp( c, "coopDiscover" ) ) {
		NET_OutOfBandPrint( NS_SERVER, from, "coopInfo \"%s\" \"%s\" %d %d", coop_name->string,
			sv_mapname ? sv_mapname->string : "", coop.state == COOP_LINK_ACTIVE ? 1 : 0, atoi( Cmd_Argv( 1 ) ) == COOP_PROTOCOL_VERSION ? 1 : 0 );
		return;
	}
	if ( coopFindUntil && coop.state == COOP_LINK_IDLE && !Q_stricmp( c, "coopInfo" ) ) {
		const char *adr = NET_AdrToString( from );
		Com_Printf( "COOP: found \"%s\" at %s, map %s%s%s\n", Cmd_Argv( 1 ), adr, Cmd_Argv( 2 ),
			atoi( Cmd_Argv( 3 ) ) ? ", full" : "", atoi( Cmd_Argv( 4 ) ) ? "" : ", other version" );
		if ( !coopFound++ ) {
			Cvar_Set( "coop_connectaddr", adr );
		}
		return;
	}
	if ( coop.role == COOP_ROLE_HOST && !Q_stricmp( c, "coopConnect" ) ) {
		const int proto = atoi( Cmd_Argv( 1 ) );
		const int challenge = atoi( Cmd_Argv( 2 ) );
		if ( proto != COOP_PROTOCOL_VERSION ) {
			NET_OutOfBandPrint( NS_SERVER, from, "coopReject \"protocol %d, host has %d\"", proto, COOP_PROTOCOL_VERSION );
			return;
		}
		if ( Q_stricmp( Cmd_Argv( 4 ), COOP_BUILD_ID ) ) {	// design 28: both players run the same build
			NET_OutOfBandPrint( NS_SERVER, from, "coopReject \"different build (host %s, yours %s) - copy the host's game files\"",
				COOP_BUILD_ID, Cmd_Argv( 4 ) );
			return;
		}
		if ( coop_checkPaks->integer && atoi( Cmd_Argv( 6 ) ) != FS_CoopPakChecksum() ) {	// design 28
			NET_OutOfBandPrint( NS_SERVER, from, "coopReject \"different game data (pk3 files) than the host - same game version and zz_coop.pk3 needed, or coop_checkPaks 0 on the host\"" );
			return;
		}
		if ( coop_password->string[0] && strcmp( Cmd_Argv( 5 ), coop_password->string ) ) {
			NET_OutOfBandPrint( NS_SERVER, from, "coopReject \"wrong password\"" );
			return;
		}
		if ( coop.state == COOP_LINK_ACTIVE ) {
			if ( NET_CompareAdr( from, coop.peer ) && challenge == coop.challenge ) {
				NET_OutOfBandPrint( NS_SERVER, from, "coopAccept %d \"%s\"", challenge, coop_name->string );	// our accept was lost
			} else {
				NET_OutOfBandPrint( NS_SERVER, from, "coopReject \"game is full\"" );
			}
			return;
		}
		coop.challenge = challenge;
		Q_strncpyz( coop.peerName, Cmd_Argv( 3 ), sizeof( coop.peerName ) );
		NET_OutOfBandPrint( NS_SERVER, from, "coopAccept %d \"%s\"", challenge, coop_name->string );
		Coop_LinkUp( from );
		return;
	}

	if ( !NET_CompareAdr( from, coop.peer ) ) {
		return;
	}
	if ( coop.state == COOP_LINK_CONNECTING ) {
		if ( !Q_stricmp( c, "coopAccept" ) && atoi( Cmd_Argv( 1 ) ) == coop.challenge ) {
			Q_strncpyz( coop.peerName, Cmd_Argv( 2 ), sizeof( coop.peerName ) );
			Coop_LinkUp( from );
		} else if ( !Q_stricmp( c, "coopReject" ) ) {
			Com_Printf( "COOP: rejected: %s\n", Cmd_Argv( 1 ) );
			NET_IP_Close();
			Coop_Reset( COOP_ROLE_NONE, COOP_LINK_IDLE );
		}
		return;
	}
	if ( coop.state == COOP_LINK_ACTIVE && !Q_stricmp( c, "coopDisconnect" ) ) {
		Coop_CloseLink( va( "peer left (%s)", Cmd_Argv( 1 ) ), qfalse );
	}
}

static void Coop_ExecuteReliable( const char *s ) {
	if ( coop_debug->integer ) {
		Com_Printf( "COOP: <- %s\n", s );
	}
	Cmd_TokenizeString( s );
	const char *c = Cmd_Argv( 0 );
	if ( !Q_stricmp( c, "hello" ) ) {
		Com_Printf( "COOP: hello from %s\n", Cmd_Argv( 1 ) );
	} else if ( !Q_stricmp( c, "say" ) ) {
		Com_Printf( "COOP %s: %s\n", coop.peerName, Cmd_Argv( 1 ) );
	} else if ( !Q_stricmp( c, "loading" ) ) {
		coop.peerLoading = qtrue;
		coop.peerLoadingSince = Sys_Milliseconds();
		Com_Printf( "COOP: peer is loading a map\n" );
	} else if ( !Q_stricmp( c, "loaded" ) ) {
		coop.peerLoading = qfalse;
		Com_Printf( "COOP: peer finished loading\n" );
	} else if ( !Coop_ExecuteReliableCmd( s ) && coop_debug->integer ) {
		Com_Printf( "COOP: unknown reliable command \"%s\"\n", c );
	}
}

static void Coop_ParsePacket( msg_t *msg ) {
	CoopRel_Ack( &coop.rel, MSG_ReadLong( msg ) );
	while ( coop.state == COOP_LINK_ACTIVE ) {
		const int cmd = MSG_ReadByte( msg );
		if ( cmd == coop_eof ) {
			return;
		}
		if ( cmd == coop_reliable ) {
			const int seq = MSG_ReadLong( msg );
			char text[COOP_MAX_RELIABLE_LEN];
			Q_strncpyz( text, MSG_ReadString( msg ), sizeof( text ) );
			// never run handlers here (we may be inside a nested Com_EventLoop): Coop_Frame runs them.
			// A full queue leaves the command unacknowledged, so the peer sends it again.
			if ( !CoopInQ_Full( &coop.inQueue ) && CoopRel_Accept( &coop.rel, seq ) ) {
				CoopInQ_Push( &coop.inQueue, text );
			}
		} else if ( cmd == coop_test ) {
			if ( !Coop_ReadTest( msg ) ) {
				return;
			}
		} else if ( !Coop_ParsePayload( cmd, msg ) ) {
			Coop_CloseLink( va( "bad packet command %d", cmd ), qtrue );
			return;
		}
	}
}

static void Coop_RunInbound( void ) {
	const char *queued;
	while ( coop.state == COOP_LINK_ACTIVE && ( queued = CoopInQ_Front( &coop.inQueue ) ) != nullptr ) {
		char text[COOP_MAX_RELIABLE_LEN];
		Q_strncpyz( text, queued, sizeof( text ) );
		CoopInQ_Pop( &coop.inQueue );
		Coop_ExecuteReliable( text );
	}
}

static void Coop_PacketEvent( netadr_t from, msg_t *msg ) {
	if ( msg->cursize >= 4 && *(int *)msg->data == -1 ) {
		Coop_ConnectionlessPacket( from, msg );
		return;
	}
	if ( coop.state != COOP_LINK_ACTIVE || !NET_CompareAdr( from, coop.peer ) ) {
		return;
	}
	if ( !CoopNetchan_Process( &coop.chan, msg ) ) {
		return;	// out of order, duplicated or a fragment
	}
	coop.lastReceived = Sys_Milliseconds();
	Coop_CountBytes( 0, msg->cursize );
	Coop_ParsePacket( msg );
}

// sets the flag for its lifetime and clears it also when Com_Error unwinds through the parser
struct CoopReceiveGuard {
	qboolean &flag;
	explicit CoopReceiveGuard( qboolean &f ) : flag( f ) { flag = qtrue; }
	~CoopReceiveGuard() { flag = qfalse; }
};

// called from Com_EventLoop; reassembled messages can exceed the engine's MAX_MSGLEN event buffer
void Coop_ReceivePackets( void ) {
	static byte		buf[COOP_MAX_MSGLEN];
	static qboolean	receiving;
	if ( receiving ) {
		return;	// a nested Com_EventLoop (cgame loading screen, cl_cgame.cpp:863) must not overwrite buf mid-parse
	}
	CoopReceiveGuard guard( receiving );
	NET_IP_FlushSimulated();
	netadr_t	from;
	msg_t		msg;
	MSG_Init( &msg, buf, sizeof( buf ) );
	while ( NET_IP_GetPacket( &from, &msg ) ) {
		Coop_PacketEvent( from, &msg );
	}
}

static void Coop_SendPacket( qboolean reliableOnly ) {
	static byte buf[COOP_MAX_MSGLEN];
	msg_t msg;
	MSG_Init( &msg, buf, COOP_MAX_PAYLOAD );	// the largest message the coop netchan can deliver
	msg.allowoverflow = qtrue;					// an overflow sets msg.overflowed instead of ERR_FATAL (msg.cpp:74-84)
	MSG_WriteLong( &msg, coop.rel.inSequence );
	const int pending = CoopRel_PendingCount( &coop.rel );
	for ( int i = 0; i < pending; i++ ) {
		int seq;
		const char *cmd = CoopRel_Pending( &coop.rel, i, &seq );
		if ( msg.cursize + (int)strlen( cmd ) + 16 > msg.maxsize / 2 ) {
			break;	// the rest goes in a later packet; the peer needs them in order anyway
		}
		MSG_WriteByte( &msg, coop_reliable );
		MSG_WriteLong( &msg, seq );
		MSG_WriteString( &msg, cmd );
	}
	if ( !reliableOnly ) {
		Coop_WriteTest( &msg );
		Coop_WritePayload( &msg );	// writers stay within Coop_MsgRoom, leaving COOP_MSG_TAIL bytes
	}
	MSG_WriteByte( &msg, coop_eof );
	coop.lastSent = Sys_Milliseconds();
	if ( msg.overflowed ) {
		Com_Printf( "COOP: outgoing packet overflowed (%d bytes), dropped\n", msg.cursize );
		return;
	}
	CoopNetchan_Transmit( &coop.chan, msg.cursize, msg.data );
	Coop_CountBytes( msg.cursize, 0 );
}

void Coop_FlushNow( void ) {
	if ( coop.state != COOP_LINK_ACTIVE ) {
		return;
	}
	Coop_SendPacket( qtrue );
	Coop_SendPacket( qtrue );	// twice: one lost packet must not cost the link
}

void Coop_MapLoadStarting( void ) {
	coop.localLoading = qtrue;
	Coop_SessionMapLoading();
	if ( coop.state != COOP_LINK_ACTIVE ) {
		return;
	}
	Coop_AddReliable( "loading" );
	Coop_FlushNow();
	Com_Printf( "COOP: map load started, peer notified\n" );
}

void Coop_MapLoadFinished( void ) {
	coop.lastReceived = Sys_Milliseconds();	// nothing could be received during the synchronous load
	Coop_SessionMapLoaded();
}

void Coop_Frame( void ) {
	NET_IP_SetSimulation( coop_simLoss->integer, coop_simLatency->integer );
	NET_IP_FlushSimulated();
	const int now = Sys_Milliseconds();
	Coop_FindFrame( now );
	if ( coop.localLoading && Coop_LocalMapReady() ) {
		coop.localLoading = qfalse;
		if ( coop.state == COOP_LINK_ACTIVE ) {
			Coop_AddReliable( "loaded" );
			Com_Printf( "COOP: map loaded, peer notified\n" );
		}
	}
	if ( coop.state == COOP_LINK_CONNECTING ) {
		if ( now - coop.connectStart > COOP_CONNECT_TIMEOUT_MS ) {
			Com_Printf( "COOP: connection to %s timed out\n", NET_AdrToString( coop.peer ) );
			NET_IP_Close();
			Coop_Reset( COOP_ROLE_NONE, COOP_LINK_IDLE );
		} else if ( now - coop.lastSent >= COOP_CONNECT_RESEND_MS ) {
			NET_OutOfBandPrint( NS_CLIENT, coop.peer, "coopConnect %d %d \"%s\" \"%s\" \"%s\" %d",
				COOP_PROTOCOL_VERSION, coop.challenge, coop_name->string, COOP_BUILD_ID, coop_password->string, FS_CoopPakChecksum() );
			coop.lastSent = now;
		}
		return;
	}
	if ( coop.state != COOP_LINK_ACTIVE ) {
		return;
	}
	Coop_RunInbound();
	if ( coop.state != COOP_LINK_ACTIVE ) {
		return;	// a command closed the link
	}
	if ( coop.onLinkPending ) {
		coop.onLinkPending = qfalse;
		Cbuf_AddText( va( "%s\n", coop_onlink->string ) );
	}
	Coop_SessionFrame();
	int timeoutMs = ( coop_timeout->integer < COOP_MIN_TIMEOUT_SEC ? COOP_MIN_TIMEOUT_SEC : coop_timeout->integer ) * 1000;
	if ( coop.localLoading || ( coop.peerLoading && now - coop.peerLoadingSince < COOP_LOAD_TIMEOUT_MS ) ) {
		timeoutMs = COOP_LOAD_TIMEOUT_MS;
	}
	if ( now - coop.lastReceived > timeoutMs ) {
		Coop_CloseLink( "timed out", qtrue );
		return;
	}
	if ( now - coop.lastSent >= COOP_PACKET_INTERVAL_MS ) {
		Coop_SendPacket( qfalse );
	}
}

void Coop_Init( void ) {
	coop_port		= Cvar_Get( "coop_port", va( "%d", COOP_DEFAULT_PORT ), CVAR_ARCHIVE );
	coop_name		= Cvar_Get( "coop_name", "Padawan", CVAR_ARCHIVE );
	coop_timeout	= Cvar_Get( "coop_timeout", "20", 0 );
	coop_debug		= Cvar_Get( "coop_debug", "0", 0 );
	coop_password	= Cvar_Get( "coop_password", "", 0 );
	coop_checkPaks	= Cvar_Get( "coop_checkPaks", "1", CVAR_ARCHIVE );	// host: refuse a guest with other game data
	Cvar_Get( "coop_connectaddr", "", CVAR_ARCHIVE );		// the Coop menu's host address	// host: required from the guest; guest: sent to the host
	coop_autotest	= Cvar_Get( "coop_autotest", "0", 0 );	// >0: send a coop_test payload of that size on link up
	coop_autotestCount	= Cvar_Get( "coop_autotestCount", "1", 0 );	// packets in a row that carry the coop_autotest payload
	coop_simLoss		= Cvar_Get( "coop_simLoss", "0", 0 );		// % of outgoing coop UDP packets dropped (testing)
	coop_simLatency		= Cvar_Get( "coop_simLatency", "0", 0 );	// ms added to outgoing coop UDP packets (testing)
	coop_onlink		= Cvar_Get( "coop_onlink", "", 0 );	// console commands run once after each link up (testing)
	coop_role		= Cvar_Get( "coop_role", "0", CVAR_ROM );
	Cmd_AddCommand( "coop_host", Coop_Host_f );
	Cmd_AddCommand( "coop_connect", Coop_Connect_f );
	Cmd_AddCommand( "coop_disconnect", Coop_Disconnect_f );
	Cmd_AddCommand( "coop_status", Coop_Status_f );
	Cmd_AddCommand( "coop_find", Coop_Find_f );
	Cmd_AddCommand( "coop_say", Coop_Say_f );
	Cmd_AddCommand( "coop_sendtest", Coop_SendTest_f );
	Cmd_AddCommand( "coop_reloadmap", Coop_ReloadMap_f );
	Coop_SessionInit();
	CoopSnapshot_Init();
	Coop_Reset( COOP_ROLE_NONE, COOP_LINK_IDLE );
}

void Coop_Shutdown( void ) {
	if ( coop.state == COOP_LINK_ACTIVE ) {
		Coop_CloseLink( "quit", qtrue );
	}
	NET_IP_Close();
}
