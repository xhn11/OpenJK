#pragma once
// Entity numbers shared by both coop machines. Pure module: no engine headers (unit tested), also
// compiled into the JK2 game DLL. Mirrors q_shared.h (JK2_MODE): MAX_CLIENTS 2,
// ENTITYNUM_MAX_NORMAL 1022 - static_asserts in codeJK2/game/g_coop.cpp and code/coop/coop_snapshot.cpp.

#define COOP_ENT_MAX_CLIENTS		2
#define COOP_ENT_MAX_NORMAL			1022

// Each machine is entity 0 itself and sees the partner as entity 1, so 0 and 1 swap on the wire.
int		Coop_SwapEntNum( int n );

// Where G_Spawn may allocate: always the vanilla range [MAX_CLIENTS, MAX_NORMAL). SP maps keep almost
// all of it busy (kejim_post: ~970 of 993 slots alive after loading), so no fixed split between host
// and guest fits. Instead the direction separates them: map entities (loading) and the host allocate
// bottom-up like vanilla, so both machines give map entities the same numbers; after the load the
// guest allocates top-down, so its few own entities stay away from the host's until the map is full.
// role: 0 solo, 1 host, 2 guest. loading: map entities are being spawned.
void	Coop_DynamicRange( int role, int loading, int *first, int *lastExclusive );
int		Coop_SpawnTopDown( int role, int loading );
