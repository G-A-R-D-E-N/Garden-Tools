# Radicle asset formats: current status

Audience: developers new to the project. Source: `nomad/dev` checkout (read only). This page covers file formats that are not plugin records: BA2 archives, NIF meshes, BGSM/BGEM materials, DDS textures, audio, SEQ files and string tables. It states what the docs and code say. It makes no CI claims. Paths are relative to the repo root.

## Summary

| Format | Radicle reads | Radicle writes | Radicle edits | PluginEditTool panel | Main gaps |
|---|---|---|---|---|---|
| BA2 (GNRL, DX10; v1, v7, v8) | yes | yes | build/rebuild entries only (no in-place edit) | `ArchivePanel.tsx` | other versions/tags, BSA; no-name-table archives rejected in PET; packing in PET uses external Archive2 |
| NIF (Fallout 4, BS 130) | yes (geometry, skin, nodes, shader texture set) | yes (re-serialize, OBJ import, fix) | limited key list (`apply_edits`) | `NifPanel.tsx`, `NifEditor.tsx` | no collision, no effect shader, UV v flip unverified, several parity items |
| BGSM / BGEM materials | yes | yes (byte-identical round trip) | set existing fields | `NifPanel.tsx` (Materials mode) + `MaterialEditor.tsx` | no "new file" constructor; conditional blocks need write + re-read |
| DDS textures | yes (header + one mip to RGBA8) | header only (BA2 DX10 rebuild) | no | none directly (used by NIF viewer and archive preview) | no encode, no BC6H, no signed BC4/BC5 decode, no cube/array slice select |
| Audio (XWM, FUZ, WAV) | no (no code in Radicle) | no | no | `AudioPanel.tsx` (external tools) | Radicle only covers sound records (SNDR, SOUN, MUSC, FSTP, FSTS), not audio files |
| SEQ (`.seq`) | yes | yes | n/a (bare list) | no panel | several engine rules UNPROVEN |
| Strings (`.STRINGS`, `.DLSTRINGS`, `.ILSTRINGS`) | yes | yes | add/set/remove entries | `LocalizationPanel.tsx` | no auto language/sidecar resolution; localized QUST CNAM writer not enabled |

Evidence note: the docs say the real-data tests run only when `FO4_DATA_DIR` is set and skip otherwise (`Radicle/docs/BA2.md`, `Radicle/docs/SEQ_FORMAT.md`, `Radicle/docs/SOUND.md`). Whether a given run happened is not claimed here.

## How the pieces fit

- Radicle is the C++ library: `Radicle/include/fo4/...` and `Radicle/src/...`. It also has a C ABI (`Radicle/include/fo4/c_api_*.h`) and a C# wrapper (`Radicle/bindings/csharp/Radicle.Interop/`).
- PluginEditTool (PET) has a native host (`PluginEditTool/native/src/...`) that calls Radicle, and a web UI (`PluginEditTool/web/src/*.tsx`) that calls the host through the interfaces in `PluginEditTool/web/src/backend.ts` (`ArchiveHost`, `NifHost`, `MaterialHost`, `AudioHost`, `StringsHost`).
- Radicle test files live in `Radicle/tests`. Tests with `_real` in the name need the Fallout 4 Data folder (`Radicle/docs/BA2.md`).

## BA2 archives

What it is: Bethesda's Fallout 4 archive container (`BTDX` header) holding general files (`GNRL`) or textures (`DX10`).

Today (`Radicle/docs/BA2.md`):
- Reads, writes and extracts `GNRL` and `DX10` at versions 1, 7 and 8. Other versions and content tags are rejected.
- DX10 extraction adds a DDS header rebuilt from stored texture info, only when the entry path ends in `.dds` and the payload does not already start with `DDS `. The header rule follows xEdit's DDS builder (`Radicle/src/archive/ba2_texture.cpp:dds_header_for`).
- Read/write round trip keeps name, extension and directory hashes and the one-byte entry metadata. The docs do not promise byte-identical archive files.
- `Ba2Reader` indexes metadata without loading payloads and extracts on demand. Path lookup ignores slash direction and ASCII case.
- `AssetResolver` (`Radicle/include/fo4/archive/assets.hpp`) resolves a Data-relative path across loose roots and archives, and can list entries with a shadowed flag.
- Measured on the installed AE 1.11.240.0 data: 79 root-level `.ba2` files, all versions 1, 7 or 8. This is a header and table measurement, not a full payload audit (`Radicle/docs/BA2.md`).

