# Atlas and texture flow

Research date: 2026-08-18

How the engine packs LOD textures into atlases and remaps object-LOD geometry UVs (`AtlasDesc.cs`, `AtlasList.cs`, `LODApp.cs`, `LODGeneratorCMD/Program.cs`). The actual DDS stitching is done by **the GUI-side `BuildAtlas` step** (xLODGen's `LODGen-AtlasMap.txt` producer); the engine consumes the atlas map and only remaps UVs + swaps texture slots.

## 1. Atlas map file (`LODGen-AtlasMap.txt`, `TEXTUREATLASMAP=`)

TAB-separated text, ANSI encoding, read with BOM detection. `;`-prefixed lines are comments. Column contract (from the engine's parser):

```
col 0   key: <sourceDiffuse>[,<sourceNormal>[,<sourceGlow>]][=<alphaThreshold>]
col 1   source texture width  (px)     col 2  source texture height (px)
col 3   position X within atlas (px)   col 4  position Y within atlas (px)
col 5   atlas texture path (diffuse)
col 6   atlas texture width (px)       col 7  atlas texture height (px)
col 8   scaleU (MiniAtlas)             col 9  scaleV (MiniAtlas)     [optional, cols ≥ 10]
col 10  effect1 ... (ignored here)
```

The key is lowercased and is the lookup key for the whole session (`AtlasList`, case-insensitive dictionary). The engine derives, per entry:

```
TextureScaleU = srcW / atlasW        TextureScaleV = srcH / atlasH
PosU          = srcX / atlasW        PosV          = srcY / atlasH
MinU = 1/(2*atlasW)   MaxU = TextureScaleU - MinU     (same for V)
AtlasTextureN = normal-name variant of the atlas path (e.g. `_n.dds` / `_msn.dds`)
AtlasTextureG = `_glow.dds` variant if the atlas path contains `_glow.dds`, else empty
AtlasTextureS = specular variant (FO4 only, `Utils.GetSpecularTextureName`)
Miniatlas    = true when cols 8..9 present (per-entry scale ≠ 1)
HDTexture    = true when the source diffuse matches a name in the HD list (see §3)
```

Duplicates (same key twice) abort with exit code 1063; a malformed line aborts with 1062.

## 2. UV remapping (`AtlasDesc.UVAtlas`)

Applied to each shape during `ParseNif` (object LOD) when all of these hold:

- the shape name does **not** contain `noatlas`;
- the shape's texture key (`AtlasList.BuildKey(textures, alphaThreshold)`) is present in `AtlasList`;
- the entry is not `HDTexture` (unless force — HD entries only remap when forced, e.g. pass-through/grass);
- every UV lies within `[AtlasToleranceMin, AtlasToleranceMax]` (default tolerance ±0.2 from the GUI's `AtlasTolerance=`; `ATLASTOLERANCE=0.2` engine default; UVs outside are logged and the shape is skipped).

Remap math per vertex (v):

```
u' = clamp(u * TextureScaleU, MinU, MaxU) + PosU
v' = clamp(v * TextureScaleV, MinV, MaxV) + PosV
```

Texture slot swap (diffuse/normal/glow/specular → `AtlasTexture*`), and the texture key is rebuilt so downstream merge groups key on the atlas textures. Grass shapes and non-clamp-mode shapes force the remap even for HD entries.

## 3. Texture key building (`AtlasList.GetKey`)

Key = `diffuse[,normal[,glow]][=floor(alpha/16)]`, with graceful fallbacks: the code first tries the exact key including the alpha bucket, then the same combination without the alpha suffix, then the `diffuse,normal` pair (and its alpha variant), and finally bare `diffuse`. `linear.dds` under `textures\pbr\` is normalized to `.dds` before keying. Alpha threshold is bucketed to 16-unit steps.

## 4. HD texture policy (`TEXTUREDIFFUSEHD=` / `NOTHDTEXTUREMASK=`)

`TEXTUREDIFFUSEHD` adds a name fragment to the HD list (lowercased, `data\` prefix stripped); `NOTHDTEXTUREMASK` feeds the non-HD list. An entry whose `SourceDiffuse` contains an HD fragment is marked `HDTexture` and **keeps its original textures** (no atlas swap) unless forced; non-HD entries always swap.

## 5. Texture list mode (`TEXTURES` / `TEXTURESLIST`)

`LODApp.TexturesList(statics)` walks every static's LOD4/8/16 model chain, parses the NIFs, and collects diffuse texture paths whose UVs all lie inside `[atlasToleranceMin, atlasToleranceMax]` and which are not HD textures. Output: one path per line to `TEXTURESLISTFILE=` (`File.WriteAllLines`) — this is the input list the GUI's atlas builder uses to decide what to stitch.

## 6. Flat textures (`FLATTEXTURES=` / `LODGenFlatTextures.txt`)

TAB-separated per line (cols 0..13, 6 required):

```
col 0  source texture (lowercased)     col 1  width        col 2  height
col 3  shiftZ                          col 4  scale        col 5  effect1 (= effect2)
col 7  glow texture (default Textures\White.dds)
col 8  vertexColor (default -1)         col 9  colorVariance (default -1)
col 10 depth (default = width)          col 11 shiftX       col 12 shiftY
col 13 material (if present)
```

Entries become `FlatDesc`s used to build the flat-terrain LOD meshes (`TerrainLOD.GenerateFlat`): per grid cell a 4-vertex quad at `landHeight/level` with a diffuse quad + optional water plane, textured with the flat texture, `WATER` node, `"chunk"`/`"land"` string markers, and per-mode userVersion2 (100 for FLATSSE). The FLATNIF input (`LODGen_flat_lod.nif`) is the source template whose shader setup is reused; grass brightness/vertex-color adjustments (`GRASSBRIGHTNESS*`, `CROWN/TRUNK/FLATTRUNKBRIGHTNESS`) are applied to the flat grass meshes.

## 7. Brightness / color adjustments

Per-mesh brightness scalars (`AdjustBrightness`) and per-vertex color variance are applied at geometry-build time to grass and flat LOD so distant grass blends with terrain lighting (`GRASSBRIGHTNESSTOP/BOTTOM R/G/B`, `COMPLEXGRASSBRIGHTNESSTOP/BOTTOM R/G/B` — see [LODGEN_ENGINE.md](../LODGEN_ENGINE.md) key list).

## 8. GUI-side atlas builder defaults (from `FUN_0141c4c0`)

The LOD Options dialog passes `AtlasWidth`/`AtlasHeight` (default **4096×4096**), `AtlasTextureSize` (default **512**), `AtlasTextureUVRange`, and `AtlasTolerance` (written as `%1.1f`) into the options file read by the engine. The `LODGen-AtlasMap.txt` path itself is `<Data>\LODSettings\LODGen\LODGen-AtlasMap.txt` and is written by the GUI before spawning the engine.
