# LODGen settings file: the GUI-side writer

Research date: 2026-08-18

Companion to [`XLODGEN_REVERSE_ENGINEERING.md`](../XLODGEN_REVERSE_ENGINEERING.md) (forms map + handoff), [`XLODGEN_QUALITY.md`](../XLODGEN_QUALITY.md)
(defaults) and `tools/validate_dlodsettings.py` (engine-side reader oracle). This doc
documents the **writer side** — how the xLODGen GUI (Delphi, `xLODGen.exe` 4.1.5o
"FO4LODGen ... Terrain LOD Beta") produces the settings file it passes to the LODGen
engine. It is the missing link between the GUI state and the engine's parser, and the
reference for the deferred "generate a real .dlodsettings and validate" check.

All addresses are virtual addresses in `xLODGen.exe` (PE image base 0x400000).

## 1. Which file, and who writes it

- The **"LODSettings File"** dialog (`frmLODSettingsFile`, "Create LODSettings File")
  is the GUI's save handler. The save flow is the function body at
  `FUN_01438a54` (a large function Ghidra splits at a jumptable; the writer code
  lives at `0x143aa00`–`0x143b600`). It scans the selected worldspace's CELL/LAND
  records to compute bounds and heights, then writes the settings file line by line.
- The engine settings file written by the terrain path starts with
  `GameMode=TERRAIN<GameName>` and is the file passed to `LODGen.exe` /
  `LODGenx64.exe` on the command line (see the spawn contract in
  [`XLODGEN_REVERSE_ENGINEERING.md`](../XLODGEN_REVERSE_ENGINEERING.md), `FUN_0141c4c0`).
- The save path is built by `FUN_01404948`:
  - FO3/FNV-family → `..\Data\LODSettings\LODSettings\<Worldspace>.dlodsettings`
  - TES5/SSE/FO4-family → `..\Data\LODSettings\<Worldspace>.lod`

## 2. Exact line order (terrain settings file)

Each line is a UTF-16LE string written through a file/stream object's virtual
`WriteLine` (call `[ecx+0x3c]`). Order, with the string-literal evidence:

| # | Line | Literal / format | Evidence |
|---|---|---|---|
| 1 | `GameMode=TERRAIN<Name>` | `"GameMode=TERRAIN"` + game-mode name | 0x143e3e8, concat at 0x143ad05 |
| 2 | `Worldspace=<name>` | `"Worldspace="` + ws name | 0x143e418 @ 0x143ad46 |
| 3 | `CellSW=<swx> <swy>` | `"CellSW="` + `"%d %d"` | 0x143e454 / 0x143e43c @ 0x143adbc |
| 4 | `MaxLevel=<n>` | `"MaxLevel="` | 0x143e470 @ 0x143adfd |
| 5 | `TerrainData=<path>` | `"TerrainData="` + path | 0x143e490 @ 0x143ae21 |
| 6 | `IgnoreWater=<bool>` | `"IgnoreWater="` | 0x143e4e8 @ 0x143ae76 |
| 7 | `LandHeight=<float>` | `"LandHeight="` | 0x143e510 @ 0x143aeda |
| 8 | `WaterHeight=<float>` | `"WaterHeight="` | 0x143e534 @ 0x143af3e |
| 9 | `ProtectCellBorders=<bool>` | `"ProtectCellBorders="`, value from prop `TerrainProtectCellBorders` | 0x143e59c / 0x143e55c |
| 10 | `HideQuads=<bool>` | `"HideQuads="`, value from prop `TerrainHideQuads` | 0x143e600 / 0x143e5d0 |
| 11 | per LOD 4/8/16/32: `QualityLOD<n>=<v>`, `MaxVertsLOD<n>=<v>`, `WaterDeltaLOD<n>=<v>` | loop, counter starts 4, doubles while ≤ 0x20 | loop @ 0x143b008–0x143b212 |
| 12 | `Skirts=<literal>` | `"Skirts=-14098"` or `"Skirts=256"` (see §4) | 0x143e76c / 0x143e7f0 |
| 13 | `PathOutput=<base>meshes\landscape\lod\<ws>` or `<base>meshes\terrain\<ws>` | `"PathOutput="` + base + path | 0x143e794 + 0x143e7b8 / 0x143e814 |
| 14 | `Season=<season>` | `"Season="` (conditional) | 0x143e840 @ 0x143b341 |
| 15 | `PathData=<path>` | `"PathData="` | 0x143e8b4 @ 0x143b3eb |
| 16 | `Resource=<...>` | `"Resource="` | 0x143e8d4 @ 0x143b480 |