Key source files:
- `Radicle/include/fo4/archive/ba2.hpp`, `ba2_texture.hpp`, `ba2_reader.hpp`, `ba2_hash.hpp`, `assets.hpp`
- `Radicle/src/archive/ba2.cpp`, `ba2_texture.cpp`, `ba2_reader.cpp`, `ba2_hash.cpp`, `assets.cpp`
- `Radicle/src/c_api_archive.cpp`

Tests:
- `Radicle/tests/test_ba2.cpp`, `test_ba2_texture.cpp`, `test_ba2_reader.cpp`, `test_ba2_hardening.cpp`, `test_ba2_long_names.cpp`
- Real data: `test_ba2_versions_real.cpp`, `test_ba2_real.cpp`
- PET: `PluginEditTool/native/tests/unit/RadicleArchiveBackendTests.cpp`

PET side:
- `PluginEditTool/native/src/archive/RadicleArchiveBackend.cpp` handles `list`, `extractFile`, `extractSelected`, `extractAll`, `compare` and `previewTexture`. Any other operation returns `ArchiveOperationUnsupported`.
- Extraction checks paths first (absolute paths, `..`, device names, case collisions, symlinks) and writes through a temp file (`Radicle/docs/BA2.md`).
- Panel: `PluginEditTool/web/src/ArchivePanel.tsx` (list, extract, extract all, compare). `ArchiveHost` also declares `Pack`.
- Packing is not done by Radicle. `PluginEditTool/native/src/archive/ArchiveService.cpp:pack` looks up an external `Archive2` tool.

Known gaps and boundaries (from `Radicle/docs/BA2.md`):
- Not supported: other BA2 versions or tags, and BSA archives.
- In PET, a non-empty archive with no string table is rejected on open (`requireAddressable`), because entries cannot be addressed by path.
- Policy differences from xEdit are listed as "pending an owner decision, code unchanged": non-`.dds` DX10 entries pass through, a missing name table gives `unnamed.dds`, zero width/height throws, chunk-header size and sentinel are enforced, and a name-table offset past the end of the file throws.
- A named NG file, `DLCworkshop03 - Voices_en.ba2`, is treated as an incomplete download in the measurement (name table runs past end of file).

## NIF meshes

What it is: the Gamebryo/Creation Engine model format. Radicle reads the Fallout 4 variant (version 20.2.0.7, user version 12, BS version 130).

Reads (`Radicle/docs/NIF.md`):
- Header, block types and sizes, string table, root list.
- NiNode-family scene graph (15 allowlisted node types in `Radicle/src/nif/nif.cpp:k_node_types`), parent map, shape-to-global transforms, billboard mode.
- Geometry for `BSTriShape`, `BSSubIndexTriShape`, `BSMeshLODTriShape`, `BSDynamicTriShape`: positions, UVs, normals, tangents, bitangents, vertex colours, skin weights and bone indices, bones with bind transforms.
- The `BSLightingShaderProperty` material path and the `BSShaderTextureSet` slots.
- Bad shader blocks do not fail the whole file. The shape gets a warning and keeps its geometry (`Shape::warning`).
- Vanilla corpus counts in the doc (for example 226,177 NIFs, 1,206,190 shapes with a node parent, 0 shader warnings) come from `Radicle/tests/test_nif_corpus.cpp`. The doc says the data is Fallout 4 1.10.163.

