# Material files (BGSM / BGEM)

Radicle reads and writes Fallout 4 material files: `.bgsm` (BGSM) and `.bgem` (BGEM).

| Piece | Path |
| --- | --- |
| Core model, reader and writer | `include/fo4/material/material_file.hpp`, `src/material/material_file.cpp` |
| C ABI | `include/fo4/c_api_material_file.h`, `src/c_api_material_file.cpp` |
| C# wrapper | `bindings/csharp/Radicle.Interop/MaterialFile.cs` |

The C ABI header is `c_api_material_file.h`, not `c_api_materials.h`: the latter already covers MSWP material swap records.

## Format

All values are little-endian.

- `u8`: one byte. `u32`: four bytes. `f32`: IEEE-754 single.
- `bool`: one byte; nonzero is true. The raw byte is kept, so a stored `2` is written back as `2`.
- `color3`: three `f32` values, red, green, blue.
- `string`: a `u32` length, then that many bytes. The usual trailing NUL is counted in the length. A length of 0 is the empty string with no bytes after it.

The first `u32` is the signature: `BGSM` (0x4D534742) or `BGEM` (0x4D454742). The reader picks the format from that value, not from the file extension, because mods ship BGEM content under `.bgsm` names.

### Header (both formats)

In order: Signature, Version, TileFlags (kept as a raw `u32`; bit 0 is TileV, bit 1 is TileU, other bits are kept), UOffset, VOffset, UScale, VScale, Alpha, AlphaBlendMode0 (`u8`), AlphaBlendMode1 and AlphaBlendMode2 (`u32`), AlphaTestRef (`u8`), eleven bools (AlphaTest through RefractionFalloff), RefractionPower.

Then two version-gated groups:

- Below version 10: EnvironmentMapping (bool) and EnvironmentMappingMaskScale (`f32`). From version 10: DepthBias (bool) in the same place.
- GrayscaleToPaletteColor (bool), then MaskWrites (`u8`) from version 6.

### BGSM body

- Texture strings. Version 3 and above: Diffuse, Normal, SmoothSpec, Greyscale, Glow, Wrinkles, Specular, Lighting, Flow, plus DistanceFieldAlpha from version 17. Versions 2 and below use a different set and order (Envmap, InnerLayer, Displacement).
- Then EnableEditorAlphaRef, and a lighting block that depends on version: from 8 the translucency fields, below 8 the rim and subsurface fields.
- Specular, wetness and fresnel floats. WetnessControlEnvMapScale exists below version 10.
- From version 3: Pbr. From 9: CustomPorosity and PorosityValue.
- RootMaterialPath (string), AnisoLighting (bool).
- EmitEnabled (bool). EmittanceColor (color3) follows only when EmitEnabled is true.
- EmittanceMult, ModelSpaceNormals, ExternalEmittance, then LumEmittance from 12.
- From 13: the adaptive emissive group. BackLighting below 8.
- Six shadow and glow bools, then EnvironmentMappingWindow and EnvironmentMappingEye below 7.
- Hair, HairTintColor, Tree, Facegen, SkinTint, Tessellate.
- Below 3: five displacement and tessellation floats.
- GrayscaleToPaletteScale, SkewSpecularAlpha from 1.
- From 3: Terrain (bool). When Terrain is true: UnkInt1 (`u32`, version 3 only), then three terrain floats.

### BGEM body

- Five texture strings: Base, Grayscale, Envmap, Normal, EnvmapMask.
- From 11: Specular, Lighting and Glow strings.
- From 21: GlassRoughnessScratch and GlassDirtOverlay strings, then GlassEnabled. When GlassEnabled is true, a glass block follows: GlassFresnelColor (color3), GlassBlurScaleBase, GlassBlurScaleFactor (from 22 only), GlassRefractionScaleBase.
- From 10: EffectEnvironmentMapping (bool) and EffectEnvironmentMappingMaskScale. These are separate fields from the header EnvironmentMapping, which exists below 10.
- Six bools (Blood, EffectLighting, Falloff, FalloffColor, GrayscaleToPaletteAlpha, Soft), BaseColor, BaseColorScale, four falloff floats, LightingInfluence, EnvmapMinLOD (`u8`), SoftDepth.
- EmittanceColor (color3) from 11. Adaptive emissive floats from 15. Glowmap from 16. EffectPbrSpecular from 20.

Both formats keep any bytes after the last field as trailing data.

There is no version ceiling. A version above the highest gate uses the newest layout.

## Round trip

