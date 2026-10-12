#include "coop_adr.h"
#include <cstring>

bool CoopAdr_SplitHostPort( const char *s, char *host, int hostSize, int *port ) {
	if ( !s || !*s || hostSize <= 0 ) {
		return false;
	}
	const char *colon = strrchr( s, ':' );
	const size_t hostLen = colon ? (size_t)( colon - s ) : strlen( s );
	if ( hostLen == 0 || hostLen >= (size_t)hostSize ) {
		return false;
	}
	memcpy( host, s, hostLen );
	host[hostLen] = '\0';
	*port = 0;
	if ( !colon ) {
		return true;
	}
	const char *p = colon + 1;
	if ( !*p ) {
		return false;
	}
	long value = 0;
	for ( ; *p; p++ ) {
		if ( *p < '0' || *p > '9' ) {
			return false;
		}
		value = value * 10 + ( *p - '0' );
		if ( value > 65535 ) {
			return false;
		}
	}
	if ( value == 0 ) {
		return false;
	}
	*port = (int)value;
	return true;
}