Writes and edits:
- `Radicle/include/fo4/nif/nif_write.hpp`: `write_nif` (re-serialize) and `apply_edits`. Supported edit keys in `Radicle/src/nif/write_edits.cpp`: `name`, `material`, `tex0` to `tex7`, and float fields `translation`, `scale`, `uvOffset`, `uvScale`, `emissiveColor`, `emissiveMultiple`, `glossiness`, `alpha`, `specularColor`, `specularStrength`. The keys `verts`, `tris` and `shaderType` are read-only. Any other key throws.
- `Radicle/include/fo4/nif/nif_import.hpp`: `import_obj` builds a new NIF from OBJ text, with options for material, diffuse and normal texture, `box_collision` and `from_blender` (Y-up to Z-up axis map in `Radicle/src/nif/import_obj.cpp`).
- `Radicle/include/fo4/nif/nif_fix.hpp`: `fix_nif` rewrites each shape's tangent frame, trims shader and texture-set strings, adds a `BSXFlags` extra to the root if missing, and reads the result back. It throws if the shapes do not match after the write (`Radicle/docs/code-notes/Radicle.md`, section "NIF fix").

Key source files:
- Read: `Radicle/src/nif/nif.cpp`, `geometry.cpp`, `skin.cpp`, `shader.cpp`, `block_transform.cpp`
- Write: `Radicle/src/nif/write_model.cpp`, `write_edits.cpp`, `import_obj.cpp`, `import_build.cpp`, `fix_nif.cpp`
- C ABI: `Radicle/src/c_api_nif.cpp`, `Radicle/include/fo4/c_api_nif.h`; C#: `NifFile.cs` in `Radicle/bindings/csharp/Radicle.Interop/`

Tests: `Radicle/tests/test_nif.cpp`, `test_nif_nodes.cpp`, `test_nif_corpus.cpp`, `test_nif_write.cpp`, `test_c_api_nif.cpp`. PET: `PluginEditTool/native/tests/unit/NifServiceTests.cpp`.

PET side:
- `PluginEditTool/native/src/assets/NifRender.cpp` uses `read_nif`, `read_shapes`, `block_transform`, `billboard_mode` and `apply_edits`. `NifService.cpp` calls `fix_nif`.
- `NifHost` (`PluginEditTool/web/src/backend.ts`) has `Import`, `Inspect`, `Geo`, `GetTexture`, `Verify`, `Fix`, `Tree`, `ApplyEdits`.
- `NifPanel.tsx` modes: View, Edit, Materials, Import, Inspect, Verify, Fix. The 3D view is `NifViewport.tsx`; the block editor is `NifEditor.tsx`.

Known gaps (from `Radicle/docs/NIF.md` and `Radicle/docs/code-notes/Radicle.md`):
- Not in scope: JSON output, collision, alpha properties, and fragment, effect and water shader property types. Only `BSLightingShaderProperty` is followed.
- UV orientation: `import_obj` writes UV v as given. The code note says Blender/OBJ use a bottom-left origin and NIF uses top-left, so textures "may need the v flip until they are checked in game or against niftool". This is unverified.
- `import_obj` refuses `box_collision` (no collision writer) and refuses more than 65535 unique vertices.
- UV2 values are read past and discarded. The vertex tail slot (bit 8) is read past and its meaning is not pinned.
- `BSDynamicTriShape`, `BSSubIndexTriShape` and `BSMeshLODTriShape` are read with the plain `BSTriShape` layout. Type-specific trailing fields are not read.
- The reader requires exactly BS version 130, while the managed reference reader accepts any value 130 or higher.
- The shader property tail after the texture set reference is not read.
- Billboard mode (a u16 after the children list) is still "the reader's reading". 709 vanilla billboard nodes parse with it.
- Skin bone indices are one byte, so a shape can address only its first 256 bones.
- A skinned shape whose skin lists no bones keeps its geometry but loses weights and bone indices, with a warning.

## BGSM / BGEM materials

What it is: Fallout 4 material files. BGSM is a lit surface material, BGEM is an effect material.

Today (`Radicle/docs/MATERIALS.md`):
- Reads and writes both formats. The format comes from the first four bytes, not the file extension.
- Round trip of a valid file gives identical bytes (the reader keeps raw bool bytes, string bytes, tile flags and trailing data).
- One layout description serves both reading and writing (`Radicle/src/material/material_file.cpp:visit_layout`), so a field cannot be readable without being writable.
- API: typed `get_field` / `set_field` and text helpers. Setting `Version` or `Signature` is refused.
- No version ceiling: a version above the highest gate uses the newest layout.

