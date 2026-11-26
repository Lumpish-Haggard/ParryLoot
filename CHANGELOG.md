# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

Nothing yet.

## [1.0.1] - 2026-11-26

Fixes from the first game sessions with 1.0.0: the buff now comes from real parries.

### Fixed
- **Only a real parry gives the buff.** 1.0.0 had two triggers and both were wrong. The "melee" one watched the game's guard step, which runs when an attack lands on a raised guard, so blocks and mistimed parries gave the buff and a clean parry never did. The "spell" one fired twice whenever a parry skill was used, hit or miss, so a parry at the air gave it too. Both are gone. The buff now comes from the game's own sign of a parry: when an attack meets a parry, the game cancels it and the parry makes its own hit on the attacker (type `0x40`, no damage), which staggers it for the riposte. In a test session all 44 parries were such hits, and none of the blocks, hits taken or other attacks were.
- **The status screen shows +25 Discovery, not +50.** The buff is the Silver-Pickled Fowl Foot effect (`+50`), which the mod corrects to `item_discovery_bonus` inside the game's Item Discovery calculation. 1.0.0 corrected only the calculation made from the character's own effect list, but the status screen asks for it with another list the game keeps for the player (in `PlayerGameData`), so it showed the full `+50`. Every calculation that counts the effect is now corrected while the parry buff runs; a real Fowl Foot still counts its full `+50`.

### Removed
- `melee_parry` and `spell_parry` in `ParryLoot.ini`: there is one trigger now, the parry itself (`enabled` turns the mod off). An INI that still has them loads fine: the two keys are ignored, and the log says so.

### Changed
- **Vortex writes the DLL into `config_eldenring.toml` itself.** Vortex rewrites that file only when its Load Order tab changes, and the 1.0.0 zip gave it nothing to put there: it drops `README.txt` (ModEngine 2's package has a file of that name) and `LICENSE` (no extension), leaving only the DLL and the INI. The readme and license now ship as `mods\ParryLoot\ParryLoot-README.txt` and `LICENSE.txt`, which Vortex keeps, so Parry Loot gets a Load Order entry and the deploy after installing it writes the line.

## [1.0.0] - 2026-11-26

Initial release of **Parry Loot**.

### Added
- Standalone ModEngine 2 DLL (`ParryLoot.dll`) with zero `regulation.bin` edits and no talisman requirement.
- Melee parry detection (`Buckler Parry`, `Parry`, `Golden Parry`, `Storm Wall`, etc.) verifying that the local player landed the parry cleanly without guard-breaking.
- Projectile/spell parry detection (`Carian Retaliation`, `Thops's Barrier`) verified against the local player's character handle.
- Applies a temporary Item Discovery boost (`+25` default in `ParryLoot.ini`, vanilla Silver-Pickled Fowl Foot is `+50`) for `15.0s` so the buff remains active through your riposte kill.
- Compatible with `ERBuffTimer` (`1.2.4+`): stamps finite remaining/total duration onto the applied `SpEffect` (`3970`) so the countdown icon displays automatically.
- Preserves longer active Silver-Pickled Fowl Foot consumables (`180s`, `+50`) without shortening or reducing them.
- Pattern-based AOB resolution on `.text` instead of brittle hardcoded offsets.