Notes:
- The `TerrainData` value is computed by a virtual call (`0x214` offset) with the
  marker string `"DATA\No LOD Water"` (0x143e4b8) — worldspaces with no water LOD
  get that marker as the terrain-data path.
- The FO3/FNV water-export section logs `"exporting placeable water references"`
  (0x143e85c) and walks `PWAT` (0x143e8f4) / `XESP` (0x143e90c) records.
- Lines 1–10 and 14–16 are written once; the QualityLOD/MaxVertsLOD/WaterDeltaLOD
  triple is emitted inside the LOD-level loop (§3).

## 3. The per-LOD loop

```
mov [obj+0x2c], 4          ; level counter
loop:                      ; 0x143b008
  ... build key "QualityLOD" + IntToStr(level) + "=" ...
  ... lookup prop "TerrainQualityLOD<n>" ...
  ... write "QualityLOD<n>=<value>"
  ... same for "MaxVertsLOD<n>" (default "32767")
  ... same for "WaterDeltaLOD<n>" (default 0)
  level *= 2               ; add edx,edx
  cmp [obj+0x2c], 0x20     ; while level <= 32
  jle loop
```

Defaults baked into the loop body: `"10"` (QualityLOD), `"32767"` (MaxVertsLOD),
`0` (WaterDeltaLOD). The actual values come from the frmLODGen terrain-options
properties `TerrainQualityLOD<n>` / `TerrainMaxVertsLOD<n>` /
`TerrainWaterDeltaLOD<n>` (per-LOD spin edits in the DFM), whose defaults are
10/15/20/25 (quality), 32767 (maxverts) and -1 (waterdelta) — see
[`XLODGEN_QUALITY.md`](../XLODGEN_QUALITY.md). The literal `"10"` is the fallback value pushed first.

## 4. The mode split: Skirts and PathOutput

At `0x143b218` the writer reads a game-mode enum byte:

```
mov eax, [0x163c634]     ; global pointer → mode enum (byte)
movzbl (%eax), eax
add al, 0xfd             ; mode - 3
sub al, 2                ; mode - 5
jae 0x143b28a            ; unsigned: mode >= 5
```

