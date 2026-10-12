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
int			CoopRel_FreeSlots( const coopReliable_t *r );		// how many more commands CoopRel_Add accepts

#define COOP_MAX_INBOUND		128

struct coopInQueue_t {			// reliable commands received in Com_EventLoop, executed later by Coop_Frame
	int		head;
	int		count;
	char	cmds[COOP_MAX_INBOUND][COOP_MAX_RELIABLE_LEN];
};

void		CoopInQ_Init( coopInQueue_t *q );
bool		CoopInQ_Push( coopInQueue_t *q, const char *cmd );		// false: full or too long
bool		CoopInQ_Full( const coopInQueue_t *q );
const char	*CoopInQ_Front( const coopInQueue_t *q );				// nullptr when empty
void		CoopInQ_Pop( coopInQueue_t *q );
