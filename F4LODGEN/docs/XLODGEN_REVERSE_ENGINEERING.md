# xLODGen.exe internals from binary analysis

Research date: 2026-08-18

This document records what a Ghidra binary analysis of the released `xLODGen.exe` (x86 build, May 2025) reveals about its internals: the binary identity, the recovered form inventory, and the exact contract by which the GUI hands work to the console engine (`Edit Scripts\LODGen.exe`, documented in [LODGEN_ENGINE.md](LODGEN_ENGINE.md)). It is reference material for the compatibility-oracle approach: F4LODGen targets equivalent observable behavior, and knowing the real binary's structure helps scope what "equivalent" means. It is not a claim about the source code, which is not published for the LODGen parts. Companion analysis: [XLODGEN_OBJECT_LOD.md](reverse-engineering/XLODGEN_OBJECT_LOD.md), [XLODGEN_OUTPUT_FORMAT.md](reverse-engineering/XLODGEN_OUTPUT_FORMAT.md), [XLODGEN_ATLAS_TEXTURES.md](reverse-engineering/XLODGEN_ATLAS_TEXTURES.md), [XLODGEN_QUALITY.md](XLODGEN_QUALITY.md), [XLODGEN_SETTINGS_WRITER.md](reverse-engineering/XLODGEN_SETTINGS_WRITER.md) (exact writer line orders).

Analyzed with Ghidra 12.1.2 (headless), binutils, and string extraction. Sample MD5: `37c0e0d94b4bd836967f05529fc60e74` (x86); the x64 build (`xLODGenx64.exe`) is the same application targeting x86-64.

## 1. Binary identity

- PE32 Windows GUI executable, x86, image base `0x00400000`.
- Ghidra identifies the compiler as `borland:pascal` — this is a **Delphi** (Embarcadero RAD Studio) application.
- PE timestamp: Wed May 28 12:53:56 2025.
- 11 sections; `.text` is roughly 17 MB of code; 1,490 imports (standard Delphi VCL set).
- Analysis recovered **34,793 functions** and 6,000+ strings.

## 2. What the binary actually is

xLODGen is built on the **xEdit (TES5Edit) framework** with a custom "LODGen" mode. Embedded Delphi source paths confirm the lineage:

```
E:\Delphi\Projects\xLODGen\Core\wbDataFormat.pas
E:\Delphi\Projects\xLODGen\Core\wbInterface.pas
E:\Delphi\Projects\xLODGen\Core\wbImplementation.pas
E:\Delphi\Projects\xLODGen\Core\wbDefinitionsCommon.pas
E:\Delphi\Projects\xLODGen\Core\wbDefinitionsFNV.pas / FO3 / FO3Saves / FO4Saves / TES5Saves
E:\Delphi\Projects\xLODGen\xEdit\xeMainForm.pas
E:\Delphi\Projects\xLODGen\xEdit\JvI\xejviScriptHost.pas
```

Third-party components embedded: DDetours (API hooking), JCL/JVCL, SynEdit, VirtualTrees, DevExpress controls, madExcept (crash handler). The executable name selects the mode: *"Application name must contain Edit, View, LODGen, OnamUpdate, MasterUpdate, ... to select mode."* `xLODGen.exe` contains `LODGen`, so it boots into LODGen mode.

### The two-executable pipeline

This is the most important architectural finding. The distributed tool is two unrelated executables working as a pair:

