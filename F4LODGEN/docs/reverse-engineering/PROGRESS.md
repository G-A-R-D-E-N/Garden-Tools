# LODGen reverse-engineering — progress

Status date: 2026-08-21. Source of truth: the local `lodgen/reverse-engineering/`
tree (this directory). Mirrored to the F4LODGen repo (`docs/reverse-engineering/`
+ the adapted `docs/` copies). Filenames are plain (no links) so this doc reads
identically in both locations.

Scope: byte/behavior-level reverse engineering of xLODGen's `xLODGen.exe`
(Delphi GUI) + `xLODGenx64.exe` / `LODGen.exe` (the .NET generation engine),
done so a native reimplementation can reach geometry/byte parity.

---

## Docs

### REPORT-xLODGen.md — ✅ done
Binary identification (Ghidra 12.1.2 headless), the two-part Delphi→.NET
pipeline, CLI surface, `.dlodsettings` file format, key decompiled functions,
artifacts, and the fast-analysis headless recipe (slow analyzers disabled to fit
the 24 MB binary).
- Open: none material.

### LODGEN_ENGINE.md — ✅ done
The .NET engine (`LODGeneratorCMD` / `LODGenerator` / `LODGenerator.Terra`):
CLI + input files, game modes, full settings key list from the engine parser,
terrain mesh generation flow (terrain-data file → quad build → simplification →
BTR/NIF emission → water/culling), **BTR2RAW mode** (`.btr` → `.raw` int32
heightmap, lengthX/lengthY derivation, quad loading), and object-LOD flow.
- Open: none material.

### XLODGEN_REVERSE_ENGINEERING.md — ✅ done
GUI forms map (26 real forms recovered), `frmLODGen` / `frmLODSettingsFile`,
and the subprocess handoff (settings-file writer `FUN_01404948`, object-LOD
build handler `FUN_0141c4c0`, runner `FUN_00d4ace0`).
- Open: none material.

### XLODGEN_SETTINGS_WRITER.md — ✅ done
The GUI-side settings writer: exact line order for the terrain settings file
(`FUN_01438a54`, 16 lines), the per-LOD loop (counter starts 4, doubles while
≤ 0x20), the **Skirts/PathOutput mode split resolved** via the mode enum
(xEdit init `FUN_01381084` — exactly which games land on each side),
`GameMode=TERRAIN<Name>` mapping, and the object-LOD writer companion section
(`FUN_0141c4c0`, §9).
- Open: the deferred end-to-end validation (writer output vs engine parse) —
  the dlodsettings oracle now covers the engine side; running the GUI to emit
  real files is blocked while the Wine environment is unavailable.

### XLODGEN_OUTPUT_FORMAT.md — ✅ done
Byte layout of `.btr` / `.bto` outputs: file naming, NIF header (`NiHeader.Write`),
body/footer, `.btr` markers, block layouts (`NiObjectNET` prefix, `NiAVObject`,
`NiGeometry`/`BSTriShape`/`BSSegmentedTriShape`, `NiTriShapeData`, others),
terrain quad + object-LOD quad specifics.
- Open: cross-checked against generated BTR via `parse_btr.py`; deeper per-block
  save-binary detail is covered from the CK side (NIF block-format reference).

### XLODGEN_TERRAIN_INTERNALS.md — ✅ done (recently corrected)
The deepest doc. §1 **WaterDelta exact behavior** — corrected 2026-08-21 from
the decompiled engine IL: `waterDelta/lodLevel` is truncating integer division
(`div` + `conv.r4`), small deltas silently truncate to 0, and **`WaterDelta=0`
never flattens heights** (clamp target is `waterHeight` itself, which a
below-water sample is already under) — the vertex handling (interior `Ignored`,
coarse-grid `Used` at lodLevel>1) still runs. §2 hide quads via `landFlags`
quadrant bits; §3 skirts/ZSHIFT (per-level depth, 2000-vertex reserve, UNDER
variants); §4 FO4 32767 budget vs 65535 chunk split; §5 Bethesda version-130
land chunk/subindex layout + VTXT; §6 water node grouping; §7 both reducers
(greedy + Delaunay/quad-edge) and the standalone water/object triangulator.
- Open (the biggest remaining parity targets):
  - §2 — exact hide-quad removal point (which triangles are removed, and when).
  - §3 — exact skirt depth/topology semantics still need a byte-level write-up.
  - §4 — how the 32767 reducer budget turns into one or multiple
    BSSubIndexTriShape / subindex chunks (splitting rule).
  - §5 — land chunk/subindex layout for version 130 corner cases.
  - §6 — water node grouping by unique water height edge cases.
  - §7 — greedy reducer tie-breaking and Delaunay edge-flip ordering, needed
    for triangle-for-triangle xLODGen parity.

