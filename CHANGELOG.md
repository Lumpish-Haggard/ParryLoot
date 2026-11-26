# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

Nothing yet.

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