| mode | Skirts line | PathOutput suffix |
|---|---|---|
| < 5 | `Skirts=-14098` (literal) | `meshes\landscape\lod\` |
| ≥ 5 | `Skirts=256` (literal) | `meshes\terrain\` |

### 4a. The mode enum — resolved (xEdit init, `FUN_01381084`)

The byte at `*0x163c634` is set by the xEdit init function `FUN_01381084`
(entry 0x1381084). The **pointer slot 0x163c634 is statically initialized** in
`.data` to 0x01751ccc (a .bss byte); the init code writes *through* it
(`mov 0x163c634,%eax; movb $N,(%eax)` at 0x138144a and 13 sibling sites — one per
game). The dispatch is the **application (exe) name**: `FUN_01380bd4` matches the
module name against the game token; the fatal string at 0x1381b86 lists all
accepted tokens:

> Application name must contain FNV, FO3, FO4, FO4VR, FO76, SSE, TES4, TES4R,
> TES5, TES5VR, Enderal, or EnderalSE, SF1 to select game.

Full table (mode value → name token → globals set), one write site each
(address of the `movb $N,(%eax)`):

| mode | token | Steam AppID | other set globals | write site (`movb $N,(%eax)`) |
|---|---|---|---|---|
| 0 | TES3 | 22320 | Morrowind | 0x1381508 |
| 1 | TES4 | 22330,900883 | Oblivion | 0x138155e |
| 2 | TES4R | 2623190 | Oblivion Remastered | 0x13815ba |
| 3 | FO3 | 22370,22300 | Fallout3 | 0x13814a9 |
| 4 | FNV | 22380,22490 | FalloutNV | 0x138144a |
| 5 | TES5 | 72850 | Skyrim | 0x138164e |
| 6 | Enderal | 933480 | Enderal / Skyrim.esm | 0x138175b |
| 7 | FO4 | (none logged) | Fallout4 | 0x13818d2 |
| 8 | SSE | 489830 | Skyrim Special Edition | 0x1381855 |
| 9 | TES5VR | 611670 | Skyrim VR | 0x13817d8 |
| 10 | EnderalSE | 976620 | Enderal Special Edition | 0x13816c0 |
| 11 | FO4VR | (none logged) | Fallout4 | 0x138194c |
| 12 | FO76 | (none logged) | Fallout76 | 0x13819f3 |
| 13 | SF1 | 1716740 | Starfield / .ba2 | 0x1381a9a |

(All 14 sites verified byte-level: `c6 00 <N>` = `movb $N,(%eax)` after
`mov 0x163c634,%eax`.)

This is exactly the public xEdit `wbGameMode` ordering (TES3=0 … SF1=13), which
**cleanly resolves the split**: the < 5 side is TES3/TES4/TES4R/FO3/FNV, the ≥ 5
side is TES5/Enderal/FO4/SSE/TES5VR/EnderalSE/FO4VR/FO76/Starfield.

So the writer's mode split corresponds to **engine generation style**: old-style
`LODGen.exe` (32-bit) terrain for FO3/FNV (and TES3/TES4, which don't generate
terrain LOD) vs the `.btr`-writing `LODGenx64.exe` path for TES5 and later. The
`GameMode=TERRAIN<Name>` line (§2) uses the same token (`PTR_DAT_0163c5d0`),
producing e.g. `TERRAINSSE`, `TERRAINFO4`, `TERRAINENDARALSE` — all matched
case-insensitively by the engine's `IsMode` (see [`XLODGEN_REVERSE_ENGINEERING.md`](../XLODGEN_REVERSE_ENGINEERING.md)).

**Skirts semantics (engine side, `TerrainLOD.AddSkirts`):** `skirts` is parsed as a
float; then `num = skirts`; if `num < 0` → `num /= quad.lodLevel` and skirt
vertices are placed at **absolute z = num**; if `num > 0` → `num -= (lodIndex-1)*63`
and vertices are placed at `vertex.z - num` (relative); `0` = no skirts.
So `-14098` means: absolute skirt z = −14098/4 = −3524.5 at LOD4 (halving per LOD
level), and `256` means: skirt 256 units below the quad edge (minus 63 per LOD
index). This matches the DynDOLOD changelog wording
("Skirts=negative = absolute z, 0 = none, positive relative z").

## 5. GameMode=TERRAIN<Name> and how the engine maps it

The line is `concat("GameMode=TERRAIN", modeName)` where `modeName` is the
game-mode name string (global `0x163c5d0`). The engine's `GAMEMODE` case
(`LODGeneratorCMD/Program.cs`) uppercases the value and switches:

- explicit cases: `TERRAINFNV` → `TerrainFO3`, `TERRAINFO4VR` → `TerrainFO4`,
  `TERRAINENDERAL*` → `TerrainTES5`/`TerrainSSE`, `TERRAINSSEVR`/`TERRAINTES5VR` → `TerrainSSE`
- plain `TERRAINSSE` / `TERRAINTES5` / `TERRAINFO4` are matched by
  `Game.IsMode(...)`, which is a **case-insensitive substring** test
  (`Mode.CaseInsensitiveContains`), so `Game.Mode="TERRAINSSE"` satisfies
  `IsMode("TerrainSSE")` etc.

The terrain generation branch is gated on
`IsMode("TerrainTES5") || IsMode("TerrainSSE") || IsMode("TerrainFO3") || IsMode("TerrainFO4")`
(exit 404 when `TerrainData` file is missing; required keys `Worldspace` → 4,
`PathOutput` → 5).

## 6. Round-trip: every written key is a parsed engine key

Every key above appears in the engine's settings switch (case-insensitively):
`WORLDSPACE`, `CELLSW` (space-split pair), `MAXLEVEL`, `TERRAINDATA`,
`IGNOREWATER`, `LANDHEIGHT`, `WATERHEIGHT`, `PROTECTCELLBORDERS`, `HIDEQUADS`,
`QUALITYLOD4/8/16/32`, `MAXVERTSLOD4/8/16/32`, `WATERDELTALOD4/8/16/32`,
`SKIRTS`, `PATHOUTPUT`, `SEASON`, `PATHDATA`, `RESOURCE`, `GAMEMODE`.
The validator (`tools/validate_dlodsettings.py`) models this reader side; this
doc is the writer-side contract it should be checked against.

## 7. Open questions (for the deferred validation)

1. ~~Mode-enum numbering at 0x163c634~~ — **resolved**: §4a, the xEdit init
   `FUN_01381084` dispatch on the exe name, exactly the public `wbGameMode`
   ordering.
2. Whether the per-LOD property values (`TerrainQualityLOD<n>` etc.) always
   override the literal defaults in the written file (the concat/format helpers
   `0x45ba30` = IntToStr, `0x40c4fc` = string concat, `0x40c584` = multi-string
   concat are confirmed; the exact value-selection register flow is not).
3. The `Resource=` value semantics (engine case `RESOURCE`), and the `PWAT`/`XESP`
   water-export section's presence per game mode.
4. Which of the 14 exe-name tokens xLODGen actually accepts as its own build
   (the shipped `xLODGen.exe` presents as FO4-only "FO4LODGen 4.1.5o"), vs. the
   sibling builds (`TES5LODGen`, `SSELODGen`, `FNVLODGen` …) that share this
   binary family.

## 8. Evidence index

- Writer body: `xLODGen.exe` @ 0x143aa00–0x143b600 (disassembled; loop @ 0x143b008).
- Literals: 0x143e3e8–0x143e90c (UTF-16LE string table).
- Helpers: `0x45ba30` IntToStr, `0x40c4fc` UStrCat, `0x40c584` UStrCatN
  (decompiled in `/tmp/ghidra_out/helpers.txt`).
- Engine reader: `LODGeneratorCMD/Program.cs` (decompiled .NET source).
- Skirts: `LODGenerator/TerrainLOD.cs` `AddSkirts`.

## 9. The object-LOD settings writer (`FUN_0141c4c0`)

Companion to §2–§4. The **object-LOD** settings file is written by a block inside
`FUN_0141c4c0` ("Building Object LOD" pipeline entry, log `"] Building Object
LOD"` — see [`XLODGEN_REVERSE_ENGINEERING.md`](../XLODGEN_REVERSE_ENGINEERING.md) §8.2). The writer fills the same
settings `TStringList` (saved to `..\Data\LODSettings\LODGen\<Worldspace>.lod` /
`.dlodsettings` per §1) that is passed to `LODGen.exe` with the `GameMode=` key
it emits. All evidence is byte-verified in `xLODGen.exe` (image base 0x400000).

