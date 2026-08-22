# Reverse Engineering Report: xLODGen.exe

Analyzed with **Ghidra 12.1.2** (headless mode), `objdump`, and string analysis.
MD5: `37c0e0d94b4bd836967f05529fc60e74` (x86 build; `xLODGenx64.exe` = 927afec4c8abd6ebbd8941021945506e)

---

## 1. Binary identification

| Property | Value |
|---|---|
| Format | PE32, Windows GUI (subsystem 2), x86 i386 |
| Compiler (Ghidra) | `borland:pascal` — **Delphi** (Embarcadero RAD Studio) |
| PE timestamp | Wed May 28 12:53:56 2025 |
| Image base | 0x00400000 |
| Code section `.text` | 0x00401000 – 0x015F51FF (~17 MB) |
| Sections | 11 (`.text`, `.itext`, `.data`, `.bss`, `.idata`, `.didata`, `.edata`, `.tls`, `.rdata`, `.reloc`, `.rsrc`) |
| Imports | 1,490 (standard Delphi VCL set: comctl32, gdi32, comdlg32, winmm, mpr, winspool, …) |
| Functions found | **34,793** |
| Strings extracted | 6,000+ (truncated in dump) |
| Entry point | 0x015FDC1C |

The 32-bit (`xLODGen.exe`) and 64-bit (`xLODGenx64.exe`) binaries are the same application; only the x86/x64 target differs. The distribution readme says the x64 build is the supported one.

## 2. What this program is

xLODGen is **sheson's LOD-generation tool for Bethesda games** (FNV, FO3, FO4, FO4VR, TES5, TES5VR, SSE, ENDERAL, ENDERALSE, and per the embedded help also TES4, FO76, Starfield "sf1"). It is built on the **xEdit (TES5Edit) framework** — the same codebase as SSEEdit/FO4Edit — with a custom "LODGen" mode.

Evidence:
- Embedded Delphi source paths: `E:\Delphi\Projects\xLODGen\Core\wbDataFormat.pas`, `wbInterface.pas`, `wbImplementation.pas`, `wbDefinitionsCommon/FNV/FO3/FO3Saves/FO4Saves/TES5Saves.pas`, `xEdit\xeMainForm.pas`, `xEdit\JvI\xejviScriptHost.pas`.
- Third-party units embedded: DDetours (API hooking), JCL/JVCL (Jedi), SynEdit, VirtualTrees, DevExpress (dx*), madExcept (crash handler), plus `Bugzilla`/XML-RPC code.
- Mode is selected by executable name: *"Application name must contain Edit, View, LODGen, OnamUpdate, MasterUpdate, MasterRestore, setESM, clearESM, sortAndCleanMasters, CheckForITM, CheckForDR or CheckForErrors to select mode."* (`xLODGen.exe` → LODGen mode.)
- Version/update checks against `https://api.github.com/repos/TES5Edit/TES5Edit/releases` and NexusMods pages.

## 3. Architecture: two-part pipeline

The distribution contains **two unrelated executables** that work as a pair:

1. **`xLODGen.exe` (Delphi GUI)** — plugin loader, worldspace/data exporter, settings UI.
   - Reads game plugins, exports terrain height data, builds a `LODSettings\LODGen\*.dlodsettings` config, then **spawns the console engine as a subprocess** ("LODGen process error, exit code …").
