#include "q_shared.h"
#include "qcommon.h"

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
	typedef int socklen_t;
#	define SOCK_LAST_ERROR	WSAGetLastError()
#	define SOCK_WOULDBLOCK	WSAEWOULDBLOCK
#	define SOCK_CONNRESET	WSAECONNRESET
#else
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <netdb.h>
#	include <unistd.h>
#	include <fcntl.h>
#	include <errno.h>
	typedef int SOCKET;
#	define INVALID_SOCKET	(-1)
#	define SOCKET_ERROR		(-1)
#	define closesocket		close
#	define SOCK_LAST_ERROR	errno
#	define SOCK_WOULDBLOCK	EAGAIN
#	define SOCK_CONNRESET	ECONNRESET
#endif

static SOCKET	ip_socket = INVALID_SOCKET;
static int		ip_port = 0;

static qboolean NET_IP_Startup( void ) {
#ifdef _WIN32
	static qboolean started = qfalse;
	if ( !started ) {
		WSADATA wsa;
		if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 ) {
			Com_Printf( "NET_IP: WSAStartup failed\n" );
			return qfalse;
		}
		started = qtrue;
	}
#endif
	return qtrue;
}

#define NET_IP_SOCKET_BUFFER	( 1024 * 1024 )		// room for bursts of fragments (a coop message is up to 55 of them)

static qboolean NET_IP_SetNonBlocking( SOCKET s ) {
#ifdef _WIN32
	u_long nonBlocking = 1;
	return (qboolean)( ioctlsocket( s, FIONBIO, &nonBlocking ) != SOCKET_ERROR );
#else
	const int flags = fcntl( s, F_GETFL, 0 );
	return (qboolean)( flags != -1 && fcntl( s, F_SETFL, flags | O_NONBLOCK ) != -1 );
#endif
}

static void NET_IP_SetBufferSize( SOCKET s, int option, const char *name ) {
	const int size = NET_IP_SOCKET_BUFFER;
	if ( setsockopt( s, SOL_SOCKET, option, (const char *)&size, sizeof( size ) ) == SOCKET_ERROR ) {
		Com_DPrintf( "NET_IP_Open: setting %s to %d failed (%d)\n", name, size, SOCK_LAST_ERROR );
	}
}

qboolean NET_IP_Open( int port ) {
	NET_IP_Close();
	if ( port != PORT_ANY && ( port < 1 || port > 65535 ) ) {
		Com_Printf( "NET_IP_Open: invalid port %d (use 1..65535)\n", port );
		return qfalse;
	}
	if ( !NET_IP_Startup() ) {
		return qfalse;
	}
	SOCKET s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( s == INVALID_SOCKET ) {
		Com_Printf( "NET_IP_Open: socket failed (%d)\n", SOCK_LAST_ERROR );
		return qfalse;
	}
	if ( !NET_IP_SetNonBlocking( s ) ) {
		Com_Printf( "NET_IP_Open: cannot make the socket non-blocking (%d)\n", SOCK_LAST_ERROR );
		closesocket( s );
		return qfalse;
	}
	NET_IP_SetBufferSize( s, SO_RCVBUF, "SO_RCVBUF" );
	NET_IP_SetBufferSize( s, SO_SNDBUF, "SO_SNDBUF" );
	struct sockaddr_in addr;
	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( (unsigned short)( port == PORT_ANY ? 0 : port ) );
	if ( bind( s, (struct sockaddr *)&addr, sizeof( addr ) ) == SOCKET_ERROR ) {
		Com_Printf( "NET_IP_Open: bind to port %d failed (%d)\n", port, SOCK_LAST_ERROR );
		closesocket( s );
		return qfalse;
	}
	socklen_t len = sizeof( addr );
	getsockname( s, (struct sockaddr *)&addr, &len );
	ip_port = ntohs( addr.sin_port );
	ip_socket = s;
	return qtrue;
}

void NET_IP_Close( void ) {
	if ( ip_socket != INVALID_SOCKET ) {
		closesocket( ip_socket );
		ip_socket = INVALID_SOCKET;
	}
	ip_port = 0;
}

int NET_IP_LocalPort( void ) {
	return ip_port;
}

qboolean NET_IP_GetPacket( netadr_t *from, msg_t *msg ) {
	while ( ip_socket != INVALID_SOCKET ) {
		struct sockaddr_in addr;
		socklen_t len = sizeof( addr );
		const int ret = recvfrom( ip_socket, (char *)msg->data, msg->maxsize, 0, (struct sockaddr *)&addr, &len );
		if ( ret == SOCKET_ERROR ) {
			const int err = SOCK_LAST_ERROR;
			if ( err == SOCK_CONNRESET ) {
				continue;	// ICMP port unreachable from a previous send (Windows), skip it
			}
#ifdef _WIN32
			if ( err == WSAEMSGSIZE ) {
				static qboolean warned = qfalse;
				if ( !warned ) {
					Com_DPrintf( "NET_IP_GetPacket: datagram larger than %d bytes dropped\n", msg->maxsize );
					warned = qtrue;
				}
				continue;	// truncated datagram, skip it
			}
#endif
			if ( err != SOCK_WOULDBLOCK ) {
				Com_Printf( "NET_IP_GetPacket: error %d\n", err );
			}
			return qfalse;
		}
		if ( ret >= msg->maxsize ) {
			Com_Printf( "NET_IP_GetPacket: oversize packet dropped\n" );
			continue;
		}
		memset( from, 0, sizeof( *from ) );
		from->type = NA_IP;
		memcpy( from->ip, &addr.sin_addr, 4 );
		from->port = addr.sin_port;
		msg->cursize = ret;
		msg->readcount = 0;
		msg->bit = 0;
		return qtrue;
	}
	return qfalse;
}

void Sys_SendPacket( int length, const void *data, netadr_t to ) {
	if ( ip_socket == INVALID_SOCKET || to.type != NA_IP ) {
		return;
	}
	struct sockaddr_in addr;
	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	memcpy( &addr.sin_addr, to.ip, 4 );
	addr.sin_port = to.port;
	if ( sendto( ip_socket, (const char *)data, length, 0, (struct sockaddr *)&addr, sizeof( addr ) ) == SOCKET_ERROR ) {
		const int err = SOCK_LAST_ERROR;
		if ( err != SOCK_WOULDBLOCK ) {
			Com_Printf( "Sys_SendPacket: error %d\n", err );
		}
	}
}

qboolean Sys_StringToAdr( const char *s, netadr_t *a ) {
	if ( !NET_IP_Startup() ) {
		return qfalse;
	}
	struct addrinfo hints, *res = NULL;
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if ( getaddrinfo( s, NULL, &hints, &res ) != 0 || !res ) {
		return qfalse;
	}
	memset( a, 0, sizeof( *a ) );
	a->type = NA_IP;
	memcpy( a->ip, &( (struct sockaddr_in *)res->ai_addr )->sin_addr, 4 );
	freeaddrinfo( res );
	return qtrue;
}