1. **`xLODGen.exe` (Delphi GUI)** — xEdit framework: loads plugins, resolves winning records, exports terrain height data, writes a settings file, and **spawns a console subprocess** ("LODGen process error, exit code ...").
2. **`Edit Scripts\LODGen.exe` / `LODGenx64.exe` (Mono/.NET console)** — the actual generation engine: "Generating terrain LOD meshes for worldspace ...", "Generating flat terrain LOD ...", "Generating object LOD meshes ...". It emits `.bto`/`.btr` meshes and terrain textures under `Data\Textures\Terrain\`, uses BZip2 streams (SharpZipLib), and its strings expose a `TerrainLOD.cs` class.

So the current Fallout 4 workflow is not just multi-tool across ecosystem projects — it is multi-process *within* xLODGen itself. F4LODGen's single-pipeline design directly removes this handoff.

## 3. Command-line surface (Delphi exe)

xEdit-framework switches (shared with TES5Edit/SSEEdit):

- `-<gamemode>` — `tes5vr fo4vr tes4 tes4r tes5 enderal enderalse sse fo3 fnv fo4 fo76 sf1`
- `-o:"path"` output path (defaults to the game folder), `-m:"path"` INI folder, `-p:"path"` plugins.txt, `-d:"path"` Data folder
- `-G:<path>` save game path, `-T:<path>` temporary directory
- `-AllowDirectSaves:<files>`, `-PseudoESL`, `-edit`, `-view`, `-VeryQuickShowConflicts`
- `-l:<language>`, `-cp:<codepage>`, `-cp-trans:<codepage>`
- modgroups via `[xEdit EXE Name].modgroups` beside the executable

## 4. How the form inventory was recovered

Delphi stores each form as a **binary DFM blob** in the `.rsrc` resource section. 30 blob candidates were located by scanning for the `TPF0` signature; 26 are real forms (3 are `TMadExcept`-family dialogs from the madExcept crash handler, 1 is the `TwbHardcodedContainer` resource container, 3 are non-DFM data hits).

A binary DFM parser was written for this task — `tools/extract_dfm.py` — and run over the executable to produce the full property tree dump (`/tmp/ghidra_out/forms.txt`). Encodings implemented (all verified against raw bytes and the FPC `classes.inc`/`reader.inc`/`writer.inc` sources):

| Value type | Byte | Encoding |
|---|---|---|
| `vaNull` | 0x00 | terminator (object props end, collection end) |
| `vaList` | 0x01 | count = 0 → 0x00, else item bytes, then props bytes |
| `vaInt8` | 0x02 | 1 byte |
| `vaInt16` | 0x03 | little-endian 2 bytes |
| `vaInt32` | 0x04 | little-endian 4 bytes |
| `vaExtended` | 0x05 | 10-byte x87 extended float |
| `vaString` | 0x06 | 1-byte length + ANSI bytes |
| `vaIdent` | 0x07 | 1-byte length + ANSI bytes |
| `vaFalse` / `vaTrue` | 0x08 / 0x09 | 0 bytes |
| `vaBinary` | 0x0A | 4-byte length + data |
| `vaSet` | 0x0B | `[ident]* 0x00` |
| `vaLString` | 0x0C | 4-byte length + ANSI bytes |
| `vaNil` | 0x0D | 0 bytes |
| `vaCollection` | 0x0E | 4-byte count, then per item `[vaList + props + vaNull]*` + final `vaNull` |
| `vaInt64` | 0x10 | 8-byte LE |
| `vaString` extended length | — | length byte ≥ 0x80: 0x80→2B, 0x81→4B, 0x82→8B, 0x83→10B, else 4-byte LE |

Object tree: `classname(name) props... 0x00 children... 0x00` (each component is `T<class> <name>` followed by properties, terminated, then child components, terminated). The root form classname is written with no name prefix.

## 5. Form map (26 real forms)

| RVA | Form | Class | Title / purpose |
|---|---|---|---|
| 0x0162fae4 | `frmDeveloperMessage` | `TfrmDeveloperMessage` | "A message from the developer" — 134 KB, dev message + changelog dialog |
| 0x01650788 | `frmEditWarning` | `TfrmEditWarning` | "Time to think..." — warning on destructive edits |
| 0x01658c64 | `frmFileSelect` | `TfrmFileSelect` | "Master/Plugin Selection" |
| 0x01659138 | `frmFilterOptions` | `TfrmFilterOptions` | "Filter..." |
| 0x0165a97c | `frmLegend` | `TfrmLegend` | "Legend" |
| 0x0165abfc | `frmLocalization` | `TfrmLocalization` | "Localization Editor" |
| 0x0165b5a4 | `frmLocalizePlugin` | `TfrmLocalizePlugin` | "Localize plugin" |
| 0x0165bb20 | `frmLODGen` | `TfrmLODGen` | **"LODGen Options" — the main LOD generation dialog** (29.5 KB, see §6) |
| 0x01662e58 | `frmLODSettingsFile` | `TfrmLODSettingsFile` | **"Create LODSettings File" (see §7)** |
| 0x01664298 | `frmLogAnalyzer` | `TfrmLogAnalyzer` | "Log Analyzer" |
| 0x01664ed4 | `frmMain` | `TfrmMain` | "xEdit" — the main window (148.5 KB; all of xEdit's UI) |
| 0x01689308 | `frmModGroupEdit` | `TfrmModGroupEdit` | "ModGroup Selection" |
| 0x0168a0f8 | `frmModGroupSelect` | `TfrmModGroupSelect` | "ModGroup Selection" |
| 0x0168b420 | `frmModuleSelect` | `TfrmModuleSelect` | "Module Selection" |
| 0x0168c8ec | `frmOptions` | `TfrmOptions` | "Options" (xEdit preferences) |
| 0x0168fef8 | `frmRichEdit` | `TfrmRichEdit` | generic rich-text viewer |
| 0x01690944 | `frmScript` | `TfrmScript` | "Apply Script" |
| 0x016912c4 | `frmTip` | `TfrmTip` | "Tip" |
| 0x01692534 | `frmViewElements` | `TfrmViewElements` | element viewer |
| 0x01692d5c | `frmWait` | `TfrmWait` | modal wait/progress dialog |
| 0x01693078 | `frmWorldspaceCellDetails` | `TfrmWorldspaceCellDetails` | worldspace/cell details |

Non-form DFM blobs: `TMadExcept 'madExcept'` (0x00085624 — the embedded madExcept exception handler settings), `TMadExcept` 0x016934c8, `TMEContactForm 'ContactForm'`, `TMEDetailsForm 'DetailsForm'`, `TMEScrShotForm 'ScrShotForm'` (madExcept bug-report dialogs), and `TwbHardcodedContainer 'wbHardcodedContainer'` (0x01694844 — a 64 KB hardcoded-records resource container).

## 6. `frmLODGen` — the LOD generation dialog

Tabs (from the DFM component tree):

- **Terrain LOD** tab — per-LOD-level (LOD4/8/16/32) groups of:
  - `cmbTerrainQualityLOD<n>` — hint: *"Lower = Better / Larger Files, Higher = Worse / Smaller Files, -1 = No Optimization"* (LOD4 only mentions `-1 = No Optimization`). Owner-drawn combo, populated at runtime → defaults live in code, see [XLODGEN_QUALITY.md](XLODGEN_QUALITY.md).
  - `edTerrainMaxVertsLOD<n>` — hint: *"Max allowed number of Vertices in case quality setting exceeds NIF limits."* (`OnChange` re-checks consistency).
  - `cmbTerrainWaterDeltaLOD<n>` — hint: *"on = merge large chunks of triangles under water resulting in less vertices. x = also m..."* (per-LOD water "optimize unseen").
  - `cmbTerrainContrastLOD<n>`, `cmbTerrainDiffuseSizeLOD<n>`, `cmbTerrainDiffuseCompLOD<n>`, `cbTerrainDiffuseMipMapLOD<n>`, `cmbTerrainGammaLOD<n>`, `cmbTerrainBrightnessLOD<n>`, `cmbTerrainNormalSizeLOD<n>`, `cmbTerrainNormalCompLOD<n>`, `cbTerrainNormalMipMapLOD<n>`, `cbTerrainNormalRaiseSteepnessLOD<n>` — texture-pipeline controls (diffuse/normal sizes, compression, mipmaps, gamma/brightness/contrast).
  - `cbTerrainProtectCellBorders` — *"Keep cell borders intact to avoid weird terrain drops at border between loaded cells and LOD."*
  - `cbTerrainHideQuads` — *"Do not generate terrain for quads set to hidden on cell record."*
  - `cbBuildNormalTextures`, `cbTerrainNormalBakeMaps` (bake blended landscape normals), `cmbTerrainDefaultDiffuseSize`, `cmbTerrainDefaultNormalSize`.
- **Object LOD** tab — atlas building controls (`BuildAtlas`, atlas width/height, texture size, UV range, brightness), `ObjectsNoVertexColors`, `ObjectsNoTangents`, `ObjectsUseAlphaThreshold`, `ObjectsUseBacklightPower`, `DefaultAlphaThreshold` (see the option-key list in §9), grass (density, GID), LOD settings file generation, worldspace browser (Alt+F3), "Create/Update TVDT LOD occlusion data" with height/quality/radius (`cmbOcclusionQuality` hint: *"Samples per cell 1=1 2=4 3=16"*), heightmaps, hide quads, protect cell borders.

All option names in this dialog match the `KEY=` names the engine consumes (see [LODGEN_ENGINE.md](LODGEN_ENGINE.md) full key list), which is what makes the GUI→engine contract lossless.

## 7. `frmLODSettingsFile` — "Create LODSettings File"

Standalone dialog for exporting a `.dlodsettings` without running generation:

- `cmbWorldspaces` — worldspace picker.
- Settings group: `LOD Origin X/Y`, `LOD End X/Y`, `Power of 2 of # of cells to reach North or East` (stride), `Minimum/Maximum LOD Level`, `Object LOD Level`, `Align LOD origin to multiple of selected value`.

