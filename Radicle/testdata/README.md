# Test fixtures

Small plugins used by the unit tests. These are not Bethesda masters.

| File | Source | Why it is here |
|------|--------|----------------|
| `FW_HeatherSurvivalPatch.esp` | FO4IDE/PluginEditTool local patch | 2 KB real FO4 plugin with three masters, form version 131, MGEF/SPEL/ALCH groups |
| `SlowTime.esp` | local Fallout 4 Plugins tree | slightly larger real ESP for smoke coverage |

Do not vendor `Fallout4.esm` or Creation Club ESLs. Optional live tests may read the game Data directory when present.

Synthetic TES4/GRUP/XXXX/compressed cases are generated in `tests/plugin_bytes.cpp`.