### 9.1 Exact line order

Each line is a UTF-16LE string written through the list object's virtual
`WriteLine` (`call [ecx+0x3c]`), exactly as in the terrain writer.

| # | Line | Composition | Evidence |
|---|---|---|---|
| 1 | `GameMode=<token>` | `"GameMode="` + mode token | "GameMode=" 0x141e180 @ 0x141cc21 |
| 2 | `Worldspace=<name>` | `"Worldspace="` + selected worldspace (tree node getter, helper `0x140412c` + vtable 0x2a4) | "Worldspace=" 0x141e1a0 @ 0x141cc6a |
| 3 | `CellSW=<swx> <swy>` | `"CellSW="` + `"%d %d"` | "%d %d" 0x141e1c4 @ 0x141ccb7; "CellSW=" 0x141e1dc @ 0x141cccd |
| 4 | `RemoveUnseenFaces=True` | **literal, unconditional** | "RemoveUnseenFaces=True" 0x141e1f8 @ 0x141cce8 |
| 5 | `IgnoreWater=<bool>` | `"DATA\\No LOD Water"` record check → `"IgnoreWater="` + True/False | 0x141e234 @ 0x141ccfe; "IgnoreWater=" 0x141e264 @ 0x141cd29 |
| 6 | `DontGenerateVertexColors=<bool>` | option `ObjectsNoVertexColors` → `"DontGenerateVertexColors="` | 0x141e28c @ 0x141cd64; 0x141e2c4 @ 0x141cd81 |
| 7 | `DontGenerateTangents=<bool>` | option `ObjectsNoTangents` → `"DontGenerateTangents="` | 0x141e304 @ 0x141cdbc; 0x141e334 @ 0x141cdd9 |
| 8 | `DefaultAlphaThreshold=<0-255>` | option `DefaultAlphaThreshold` (combo `cmbDefaultAlphaThreshold`, engine parses as byte) → `"DefaultAlphaThreshold="` | 0x141e36c @ 0x141ce18; 0x141e3a4 @ 0x141ce3c |
| 9 | `UseAlphaThreshold=<bool>` | option `ObjectsUseAlphaThreshold` → `"UseAlphaThreshold="` | 0x141e3e0 @ 0x141ce77; 0x141e420 @ 0x141ce94 |
| 10 | `UseBacklightPower=<bool>` | option `ObjectsUseBacklightPower` → `"UseBacklightPower="` | 0x141e454 @ 0x141cecf; 0x141e494 @ 0x141ceec |
| 11 | `TextureAtlasMap=<file>` | `"TextureAtlasMap="` + atlas filename (see 9.3) | "TextureAtlasMap=" 0x141e4c8 @ 0x141cf1a |
| 12 | `AtlasTolerance=<%1.1f>` | `"AtlasTolerance="` + `(AtlasTextureUVRange − 1)` formatted `"%1.1f"` | "%1.1f" 0x141e4f8 @ 0x141cf66; "AtlasTolerance=" 0x141e510 @ 0x141cf7c |
| 13 | `PathData=<DataPath>` | `"PathData="` + Data folder (`*PTR_DAT_0163e500`) | "PathData=" 0x141e53c @ 0x141cfa5 |
| 14 | `PathOutput=<DataPath>meshes\\terrain\\<ws>\\Objects` | 5-arg concat: `"PathOutput="` + Data folder (`*PTR_DAT_0163ebf4`) + `"meshes\\terrain\\"` + ws + `"\\Objects"` | "PathOutput=" 0x141e55c @ 0x141cfc0; "meshes\\terrain\\" 0x141e580 @ 0x141cfcc; "\\Objects" 0x141e5ac @ 0x141cff9; UStrCatN(5) @ 0x141d003 |
| 15 | `Resource=<file>` × N | `"Resource="` + each entry of the xEdit file list (`*PTR_DAT_0163c49c`), first 2 entries skipped | "Resource=" 0x141e5cc @ 0x141d09b; loop 0x141d04c–0x141d0bb |