This form drives the same settings-file writer used by the generation path (`FUN_01404948`, the `LODSettings\LODGen\<Worldspace>.dlodsettings` builder; legacy modes get `.lod` instead).

## 8. Subprocess handoff (GUI → LODGen.exe)

### 8.1 Settings-file writer (`FUN_01404948`)

Builds the per-worldspace settings file path:

```
<Data>\LODSettings\LODGen\<Worldspace>.dlodsettings   (modern game modes)
<Data>\LODSettings\LODGen\<Worldspace>.lod            (legacy modes)
```

`.dlodsettings` vs `.lod` is selected by the mode byte pair at `PTR_DAT_0163c634` (verified in the decompiled writer).

### 8.2 Object-LOD build handler (`FUN_0141c4c0`)

This is the "Building Object LOD" pipeline entry (status log: `"] Building Object LOD"`). It:

1. Calls `FUN_01404948` to get the settings path, reads the options file `<Edit Scripts>\LODGen.txt` if present (log `"] Using options file: <path>` / `"] No options file found: <path>`), then opens the LOD options dialog (`" LOD Options"`).
2. **Gathers references** (`"] Gathering references"`), **builds the object LOD** (`"] Building Object LOD"`).
3. Writes the settings file with these keys (in order, from the decompile):

```
GameMode=<mode>                 Worldspace=<name>            CellSW=<x> <y>
RemoveUnseenFaces=True          IgnoreWater=<bool>           DontGenerateVertexColors=<bool>
DontGenerateTangents=<bool>     DefaultAlphaThreshold=<n>    UseAlphaThreshold=<bool>
UseBacklightPower=<bool>        [TextureAtlasMap=<path>      AtlasTolerance=<0.1f>]
PathData=<data dir>             PathOutput=<Data>\meshes\terrain\<ws>\Objects
[Resource=<bsa> ...]            [LODLevel=<n> LODX=<n> LODY=<n>]
```

