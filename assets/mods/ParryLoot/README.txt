Parry Loot (Standalone ModEngine 2 DLL mod)
===========================================
Grants a short Item Discovery boost after landing a successful parry so the buff
stays active through your riposte kill and displays a native countdown timer
in ERBuffTimer. It touches no regulation.bin file and needs no talisman.

Installation (ModEngine 2, offline / EAC off):
1. Put the mods folder next to modengine2_launcher.exe and config_eldenring.toml
   (your ModEngine 2 folder), so you have mods\ParryLoot.dll and mods\ParryLoot.ini.
2. Open config_eldenring.toml and add the DLL to external_dlls under [modengine]:
       external_dlls = [ "mods\\ParryLoot.dll" ]
   (keep any DLLs already in the list, separated by commas).
3. Start the game with ModEngine 2. mods\ParryLoot.log shows what the mod did.

To uninstall: remove "mods\\ParryLoot.dll" from external_dlls, then delete
mods\ParryLoot.dll, mods\ParryLoot.ini, mods\ParryLoot.log and mods\ParryLoot\.

Configuration (mods\ParryLoot.ini):
- enabled = true               Enable/disable the mod
- duration = 15.0              Seconds the buff lasts (0.5 - 170.0)
- item_discovery_bonus = 25    Item Discovery added (+25 vanilla-friendly; 50 = Silver Fowl Foot)
- melee_parry = true           Trigger on melee weapon/shield parries (including Golden Parry)
- spell_parry = true           Trigger on projectile/spell parries (Carian Retaliation / Thops's Barrier)
- sp_effect_id = 3970          SpEffect used (3970 = Silver-Pickled Fowl Foot icon; keep it:
                               the bonus is set relative to that effect's own +50)
- log = true                   Write mods\ParryLoot.log

Stacking & Limits:
- Stacks additively with Silver Scarab (+75), Arcane stats, and Arcane gear.
- Adds directly on top of the 99 Arcane (199 discovery) scaling cap.
- Uses SpEffect 3970, so if a longer Silver-Pickled Fowl Foot (180s, +50) is
  already active, Parry Loot leaves it untouched without shortening or weakening it.