Key source files:
- `Radicle/include/fo4/material/material_file.hpp`, `Radicle/src/material/material_file.cpp`
- C ABI: `Radicle/include/fo4/c_api_material_file.h`, `Radicle/src/c_api_material_file.cpp`
- C#: `Radicle/bindings/csharp/Radicle.Interop/MaterialFile.cs`
- The older `c_api_materials` files are for MSWP material swap records, not material files.

Tests: `Radicle/tests/test_material_file.cpp`, `test_material_file_real.cpp`, `test_c_api_material_file.cpp`. PET: `PluginEditTool/native/tests/unit/MaterialServiceTests.cpp`.

PET side:
- `PluginEditTool/native/src/material/MaterialService.cpp` reads a file and reports `BGSM` or `BGEM`.
- `MaterialHost` has `Inspect` and `SetFields`. UI: `MaterialEditor.tsx`, shown from `NifPanel.tsx` in Materials mode.

Known gaps (`Radicle/docs/MATERIALS.md`):
- Only edits existing files. There is no constructor for a new material.
- Setting a conditional field such as `EmitEnabled` does not add its block to the in-memory field list until the file is written and read again.
- BGEM `LightingInfluence` is stored raw. The doc says the game converts it to a byte in a way that wraps above 1.0, so keep values within 0.0 to 1.0.
- Field order and version gates come from an external layout (py-creation-lib). The doc says they "were not re-checked against that source in this change". All vanilla materials are version 2, so higher-version branches are covered only by synthetic tests.
- The doc's game proof covers only the version-2 BGEM body, not the header or the BGSM layout.

## DDS textures

What it is: the DDS image container. Radicle can read a DDS header and decode one mip to RGBA8 (`fo4::texture`).

Today (`Radicle/docs/TEXTURES.md`):
- `read_dds` accepts DX10 headers, a list of legacy FourCCs, and legacy mask layouts. An unknown format is an error, not a guess.
- `decode_dds_rgba` decodes exactly one mip: BC1, BC2, BC3, BC4, BC5, BC7 and several uncompressed layouts.
- `reconstruct_z` rebuilds the blue channel for unsigned BC5 normal maps only. Other formats throw.
- BA2 DX10 headers are built by `build_dds_header` and `dds_header_for` (see the BA2 section).
- The block decoder is a port of bcdec v0.97, copied from PET's `BcnDecoder.cpp` (`THIRD_PARTY_NOTICES.md` holds the licence).

Key source files: `Radicle/include/fo4/texture/dds.hpp`, `Radicle/src/texture/dds.cpp`, `dds_codec.cpp`, `bcn.cpp`; C ABI `Radicle/src/c_api_dds.cpp`.

Tests: `Radicle/tests/test_dds.cpp`, `test_parity_dds.cpp`. PET: `PluginEditTool/native/tests/unit/TextureCodecTests.cpp`.

PET side: there is no texture panel. PET has its own copy of the decoder (`PluginEditTool/native/src/textures/BcnDecoder.cpp`, `DdsDecoder.cpp`, `DdsCodec.cpp`, `PngWriter.cpp`). `TextureResolver.cpp` and the archive `previewTexture` operation use it. `TEXTURES.md` says repointing PET at `fo4::texture` is "a follow-up".

Known gaps (`Radicle/docs/TEXTURES.md`):
- Not in scope: encoding, PNG output, cube or array slice selection (slice 0 is decoded), and BC6H.
- BC4_SNORM (81) and BC5_SNORM (84) are accepted by `read_dds` but decode returns a parse error.
- The 484-texture DirectXTex comparison was done for PET's port and "was not repeated here".

## Audio

What it is: two things share the name.
1. Sound records in plugins (SNDR descriptors, SOUN markers, MUSC music, FSTP and FSTS footsteps). Radicle handles these.
2. Audio files (XWM, FUZ, WAV). Radicle has no code for these (a search of `Radicle/src` and `Radicle/include` for xwm and fuz found nothing).