The boolean/scale values come from named options: `ObjectsNoVertexColors`, `ObjectsNoTangents`, `ObjectsUseAlphaThreshold`, `ObjectsUseBacklightPower`, `DefaultAlphaThreshold`, `AtlasWidth` (default **0x1000 = 4096**), `AtlasHeight` (default **0x1000 = 4096**), `AtlasTextureSize` (default **0x200 = 512**), `AtlasTextureUVRange` (default **1.5**, so `AtlasTolerance=0.5`). `IgnoreWater` is derived from the plugin file `DATA\No LOD Water`. `PathOutput` is `<DataPath>meshes\terrain\<worldspace>\Objects` (the FO4 object-LOD output folder; engine appends `.bto` files there) — the exact per-line evidence table for this writer is in [XLODGEN_SETTINGS_WRITER.md](reverse-engineering/XLODGEN_SETTINGS_WRITER.md) §9 (`FUN_0141c4c0`).

4. Builds the command line:

```
"<Data>\LODGen.exe" "<settings file path>" [--lodlevel <n>] [--x <n>] [--y <n>]
```

(format literal `"\"%s\" \"%s\""`, engine filename `<Data-dir>LODGen.exe`). The `--lodlevel/--x/--y` switches are appended only when the corresponding `LODLevel/LODX/LODY` options are set (single-level / single-quad debug runs).

5. Spawns the process via `FUN_00d4ace0(cmdline, 1)` and checks the exit code; a nonzero exit raises:

```
LODGen process error, exit code <n>
```

