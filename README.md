<div align="center">

# Parry Loot

**A tiny, vanilla-friendly Elden Ring ModEngine 2 DLL mod that rewards successful parries with a temporary Item Discovery boost, touching zero `regulation.bin` files and requiring no talisman.**

![Elden Ring](https://img.shields.io/badge/Elden%20Ring-1.17.2%2B-8b6f3d)
![ModEngine 2](https://img.shields.io/badge/ModEngine%202-External%20DLL-informational)
![Zero regulation.bin](https://img.shields.io/badge/regulation.bin-untouched-brightgreen)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

[Download](../../releases/latest) ·
[Report a bug](../../issues/new/choose) ·
[Contributing](CONTRIBUTING.md) ·
[Changelog](CHANGELOG.md)

</div>

---

Pulling off a clean parry in Elden Ring feels great, and when it sets up a lethal riposte, getting a slightly better chance at the enemy's drop feels earned.

**Parry Loot** hooks Elden Ring's parry resolution directly in memory: whenever **you** land a real parry, it briefly grants a modest **Item Discovery** bonus (**+25** for **15 seconds** by default) so the buff stays active through your riposte kill. Because it changes no `regulation.bin` parameters and borrows no talisman slot, it stays compatible with overhaul mods, randomizers, weapon mods, and custom `regulation.bin` merges out of the box.

## Features

- **Pure DLL hook, zero `regulation.bin` edits:** Safe to combine with any parameter mod (Reforged, Convergence, Randomizer, custom merges, etc.) and completely save-safe to add or remove anytime.
- **No talisman required:** Works regardless of what talismans, weapons, or shields you equip.
- **Melee parries:** Parry (on shields, daggers such as the Parrying Dagger, curved swords and other weapons, or as an Ash of War), Buckler Parry, Golden Parry, Storm Wall, and Carian Retaliation (melee).
- **Spell / projectile parries: off by default.** `spell_parry` in `ParryLoot.ini` turns on a trigger for Carian Retaliation and Thops's Barrier deflecting a spell or projectile, but the game event it listens to also fires when a parry skill hits nothing, so with it on, a parry at the air counts too.
- **Zero false positives (with the defaults):** Ignores normal shield blocks, Guard Counters, i-frame rolls, whiffed parries, partial guard-breaks, and enemies parrying you.
- **Native `ERBuffTimer` countdown icon:** Stamps finite remaining/total duration onto the applied `SpEffect` (`3970`), so `ERBuffTimer` shows the Silver-Pickled Fowl Foot icon ticking down (`15` → `0`).
- **Respects Silver-Pickled Fowl Foot consumables:** Never shortens or weakens a real `180s` (`+50`) Silver-Pickled Fowl Foot if one is already active, or if you consume one during a parry buff.
- **Configurable via `ParryLoot.ini`:** Tune the bonus (`+25`, `+50`, etc.), duration, triggers, or logging without recompiling.
- **Tiny & self-contained:** Compiled C++20 DLL (~74 KB) with zero external runtime dependencies (only `KERNEL32.dll` and `msvcrt.dll`), resolved at runtime via unique `.text` AOB signatures.

## Stacking & Arcane Limit Behaviour

How Elden Ring calculates Item Discovery (verified directly in `eldenring.exe` `GetItemDiscovery` and `CalcItemDropRateSum`):

1. **Arcane base curve (`CalcCorrectGraph` ID `140`):** Reads your Arcane stat and caps scaling at **`99` Arcane** (`199` base Item Discovery). Arcane-boosting equipment (`Silver Tear Mask`, `Marika's Scarseal/Soreseal`, `Godrick's Great Rune`, `Outer God Heirloom`) raises your Arcane stat up to that `99` (`199`) ceiling.
2. **`SpEffect` Item Discovery sum (`CalcItemDropRateSum`):** Walks all active `SpEffect` nodes on your character and adds every `itemDropRate` entry directly on top of the Arcane base curve, with **no upper cap** (`max(0.0, ArcaneBase + SpEffectSum)`).

| Bonus Source | Stacks with Parry Loot? | Notes |
|---|---|---|
| **99 Arcane (`199` base)** | **Yes** | `ParryLoot` adds **+25 → 224** on top of the `99` Arcane cap (the `SpEffect` sum has no upper cap). |
| **Silver Scarab (`+75`)** | **Yes** | Separate `SpEffect` ID; sums cleanly (**`199` + `75` + `25` = `299`** at 99 Arcane). |
| **Arcane gear / seals / Great Rune** | **Yes** | Raises base Arcane up to `99`; `ParryLoot` stacks on top either way. |
| **Silver-Pickled Fowl Foot (`+50`, `180s`)** | **No** *(by design)* | Both share `SpEffect 3970` so `ERBuffTimer` renders its vanilla icon cleanly. A real `180s` Fowl Foot always takes priority (`+50`) and is never shortened or overwritten. |

## Requirements

| Requirement | Notes |
|---|---|
| Elden Ring | Tested on App Ver. `1.17.2` (`eldenring.exe` `2.7.2.0`); uses AOB pattern scans. |
| [ModEngine 2](https://github.com/soulsmods/ModEngine2) | Recommended loader (`external_dlls` in `config_eldenring.toml`). |
| ERBuffTimer | *Optional*: displays the countdown timer icon on screen (`1.2.4+`). |

## Installation

1. Download `ParryLoot-<version>.zip` from the [latest release](../../releases/latest) or from [Nexus Mods](https://www.nexusmods.com/eldenring/mods/11747).
2. Install it with Vortex or by hand:
   - **Vortex:** Use *Mod Manager Download* on Nexus Mods (or drag the zip onto Vortex's Mods page), enable the mod and deploy. Vortex's Elden Ring support sets up ModEngine 2 itself and puts the DLL in the game's `mods` folder.
   - **Manual (ModEngine 2):** Extract the `mods` folder into your ModEngine 2 folder (the one with `modengine2_launcher.exe` and `config_eldenring.toml`; that is the Elden Ring `Game` folder if you installed ModEngine 2 there), so you have `mods\ParryLoot.dll` and `mods\ParryLoot.ini` next to the launcher. ModEngine 2 reads relative paths in `external_dlls` from that folder.
3. Open `config_eldenring.toml` (in that same folder) and make sure `external_dlls` lists `"mods\\ParryLoot.dll"`; add it if not:

   ```toml
   [modengine]
   external_dlls = [
     # ... your other DLL mods ...
     "mods\\ParryLoot.dll"
   ]
   ```

   **With Vortex, check this too.** Vortex writes that list itself, but only when its *Load Order* tab changes. From 1.0.1 on, Parry Loot adds an entry there, so the deploy after installing it writes the line. If the line is still missing, drag any entry on the Load Order tab one place down and back, or add the line by hand (Vortex keeps it from then on).

4. Launch the game through ModEngine 2 (**offline / non-EAC only**, as with any DLL mod). The mod writes `mods\ParryLoot.log` next to `ParryLoot.dll` each time the game starts.

To uninstall with Vortex, remove the mod and deploy. By hand, remove `"mods\\ParryLoot.dll"` from `config_eldenring.toml` and delete `mods\ParryLoot.dll`, `mods\ParryLoot.ini`, `mods\ParryLoot.log`, and `mods\ParryLoot\`.

## Configuration (`mods\ParryLoot.ini`)

```ini
[ParryLoot]
; Master toggle (true/false or 1/0)
enabled = true

; Buff duration in seconds after a successful parry (0.5 to 170: it stays shorter
; than a real Silver-Pickled Fowl Foot's 180 s, which is how the mod tells them apart)
duration = 15.0

; Item Discovery bonus granted while active
; Default: 25 (vanilla-friendly, half a Silver-Pickled Fowl Foot; 50 matches a full foot)
item_discovery_bonus = 25

; Triggers
melee_parry = true
; Spell / projectile parries (Carian Retaliation, Thops's Barrier). Off by default:
; the game event it listens to also fires when a parry skill hits nothing, so with
; it on, a parry at the air gives the buff too.
spell_parry = false

; Vanilla SpEffect ID used for the icon and ERBuffTimer countdown (3970 = Silver-Pickled Fowl Foot).
; Leave it at 3970: item_discovery_bonus is set relative to that effect's own +50.
sp_effect_id = 3970

; Write mods\ParryLoot.log next to ParryLoot.dll
log = true
```

## Building & Packaging from Source

Requires a 64-bit Windows C++20 compiler (`x86_64-w64-mingw32-g++` / `w64devkit` or MSVC) to rebuild `ParryLoot.dll`, and PowerShell + Python 3.10+ for packaging and verification checks:

```powershell
# Compile ParryLoot.dll (using mingw-w64 / w64devkit) and package dist\ParryLoot-<version>.zip
.\pack.ps1 -Build

# Build, package, and copy mods\ into your ModEngine 2 folder (the one with modengine2_launcher.exe)
.\pack.ps1 -Build -Install -GamePath "D:\Games\ELDEN RING\Game"

# Run offline smoke checks, release notes extraction, and Nexus page validation
python tools/smoke_check.py
python tools/nexus_page.py check
python tools/release_notes.py 1.0.1
```

## FAQ

**Does this require equipping any talisman (like Assassin's Crimson Dagger)?**
No. It triggers directly from your parries, with zero talisman slot requirement.

**Why is this a ModEngine 2 DLL mod instead of a `regulation.bin` edit?**
Editing `regulation.bin` modifies game parameter files and clashes with overhauls (Reforged, Convergence, Randomizer, custom weapon/rule merges) or forces manual Smithbox re-merges on updates. Because `ParryLoot.dll` hooks parry resolution purely in memory, it touches **zero** `regulation.bin` files and is completely safe to add to or remove from any save.

**Why +25 Item Discovery for 15 seconds?**
When you parry an enemy, they rarely die from the parry itself: you perform a riposte first, and loot drops roll when the enemy dies. Fifteen seconds gives plenty of time for the riposte animation to finish and score the kill, while `+25` keeps drops vanilla-friendly (half a Silver-Pickled Fowl Foot's `+50`). Both are configurable in `mods\ParryLoot.ini`.

**Which parries trigger it, and can blocks, rolls, Guard Counters, or enemies parrying me trigger it?**
Melee parries (Parry on any shield or weapon that has it, Buckler Parry, Golden Parry, Storm Wall, Carian Retaliation melee) trigger it. Normal blocks, Guard Counters, i-frame rolls, whiffed parries, partial guard-breaks, and an enemy parrying you are ignored. Spell/projectile parries (Carian Retaliation, Thops's Barrier) count only with `spell_parry = true`, which is off by default because that trigger also fires for a parry that hits nothing.

**Will `ERBuffTimer` show a timer icon on screen?**
Yes. `ParryLoot` applies vanilla `SpEffect 3970` and stamps finite remaining/total duration (`15.0s`), so `ERBuffTimer` (`1.2.4+`) renders the Silver-Pickled Fowl Foot countdown icon natively without extra configuration.

**Does it stack with Silver Scarab, Arcane gear, or Great Runes?**
Yes, additively. `CalcItemDropRateSum` sums all active Item Discovery `SpEffect` nodes, so wearing **Silver Scarab** (`+75`) stacks directly (`+75 + 25 = +100`), and Arcane-boosting gear (`Silver Tear Mask`, `Marika's Scarseal/Soreseal`, `Godrick's Great Rune`, `Outer God Heirloom`) raises your base Arcane curve underneath it.

**Does it still work past the `99` Arcane hard limit?**
Yes. Elden Ring's `GetItemDiscovery` clamps the Arcane stat scaling curve (`CalcCorrectGraph` ID `140`) at `99` Arcane (`199` base discovery), then adds `CalcItemDropRateSum` afterward with **no upper cap** (`max(0.0, ArcaneBase + SpEffectSum)`). At `99` Arcane, `ParryLoot` still adds `+25` (`224` total, or `299` with Silver Scarab).

**Does it stack with a Silver-Pickled Fowl Foot consumable?**
No, by design. Both use vanilla `SpEffect 3970` so `ERBuffTimer` shows its icon without `regulation.bin` changes. If a real `180s` Silver-Pickled Fowl Foot (`+50`) is already active, `ParryLoot` leaves it untouched without shortening or weakening it; if you consume one while `ParryLoot`'s `15s` timer is active, `ParryLoot` steps aside immediately so you get the full `180s` at `+50`.

**Can I customize the bonus or the triggers?**
Yes. Edit `mods\ParryLoot.ini` to change `item_discovery_bonus` (e.g., `25` or `50`), `duration`, `melee_parry`, `spell_parry` (off by default, see above), `enabled`, or `log`.

**The status screen showed +50 Discovery during the buff, not +25. Why?**
That was a bug in 1.0.0, fixed in 1.0.1. The buff is the Silver-Pickled Fowl Foot effect (`+50`), which the mod corrects to `item_discovery_bonus` inside the game's Item Discovery calculation. 1.0.0 corrected only the calculation made from your character's own effect list, but the status screen asks for it with another list the game keeps for the player, so it showed the full `+50`. 1.0.1 corrects every calculation that counts the effect.

**Can I use this online / with Easy Anti-Cheat?**
No. Like all ModEngine 2 DLL mods, play offline with EAC disabled.

**My parries do nothing. What should I check?**
`mods\ParryLoot.log` next to `ParryLoot.dll`. If it was not written when the game started, ModEngine 2 did not load the DLL: `config_eldenring.toml` must list `"mods\\ParryLoot.dll"` in `external_dlls` (see step 3 of *Installation*, including the Vortex note). With Vortex, install the zip through Vortex rather than copying the files by hand: whenever Vortex saves its load order, it rewrites that list from the mods it installed. If the log has an `ERROR` or `WARNING` line, please [report it](../../issues/new/choose) with the log attached.

## Credits

- **soulsmods**: [ModEngine 2](https://github.com/soulsmods/ModEngine2), which loads the DLL.
- **FromSoftware**: Elden Ring.

This project is not affiliated with or endorsed by FromSoftware or Bandai Namco Entertainment.

## License

[MIT](LICENSE). Elden Ring names and identifiers belong to FromSoftware and Bandai Namco Entertainment. This repository contains no game files or game data: the DLL finds what it needs in the running game.