Sound records today (`Radicle/docs/SOUND.md`):
- Reads and edits all five record families through the shared field writer. The doc has a table of read, write, add, remove, create and remap support per field. Some fields are marked opaque or refused (for example the FSTS counts and SOUN creation).
- `resolve_sound_files` (`Radicle/src/environment/sound.cpp`) turns a SOUN marker or SNDR descriptor into the file paths it plays. It follows Standard, Compound and AutoWeapon kinds, caps depth at eight, and fails closed on cycles and limits.
- Playback helpers are deterministic and take the random draw as input (rate-file pick, no-repeat pick, frequency factor, loop flag, sidechain and rumble sends). Nothing plays audio.
- Unknown kinds or sizes fail closed and untouched bytes are preserved.

Key source files: `Radicle/include/fo4/environment/sound.hpp`, `Radicle/src/environment/sound.cpp`, `Radicle/src/c_api_sound.cpp`.

Tests: `Radicle/tests/test_sound.cpp`, `test_sound_subrecords.cpp`, `test_sound_authoring.cpp`, `test_sound_writer_c_api.cpp`, `test_sound_real.cpp`, `test_sound_writer_real.cpp`, `test_audio_records.cpp`, `test_audio_records_real.cpp`.

PET side for audio files:
- `PluginEditTool/native/src/audio/AudioService.cpp` runs external tools: ffmpeg to normalize to PCM WAV, xWMAEncode for WAV to XWM, and a FUZ encoder (`BmlFuzEncode`) for FUZ. It has a 60 second ffmpeg timeout.
- `AudioPanel.tsx` has four modes: to XWM, from XWM, make FUZ, extract FUZ. It also needs lip files for FUZ.
- Tests: `PluginEditTool/native/tests/unit/AudioServiceTests.cpp`.

Known gaps (`Radicle/docs/SOUND.md`):
- Alternate (SNAM) substitution at play time, category and output-model effects, and Envelope Fast/Slow behaviour are not implemented.
- `resolve_sound_files` differs from the engine for Compound duplicates, AutoWeapon zero-rate entries, missing CNAM and cycles. These are recorded and unchanged.
- The REPT ninth byte follows payload size, not form version. The doc says full-corpus agreement is "not checked here".
- The GardenEngine consumer probe is documented as "Not yet run".

## SEQ files

What it is: `Data/Seq/<plugin>.seq`, a list of the start-game-enabled quests a plugin adds (`Radicle/docs/SEQ_FORMAT.md`).

Today:
- The container is a bare array of little-endian u32 runtime FormIDs. There is no header, count or magic.
- Radicle reads and writes it as raw FormIDs, in file order. It never resolves entries to plugins (that needs a load order).
- A zero-byte file reads as an empty list. A length that is not a multiple of four is a parse error.
- PET creates SEQ files from a plugin: `PluginEditTool/native/src/plugin/RadiclePluginSeq.cpp` (`createSeqFile`). The writer skips quests that an earlier plugin in the load order already enabled.

Key source files: `Radicle/include/fo4/seq/seq.hpp`, `Radicle/src/seq/seq.cpp`, `Radicle/src/c_api_seq.cpp`.

Tests: `Radicle/tests/test_seq.cpp`, `test_seq_corpus.cpp`, `test_c_api_seq.cpp`.

PET panel: none found among the panels listed for this page.

UNPROVEN, as named in `Radicle/docs/SEQ_FORMAT.md`:
- What the engine does with a zero-byte file or a length not divisible by four.
- Whether entries must be sorted or unique.
- How the engine resolves each entry's master index.
- Precedence between plugins' SEQ files, and what happens when a SEQ disagrees with the plugin's record flags. The "only newly enabled quests" rule comes from xEdit, not from an engine probe.
- Evidence base: five local files of 4, 8 and 12 bytes.

## String tables

What it is: the `.STRINGS`, `.DLSTRINGS` and `.ILSTRINGS` sidecar files that hold text for localized plugins (`Radicle/docs/LOCALIZED_STRINGS.md`).