2. **`Edit Scripts\LODGen.exe` / `LODGenx64.exe` (Mono/.NET console)** — the actual generation engine.
   - "Generating terrain LOD meshes for worldspace …", "Generating flat terrain LOD …", "Generating object LOD meshes …".
   - Produces `.bto` / `.btr` terrain meshes and terrain textures under `Data\Textures\Terrain\`.
   - Uses BZip2 streams (SharpZipLib), large-ref handling (`-LargeRef`), atlas/texture-list support.
   - Source class name visible: `TerrainLOD.cs` (C#).

The Delphi side does the heavy data extraction (it has the full xEdit record definitions for every game), and the .NET side does mesh/texture generation.

## 4. Command-line interface (Delphi exe)

xEdit-framework switches (all inherited from TES5Edit):
- `-<gamemode>` — one of `tes5vr fo4vr tes4 tes4r tes5 enderal enderalse sse fo3 fnv fo4 fo76 sf1`
- `-o:"path"` output path (default: game folder)
- `-m:"path"` INI/My Games folder, `-p:"path"` plugins.txt, `-d:"path"` Data folder
- `-G:<path>` save game path, `-T:<path>` temporary directory
- `-AllowDirectSaves:<files>`, `-PseudoESL`, `-edit` / `-view` modes, `-VeryQuickShowConflicts`
- `-l:<language>`, `-cp:<codepage>`, `-cp-trans:<codepage>`
- modgroups: `[xEdit EXE Name].modgroups` file next to the exe

LOD-specific UI options (from component strings / DFM): atlas building (`BuildAtlas`, atlas width/height/size, texture size, UV range, brightness), terrain LOD texture generation (diffuse + normal, mipmaps at LOD4/8/16/32, brightness/contrast per LOD), terrain mesh generation (`Build terrain LOD meshes`), occlusion (`Create/Update TVDT LOD occlusion data in Occlusion.esp`, height/quality/radius), heightmaps, "hide quads", "protect cell borders", "Create LODSettings File", worldspace browser (`Alt+F3`).

## 5. LODSettings file format (`.dlodsettings`)

Written by the GUI into `LODSettings\LODGen\` and consumed by the .NET engine. Keys found (UTF-16 strings):

```
GameMode=            Worldspace=          CellSW=          Season=
PathData=            PathOutput=          Resource=        TerrainData=
LandHeight=          WaterHeight=         MaxLevel=        HideQuads=
ProtectCellBorders=  IgnoreWater=         IgnoreTranslation=
FlatTextures=        TextureAtlasMap=     TextureDiffuseHD=  TextureNormalHD=
AtlasTolerance=      AlphaDoubleSided=    DefaultAlphaThreshold=  UseAlphaThreshold=
DontGenerateTangents=  DontGenerateVertexColors=  UseBacklightPower=
```

- Extensions: `.dlodsettings` for modern game modes (byte 0..1 at `PTR_DAT_0163c634`), legacy `.lod` otherwise (verified in decompiled `FUN_01404948`).
- `GameMode=TERRAIN` was seen as a value.
- Naming strings: `LODGen_Terrain_*`, `LODGen-AtlasMap`, `LODGen_flat_lod.nif`, `LODGenFlatTextures.txt`, log to `LODGen.txt`.

## 6. Key functions decompiled (Ghidra)

| Address | Role | Notes |
|---|---|---|
| `0x015FDC1C` | `entry` | Delphi RTL init; `Application.Initialize`; loads icon; calls theme loader |
| `0x01385250` | theme loader | Scans `Themes` dir for `*.vsf`, registers VCL styles |
| `0x013850E4` | startup check | calls `FUN_01381084` (mode/startup validation) |
| `0x01404948` | LODSettings path builder | Builds `lodsettings\<Worldspace>.dlodsettings` (or `.lod` for legacy modes) |

Decompiled C output is in `ghidra-dump/` (see artifacts below).

## 7. Artifacts produced

Companion deep-dives: [XLODGEN_REVERSE_ENGINEERING.md](../XLODGEN_REVERSE_ENGINEERING.md) (GUI forms map + subprocess handoff), [LODGEN_ENGINE.md](../LODGEN_ENGINE.md) (the .NET engine), [XLODGEN_QUALITY.md](../XLODGEN_QUALITY.md) (Quality/WaterDelta reconciliation), [XLODGEN_OBJECT_LOD.md](XLODGEN_OBJECT_LOD.md), [XLODGEN_OUTPUT_FORMAT.md](XLODGEN_OUTPUT_FORMAT.md), [XLODGEN_ATLAS_TEXTURES.md](XLODGEN_ATLAS_TEXTURES.md).

Everything below is saved under `reverse-engineering/`:

- `ghidra-dump/xLODGen.exe.analysis.txt` — full Ghidra dump: 34,793 functions, 1,490 imports, 6,000 strings, memory map.
- `ghidra-dump/decompiled.txt`, `decompiled2.txt` — entry + init functions.
- `ghidra-dump/lodsettings.txt` — decompiled LODSettings path builder.
- Ghidra project itself lives in `/tmp/ghidra_proj/xlodgen` (program: `/xLODGen.exe`); scripts in `/tmp/ghidra_scripts/`.

### Reproducing

```
# fast analysis (slow analyzers disabled to fit the 24MB binary in headless time)
cd /tmp/ghidra_12.1.2_PUBLIC
./support/analyzeHeadless /tmp/ghidra_proj xlodgen \
  -process xLODGen.exe \
  -preScript LimitAnalyzers.java -postScript DumpAnalysis.java /tmp/ghidra_out \
  -scriptPath /tmp/ghidra_scripts
```

## 8. Notes & caveats

- Analysis ran **without** the Stack, Decompiler Parameter ID, Decompiler Switch Analysis, Aggressive Instruction Finder, and Function ID analyzers (they make full analysis of this 24 MB Delphi binary exceed headless time limits). Consequences: function signatures are rough, Delphi RTTI names are not recovered, and decompilation of complex functions degrades (hence the "could not recover jumptable" warnings). Running the full analyzer set in the Ghidra GUI (or a longer headless run) would refine the decompilation.
- Ghidra's stock tooling has no Delphi language module; function names are auto-generated (`FUN_xxxxxxxx`). Installing the community **Ghidra Delphi** plugin would recover class/method names from RTTI.
- This is a **legitimate open-source-adjacent tool** (xEdit is licensed; xLODGen is distributed for free). The RE work here is for understanding/education; the sources of the xEdit framework are publicly available on GitHub (TES5Edit/TES5Edit), which makes the embedded symbols and logic easy to cross-reference.