### XLODGEN_OBJECT_LOD.md — ✅ done
Object LOD pipeline (`LODApp`): entry, `DoLOD` per-quad build, reference
resolution (`ParseNif`), culling (`RemoveUnseenTerrain`), grass LOD (`.gid`),
`WriteMHDT()` object heightmap.
- Open: none material.

### XLODGEN_ATLAS_TEXTURES.md — ✅ done
Atlas map (`LODGen-AtlasMap.txt` / `TEXTUREATLASMAP=`), UV remapping
(`AtlasDesc.UVAtlas`), texture-key building, HD texture policy
(`TEXTUREDIFFUSEHD=` / `NOTHDTEXTUREMASK=`), texture-list mode, flat textures
(`FLATTEXTURES=` / `LODGenFlatTextures.txt`), brightness/color adjustments, and
GUI-side atlas defaults (`FUN_0141c4c0`).
- Open: none material.

### XLODGEN_GID_GRASS.md — ✅ done
The binary `.gid` grass-placement format: discovery/naming, header, per-grass-
type / per-instance / per-placement reference layout, rotation-matrix assembly,
half-float conversion (`Utils.ShortToFloat`), coordinate spaces, and
conversion to statics (`GIDFile.ToStatics`).
- Open: none material.

### XLODGEN_QUALITY.md — ✅ done (recently corrected)
Quality / MaxVerts / WaterDelta defaults reconciliation (engine `Program.cs`
vs GUI `frmLODGen`), semantics, special interactions (including the
truncating-division + `WaterDelta=0` correction), units/coordinate check.
- Open: none material.

---

## Tools

### validate_dlodsettings.py — ✅ mature (the oracle)
Validates a `.dlodsettings` file against the engine's full key list and flag
parsing rules (duplicate/empty/malformed handling). Modes: default validate,
`--list-keys` (every accepted key with type/default/note), `--dump-tree` (mode
family, side-file readers, reachable exit codes per GameMode), mode dispatch
(object-LOD tokens SSE/TES5/FO4/FO3/FNV → object-LOD reader contract, mirroring
the engine's `IsMode` switch), side-file cross-checks (LODGen-AtlasMap.txt,
LODGenFlatTextures.txt, LODGen_Terrain_*.bin header), BTR2RAW `.btr` input
check for `TerrainData=`.
- Open: end-to-end run against a real GUI-written file (blocked on Wine).

### parse_btr.py — ✅ done
Byte-parses `.btr` terrain output against XLODGEN_OUTPUT_FORMAT.md.

### extract_dfm.py — ✅ done
Extracts Delphi DFM form definitions from `xLODGen.exe` (the 26-form inventory).

### samples/ — ✅ done
Fixture settings files (e.g. `valid-sse-terrain.dlodsettings`) used by the
oracle.

### ghidra-dump/ — ✅ done
Headless Ghidra outputs: `xLODGen.exe.analysis.txt`, `decompiled*.txt`,
`lodsettings.txt`, `buildatlas.txt`.

### engine/ — ✅ done
The shipped engine readmes (Terrain-LOD, Terrain-Heightmap, Terrain-Underside,
Combine-Worldspaces, Seasons, Skyrim-Occlusion, LODSettings-File) — the
authoritative user-facing contract.

---

## Recommended next steps

1. Close the §7 reducer tie-breaking / edge-flip ordering questions — the last
   blocker for triangle-for-triangle parity (needs no architecture change; maps
   into the existing greedy/Delaunay modules).
2. Byte-level skirt depth/topology write-up (§3).
3. Hide-quad removal point (§2) and FO4 subindex chunk splitting (§4/§5).
4. End-to-end oracle validation with a real GUI-written settings file once Wine
   is available.

## Cross-links

- CK-side NIF save-binary reference lives in the CKRE tree (the CK's own NIF
  writer is the ground truth for block formats the engine reuses).
- The F4LODGen-adapted docs (`docs/XLODGEN_QUALITY.md`, `docs/LODGEN_ENGINE.md`,
  `docs/XLODGEN_REVERSE_ENGINEERING.md`) are the clean-room-side copies and are
  kept reconciled with this tree.