Reading a valid file and writing it back gives identical bytes. That holds because the reader keeps the raw bool byte, the string bytes including any NUL, TileFlags as a raw `u32`, and the trailing bytes.

Conditional fields are decided by the current values when writing. EmitEnabled decides EmittanceColor, GlassEnabled decides the glass block, Terrain decides the terrain block, and the version gates decide the rest. When a conditional field turns on and no value is stored for it, the writer emits a zero default. For example, setting EmitEnabled true on a file without EmittanceColor writes three zero floats.

Setting a field does not add or remove list entries. A conditional field that is switched off stays in the list but is not written.

## Shared layout

`src/material/material_file.cpp:visit_layout` describes the layout once. It is instantiated with a reader that decodes bytes into the field list and with a writer that encodes the field list back to bytes. Each field call returns its value, so the same code path gates later fields for both directions. A field therefore cannot be readable without also being writable.

## API surface

C++ (`namespace fo4::material`):

- `MaterialFile read_material_file(std::span<const std::byte>)`: throws `fo4::parse_error` on bad magic or truncation.
- `std::vector<std::byte> write_material_file(const MaterialFile&)`.
- `MaterialFile::version()`, `find_field` (case-insensitive, const and mutable overloads).
- `get_field<T>` and `set_field<T>` for `bool`, `std::uint8_t`, `std::uint32_t`, `float`, `Color3` and `std::string`. A getter returns `std::nullopt` when the field is missing or has another type. A setter returns false in the same cases. Setting `Version` or `Signature` throws `std::invalid_argument`.
- `set_text` and `text`. `text` drops one trailing NUL. `set_text` appends one NUL.

C ABI (`radicle_material_file_*`): read, destroy, write, format, version, field count, field name and type by index, typed get and set by name, and string get and set through the text semantics above. Status codes: `NOT_FOUND` for a missing name, `INVALID_ARGUMENT` for a type mismatch, a null argument, a bad index, or a write to Version or Signature, and `PARSE_ERROR` for malformed bytes. Buffers from `write` and string getters are released with `radicle_free_bytes`.

C# (`Radicle.Interop.MaterialFile`): `FromBytes`, `ToBytes`, `Format`, `Version`, `Fields`, typed `Get*` and `Set*`, `GetText` and `SetText`, and `IDisposable`. Text uses Latin-1, so every byte round-trips. Characters above U+00FF become `?` on the way in.

## Limits

- The API only edits existing material files. There is no constructor for a new file.
- Setting a conditional field such as EmitEnabled does not add its block to the in-memory field list until the file is written and read again.
- BGEM `LightingInfluence` is stored and returned as the raw float. The game converts it to a byte as `(byte)(int)(f*255)`, which wraps above 1.0 instead of clamping. Vanilla `EyeGloss` and `EyeWet` store 1.5, which the game uses as about 0.49. A writer that wants the game to see a value must keep it within 0.0–1.0.
- Only the `.bgsm` and `.bgem` layouts above are covered. Other material kinds are out of scope.

## Sources

- Field order, widths and version gates come from the py-creation-lib material layout (`native/materials/src/base.rs`, `bgsm.rs`, `bgem.rs`). They were taken from the format spec kept with this work and were not re-checked against that source in this change.
- Game proof for the version-2 BGEM body: static analysis of Fallout 4 AE 1.11.240 found that GardenEngine's `opencw/Services/Materials/BgemCodec.cs` matches the game's BGEM reader exactly (ReverseEng/Fallout4_Runtime_Research PR #13, `RE-Work/subsystems/garden-engine-376/`, commit `38b29ffc`; summarized in GardenEngine PR #376 comment 11885). That codec reads the same version-2 body as `src/material/material_file.cpp:visit_bgem`: five texture strings, six bools, BaseColor, BaseColorScale, four falloff floats, LightingInfluence (`f32`), EnvmapMinLOD (`u8`) and SoftDepth, in that order. The shared header and the BGSM layout were not part of that check. The same analysis is the source of the `LightingInfluence` wrap noted under Limits.
- Vanilla verification: the format spec reports that GARDEN-TOOLS PluginEditTool round-tripped every `.bgsm` (6,623) and `.bgem` (283) byte-identically in the vanilla `Fallout4 - Materials.ba2`, per `PluginEditTool/docs/MCP_SETUP.md`. The archive name has no space after "Fallout4". All vanilla materials are version 2, so the higher-version branches are covered only by synthetic files in `tests/test_material_file.cpp`.
- `tests/test_material_file_real.cpp` repeats the vanilla check with Radicle's own BA2 reader when `FO4_DATA_DIR` is set. It never writes to the Data folder.
