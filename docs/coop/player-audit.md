# Audit: code that assumes one player (M7)

`scripts/coop/audit.ps1 [-Detail]` lists every line in `codeJK2/game` that compares with entity 0
(`g_entities[0]`, `s.number == 0`, `!ent->s.number`, `player->`, `level.clients[0]`) and does not
already go through the coop helpers (`G_IsPlayer`, `G_PlayerNum`, `G_ClosestPlayer`). About 400 lines
on 2026-10-12. Each machine runs its own player as entity 0 natively, so most of them stay correct.
The partner is entity 1, a puppet that only replays its owner's snapshots.

## Decisions by category

| Category | Files (most lines) | Decision |
|---|---|---|
| Own player's movement, weapons, saber, Force, HUD, view, camera | `bg_pmove`, `wp_saber`, `g_active`, `g_weapon`, `g_client`, cgame | **Stays.** Entity 0 is the local player on each machine. The partner's actions on our entities arrive as hits, uses and pushes (`g_coop_act.cpp`). |
| AI picking a player as enemy | `NPC_combat` (`NPC_PickEnemy` findPlayersFirst), `NPC_utils` (`NPC_CheckPlayerDistance`, `NPC_FindPlayer`, alerts), `AI_Stormtrooper`, `AI_Howler`, `AI_MineMonster` | **Changed:** both players. |
| AI hearing / seeing the player | `NPC_senses` (`G_CheckAlertEvents`, `AddSoundEvent`, `AddSightEvent`) | **Changed:** both players alive count. The local player's alerts are forwarded (`alert`). |
| Triggers with PLAYERONLY, "touched by the player" | `g_trigger` (`Touch_Multi`, `trigger_multiple`) | **Changed:** either player. The host runs `G_TouchTriggers` for the puppet. `trigger_push` / `trigger_hurt` PLAYERONLY stay "own player": they are physical and each machine runs them for its own player. |
| Items "only the player picks up" | `g_items` (`Touch_Item`) | **Changed:** either player. The host decides, the guest's pickups go back as `pickup`. |
| Corpse removal while the player looks | `NPC.cpp` | **Changed:** neither player may see it. |
| Script effects on Kyle | `Q3_Interface` (`Q3_Set`) | **Mirrored:** to player 2 (`kset`). Objectives and mission state go to both. |
| Death / respawn / level end | `g_combat` (`player_die`), `g_active` (respawn), `g_utils` (`G_ChangeMap`) | **Coop flow:** respawn next to the partner. Level changes are started by the host, the guest follows. |
| Savegame client 0 | `g_savegame` | **Stays:** the host saves its own client, player 2's state goes in chunk `COP2`. |
| Jedi AI special cases, Seeker / Sentry / Remote droids, ATST, turrets, E-web, emplaced guns | `AI_Jedi`, `AI_Seeker`, `AI_Sentry`, `AI_Remote`, `wp_atst`, `g_turret`, `g_emplaced` | **Open:** they still treat player 0 (Kyle on the host) as "the player". Turret and E-web ownership (design 26) is not done. |
| Debug and cheat commands | `g_svcmds` | **Stays:** local player. |
