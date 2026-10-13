
#pragma once
// Coop (2 players) in the JK2 game DLL. The engine side lives in code/coop.

#define COOP_MODE_SOLO		0
#define COOP_MODE_HOST		1		// same values as the engine's coopRole_t and coopOwner_t
#define COOP_MODE_GUEST		2

extern int		g_coopMode;				// latched in InitGame from the engine cvar coop_role, fixed for the level
extern qboolean	g_coopSpawningReplica;	// G_SpawnAtNum is building a replica of a host entity

void		G_CoopInitGame( void );									// InitGame, before any entity spawns
void		G_CoopSpawnRange( int *first, int *lastExclusive, qboolean *topDown );	// G_Spawn: where and in which direction
void		G_CoopHookImports( void );								// GetGameAPI, after gi = *import

// ownership (design 2, 3, 6): the owner simulates an entity, the other machine only replays it
int			G_CoopOwner( const gentity_t *ent );					// COOP_MODE_HOST or COOP_MODE_GUEST
int			G_CoopPartnerMode( void );								// the other machine's mode
qboolean	G_CoopOwnsEnt( const gentity_t *ent );					// solo: always qtrue
void		G_CoopSetOwner( gentity_t *ent, int owner );
void		G_CoopEntityInit( gentity_t *ent );						// G_InitGentity: owner by who allocates, portal count 0

// what the guest may do to host entities (M5 turns the blocked cases into messages to the host)
qboolean	G_CoopIsPhysicalTrigger( const gentity_t *ent );		// design 17: each machine runs these for its own player
qboolean	G_CoopRunsLocally( const gentity_t *ent );				// G_RunFrame: think/move here
qboolean	G_CoopBlockUse( const gentity_t *self );
qboolean	G_CoopBlockTouch( const gentity_t *self );
qboolean	G_CoopBlockDamage( const gentity_t *targ );

// areaportals: net count of open calls per entity (the engine keeps reference counts)
void		G_CoopAdjustAreaPortal( gentity_t *ent, qboolean open );
int			G_CoopPortalCount( int entNum );

void		G_CoopEnts_f( void );									// console: coop_ents

// replicas (g_coop.cpp, g_coop_replica.cpp)
gentity_t	*G_SpawnAtNum( int num );								// a replica on exactly this free slot, owned by the host
int			G_Coop_EntityOwner( int entNum );						// game exports, see code/game/coop_shared.h
void		G_Coop_GetEntityExtra( int entNum, coopEntExtra_t *out );
void		G_Coop_ApplyEntity( int entNum, const entityState_t *es, const playerState_t *ps, const coopEntExtra_t *ex );
void		G_Coop_FreeEntity( int entNum );
void		G_CoopEntInfo_f( void );								// console: coop_entinfo <num>
// game commands to the partner's game (code/coop: reliable "g <text>")
int			G_CoopWireNum( int entNum );							// our entity number in the partner's numbering
qboolean	G_CoopPartnerInWorld( void );
void		G_CoopSend( const char *fmt, ... );						// "<name> <args>", dropped when the partner is not here
void		G_Coop_GameCommand( void );								// game export: argv(1) is the name
void		G_CoopGTest_f( void );									// console: coop_gtest <text>
// interactions across the ownership boundary (g_coop_act.cpp, design 7, 16-19)
void		G_CoopForwardDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, const vec3_t dir,
				const vec3_t point, int damage, int dflags, int mod, int hitLoc );	// G_Damage on a target we do not own
