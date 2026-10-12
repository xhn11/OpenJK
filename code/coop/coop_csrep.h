#pragma once
// Which configstrings the guest still needs (host -> guest). Pure module (unit tested).

#define COOP_CS_MAX		1300		// >= MAX_CONFIGSTRINGS of both SP engines (JK2 1024, JA 1300)

struct coopCsRep_t {
	int		version[COOP_CS_MAX];	// 0: unchanged since CoopCs_Reset
	int		seq;					// last version handed out
};

void	CoopCs_Reset( coopCsRep_t *r );
void	CoopCs_Touch( coopCsRep_t *r, int index );							// index changed: it gets the next version
int		CoopCs_Select( const coopCsRep_t *r, int acked, int *out, int maxOut );	// indices changed after `acked`, oldest change first
