#include "coop_test.h"
#include "coop_adr.h"
#include <cstring>

COOP_TEST( adr_host_only ) {
	char h[64]; int p = -1;
	CHECK( CoopAdr_SplitHostPort( "192.168.1.5", h, sizeof( h ), &p ) );
	CHECK( !strcmp( h, "192.168.1.5" ) );
	CHECK_EQ( p, 0 );
}
COOP_TEST( adr_host_and_port ) {
	char h[64]; int p = 0;
	CHECK( CoopAdr_SplitHostPort( "10.0.0.2:29170", h, sizeof( h ), &p ) );
	CHECK( !strcmp( h, "10.0.0.2" ) );
	CHECK_EQ( p, 29170 );
}
COOP_TEST( adr_hostname_and_port ) {
	char h[64]; int p = 0;
	CHECK( CoopAdr_SplitHostPort( "my.host.net:1234", h, sizeof( h ), &p ) );
	CHECK( !strcmp( h, "my.host.net" ) );
	CHECK_EQ( p, 1234 );
}
COOP_TEST( adr_rejects_bad_input ) {
	char h[64]; int p;
	CHECK( !CoopAdr_SplitHostPort( "", h, sizeof( h ), &p ) );
	CHECK( !CoopAdr_SplitHostPort( ":29170", h, sizeof( h ), &p ) );
	CHECK( !CoopAdr_SplitHostPort( "1.2.3.4:", h, sizeof( h ), &p ) );
	CHECK( !CoopAdr_SplitHostPort( "1.2.3.4:0", h, sizeof( h ), &p ) );
	CHECK( !CoopAdr_SplitHostPort( "1.2.3.4:70000", h, sizeof( h ), &p ) );
	CHECK( !CoopAdr_SplitHostPort( "1.2.3.4:12ab", h, sizeof( h ), &p ) );
}
COOP_TEST( adr_rejects_host_too_long ) {
	char h[4]; int p;
	CHECK( !CoopAdr_SplitHostPort( "abcdef", h, sizeof( h ), &p ) );
}
