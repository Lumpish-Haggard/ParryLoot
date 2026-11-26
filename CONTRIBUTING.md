# Contributing

Thanks for helping improve **Parry Loot**! Please read the [Code of Conduct](CODE_OF_CONDUCT.md) first.

## Reporting Bugs

Before opening a bug report:

1. Reproduce the issue with `log = true` in `mods\ParryLoot.ini`.
2. Attach `mods\ParryLoot.log` and `modengine2\logs\modengine_*.log` (or quote any error lines).
3. Include your Elden Ring App Version, ModEngine version, and parry skill / enemy involved.

If a crash happens without `"mods\\ParryLoot.dll"` loaded, report it to the loader or conflicting mod instead.

## Development Setup

| Task | Requirements |
|---|---|
| Edit docs, config, or run offline checks | Python 3.10+ |
| Compile `ParryLoot.dll` on Windows | `x86_64-w64-mingw32-g++` (`w64devkit` / MSYS2 mingw64) or `cl.exe` (MSVC x64) |
| Package release zip | PowerShell 5.1+ (`.\pack.ps1`) |

### Local Workflow

```powershell
# Compile src\ParryLoot.cpp into build\ParryLoot.dll and create dist\ParryLoot-<version>.zip
.\pack.ps1 -Build

# Run offline checks (zip contents, DLL PE headers, INI defaults, Nexus BBCode & changelog)
python tools/smoke_check.py
python tools/nexus_page.py check
```

## Code & Engineering Rules

- **Verify, don't guess:** Any offset, AOB signature, or `SpEffect` layout change must be verified against the game binary and tested in-game.
- **No external runtime dependencies:** Keep `ParryLoot.dll` statically linked (`-static -static-libgcc -static-libstdc++`) so users never need MinGW or VC++ redistributables.
- **Unique `.text` AOBs:** Pattern scans must assert a single unique match before patching; abort cleanly and log if any signature fails or is ambiguous.
- **Update [`CHANGELOG.md`](CHANGELOG.md):** Document user-visible changes under `## [Unreleased]`.

## Testing in game

`.\pack.ps1 -Build -Install -GamePath "<your ModEngine 2 folder>"` builds the DLL and copies it next to `modengine2_launcher.exe` (it never overwrites an existing `ParryLoot.ini`). If Vortex manages your ModEngine 2, install `dist\ParryLoot-<version>.zip` through Vortex instead (drag it onto the Mods page, enable, deploy) and check that `config_eldenring.toml` lists `"mods\\ParryLoot.dll"`. Vortex rewrites that file only when its load order changes, with the DLL mods it installed from an archive: a copied DLL is dropped. The zip's `ParryLoot-README.txt` and `LICENSE.txt` give the mod a Load Order entry (Vortex drops `README.txt`, which ModEngine 2's package also has, and files without an extension), so the deploy after installing it writes the line. With ERBuffTimer loaded you can watch the buff's timer. Check, and keep `mods\ParryLoot.log` from the session:

1. **A parry that catches an attack** (the enemy staggers and could be riposted): one `Parry -> applied ... for 15.0s` line, the timer counts down from 15, the status screen's Discovery is 25 higher while it runs, and it is gone after 15 s.
2. **A second parry within 15 s** refreshes it (another `applied` line, not `already runs longer`).
3. **Other parry skills** (Buckler Parry, Golden Parry, Storm Wall, Carian Retaliation, and Carian Retaliation or Thops's Barrier against a sorcery): note which ones give an `applied` line. Only a shield's Parry has been tested so far.
4. **Nothing else triggers it:** blocks, a mistimed parry that lands on your guard, a parry at nothing, Guard Counters, ripostes, rolls, your own spells, getting hit. Any `Parry` line you did not cause is a bug.
5. **A real Silver-Pickled Fowl Foot**: eaten before a parry, it keeps its 180 s and +50; eaten during the 15 s buff, it gives the full 180 s and +50.
6. A `WARNING` line in the log is a bug: report it with the log.

## Releasing (maintainers)

1. Bump `VERSION` and `kVersion` in `src/ParryLoot.cpp` (`tools/smoke_check.py` checks that they match, and that the DLL was built from that source).
2. Move the *Unreleased* changelog entries under the new version.
3. `.\pack.ps1 -Build` and test that build in game (see *Testing in game*).
4. Tag `vX.Y.Z` and publish a GitHub release with `dist/ParryLoot-X.Y.Z.zip` attached. Attach the zip you tested; do not rebuild it in between. `python tools/release_notes.py X.Y.Z` prints the changelog section for the release text.
5. Publishing the release runs [`nexus-release.yml`](.github/workflows/nexus-release.yml). It checks that the tag, `VERSION` and the attached zip agree, then sends that zip to Nexus Mods as a new version of the main file, with the version's changelog section. It needs the repository secret `NEXUSMODS_API_KEY` and `NEXUS_MOD_ID` in the workflow; without `NEXUS_MOD_ID` every run only checks. While `NEXUS_FILE_ID` is empty, the release creates the mod's main file with its first version instead (`tools/nexus_first_file.py`, since the upload action can only add versions to a file that exists); copy the file ID from that run's summary into `NEXUS_FILE_ID`. Pre-releases are skipped.
6. The mod page text lives in [`.github/nexus/`](.github/nexus/README.md) (`description.bbcode`, `summary.txt`). The same run updates the page when that text changed since the previous release, after saving the old description as a run artifact (details in that README). Edit the page there, not on the website, or the next update overwrites the website edit. Images are still managed on Nexus by hand.
7. From the Actions tab the workflow can also be run by hand: *dry run* (the default: checks everything, sends nothing), *upload*, or *page only* (for example to correct the page without a new release; the ref can be `main`).
