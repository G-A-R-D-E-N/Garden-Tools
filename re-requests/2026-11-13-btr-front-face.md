# RE request (CLOSED, answered by exact replication): front-face winding and culling of terrain LOD (.btr) shapes

Status 2026-11-13: no engine answer is needed. The writer reproduces the vanilla winding (counter-clockwise from above for land, skirt and water), the vanilla shader blocks and flag words, and (level-4 water) the vanilla BSSubIndexTriShape segment structure, all measured on 8,271 vanilla tiles. Whatever front-face and culling convention the engine applies, it applies to our tiles exactly as to vanilla's, so the in-game result is the same by construction. The questions below stay as background for anyone who later wants the engine rule itself; nothing is blocked on them.

Date: 2026-11-13
Requested by: Radicle LODGen terrain writer (`Radicle/src/lod/terrain_tile.cpp`, branch `nomad/lodgen-parity`, based on `nomad/lodgen-skirts`)
Executable version and hash: version unknown, no executable was opened for this request. `Fallout4.exe` on the measured install is 55,343,016 bytes, SHA-256 `fdcef37ac1230af6d0b0050eb2142b139ef3a867b37b9211fb6edfcc646072f8`. All measurements below come from game data only: `Fallout4.esm` (330,776,576 bytes, SHA-256 `a3fa1e66d75f80f33f864bf471d25b27f48c1cdbb97acb55c4df83b64ccee1d9`) and the `.btr` files inside `Fallout4 - Meshes.ba2` (1,491,573,805 bytes, SHA-256 `45c70efe7ab87c0a015eb0aa4d8c72508f74149f5869541c5fb4203c5f206130`), `DLCCoast - Main.ba2` (SHA-256 `48c8d515a19a960b7421053016139f1c7bf28cfa68fecc643c1c46bb6d6a89ae`) and `DLCNukaWorld - Main.ba2` (SHA-256 `8f13f43cee9e4171515aa61279df052131107789de747e3d265d2f73229573b2`).

## What is answered from data (no RE needed)

All 8,271 vanilla terrain tiles (`Meshes\Terrain\<World>\<World>.<level>.<x>.<y>.btr`, worlds Commonwealth, SanctuaryHillsWorld, DLC03FarHarbor, NukaWorld, DiamondCity) were read:

- The land shape is a `BSTriShape` named `Land` with one `BSLightingShaderProperty`, identical in 8,271 of 8,271 tiles: shader type 18, flags1 `0x80401000` (Model_Space_Normals, Own_Emit, ZBuffer_Test), flags2 `0x00000003` (ZBuffer_Write, LOD_Landscape), Double_Sided (flags2 bit 4) clear, no alpha property, no stencil property. The vertex descriptor has no normals, tangents or vertex colours (position and UV only).
- The water shapes (5,783 tiles) carry one `BSEffectShaderProperty`, byte-identical in 5,783 of 5,783: flags1 `0x80000000`, flags2 `0x00000001`, empty source texture. Double_Sided is clear. Level-4 water is a `BSSubIndexTriShape` with 16 per-cell segments, level 8 and up a plain `BSTriShape`.
- Winding seen from above (+z): of 7,137,851 land triangles, 7,119,215 are counter-clockwise (99.74 % of all of them, 8,613 clockwise, 10,023 degenerate). Skirt triangles face away from the tile centre in 99.98 %. Water triangles are counter-clockwise (20,436 sampled, all of them).
- So the files are consistent with a single-sided mesh whose front face is counter-clockwise seen from above (the sky side) for land, skirt and water alike. The data cannot say which handedness the engine calls "front", nor whether the shader flags decide culling.

## Engine rule itself (not needed for parity; unanswered)

1. For a `BSTriShape` carrying a `BSLightingShaderProperty` with the LOD_Landscape flag (flags2 bit 1), does the engine treat counter-clockwise (seen from above, vertex order as stored) as the front face, and does it cull the back face? Or is culling forced off, or forced on, by the LOD_Landscape path regardless of the Double_Sided bit?
2. Same question for the water `BSEffectShaderProperty` shapes (`BSTriShape` at levels 8 and up, `BSSubIndexTriShape` at level 4): front-face handedness and culling.
3. Is the terrain LOD index winding mirrored anywhere on load (for example by the multibound node or by the shape scale being the positive level), so that "counter-clockwise in the file" does not mean "counter-clockwise on screen"?
4. Secondary: does the engine hide the segments of a level-4 water `BSSubIndexTriShape` (segment k is cell `cx * 4 + cy` of the 4x4 block, all 16 segments listed, an empty one is start 0 and primitives 0) when the matching exterior cell is attached, the way the object `.bto` level-4 segments are used? Vanilla splits level-4 water into one shape per distinct water height, ascending (63 tiles have two or three), and the land shape has no segments. Radicle now writes the same segmented level-4 water structure (see `docs/lodgen-plan.md`).

## Answer format needed

For 1 to 3: one of "counter-clockwise seen from above is front and the back face is culled", "counter-clockwise is front and the back face is drawn", "clockwise is front", or "culling is forced regardless of flags", with the function or address evidence and the engine version it was read from (OG 1.10.163, NG 1.10.984, AE 1.11.240). For 4: "segments are hidden per attached cell", "segments are ignored", or "needed to avoid double water", with the same evidence.

## What the answer unblocks

Whether the writer's counter-clockwise land, skirt and water winding is the visible side in game or whether every triangle must be flipped (a mirrored tile would show only its underside and cull the surface), and whether the shader flag words (flags2 `0x00000003`, Double_Sided clear) are safe as emitted. Question 4 decides whether level-4 water must become a segmented `BSSubIndexTriShape` per height (currently documented as unmatched in `docs/lodgen-plan.md`).