Then the **LODGen.txt merge** (§9.4) and the log line `"] Saving LODGen data: "`
(0x141e68c @ 0x141d2f3) before the list is saved.

Notes:
- Lines 11–12 are emitted inside the atlas block, which only runs when the
  atlas list is non-empty (`iVar15 = list.Count; if (iVar15 != 0)` at 0x141c9b1).
- Line 4 is unconditional — there is no `RemoveUnseenFaces` option read in this
  writer; the engine's `REMOVEUNSEENFACES` (`flag2 = bool.Parse`) will always see
  `True`.
- The option keys (lines 6–10) are looked up in the options dialog named
  `<mode> LOD Options` (`concat(modeToken, " LOD Options")`, literal 0x141e860);
  the values are the checkbox states, written as `True`/`False` (string table
  `PTR_u_False_0163ab5c`).
- `IgnoreWater` (line 5) is `True` when the worldspace's plugin file carries the
  `DATA\No LOD Water` marker (virtual read with marker string 0x141e234).

### 9.2 The `GameMode` value and the PathOutput data folder

- `GameMode=<token>` uses the **same mode-token global** as the terrain writer
  (`*PTR_DAT_0163c5d0`, §4a table: TES3/TES4/TES4R/FO3/FNV/TES5/Enderal/FO4/SSE/
  TES5VR/EnderalSE/FO4VR/FO76/SF1) — but **without** the `TERRAIN` prefix. The
  engine's `GAMEMODE` switch matches these with the same case-insensitive
  `IsMode` substring test.
