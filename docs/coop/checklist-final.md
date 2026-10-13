# Checklist – ruční zkouška coopu (M4–M11)

Spuštění na jednom PC: `powershell -ExecutionPolicy Bypass -File scripts/coop/play-pair.ps1` (host vlevo, guest vpravo). Na dvou PC: složka `dist\JK2Coop` z `scripts/coop/package.ps1`, menu COOP nebo `.bat` soubory.

Ke každému bodu si poznamenej OK nebo co je špatně (případně s `qconsole.log` obou stran).

## Partner (M4)
1. Oba se vidí: běh, skok, přikrčení, otáčení. Animace jsou plynulé a postava nesklouzává.
2. Guest je **Jan** (kejim_post), host je Kyle. Jméno je nad hlavou a vlevo nahoře je jméno, zdraví a štít partnera.
3. Zbraň v ruce partnera odpovídá. Výstřely partnera jsou vidět i slyšet.

## Boj a interakce (M5, M6)
4. Guest zastřelí stormtroopera. Host vidí, jak padá.
5. Trooper hosta střílí na guesta a guestovi ubývá zdraví.
6. Guest otevře dveře tlačítkem a host je vidí otevřené.
7. Guest sebere munici nebo lékárničku a u hosta zmizí. Zbraň, kterou guest sebere (třeba blaster po trooperovi), host nedostane. Host ji dál vidí ležet a může ji sebrat sám.
8. Nepřítel, který bojuje s guestem, se u guesta hýbe plynule. `coop_npcs` u hosta ukazuje, kolik NPC běží u guesta.
9. Hráči se navzájem nezraní.

## Mise (M7–M9)
10. Nepřátelé si všímají i guesta: útočí na něj a reagují na jeho střelbu.
11. Úvodní cutscéna kejim_post běží u obou. Jan v ní hraje a guest je schovaný. Po cutscéně stojí guest tam, kde Jan skončil.
12. Přeskočení cutscény: stiskne use jen guest, pak oba. Host může přeskočit sám dvojím stiskem.
13. Kulomet nebo věž: použije guest, host dostane „Occupied“, a naopak.

## Smrt, přechody, save (M10)
14. Guest zemře a za 10 s se objeví vedle hosta. Když zemřou oba, host načte autosave a guest ho následuje.
15. Konec levelu (nebo `coop_skiplevel`) přenese oba do další mapy. Guest si nese svou výbavu.
16. Host uloží hru, ukončí ji, načte save přes menu COOP → „Load a saved game“. Guest se připojí a má svou výbavu.
17. Guest se připojí uprostřed mise a objeví se vedle Kyla.
18. Guest otevře menu (Esc) a hra u hosta běží dál.

## Menu (M11)
19. V hlavním menu je tlačítko COOP a menu jde ovládat: jméno, heslo, hostovat, „Najit hry v LAN“ (vyplní IP) a připojit.
20. Luke (od ns_streets nebo po `coop_skiplevel` z yavin_trial) hodí meč a partner vidí meč letět.
21. Když host ukončí hru, guest skončí v hlavním menu s hláškou „hostitel opustil hru“.