6. Terrain side: `FUN_014066a4` writes the **terrain heightmap handoff** `<Data>\LODSettings\LODGen\LODGen_Terrain_<Worldspace>.bin` — the binary terrain data file read back by the engine's `TerrainData` reader (format in [LODGEN_ENGINE.md](LODGEN_ENGINE.md) §"Terrain data file").
7. The GUI-side **settings-file writer** (the `frmLODSettingsFile` save flow, `FUN_01438a54` @ `xLODGen.exe` 0x143aa00–0x143b600) is documented in [XLODGEN_SETTINGS_WRITER.md](reverse-engineering/XLODGEN_SETTINGS_WRITER.md) — exact line order (`GameMode=TERRAIN<Name>`, `Worldspace=`, `CellSW=`, `MaxLevel=`, `TerrainData=`, `IgnoreWater=`, `LandHeight=`, `WaterHeight=`, `ProtectCellBorders=`, `HideQuads=`, per-LOD `QualityLOD<n>/MaxVertsLOD<n>/WaterDeltaLOD<n>`, `Skirts=`, `PathOutput=`, `Season=`, `PathData=`, `Resource=`), defaults, and the mode split.
8. Material handling in the same handler: `.bgsm` material files are read from resources (`wbDataFormatMaterial::TwbBGSMFile`), with the FO4-style fallback that strips the `__<color>` suffix when the `_lod__black17.bgsm` form is not found (`metalpaneltrim01_lod__black17.bgsm` → `metalpaneltrim01_lod_black17.bgsm`), and the `Textures\Diffuse` entry is pulled for texture resolution.

### 8.3 Runner (`FUN_00d4ace0`)

Process launcher wrapper (the call site `FUN_0141c4c0` and terrain handler both use it with a `1` flag). It returns the process exit code (0 = success) — the exact failure string "LODGen process error, exit code" is the observable contract that a compatibility oracle should replicate: spawn, wait, propagate nonzero exit with that message.

## 9. The `.dlodsettings` contract