Today:
- Reads and writes all three kinds (`Radicle/include/fo4/localization/strings.hpp`). Header is `numStrings` and `dataSize`, then `{id, offset}` rows, then the data block. `.STRINGS` records are raw text plus NUL. DL and IL records have a u32 length that includes the NUL.
- The reader fails closed on truncation, size mismatch, unsorted or duplicate IDs, bad offsets and missing terminators.
- Text is kept as raw bytes with no transcoding. The doc says shipped tables include legacy CP1252 and GB2312 content, so display decoding needs an explicit language policy above the container.
- The writer sorts rows by ID, deduplicates identical text, and rejects embedded NUL. Output is logically round-trip safe but not promised byte-identical to Bethesda's ordering.
- Helpers: `add_string`, `set_string`, `remove_string`, `next_string_id`, `string_table_path`, `write_localization_tables`.
- A plugin string field decodes to either a four-byte string ID or inline text, based on the TES4 localized flag (bit `0x80`) and the field schema (`Radicle/docs/FORMAT_NOTES.md`, section "Localized string subrecords in shipped masters"). Decoding does no I/O. Only RACE is wired in that section; the doc says other typed families "still read strings as inline text".
- `fo4::io::write_file_pair_dependency_first` writes a dependency file and then a primary file, with rollback. It is the base for later localized CNAM authoring. A crash between the two writes can leave the new sidecar with the old plugin, which is deliberate.

Key source files: `Radicle/src/localization/strings.cpp`, `Radicle/src/c_api_strings.cpp`, `Radicle/src/environment/strings` (read side used by PET).

Tests: `Radicle/tests/test_localized_strings.cpp`, `test_localization_tables.cpp`, `test_localized_fields.cpp`, `test_localized_value_writes.cpp`, `test_plugin_localization.cpp`, `test_environment_strings.cpp`. PET: `PluginEditTool/native/tests/unit/RadiclePluginStringsTests.cpp`.

PET side:
- `PluginEditTool/native/src/plugin/RadiclePluginStrings.cpp` and `RadiclePluginStringsEdit.cpp` use `fo4/localization/strings.hpp`.
- `StringsHost` (`PluginEditTool/web/src/backend.ts`) has list tables, get, lookup, set, add, remove, import, export and save. UI: `LocalizationPanel.tsx`.

Known gaps (`Radicle/docs/LOCALIZED_STRINGS.md`):
- Radicle does not fetch text from a language sidecar by itself. The caller must pick language, table kind and ownership.
- Typed EYES `FULL` editing treats a four-byte payload as opaque and refuses text mutation.
- Localized QUST stage-log CNAM authoring is described as an ownership contract only: "they do not claim that the current backend already implements them". Creating a new sidecar from an absent or archive-only source is out of scope.
- #29 is described as partial, and CK table emission and live behaviour are open (#390).
- `RE_INVENTORY.md` says the on-disk strings-file directory layout is "still a gap" in the RE tree, and `FORMAT_NOTES.md` lists the engine truncating oversize chunks while Radicle errors (a documented policy difference).

## Related: compressed records

Plugin records use zlib framing (window bits 15). This is evidence for plugin records, not for BA2 chunks (`Radicle/docs/COMPRESSION_EVIDENCE.md`, `Radicle/docs/FORMAT_NOTES.md`).
- A record with bad framing or checksum loads as undecodable, its raw bytes are kept, edits to it are refused, and a compressed TES4 that fails to decode still fails the plugin.
- Zlib framing is stated as verified on OG, NG and AE builds. Data sizes 1 to 3 and live malformed-input behaviour are listed as unresolved.
- Radicle `fo4` BA2 chunks use zlib framing too (`Radicle/docs/BA2.md`), but the BA2 doc does not cite the RE evidence.

## Reading list for a new developer

1. `Radicle/docs/BA2.md`, then `Radicle/docs/NIF.md` (long, 506 lines; start at "Scope" and "Deliberate omissions").
2. `Radicle/docs/MATERIALS.md` and `Radicle/docs/TEXTURES.md` (short).
3. `Radicle/docs/SEQ_FORMAT.md` and `Radicle/docs/LOCALIZED_STRINGS.md`.
4. `Radicle/docs/SOUND.md` for sound records. Audio files are PET-only today.
5. `Radicle/docs/RE_INVENTORY.md` for what has real reverse-engineering evidence. For BA2 it says no dedicated v7/v8 layout write-up was found in the RE tree, so the BA2 evidence is file measurement, not RE.
