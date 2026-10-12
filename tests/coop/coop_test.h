#pragma once
#include <cstdio>
#include <vector>

struct CoopTest { const char *name; void (*fn)(); };
std::vector<CoopTest> &CoopTestRegistry();
extern int g_coopTestFailures;

struct CoopTestRegistrar {
	CoopTestRegistrar( const char *n, void (*f)() ) { CoopTestRegistry().push_back( { n, f } ); }
};

#define COOP_TEST( name ) \
	static void name(); \
	static CoopTestRegistrar name##_registrar( #name, name ); \
	static void name()

#define CHECK( cond ) do { if ( !( cond ) ) { \
	std::printf( "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond ); \
	g_coopTestFailures++; } } while ( 0 )

#define CHECK_EQ( a, b ) do { long long _a = (long long)( a ), _b = (long long)( b ); \
	if ( _a != _b ) { std::printf( "  %s:%d: CHECK_EQ(%s, %s) failed: %lld != %lld\n", \
		__FILE__, __LINE__, #a, #b, _a, _b ); g_coopTestFailures++; } } while ( 0 )