- `PathOutput` uses `*PTR_DAT_0163ebf4`, a second copy of the Data path made at
  init (`FUN_0137ea9c` @ 0x137efc9: `UStrAsg(PTR_DAT_0163ebf4, *PTR_DAT_0163e500)`,
  then made absolute from the current directory when the game folder is
  relative). So `PathOutput` is an **absolute** path, and the engine's
  `TerrainData` constructor strips it at `\MESHES\`
  (`outputDir.Remove(IndexOf("\\MESHES\\") + 1)`, `TerrainData.cs:31`), so the
  value must sit under `Data\meshes\`.

### 9.3 The atlas filename (`TextureAtlasMap=`)

5-arg concat (0x141ca83–0x141caaf, UStrCatN @ 0x141caaf):

```
<Edit Scripts folder> + <mode token> + "LODGen-AtlasMap" + <worldspace> + ".txt"
```

(`*PTR_DAT_0163e934` — the tool's `Edit Scripts` folder, cf. `FUN_0137ea9c`
@ 0x137eb1c concat with `"Edit Scripts\\"`; literals "LODGen-AtlasMap" 0x141e044
@ 0x141ca71, ".txt" 0x141e070 @ 0x141ca9e). A second variant swaps the middle
segment for `"-AtlasMap-"` (0x141e15c @ 0x141cbca) and is used when the
`BuildAtlas` flag is off and the first-named file does not exist (0x141cbb7–
0x141cc08).

### 9.4 The LODGen.txt options-file merge

After `PathOutput`, the writer looks for the per-game options file
`<Edit Scripts folder>\LODGen.txt` (`concat(*PTR_DAT_0163e934, "LODGen.txt")`,
literal 0x141e668 @ 0x141d2bc):

- **found** → log `"] Using options file: <path>"` (0x141e5ec @ 0x141d20a),
  load its lines (`TStringList.LoadFromFile`, vtable 0x6c), append them to the
  settings list (`AddStrings`, vtable 0x44) and **return immediately** — the
  `Resource=` loop and the final save run only when no options file exists.
- **not found** → log `"] No options file found: <path>"` (0x141e628 @
  0x141d27e), then the `Resource=` loop (§9.1 row 15) and `"] Saving LODGen
  data: <path>"`.

### 9.5 The older sibling writer (`FUN_014131c4`)

An earlier object-LOD writer exists in the same binary (mode split at 0x1416f61,
strings in the 0x1419xxx table):

- `GameMode=` @ 0x1416c93, `PathData=` @ 0x1416f47, `Season=` @ 0x141709a
  (this writer **does** emit `Season=`, the new writer does not).
- `PathOutput` split on the mode byte: modes 0–6 and 7–10 →
  `<DataPath>meshes\\terrain\\<ws>\\Objects` (0x1416f74, literals 0x1419ab8 /
  0x1419ae4); modes 11+ (FO4VR/FO76/SF1) →
  `<DataPath>meshes\\landscape\\lod\\<ws>\\Blocks` (0x1416fdb, literals
  0x1419b04 / 0x1419b3c `"\\Blocks"`).
- It raises `"Unsupported LODGen game"` (0x1419b58 @ 0x141704d) for the modes
  that fall between the two branches — i.e. this path predates the modern
  FO4/SSE object-LOD flow and is dead for those modes; `FUN_0141c4c0` is the
  active writer.

### 9.6 Round-trip

Every key above is parsed by the engine (`LODGeneratorCMD/Program.cs`,
case-insensitively): `GAMEMODE`, `WORLDSPACE`, `CELLSW` (space-split pair),
`REMOVEUNSEENFACES` → `flag2`, `IGNOREWATER` → `flag3`, `DONTGENERATEVERTEXCOLORS`
and `DONTGENERATETANGENTS` → inverted bool per LOD level, `DEFAULTALPHATHRESHOLD`
→ `byte.Parse`, `USEALPHATHRESHOLD` → `flag7`, `USEBACKLIGHTPOWER` → `flag8`,
`TEXTUREATLASMAP` → atlas reader, `ATLASTOLERANCE` → float, `PATHDATA` →
`gameDir`, `PATHOUTPUT` → output dir, `RESOURCE` → `File.Exists` + list,
`SEASON` → `"." + name`. The validator's KEYS table already models these;
§9.1 is the writer-side counterpart.
