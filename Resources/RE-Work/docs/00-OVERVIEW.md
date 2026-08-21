# CreationKit.exe — Reverse-Engineering Overview

**Target**: `CreationKit.exe` — the Fallout 4 Creation Kit (the "CK"), Steam edition
**Location**: `.../steamapps/common/Fallout 4 1946160/CreationKit.exe`
**FileVersion / ProductVersion**: `1.11.137.0` (VS_VERSION_INFO resource)
**RSDS path embedded** (CODEVIEW/RSDS): `E:\BuildAgent\work\4a2aa37ca4b2ffa3\scripts\_workspace\Code\Build\PC\CreationKit`
**RSDS GUID**: `f276f0de-3312-9745-8404-bd4b2df2a9aa` (age `01`)
**Build provenance**: TeamCity BuildAgent path → CI-built MSVC release binary. No public debug file.

## Binary identity

| Property | Value |
|---|---|
| Format | PE32+ (x86-64), GUI subsystem, 11 sections |
| Size | 68,844,544 bytes (65.7 MiB) |
| `.text` | VA 0x1000, virtual 49,608,629 bytes — **real code**, entropy 5.86 (NOT packed; contrast: the DRM-encrypted `Fallout4.exe` from the same era measured ~8.0) |
| Compiler | MSVC 2015+ (VCRUNTIME140 / MSVCP140 / api-ms-win-crt-*) |
| RTTI | Present — full MSVC RTTI type names recoverable (`.?AVBGSRenderWindowLandscapeEditModule@@`, `.?AVTESRegionDataLandscape@@`, `.?AVBSResource::CacheDrive@@` …) |
| Section map | `.text` / `.interpr` (57 KB stub) / `.rdata` / `.data` (71 MB BSS — huge static data) / `.pdata` / `.idata` / `.tls` / `.00cfg` / `_RDATA` / `.rsrc` (1.7 MB) / `.reloc` |
| TLS | Callbacks at `0x142f8b4f0` (TLSDirectory) |
| Debug | RSDS record only (no embedded debug file) |

## Import table (DLL surface)

- **System**: KERNEL32 (216), USER32 (197), GDI32 (32), ADVAPI32 (29), OLEAUT32, ole32, SHELL32, SHLWAPI, COMDLG32, COMCTL32, VERSION, SETUPAPI, WINMM, WS2_32, WLDAP32, HID.DLL (raw input), UxTheme, dbghelp (symbol server — the CK debugs itself)
- **Runtime**: MSVCP140 (149), VCRUNTIME140, full api-ms-win-crt-* family
- **Graphics**: d3d11 (`D3D11CreateDeviceAndSwapChain`), d3d9 (PIX event markers), dxgi, GFSDK_GodraysLib.x64.dll, GFSDK_SSAO_D3D11.win64.dll (NVIDIA GameWorks), X3DAudio1_7 + XINPUT1_3 (XAudio legacy)
- **Script editor**: `SSCE5564.dll` (9 imports: `SSCE_OpenSession`, `SSCE_SetMainLexFiles`, `SSCE_SetKey` …) — the same file shipped in the game folder
- **Online**: WINHTTP (14), WININET (8), steam_api64 (10)
- **Crypto / signing**: CRYPT32 (`CertOpenSystemStoreA`, `PFXImportCertStore`, `PFXIsPFXBlob` …), ADVAPI32 CryptoAPI (`CryptCreateHash` …)

## Third-party components (from strings / imports)

- Havok (physics/animation) — "Uses Havok (TM)" copyright
- Bink Video (RAD Game Tools)
- Umbra Software (visibility/occlusion)
- NVIDIA GameWorks (Godrays, SSAO)
- ANTLR 3 (Wintertree Software) — Sentry Spelling-Checker Engine
- Autodesk (3ds Max interop strings, "3dsMax Version")
- Lex/Flex + Yacc lineage in the script compiler (Papyrus Compiler folder ships alongside)

## High-value subsystems (string/RTTI reconnaissance)

- **Shared engine**: BSResource (CacheDrive, file pathing), BGS (form/record framework), TES* (TESRegionDataLandscape, TESWaterObject), BSLightingShaderMaterial*, BGSTerrainMeshBuilder, BSHEMesh (boolean mesh ops, `SaveDebugNIF`), BGSAIWorldLocation* (pathing/navmesh)
- **Editor modules**: `BGSRenderWindowLandscapeEditModule` (+ Factory), render window landscape editing
- **Masterfile system** (next-gen update): `MasterfileOptimizer`, `DLCMasterfileOptimizer`, `MasterfileDelocalizer`, `MasterfileTagifier`, `MasterfileTextExporter`, `MasterFilePathingStream*` — CLI `-SaveDefaultPlugin:<PluginFilename><MasterFilename>` exists
- **Content packs** (2021-02-28 GUIDs): Cloth, Animation, Physics, Behavior — `Bethesda_Fallout4_PS4-Windows-XboxOne`
- **Online**: bethesda.net Creations/Mods pages (`<A HREF="%s/%s/mods/fallout4">`), upload/download flows, `AccountVPCRequiredLogin` (Virtual Possession Code), "update this DLC" prompts; the **bethesda.net SDK is statically linked** (`bnet` namespace: `Mods@bnet`, `Notification@bnet`, `UserWebSocket`, HTTP lambdas), and the client identity comes from `Assets\bnet.p12` (a PKCS#12 store — matches the `PFXImportCertStore`/`PFXIsPFXBlob` imports)
- **Console dev-kit deployment** (legacy): "Error pushing plugin to PS4: Could not connect to PS4 with IP address %s", XB1 via `XtfCreateFileIOClient` from `XtfFileIO.dll`, menu items "Save and push plugin to Xbox One/PS4/PC"
- **Navmesh**: cell finalization ("About to finalize %u cells with navmeshes..."), pathing rebuild
- **Record/plugin strings**: `CombinedObjects.esp`, `Data\CombinedObjects.esp`, `HEDR`, `Close masterfile & plugins...HotLoadPlugin` console command
- **Copyright banner**: "2016 Bethesda Softworks LLC... Creation Kit Engine... Pip-Boy, Vault-Tec..." → the CK is the same engine family as the game, editor build

## Companion files in the game folder

| File | Role |
|---|---|
| `CreationKit.ini` | CK config (see 01-CREATIONKIT-INI.md) |
| `EditorTips.txt` | tip-of-the-day data |
| `Papyrus Compiler/` | script compiler (`PapyrusCompiler.exe` + `ScriptCompile` config) |
| `Tools/` | Archive2, Audio, Elric, LipGen, MaterialEditor, NIF_Exporter, Papyrus profile/stack tools |
| `lex/` | lexer assets for the script editor |
| `Steamless-master/` | unpacker (used for the game exe; CK itself is not packed) |

- **Archive (BSA) building**: `*.achlist` file lists, `ArchiveInvalidation.txt`, version-gated repackaging, size warnings
- **Flowchart**: quest dialogue graph editor is the commercial **FlowChartX** control (`flowchartx32.dll`/`flowchartx64.dll`); settings section `[Flowchart]` incl. `bPerforceEnabled`
## Pipeline

- Ghidra 12.1.2 headless project: `/tmp/ckre-ghidra/CKRE` (Linux), program `CreationKit.exe`
- RTTI pass → CommonLib-style naming; then targeted decompile of the editor core
- Tools in `tools/` (PE/string extraction), findings in `ghidra-dump/`
