# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

Nothing yet.

## [1.0.1] - 2026-11-26

Two fixes from the first game sessions with 1.0.0.

### Fixed
- **The status screen shows +25 Discovery, not +50.** The buff is the Silver-Pickled Fowl Foot effect (`+50`), which the mod corrects to `item_discovery_bonus` inside the game's Item Discovery calculation. 1.0.0 corrected only the calculation made from the character's own effect list, but the status screen asks for it with another list the game keeps for the player (in `PlayerGameData`), so it showed the full `+50`. Every calculation that counts the effect is now corrected while the parry buff runs; a real Fowl Foot still counts its full `+50`.
- **A parry that hits nothing no longer gives the buff.** The spell parry trigger also fired whenever a parry skill was used, hit or miss (twice per use, about 0.4 s apart), so a parry at the air kept the bonus up. `spell_parry` is now off by default, and its hook is only installed when it is on. Melee parries are detected exactly as before.

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
