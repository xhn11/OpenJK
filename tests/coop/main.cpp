#include "coop_test.h"

std::vector<CoopTest> &CoopTestRegistry() { static std::vector<CoopTest> r; return r; }
int g_coopTestFailures = 0;

int main() {
	int failedTests = 0;
	for ( const CoopTest &t : CoopTestRegistry() ) {
		const int before = g_coopTestFailures;
		t.fn();
		const bool ok = g_coopTestFailures == before;
		std::printf( "%s %s\n", ok ? "[PASS]" : "[FAIL]", t.name );
		if ( !ok ) failedTests++;
	}
	std::printf( "%d tests, %d failed\n", (int)CoopTestRegistry().size(), failedTests );
	return failedTests ? 1 : 0;
}
