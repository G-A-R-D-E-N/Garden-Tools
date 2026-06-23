# CombatConflictScanner

Standalone Mutagen console tool. Parses the **entire MO2 load order** off disk (no usvfs) and emits a
JSON report — plus ready-to-paste **RobCo Patcher INI** lines — of everything that conflicts with the
**FallenWorldCombat** keyword-driven damage system.

## What it finds

1. **Coverage gaps** — armor with no `DT_Phys_*` keyword and ammo with no `AmmoPierce_*` keyword. These
   silently fall through FallenWorldCombat's pierce/DR pipeline. Output includes a heuristic tier
   suggestion derived from the armor rating / ammo damage.
2. **Competing overrides** — armor, ammo, and NPC records edited by 2+ plugins (other mods fighting over
   the same combat-relevant data), sorted most-contested first. NPC entries include the winning health
   and race so you can spot bulk conflicts (e.g. a behemoth at 1995 HP set by a difficulty mod).
3. **Race-scaling gaps** — races that NPCs actually use but that are **missing from `RaceScaling.json`**,
   sorted by how many NPCs use each. Feed these back into the DLL's `RaceScaling.json`.

## Run

```powershell
cd Tools\CombatConflictScanner
dotnet run -c Release
```

Defaults to the `D:\Games\ModlistDownloads` instance, the active profile (from `ModOrganizer.ini`), and
the deployed `RaceScaling.json`. Override any of them:

```powershell
dotnet run -c Release -- --instance "D:\Games\ModlistDownloads" --profile "Fallen World" `
    --racejson "D:\...\FallenWorldCombat\RaceScaling.json" --out ".\out"
```

## Output (in `--out`, default `bin\Release\net9.0\out\`)

| File | Contents |
|------|----------|
| `combat_conflicts.json` | Full structured report (all three scans). |
| `robco_armor_DT.ini` | `filterByArmors=...:keywordsToAdd=DT_Phys_N` lines, grouped by tier. |
| `robco_ammo_AmmoPierce.ini` | `filterByAmmo=...:keywordsToAdd=AmmoPierce_N` lines, grouped by tier. |

Drop the INIs into a mod's `F4SE\Plugins\RobCo_Patcher\armor\` or `\ammo\` folder. **Review the suggested
tiers first** — they are heuristics from armor rating / ammo damage, not authoritative.

## Tuning the tier heuristics

The armor-rating→`DT_Phys` and ammo-damage→`AmmoPierce` thresholds live in `SuggestDtPhys` /
`SuggestPierce` at the top of `Program.cs`. Adjust the breakpoints to match your balance.

## How the load order is read

`Mo2LoadOrder.cs` reconstructs MO2's view from `profiles/<profile>/plugins.txt` (enabled order) +
`modlist.txt` (mod priority) + the game Data / mods / overwrite folders — the same approach the
FO4RecordEditor app uses, distilled to a read-only overlay load. Base masters (Fallout4.esm + DLCs) are
force-prepended since FO4 implicit-loads them.
