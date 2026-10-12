#pragma once
// Types shared by the engine (code/coop) and the JK2 game DLL for coop replication. Both are always
// built together: a layout change bumps COOP_EXTRA_VERSION and COOP_PROTOCOL_VERSION.
// Include after q_shared.h (entityState_t, playerState_t).
#include <type_traits>

#define COOP_EXTRA_VERSION	2

typedef enum {
	COOP_OWNER_NONE		= -1,	// free slot
	COOP_OWNER_HOST		= 1,	// same values as coopRole_t
	COOP_OWNER_GUEST	= 2
} coopOwner_t;

// Game state of an entity that the engine does not see in entityState_t / playerState_t but the other
// machine needs to rebuild it. Entity numbers are in the receiver's numbering (the sender swaps 0 <-> 1).
typedef struct coopEntExtra_s {
	int		inuse;
	char	classname[32];
	char	npcType[32];			// NPC_type of entities with a client, "" otherwise
	int		spawnflags;
	int		health, maxHealth;
	int		ownerNum, enemyNum;		// ENTITYNUM_NONE when unset
	int		flags, svFlags, contents;
	int		hasClient;
	int		linked;
	float	mins[3], maxs[3];
	int		areaPortalOpen;			// net count of AdjustAreaPortalState(open) minus (close) since the entity spawned
	int		triggerEnabled;			// !(svFlags & SVF_INACTIVE); applied from M5
	int		buttons;				// clients: usercmd buttons (BUTTON_USE triggers on the other machine)
} coopEntExtra_t;

static_assert( std::is_trivially_copyable<coopEntExtra_t>::value, "coopEntExtra_t is replicated raw" );
static_assert( sizeof( coopEntExtra_t ) % 4 == 0, "the delta codec works on 32-bit words" );
static_assert( std::is_trivially_copyable<entityState_t>::value, "coop replicates entityState_t raw" );
static_assert( sizeof( entityState_t ) % 4 == 0, "the delta codec works on 32-bit words" );
// playerState_t is copied raw too, except saberModel: a pointer into the sender's memory (plan-M3 R1)
static_assert( std::is_trivially_copyable<playerState_t>::value, "coop replicates playerState_t raw" );
static_assert( sizeof( playerState_t ) % 4 == 0, "the delta codec works on 32-bit words" );