The GUI writes a settings file into `LODSettings\LODGen\` and the .NET engine consumes it. This is the data contract between the two halves and the most actionable artifact for F4LODGen.

Keys recovered from the binary (UTF-16 string pool):

```
GameMode=                Worldspace=               CellSW=              Season=
PathData=                PathOutput=               Resource=            TerrainData=
LandHeight=              WaterHeight=              MaxLevel=            HideQuads=
ProtectCellBorders=      IgnoreWater=              IgnoreTranslation=
FlatTextures=            TextureAtlasMap=          TextureDiffuseHD=    TextureNormalHD=
AtlasTolerance=          AlphaDoubleSided=         DefaultAlphaThreshold= UseAlphaThreshold=
DontGenerateTangents=    DontGenerateVertexColors= UseBacklightPower=
```

- File extension depends on game mode: `.dlodsettings` for modern modes, legacy `.lod` otherwise. The decompiled path builder (`FUN_01404948`) constructs `lodsettings\<Worldspace>.dlodsettings` and appends the extension per mode.
- `GameMode=TERRAIN` appears as a value.
- Related naming strings: `LODGen_Terrain_*`, `LODGen-AtlasMap`, `LODGen_flat_lod.nif`, `LODGenFlatTextures.txt`, log file `LODGen.txt`.

This directly matches the terrain-generation-mode contract referenced in the DynDOLOD changelog (`QualityLOD*`, `MaxVertsLOD*`, `ProtectCellBorders`, `WaterDeltaLOD*`): the .NET engine is what interprets those settings, and its exact vertex-removal implementation is not published.

## 10. Key decompiled functions

| Address | Role |
|---|---|
| `0x015FDC1C` | `entry` — Delphi RTL init, `Application.Initialize`, icon load, theme loader call |
| `0x01385250` | VCL theme loader — scans `Themes` dir for `*.vsf`, registers styles |
| `0x013850E4` | startup validation |
| `0x01404948` | LODSettings path builder — `lodsettings\<Worldspace>.dlodsettings` / `.lod` |

## 11. RTTI symbol recovery (Ghidra + DelphiReSym)

Because the tool is a Delphi binary, Ghidra alone leaves functions auto-named `FUN_xxxxxxxx`. To recover real names, we installed **DelphiReSym** (WenzWenzWenz/DelphiReSym, v1.2.1) and ran it through **pyghidra** (pip package 3.1.0, Python 3.13 venv) in headless mode against the already-analyzed program. DelphiReSym walks the after-compilation RTTI metadata (VMTs → MDTs → MethodEntries) and restores fully qualified names, signatures, and virtual-method-table structures.

Setup notes for reproducing headlessly:

- `pip install pyghidra` in a venv; set `GHIDRA_INSTALL_DIR` to the Ghidra distribution.
- Open the analyzed program with `pyghidra.core.open_program(None, project_location=..., project_name=..., program_name=..., nested_project_location=False)`.
- The script expects Script Manager globals `currentProgram`, `monitor`, `toAddr` plus the builtins `getFunctionAt`, `createFunction`, `createClass`; provide Ghidra-API equivalents (FunctionManager/DataTypeManager calls) when running headless.
- The headless monitor ignores `cancel()`; wrap it in a small proxy honoring a local cancel flag so long runs can stop cleanly and persist per-operation progress.
- `apply_symbols` was checkpointed with periodic transaction commits (every 50 VMTs) so progress survives time-boxed runs.

Results on xLODGen.exe:

- **2,738 virtual method tables found, 100% recovered** (names + signatures).
- **20,328 functions carry user-defined names** in the final program.
- Class structures were created for the recovered classes, including the LOD code units: `wbLOD`, `wbInterface`, `wbDataFormat` types (`TwbCellData`, `TCacheData`, `TwbTextureSet`, `TwbGridCell`, ...).
- Notable recovered method: `0x0140909C` is now decompiled as `wbLOD::TwbLodTES5TreeList::BuildAtlas(TwbLodTES5TreeList *Self, int MaxAtlasSize)`, with siblings `GetAtlasRect`, `LoadAtlas`, `SaveFromAtlas`, `CopyFromAtlas`, `ChangeAtlasBrightness`. This confirms the atlas/terrain-LOD orchestration lives in the Delphi side's `wbLOD` unit (matching the `Core/wbLOD.pas` reference in RESEARCH.md).

## 12. Tooling

- `tools/extract_dfm.py` — binary DFM extractor/parser (run: `cd reverse-engineering/tools && python3 extract_dfm.py ../../xLODGen.exe`); output is a text property tree per form plus per-blob parse status.
- Ghidra project: `/tmp/ghidra_proj/xlodgen`; scripts in `/tmp/ghidra_scripts/` (`FindRefs.java`, `FindRefsAddr.java`, `DecompileContaining.java`); extracted decompilations in `ghidra-dump/` and `/tmp/ghidra_out/handoff.txt`, `terrain.txt`, `forms.txt`.

## 13. Relevance to F4LODGen

- The pipeline split (Delphi exporter → .NET engine) is why the current ecosystem has odd seams: settings written to disk, exported terrain data handed off, and stdout/log archaeology for diagnostics. F4LODGen's in-process DAG replaces each seam with an internal dependency.
- The `.dlodsettings` keys above are a compact checklist of the parameters the reference implementation actually feeds its terrain/object pipeline — a useful compatibility checklist even where F4LODGen renames them.
- `PathData`/`PathOutput`/`Resource`/`TerrainData` confirm the reference separates game-data extraction from output paths, matching F4LODGen's record-resolver vs asset-resolver split.
- xEdit itself is MPL-2.0 (see RESEARCH.md); the LODGen.exe engine is not part of the published xEdit tree, so treat its behavior as oracle-only.

## 14. Caveats and reproducibility

- The analysis ran with slow Ghidra analyzers disabled (Stack, Decompiler Switch, Decompiler Parameter ID, Aggressive Instruction Finder, Function ID) to fit the 24 MB binary in headless time limits. Function signatures are rough; complex-function decompilation degrades (the decompiler warns "could not recover jumptable"). Rerunning with the full analyzer set in the GUI would refine them further.
- Stock Ghidra has no Delphi language module; the RTTI pass above (DelphiReSym) recovered class/method names only for **virtual** methods — global functions and non-virtual methods keep `FUN_xxxxxxxx` names (for example the LODSettings path builder at `0x01404948`). Debug-info-based recovery (e.g. IDR + Dhrake) would recover more but is not available headlessly here.
- The 6,000-string extraction is truncated; the full dump is kept locally, not in this repository.
