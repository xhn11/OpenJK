#pragma once
// Who simulates an NPC (design 6, 9). Pure module: no engine headers (unit tested), compiled into
// the JK2 game DLL. Players: 0 host, 1 guest (the host's numbering); owners: COOP_OWNER_* values.

#define COOP_OWNER_HYSTERESIS_MS	2000

typedef struct {
	int		enemyPlayer;		// -1: no player as enemy, 0: the host's player, 1: the guest's player
	int		scripted;			// an ICARUS task runs on it
	int		ally;				// TEAM_PLAYER
	int		forcedPlayer;		// -1, or the player a saber lock / grip binds it to
	int		pullAll;			// the host wants everything back (save, cutscene, level change)
	int		currentOwner;		// 1 host, 2 guest
	int		lastTransferTime;
	int		now;
	int		inAttack;			// in the middle of an attack animation
	int		dead;
} coopOwnerInput_t;

// the owner the NPC should have now (1 host, 2 guest); equals currentOwner when nothing should change
int CoopOwner_Decide( const coopOwnerInput_t *in );
