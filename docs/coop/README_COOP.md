# Jedi Outcast – coop pro 2 hráče

Kampaň Jedi Outcast ve dvou přes síť. Hostitel hraje Kyla. Druhý hráč hraje Jana, dokud Kyle nemá meč, a Luka od chvíle, kdy ho Kyle má.

## Instalace (oba hráči)

1. Potřebujete vlastní Jedi Outcast (Steam / GOG).
2. Obsah složky `JK2Coop` zkopírujte do složky `GameData` hry, tedy tam, kde je `base\assets0.pk3`.
3. Oba hráči musí mít **stejný build** (stejné soubory z této složky), jinak se nepřipojí.

## Hraní

V hlavním menu je tlačítko **COOP**. Hostitel v něm založí novou hru (podle obtížnosti) nebo načte save, druhý hráč zadá IP hostitele a připojí se. Totéž jde přes soubory `.bat`:

| Kdo | Co spustit |
|---|---|
| Hostitel, nová hra | `host-coop.bat` (začne kejim_post) nebo `host-coop.bat jinamapa` |
| Hostitel, uložená hra | `host-coop-load.bat nazev_savu` |
| Druhý hráč | `join-coop.bat IP_hostitele` (např. `join-coop.bat 192.168.1.20`) |

Druhý hráč se může připojit kdykoli. Objeví se vedle Kyla a dostane svou uloženou výbavu, nebo kopii Kylových zbraní.

## Síť

- UDP port **29170** (změna: `coop_port`).
- Přes internet: na hostiteli přesměrujte UDP 29170 na routeru (port forwarding), nebo použijte virtuální LAN (Tailscale, ZeroTier) a připojte se na její IP.
- Heslo: hostitel nastaví `coop_password tajne`, druhý hráč totéž před `coop_connect`.

## Pravidla

- **Smrt:** za 10 s (`coop_respawnDelay`) se objevíte vedle partnera. Když jsou mrtví oba, hostitel načte poslední autosave.
- **Ukládá jen hostitel.** Uložení u druhého hráče pošle hostiteli žádost. Save obsahuje i výbavu druhého hráče.
- **Cutscény:** přeskočení chce stisk use od obou. Hostitel může cutscénu vynutit dvojím stiskem.
- **Zbraně** má každý hráč vlastní. Když jeden zbraň sebere, zmizí jen jemu a pro druhého zůstane ležet. **Klíče** dostanou oba hráči. Munici, zdraví a štíty dostane ten, kdo je sebere první.
- **Přeskočení levelu:** `coop_skiplevel` (hostitel). Druhý hráč tím hostitele jen požádá.
- Hráči se navzájem nezraňují.

## Příkazy v konzoli

| Příkaz | Význam |
|---|---|
| `coop_host` | začít hostovat (pak `map` / `load`) |
| `coop_connect ip[:port]` | připojit se |
| `coop_find` | najít hry v místní síti (vyplní adresu v menu) |
| `coop_disconnect` | odpojit |
| `coop_status` | stav spojení, přenos v kbit/s |
| `coop_say text` | zpráva partnerovi |
| `coop_skiplevel` | přeskočit level |
| `coop_name jmeno` | jméno nad hlavou |
| `cg_coopPartnerArrow 1` | šipka k partnerovi, když není vidět |
| `coop_lukeForceBonus 1` | Luke má o úroveň silnější Force |

## Známá omezení

- NPC postava, kterou hraje druhý hráč (Jan / Luke), zůstává ve hře jako neviditelný dublér, aby skripty mise fungovaly jako v originále. Ukáže se jen v cutscénách, kde hraje roli druhého hráče. Druhý hráč je mezitím schovaný a po cutscéně se objeví tam, kde dublér skončil.
- Kulomety, věže a kamery v panelech ovládá ten, kdo je použil. Druhý hráč dostane „Occupied“.
- Texty v menu a hláškách jsou česky bez diakritiky, protože herní fonty háčky a čárky neumí.
- Hra nekontroluje verzi ani data hry. Oba hráči by měli mít stejnou verzi coopu a stejný `zz_coop.pk3`, jinak se hra nemusí chovat správně.
