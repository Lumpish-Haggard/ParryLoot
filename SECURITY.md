# Security policy

## What the mod does on your machine

`ParryLoot.dll` is loaded into `eldenring.exe` by ModEngine 2 (or another DLL loader). It:

- scans `eldenring.exe`'s `.text` section in memory via AOB signatures and hooks parry resolution, spell-parry dispatch, and Item Discovery calculation;
- reads configuration from `ParryLoot.ini` next to the DLL;
- writes an optional diagnostic log to `ParryLoot.log` next to the DLL (can be disabled via `log = false`).

It makes no network connections, reads or writes no saves, touches no game archives or `regulation.bin` files, and runs no external programs.

## Supported versions

Only the latest release receives fixes.

## Reporting a vulnerability

Please report security problems privately with GitHub's **Report a vulnerability** button on the repository's *Security* tab, not in a public issue. Include reproduction steps and your mod and Elden Ring versions.
