# RE request: generate reference LOD with the Creation Kit's own World LOD builder (oracle for LODGen parity)

Date: 2026-11-13
Requested by: Radicle LODGen (PR 1270, branch `nomad/lodgen-parity`; evidence in `docs/lodgen-plan.md` "Object LOD parity rules")
Context: the RE note `08-CK-LOD-SUBSYSTEM.md` (not in this repo; UNVERIFIED from this checkout) shows the CK 1.11.137 ships a LOD builder (menu "World LOD...", strings `Building LOD for %s...`, `Generating LOD tiles using %d threads`, `Connecting LOD tiles`, settings `Data\LODSettings\<World>.LOD`). It is the engine vendor's own generator, so its output is the ground truth that xLODGen only approximates.

## What we need (any part helps; none of it needs disassembly, only running the CK)

1. Run the CK World LOD build (object and terrain, all levels 4/8/16/32) for the unmodified base game worldspaces Commonwealth and NukaWorld, and for a test plugin worldspace described in 3. Output to a scratch folder, not the install. Report CK version, `Fallout4.exe` hash, and the `Data\LODSettings` file used.
2. Provide the generated `Meshes\Terrain\<World>\*.btr`, `Meshes\Terrain\<World>\Objects\*.bto` and `Textures\Terrain\<World>\Objects` atlas files, plus the CK log. Radicle compares them tile by tile with its output and with the shipped vanilla tiles.
3. Test plugin (or an existing DLC): an enable-parent REFR with record flag 0x100 ("LOD Respects Enable State") defined in (a) Fallout4.esm, (b) DLCCoast.esm or DLCNukaWorld.esm, (c) a light plugin (.esl), each with a LOD-bearing STAT child (XESP link). Report the `ToggleRefID` NiIntegerExtraData value the CK writes for each and the plugin load order used. This answers the open question in `2026-11-13-bto-toggle-ref-dlc-parents.md` directly.
4. Answer from the CK output or log: which refs it includes (initially disabled, MultirefLOD children, SCOL parts, refs whose STAT has a MNAM slot but 0x8000 unset), whether it drops triangles under terrain and by what margin (Radicle measured: all three vertices more than 64 units below LAND), and the first segment word of level-4 water `BSSubIndexTriShape` (Radicle writes the triangle count; vanilla Far Harbor and Nuka-World tiles store twice that).

## Known and not needed

Vanilla tile structure, skirt shape, shader blocks, winding and the object inclusion rules are already measured on the shipped files (see the plan). 16 of 643 shipped object tiles still differ from Radicle's prediction; the CK output for Commonwealth and NukaWorld would show which of Radicle's rules those tiles break.

## Answer format

The output files in a folder under `re-requests/answers/2026-11-13-ck-world-lod/` plus a short markdown with the toggle ID values (3), the inclusion and culling answers (4) and the CK build settings used.

## What it unblocks

The DLC and light-plugin toggle ID rule, the remaining 16 object tiles if CI shows they are not already explained, and a byte-level oracle for terrain tiles beyond the shipped files (all 8,271 are already used).

## Remaining mismatches the CK output would explain (Radicle model, base-game data, 627 of 643 shipped object tiles exact)

16 tiles stay unexplained: Commonwealth L4 (-8,0), (-8,4), (16,24) and L8 (-8,0) (some references were edited in 2018 and 2021, after the shipped LOD was generated, plus 2021 LAND edits; one vertex at z -73.3 unexplained), and Nuka-World L4 (-16,-12), (-8,-4), (-4,-8), (-8,4), (-4,4), (-4,8), (0,0) and L8 (-16,-16), (-8,-8), (-8,0), (-8,8), (0,0) (no date cutoff there; residuals of 3 to 348 vertices, e.g. (-8,-4) loses 264 vertices near stacked IndFrame frames). A CK rebuild from the same plugin data shows whether the shipped tiles came from a different data state or from a rule Radicle lacks.
