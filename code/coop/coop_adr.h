#pragma once
// Splits "host[:port]". *port = 0 when no port is given. IPv4/hostname only.
bool CoopAdr_SplitHostPort( const char *s, char *host, int hostSize, int *port );