qboolean	G_CoopForwardUse( gentity_t *self, gentity_t *other, gentity_t *activator );	// qtrue: not ours, sent / dropped
qboolean	G_CoopSharedItem( const gitem_t *item );				// keys: both players get them
qboolean	G_CoopItemTakenBy( const gentity_t *item, const gentity_t *player );	// weapons: this player has taken it
qboolean	G_CoopItemTake( const gentity_t *item, const gentity_t *player, qboolean *first );	// qtrue: the item can go
qboolean	G_CoopItemHidden( int num );							// cgame: our player took this weapon
void		G_CoopItemReset( int num );								// a new entity in this slot
int			G_CoopItemTakenBits( int num );							// replication of who took a weapon item
void		G_CoopItemSetTakenBits( int num, int bits );
void		G_CoopSendPickup( const gentity_t *item );				// the partner gets this item
qboolean	G_CoopForwardThrow( gentity_t *self, gentity_t *target, qboolean pull );	// qtrue: the target's owner pushes it
void		G_CoopForwardAlert( int type, const gentity_t *owner, const vec3_t pos, float radius, int alertLevel, float extra );	// AddSound/SightEvent
void		G_CoopRunPuppet( gentity_t *ent );						// G_RunFrame, entity 1
void		G_CoopUnstickBegin( gentity_t *ent );					// ClientThink_real around Pmove: partner bodies we are inside are not solid
void		G_CoopUnstickEnd( void );
void		G_CoopHurt_f( void );									// console: coop_hurt <entity> <damage>
void		G_CoopUse_f( void );									// console: coop_use <entity>
// NPC authority migration (g_coop_npc.cpp, design 6, 9)
void		G_CoopNpcInit( void );
void		G_CoopNpcFrame( void );									// G_RunFrame (host): who should run which NPC
qboolean	G_CoopNpcGiving( const gentity_t *ent );				// handed to the partner, frozen until it confirms
void		G_CoopGiveNpc( gentity_t *ent );
qboolean	G_CoopPullAllToHost( qboolean pull );					// host: qtrue when every NPC is home
void		G_CoopNpcBeforeDamage( gentity_t *targ, int damage );	// G_Damage: a guest's killing blow sends the NPC home
qboolean	G_CoopNpcKeep( gentity_t *ent );						// host: a guest NPC that would be freed comes home
void		G_CoopNpcCommand( const char *name );
void		G_CoopNpcs_f( void );									// console: coop_npcs
void		G_CoopNpcGive_f( void );								// console: coop_npcgive <entity>
// replica motion (design 4, 16)
void		G_CoopRunReplica( gentity_t *ent );						// G_RunFrame on the guest, every host entity
void		G_CoopRunReplicaMover( gentity_t *ent );				// g_mover.cpp: a host mover pushes only our entities
void		G_CoopSetPortalCount( gentity_t *ent, int count );		// match the host's areaportal open count
// two players (g_coop.cpp, M7): entity 0 is our player, entity 1 the partner's puppet
qboolean	G_IsPlayer( const gentity_t *ent );
gentity_t	*G_PlayerNum( int i );									// NULL when that player is not here
gentity_t	*G_ClosestPlayer( const vec3_t org, qboolean aliveOnly );	// never NULL: falls back to entity 0
qboolean	G_AnyPlayerAlive( void );
// player 2 as Jan / Luke (g_coop_p2.cpp, M8, design 11, 15, 26)
#define COOP_P2_NONE		0
#define COOP_P2_JAN			1
#define COOP_P2_LUKE		2
extern int	g_coopP2Char;
void		G_CoopP2Init( void );
void		G_CoopP2Frame( void );									// G_RunFrame (host): decide, tell the guest, Luke's Force
void		G_CoopP2Command( const char *name );
const char	*G_CoopP2Name( void );									// "jan" / "luke"
qboolean	G_CoopP2HasForce( void );
void		G_CoopP2Restrict( gentity_t *ent );						// guest as Jan: no saber, no Force
qboolean	G_CoopFindSpotNear( gentity_t *ent, const vec3_t org, vec3_t out );
void		G_CoopTeleportNear( gentity_t *ent, const vec3_t org );	// next to org, never telefragging
// cutscenes (g_coop_cam.cpp, M9, design 14, 21)
struct interface_export_s;
void		G_CoopCamGroup( const gentity_t *ent, const char *group );	// Q3_CameraGroup
void		G_CoopCamGroupSettings( void );							// Q3_CameraGroupZOfs / Tag
qboolean	G_CoopCinematicSkipPressed( void );						// qtrue: skip now
void		G_CoopCamCommand( const char *name );
void		G_CoopHookCamera( struct interface_export_s *pe );		// Interface_Init: camera calls also go to the guest
// death, respawn, player 2's state, skipping levels (g_coop_life.cpp, M10, design 10, 13, 22-24)
void		G_CoopLifeInit( void );
void		G_CoopLifeFrame( void );								// G_RunFrame
void		G_CoopLifeCommand( const char *name );
qboolean	G_CoopRespawnThink( gentity_t *ent );					// ClientThink, dead: qtrue = coop handles it
void		G_CoopSkipLevel_f( void );								// console: coop_skiplevel
qboolean	G_CoopSaveEnt( const gentity_t *ent );					// WriteGEntities: ours, and the guest's NPCs
qboolean	G_CoopNoFriendlyFire( const gentity_t *targ, const gentity_t *attacker );	// G_Damage: players never hurt each other
void		G_CoopAlliesFollow( void );								// G_RunFrame (host): allies follow player 2 while Kyle is down
gentity_t	*G_CoopStandIn( void );									// host: the NPC of player 2's character, NULL if none
void		G_CoopStandInRunFrame( void );							// G_RunFrame: hide / show the stand-in, the guest in cutscenes
// guns, turrets, cameras owned by who uses them (g_coop_act.cpp, design 26)
void		G_CoopClaimInit( void );
void		G_CoopClaimFrame( void );
qboolean	G_CoopClaimUse( gentity_t *self, gentity_t *activator );	// GEntity_UseFunc (host): qtrue = handled
qboolean	G_CoopClaimKeep( gentity_t *ent );
qboolean	G_CoopForwardMindTrick( gentity_t *self, gentity_t *target );	// ForceTelepathy: qtrue = the owner applies it
void		G_CoopTriggerFrame( void );								// G_RunFrame (host): physical triggers on/off to the guest
void		G_CoopCenterPrint( const char *text );					// local only: coop messages (Czech, no diacritics: the fonts lack them)
void		G_CoopNpcInfo_f( void );								// console: coop_npcinfo
void		G_CoopDropPuppet_f( void );								// console: coop_droppuppet (testing)
void		G_CoopNpcMove_f( void );								// console: coop_npcmove <npc> <x> <y> <z> (testing)
void		G_CoopDropItem_f( void );								// console: coop_dropitem <classname> <x> <y> <z> (testing)
int			G_CoopLastDroppedItem( void );						// the entity of the last coop_dropitem, -1: none
