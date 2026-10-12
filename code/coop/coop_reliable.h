#pragma once
#define COOP_MAX_RELIABLE		128
#define COOP_MAX_RELIABLE_LEN	1024

struct coopReliable_t {
	int		outSequence;		// last sequence assigned to an outgoing command
	int		outAcknowledged;	// last sequence the peer confirmed
	char	outCmds[COOP_MAX_RELIABLE][COOP_MAX_RELIABLE_LEN];
	int		inSequence;			// last sequence executed from the peer
};

void		CoopRel_Init( coopReliable_t *r );
bool		CoopRel_Add( coopReliable_t *r, const char *cmd );
void		CoopRel_Ack( coopReliable_t *r, int ack );
int			CoopRel_PendingCount( const coopReliable_t *r );
const char	*CoopRel_Pending( const coopReliable_t *r, int index, int *seq );
bool		CoopRel_Accept( coopReliable_t *r, int seq );
