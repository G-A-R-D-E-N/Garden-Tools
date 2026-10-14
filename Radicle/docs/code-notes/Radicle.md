# Radicle code notes

## src/environment/world.cpp:resolve_water

The read-only selection query follows the static OG water evidence in the RE
record [RE 6916afb](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md), RE commit
`6916afbcbec8ab09ecc0dfd30661502d0064f4f7`, sections 3.1-3.7 (verified addresses
0x3B53D0, 0x493910, 0x3B4D40, 0x493400, 0x3ADC29 and 0x491910).
CELL XCWT takes precedence, exterior cells may use their winning WRLD, and the
global default selects Fallout4.esm form 0x18. The default is the winning `Fallout4.esm:0x18` WATR record, looked up by form ID, the same lookup the engine's global default makes (`VERIFY-2026-10-10.md` section 1 row 28). Radicle also treats a deleted `Fallout4.esm:0x18` as missing (`src/environment/world.cpp:winning_type`); RE `4cdafd2` does not cover deletion. A missing default record remains
explicitly unresolved; the SDK does not synthesize its visual data. An XCWT whose
target is missing or not a WATR is dropped, as the engine drops it in the
cell's InitItem fix-up, so the cell falls back to the worldspace water or the default
(RE `4cdafd2`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md`
Follow-up 2; `VERIFY-2026-10-10.md` section 7 item 3 for OG; section 3 row 9 for NG, with the same steps including the unlink; section 4 for AE, which shows no visible remap step; its unlink is COULD NOT CONFIRM, section 1 row 33). The RE does
not address deleted XCWT targets, so Radicle treats them as missing. A CELL or WRLD handle from an older override resolves like the winning version of that form (`src/environment/world.cpp:resolve_water` decodes `env.winner(form_key)`). In Radicle's model (not from the RE), the winning CELL version is decoded on its own: a winning override that omits XCWT or XCLW does not take them from the master. An XCWT of 0 is treated as absent (test CELL `0x921`); the engine's setter removes the extra for 0, confirmed for OG and NG and not read for AE (`VERIFY-2026-10-10.md` section 1 row 17). The engine's XCWT fix-up uses the last file that defined the cell (`FUN_140153690(cell, -1)`, [README, Follow-up 2](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/4cdafd28b8b576749fea4ffdfde5f3c32fefdb7c/RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md)). The RE still leaves
the fix-up timing against a WATR from another plugin (F2-11) and runtime behaviour open.
An invalid or deleted WNAM parent ends inheritance. An invalid or non-WATR NAM2 at the
world where inheritance stops selects the default key with uses_default_water=1.
Inheritance uses the current world's bit 3 for water type and bit 0 for height,
respecting the effective WNAM/PNAM flags of `effective_parent_flags`, which the raw
PNAM readers do not apply. A visited set rejects active inheritance cycles. RE `6916afb`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md` §3.3 shows the engine has no cycle guard, so the RE expects a hang on such a cycle (static only, not executed; §5 fixture 7, VERIFY section 1 row 22). This throw is a deliberate divergence. A WRLD input always resolves its height through PNAM bit 0 (`src/environment/world.cpp:resolve_water`), so a parent cycle whose worlds set bit 0 makes the whole selection throw, even when the water-type walk (bit 3) would not cycle.

An absent XCLW selects the world default height for an exterior cell with Has Water
and a winning world (Radicle's inference: the RE shows only that the engine tests XCLW >= 2^31, VERIFY-2026-10-10 section 1 row 15, which calls the README's 'untouched' reading interpretation; the RE does not show what an absent XCLW starts as). A WRLD with no DNAM reports height 0.0 with has_height set, and
a WRLD input reports a height when its winning version is live. A WRLD input whose winning version is deleted resolves to the default water with no height, because `src/environment/world.cpp:resolve_water` routes the direct WRLD branch through `winning_type`, which treats a deleted winner as missing, so the deleted version's water and height fields are not used. A CELL whose winning worldspace is deleted resolves the same way. Tests `DeletedWinningWorldspaceInputTakesNoWaterOrHeight` and `CellInDeletedWinningWorldspaceResolvesDefaultWater` in `Radicle/tests/test_environment_water_edges.cpp` cover them. xEdit CELL DATA bits 0 and 1 are
[lines 5968-5969](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L5968-L5969);
WRLD PNAM bits 0 and 3 are [lines 13128 and 13131](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L13128-L13131).

The query preserves raw XCLW and reports its load-time integer transform only
in the effective height: positive halves round up, negative values truncate,
NaN or values below -2^31 become -2^31, and values at least +2^31 select the
world default. Thus -2^31 is a real height. Interior height volumes are outside
this query. These are static semantics, not live game acceptance. `VERIFY-2026-10-10.md` confirms NG statically on every row: from bytes for section 3 rows 1, 2, 3, 5 and 7, and from decompiles only for rows 4 (InitItem NAM2 cast and parent handling), 6 (default water init), 8 (WNAM/PNAM load) and 9 (XCWT fix-up). AE is in VERIFY section 4.

`include/fo4/c_api_records.h:radicle_water_resolve` returns the selected key,
borrowed winning WATR handle, default-selection flag and optional height.
The ABI number is 28, re-bumped if another ABI PR lands first; see
`docs/code-notes/radicle-water-writer.md`.
`bindings/csharp/Radicle.Interop/WaterSelection.cs:EffectiveWater` consumes it.

`probes/gardenengine/WaterSelectionProbe.cs:WaterSelectionProbe._Ready` reads
Commonwealth CELL selections through C ABI and passes each selected key to
GardenEngine's existing private `WaterBuilder.Material` conversion. It checks
the resulting material key/fog value against the selected winning WATR. The
reflection call remains confined to this probe; rendering, `WaterBuilder.Build`
adoption and in-game parity are separate from this consumer conversion check.
`Radicle/probes/gardenengine/*` is copied into a GardenEngine checkout to run; no CMakeLists in this repo builds it.
The default water key (Fallout4.esm, 0x18) is one shared constant pair, `src/environment/default_water.hpp:k_default_water_plugin` and `k_default_water_local`, used by `src/environment/world.cpp:resolve_water` and `src/c_api_world.cpp:radicle_water_resolve`.

GT-119 checks RE `6916afb`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md` §3.1 (CELL XCWT precedence), §3.2 and §3.5 (PNAM bit 3 for water type, bit 0 for height), §3.6 (XCLW load transform), §3.7 (WNAM/PNAM order), and §3.4 (invalid NAM2 selects the default key; a missing parent ends inheritance). Verified and matched: an invalid XCWT target, which the engine drops in the cell InitItem fix-up (RE `4cdafd2` Follow-up 2; `VERIFY-2026-10-10.md` section 7 item 3 for OG; section 3 row 9 for NG; section 4 for AE, which shows no visible remap step, and its unlink is COULD NOT CONFIRM, section 1 row 33); Radicle drops it too. Unresolved: the fix-up timing against a WATR from another plugin (F2-11), the runtime behaviour of that removal, the absent-XCLW initial value (Radicle's inference; VERIFY section 1 row 15), and a deleted parent or XCWT target (not addressed by the RE; treated as missing). Deliberate divergences: an active inheritance cycle throws where the RE expects a hang (static only, not executed; §3.3, §5 fixture 7), and a missing form ID 0x18 is not synthesised as a DefaultWater record (§3.4).

## Water corpus evidence (#341)

`tests/test_water_real.cpp:WaterRealData` covers every on-disk WATR in the
seven official masters through the native decoder and existing C ABI,
records DNAM sizes, and compares untouched master serialization byte for
byte. `docs/WATER.md` pins layout evidence for
`src/environment/base_records.cpp:decode_water`. The selection query above uses
static OG RE evidence from #395; runtime questions remain open. Raw links and heights are preserved by
`src/environment/world.cpp:decode_cell` and `decode_worldspace`.

## src/environment/sound.cpp:decode_sound_descriptor and decode_sound_marker

The pinned loader record is
`https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/sound-399-340/README.md`
at commit `6916afbcbec8ab09ecc0dfd30661502d0064f4f7`, sections 5.2–5.5 (OG
addresses 0x4C1C10, 0x2CB540 and 0x55A1E0). The OG Standard loader would accept a
top-level FNAM as legacy flags, but xEdit defines FNAM only inside rate groups
and no official record has one, so the read surface no longer exposes it (see
`docs/code-notes/radicle-sound-writer.md`). Repeated LNAM retains its last
stored value. SOUN's ninth REPT byte is exposed whenever REPT is nine bytes. Playback selection,
resource hashing and compound recursion remain outside these stored reads.

## src/environment/sound.cpp:decode_sound_descriptor

`docs/SOUND.md` pins each stored layout and the independent official-master
census. SNDR's CNAM controls the BNAM union; known kinds keep their stored
links, raw controls and complete rate blocks; a missing CNAM selects the values
BNAM layout. Unknown kinds, duplicate scalar fields other than last-value LNAM,
a top-level FNAM, wrong sizes and
incomplete/count-mismatched rate groups fail closed. Static OG selection paths
are documented in the newer record; category mixing and live playback remain
outside this read surface.

`src/environment/sound.cpp:decode_sound_marker` accepts the optional REPT tail
without filling in a default. `decode_footstep_set` maps the reverse disk order
into walking/running/sprinting/sneaking/swimming categories after validating
the complete count table. `decode_music_type` and `decode_footstep` expose
only their evidenced fields; unknown container bytes are untouched.

`src/c_api_sound.cpp:radicle_sound_descriptor_decode` and the other sound
exports use owned array holders and existing environment string storage.
`bindings/csharp/Radicle.Interop/SoundRecords.cs:RadicleEnvironment` copies
every array before freeing its holder. The generated declaration/layout probe
includes `include/fo4/c_api_sound.h`; the current ABI is 27 against main ABI 26
(re-bump if another ABI PR lands first; see `docs/code-notes/radicle-sound-writer.md`).

## src/environment/vegetation.cpp:decode_grass / decode_tree

The pinned private evidence at RE commit `6916afbcbec8ab09ecc0dfd30661502d0064f4f7`,
`RE-Work/subsystems/radicle-re-garden-tools/vegetation-403-342/README.md`, section
3.1, verifies the OG grass loader address 0x31AA00 and bounded copy helper 0x1523A0:
present DATA copies at most 32 bytes into zeroed storage. The typed view will
zero-fill a short present DATA and read the prefix of a long one; original
bytes remain available through the lossless container and payload getter.
Missing DATA remains absent on this stored-field surface, rather than creating
an authored payload. Section 5 verifies TREE CNAM is read only at exactly 48
bytes; other lengths stay opaque without failing the rest of the record.
Regression tests exercise short/long DATA and nonmatching CNAM before the fix.
No placement, shader, wind or seasonal simulation is added.

## src/environment/vegetation.cpp:decode_grass, decode_tree, decode_flora

`docs/VEGETATION.md` pins stored fields and official-master size measurements.
The shared model/bounds fields retain the existing metadata API; these getters
validate present MODL and OBND rather than duplicating their surface. Raw flags,
discriminators, seasonal bytes and tree wind values carry no runtime defaults.
`decode_landscape_grasses` preserves repeated GNAM order and validates each
individual link. Unknown payloads stay untouched in the container.

`src/c_api_vegetation.cpp:radicle_landscape_grasses_decode` owns its key array
until free; fixed record getters reuse environment-owned strings. C#
`Vegetation.cs:RadicleEnvironment` copies arrays and releases the holder.
ABI 26 follows merged main's ABI 25 and preserves its existing exports.
`CMakeLists.txt:RADICLE_CSHARP_TESTS` is
enabled in native CI with a writable runner-local .NET install directory.

## include/fo4/environment/base_records.hpp:ClimateTiming

The pinned private RE record at commit `6916afbcbec8ab09ecc0dfd30661502d0064f4f7`,
`RE-Work/subsystems/radicle-re-garden-tools/weather-climate-397-343/README.md`,
is linked from `Radicle/docs/CLIMATE_WEATHER.md`; the local copy is not committed.
Its sections 3.2 and 3.4 verify the unsigned four timing bytes multiplied by
the float32 one-sixth constant (OG getter addresses 0x648A50–0x648B40), plus the
moon byte's low six phase-day bits and Secunda/Masser bits 6/7 (0x648A10,
0x658B00 and 0x658B10). `ClimateTiming` exposes these interpretations alongside
the raw bytes, without clock clamping or wrapping. The existing climate C ABI
getter carries the same values; integrating main
`c50ad4d5405e1571113019654b10e3d4a05ca0f1` advances ABI 11 to 12.
Re-bump if another ABI PR merges first. Native and managed boundary checks
include unsigned values 128/255 and moon byte 0xFF. Volatility and weather
execution remain raw; no transition simulation or assumed GMST override is added.

## Climate timing and weather parameters (#343)

`src/environment/base_records.cpp:decode_climate` decodes the six-byte CLMT
TNAM and `decode_weather` decodes the measured 19/20-byte WTHR DATA variants.
`docs/CLIMATE_WEATHER.md` pins the field-layout evidence and explicitly limits
interpretation to stored bytes. `src/c_api_base_records.cpp:radicle_climate_decode`
and `radicle_weather_decode` expose them in ABI 24; managed properties preserve
the raw encoding. `include/fo4/forms/weather.hpp:Weather` provides a stable
include name while `weather_concept.hpp` keeps existing consumers building.

`bindings/csharp/tools/generate_interop.py:emit_probe` names the authoritative
public header directory in the generated probe's existing source constraint.
Its `offsetof` values describe Radicle's own compiled ABI, not game layouts.

`probes/gardenengine/ClimateProbe.cs:ClimateProbe._Ready` runs in the current
GardenEngine project, reads Commonwealth's climate and chosen weather through
the generated C ABI binding, and invokes `CellService.BuildExteriorSkyJson`
to verify that actual consumer path. It serializes stored timing/parameters
and the existing sky payload, without a Mutagen lookup or asset conversion.
The reflection call is confined to this integration probe because the
existing consumer is private. The probe does not validate live transitions.

`src/environment/base_records.cpp:decode_weather` keeps transition delta
encoded. The private record pinned above, section 3.5, confirms DATA byte 3
in `Sky::UpdateWeather` (address `0x64E530`) and names the two transition settings.
Its sections 5–6 retain GMST override and live-simulation uncertainty; the
decoder supplies stored bytes without adding a duration formula.

`src/environment/base_records.cpp:decode_weather` accepts WTHR FNAM only at 32, 56 or 72 bytes, the sizes xEdit allows (`source:xEdit Core/wbDefinitionsCommon.pas:9521-9572 @93cc0bc5`). The former check accepted any multiple of four from 16 bytes up, such as 20, 24 or 28 bytes, that the forms model refuses, so the two readers now agree. The reader does not check form version against the 56 and 72 byte layouts.

## include/fo4/forms/weather.hpp:Weather.cloud_speed_y / Weather.cloud_speed_x

At pinned xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`,
`Core/wbDefinitionsCommon.pas:9366–9376` declares RNAM and QNAM as
32-element `itU8` arrays. `src/forms/weather.cpp:decode_speed_bytes` must
return their unsigned stored values: its signed arrays turned 128–255 into
negative values. The shared helper and both arrays use `uint8_t`, without
adding a physical scroll-rate conversion. The editor callback at lines
4370–4376 is compatibility evidence, not a runtime rate specification.
`tests/test_weather_concept.cpp:CloudSpeedsPreserveUnsignedStoredBytes`
covers 0, 127, 128, 254 and 255 on both axes. These fields have no C ABI or
managed exposure; the climate/weather C containers' separately added fields
remain governed by ABI 24.

## tests/test_ba2_versions_real.cpp:Ba2NamedRealData

Six installed archive fixtures cover GNRL and DX10 at versions 1, 7 and 8.
The streaming reader and full models must extract identical first-entry
bytes, and rebuilding that entry must preserve extracted content (including
the reconstructed DDS header), the name/extension/directory hashes, and for
DX10 the six texture info fields and each chunk's mip range. This does not
require byte-identical archive padding. `docs/BA2.md` records the named fixture files (checked by `Ba2NamedRealData`)
and the totals printed by `tools/corpus/ba2_measure.py`. The test reads game
data only through `FO4_DATA_DIR` and runs only when it is set. In CI
the `fo4-data` runner supplies it to the `native-windows-real-data` job of `native-ci`; both `native-windows-tests` passes clear it. The Linux job passes `FO4_DATA_DIR_LINUX`, which is currently unset,
so the test skips there. Missing data or a
missing named archive skips without committing payloads. A present archive whose
version, kind or entry count differs fails the test.

## tests/test_ba2_texture.cpp:Ba2Texture.RoundTripStoredAndCompressed

The synthetic DX10 stored and compressed round trip runs once per supported
version (1, 7, 8) because the writer emits the same field widths for all three;
a version-specific regression in the writer or reader shows up under its
`SCOPED_TRACE` value. `Ba2Texture.CubemapFlagSurvivesRebuildAndMarksDdsHeader`
covers the same three versions for a flags bit 0 (cubemap) entry: the flag
survives write and read, the extracted DDS header sets caps2 `0xFE00` and DX10
miscFlag `0x4` (`ba2_texture.cpp:dds_header_for`), and a rebuild keeps flags and
extracted bytes unchanged.

## src/archive/ba2_texture.cpp:dds_header_for

DX10 extraction picks the DDS header the way xEdit's writer does. Formats in
`dx9_format` (the xEdit `TwbDDS.DXGI_DX9` set, `Core/wbDDS.pas:249-265`) get the
128-byte classic header with a FourCC or pixel masks; the rest get the 148-byte
DX10 extension. Cubemaps set `DDSCAPS_COMPLEX` even with one mip
(`Core/wbDDS.pas:600-606`). The rule, its xEdit citations and the Radicle policy
differences are in `Radicle/docs/BA2.md` under "DDS header rule".

## src/archive/ba2_texture.cpp:dds_pitch_and_flag

DX10 formats outside the DX9 set take `dwPitchOrLinearSize` and the size flag from
xEdit's per-format case table (`Core/wbDDS.pas:610-764`); the default branch uses
`k_bits_per_pixel`, a copy of `TwbDDS.GetBitsPerPixel` (`Core/wbDDS.pas:788-928`).
`Radicle/docs/BA2.md` under "DDS header rule" groups the cases by flag with their
line ranges. Radicle's zero-width refusal stays in `dds_header_for`.

## tools/corpus/ba2_measure.py:measure

Read-only, stdlib-only corpus check for `docs/BA2.md`. It walks each root-level
`.ba2` header and entry/chunk table without reading payloads, except that
`--first-chunk` reads and decodes one chunk per archive. The entry's
chunk-header-size field (`0x10` GNRL, `0x18` DX10) is a stored value checked
against the expected one, not the chunk record length: GNRL chunk records are
20 bytes, so the script reads each record by its struct size. An archive that
cannot be opened or parsed (`OSError`, `struct.error`, `zlib.error`, or a
`ValueError`/`OverflowError` from seeking to an out-of-range first-chunk offset)
and a name-table offset past the end of the file are recorded as problems for that
archive. Archives with problems are left out of the totals; the run continues and
exits non-zero. It is not wired into CMake or CI.

## tests/test_vmad_unsupported.cpp:make_plugin

The unsupported property follows a known object reference so a tolerant scan must discard that earlier hit. Optional struct nesting checks the same refusal recursively. The verified zero-count exception is tested with references on both sides; remapping must change only their index bytes and preserve the empty property. Type 6 and nonempty type 16 stay opaque. `src/schema/vmad.cpp:walk_property` and `read_property` consume the count before deciding whether type 16 is safe. The verified private record and its limited build/corpus scope are listed in `docs/VMAD_VARIABLE_EVIDENCE.md`.

## src/c_api_vmad.cpp:radicle_plugin_set_vmad_string

VMAD string property values, single and string-array elements, are stored as the UTF-8 bytes the caller passes and are returned unchanged on read; the C ABI does not convert them to Windows-1252. xEdit decodes these values as UTF-8: `source:xEdit Core/wbInterface.pas:23869 @93cc0bc5` sets `wbEncodingVMAD := TEncoding.UTF8`, applied with `.OverrideEncoding(wbEncodingVMAD)` to the String and Array of String element definitions at `source:xEdit Core/wbDefinitionsFO4.pas:3771, 3778, 3803, 3810, 3839, 3846 @93cc0bc5`. Script names, property names (`wbDefinitionsFO4.pas:3862`, no override) and fragment names stay Windows-1252 (`wbInterface.pas:23866-23867 @93cc0bc5`). The shared writer is `src/plugin/vmad_writer.cpp:encode_value`, which copies the string bytes as given; the reader `src/schema/vmad.cpp:read_property` keeps raw bytes. `src/c_api_placed.cpp:build_view` still converts property names from Windows-1252 but no longer converts property text. Tests: `tests/test_vmad_writer_c_api.cpp:CApiVmadWriter.TypedSettersReparseAndRejectInvalidBoundaryValues` asserts stored bytes `43 61 66 C3 A9` for a scalar and for array element 0; `tests/test_environment_placed.cpp` reads a property name `Caf\xE9` back as UTF-8 while its text `Caf\xC3\xA9` is returned unchanged. Invalid UTF-8 is refused before any mutation with `INVALID_ARGUMENT`, checked by `src/text/encoding.cpp:is_valid_utf8` (the decoder `utf8_to_windows1252` also uses); the same test asserts `\xFF` is refused and the plugin bytes stay unchanged.

## src/environment/navmesh.cpp:decode_navmesh and decode_navmesh_info

`docs/NAVM_NAVI.md` records the measured version-15 disk grammar and the
resolved CK reader discrepancy. The pinned private record is
`https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/navigation-401-339/README.md`
at commit `6916afbcbec8ab09ecc0dfd30661502d0064f4f7`, section 3 (OG readers
@0x14250FCD0 and @0x14250FFB0; independently matched AE readers). Triangles
store f32 clearance, a raw traversal byte and one u32 flags word; splitting
that word discarded flags above bit 15. Form versions below 128 fail closed
because the existing complete grammar assumes the 11-byte extra-edge form.
Both decoders reuse the existing full
payload validator before reading counted data. Geometry, adjacency values
and links are stored reads; flag meanings and runtime pathfinding remain
unverified. Cover/grid and optional island bytes stay opaque and borrowed
from the immutable environment. Stored writer constraints are documented in
`docs/code-notes/radicle-navmesh-writer.md:Radicle/src/plugin/navmesh_writer.cpp`.

`src/c_api_navmesh.cpp:radicle_navmesh_decode` and `radicle_navi_decode`
own their array views until the paired free call; strings and opaque bytes
remain borrowed from the environment. `bindings/csharp/Radicle.Interop/Navmesh.cs`
copies every array before freeing. ABI 31 advances main's ABI 30 once;
re-bump if another additive ABI PR merges first. Existing exported signatures
and layouts are retained.

`CMakeLists.txt:RADICLE_CSHARP_TESTS` is enabled in the two native jobs of
`.github/workflows/native-ci.yml` so the new getters and their ownership are
validated in CI. The existing test supplies the branch's native library,
layout probe and ABI stubs; the workflow supplies the .NET 8 SDK.

## src/plugin/navmesh_writer.cpp:set_navmesh_triangle and set_navmesh_door

`edit` reads the NVNM counts for all four blocks (vertices, triangles, edge
links, doors) before patching, so each setter checks its index against the
live counts and throws before the candidate bytes are copied back; a refused
write leaves the plugin bytes identical. Triangle edge and vertex index ranges are Radicle policy, stricter than xEdit.
xEdit's decider `wbEdgeLinksTo` (`source:xEdit Core/wbDefinitionsCommon.pas:2611-2662 @93cc0bc5`)
rejects nothing: it resolves a link for display and returns nil for any index it cannot
resolve. For edge i, if bit i of the triangle's u16 Flags is set (`Flags and (1 shl aEdge)`,
line 2636) the value indexes NVNM `Edge Links` (count checked at 2640); otherwise it
indexes NVNM `Triangles` (count checked at 2658). Negatives are not resolved (2643, 2661).
The setter accepts -1 (none) and refuses -2 and any index at or past the selected count.
Vertices must be below the NVNM vertex count. The selector is the low 16 bits of
`NavmeshTriangle::flags` (the u16 Flags at triangle offset 17 in
`source:xEdit Core/wbDefinitionsFO4.pas:4387-4397 @93cc0bc5`), and the flags word is
preserved against the stored bytes before this check runs. A vertex or edge index is
range-checked only when its new value differs from the stored value (fix in
`src/plugin/navmesh_writer.cpp:set_navmesh_triangle`), so a triangle that already holds an
out-of-range index keeps it while its clearance or other unchanged fields are edited;
changing that index to another out-of-range value is refused.
Door triangles use `wbTriangleLinksTo` (`source:xEdit Core/wbDefinitionsCommon.pas:2813-2843 @93cc0bc5`,
index checked at 2837 and 2840) against NVNM `Triangles`. The setter requires
`0 <= triangle < triangle count` as Radicle policy (Door Links at `source:xEdit Core/wbDefinitionsFO4.pas:4412-4418 @93cc0bc5`):
the decider returns nil for -1 (2840) and any out-of-range value without rejecting it, and the
setter refuses -1 so a door always names a triangle.
Deliberate policy: `src/environment/navmesh.cpp:decode_navmesh_info` requires NAVI NVER
== 15 (`version 15 is required`), while xEdit reads NVER as a plain u32
(`source:xEdit Core/wbDefinitionsFO4.pas:7242 @93cc0bc5`). The tighter check is kept so
that only the measured version-15 grammar is written; widen it only with evidence for
other versions.

## src/environment/behavior.cpp:decode_package

The private loader record at
[packages-combat-idle-407-345/README.md](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/packages-combat-idle-407-345/README.md)
commit `6916afbcbec8ab09ecc0dfd30661502d0064f4f7`, section 3 Q1, stamps
the OG loader at address 0x719180 and schedule consumer at 0x71EC30. xEdit types PSDT duration as unsigned u32 minutes (`source:xEdit Core/wbDefinitionsFO4.pas:10569 @93cc0bc5`, `wbInteger('Duration (Hours)', itU32, wbDiv(60, 4))`), and `decode_package` keeps the raw u32, so 0xFFFFFFFF stays 4294967295 and is not read as -1. Whether the OG schedule consumer reads it as signed is an RE question (RE #407), not settled by xEdit.
PKDT's final u16 is exposed as an unnamed trailing value rather
than discarded. Raw signed month/hour/minute bytes remain unchanged;
legacy month adjustment and schedule execution are not simulated.

## src/environment/dialogue.cpp:decode_scene

Stored action-header SNAM/ENAM are four-byte unsigned values in pinned xEdit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas:9010-9011`.
The seven-master corpus test caught the incorrect two-byte requirement. Native,
C and C# preserve all 32 stored bits, including values above 65535. The private
record `https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/dialogue-scenes-405-344/README.md`
stamps OG runtime action storage/range use at addresses 0x5975E0/0x597910 as 16-bit;
that runtime representation does not establish the on-disk field size. Action
payload SNAM after the header remains opaque. SCQS values keep schema-derived
labels only; no quest-stage execution behavior is implemented.

## src/formid.cpp:classify_plugin_kind

`src/formid.cpp:classify_plugin_kind` is the single light test: TES4 SmallFile flag or a case-insensitive `.esl` file name. xEdit does the same: `source:xEdit Core/wbLoadOrder.pas:363-364 @93cc0bc5` forces the light flag from the `.esl` extension, and `source:xEdit Core/wbImplementation.pas:5304-5306 @93cc0bc5` sets the light state for an `.esl` module. Callers with a loaded plugin's header and name use it: `src/environment/environment.cpp:kind_of`, `src/plugin/reader.cpp:plugin_form_id_context` and `stamp_master_kinds`, `src/plugin/master_kinds.cpp:stamp_master_kinds_from_files`, `src/plugin/load_order.cpp:build_runtime_form_id_context`, `src/forms/weather_concept.cpp:resolve_master_kind`, and `src/plugin/mutation.cpp:local_form_id_range` and `owner_kind`. Name-only sites keep `PluginKey::looks_light`: TES4 master references in `src/plugin/tes4.cpp`, the PluginKey overload of `src/plugin/writer.cpp:remap_plugin_masters`, and the fallback branches of `owner_kind`. The stranded-record check in `remap_plugin_masters` uses `classify_plugin_kind`, because xEdit sets light for an `.esl` module on save (`source:xEdit Core/wbImplementation.pas:5304-5306 @93cc0bc5`); test `tests/test_extension_light_kinds.cpp:EslExtensionWithoutFlagRejectsRemovingLastMasterWithLowIds`. `src/plugin/writer.cpp:make_empty_plugin` sets the flag from the extension when it creates a file. `src/forms/placed_reference_copy.cpp:owner_kind` calls it for the owner-is-source branch. Tests: `tests/test_formid.cpp:ClassifyPluginKindUsesLightFlagOrEslExtension`, `tests/test_formkey_load.cpp:EslExtensionWithoutLightFlagSetsCurrentKindLight`, `tests/test_master_kinds.cpp:StampsEslExtensionWithoutLightFlagFromFile`, `tests/test_master_kinds.cpp:StampsEslExtensionWithoutLightFlagFromLoadedPlugin`, `tests/test_environment.cpp:EslExtensionWithoutLightFlagIsLightInPluginInfo`, `tests/test_mutation.cpp:EslExtensionWithoutLightFlagUsesLightRange`.

`src/formid.cpp:is_master_plugin` is the master-status twin: an `.esm` or `.esl` name counts as master (`PluginKey::looks_master`), as xEdit sets ESM for both (`source:xEdit Core/wbImplementation.pas:5301-5306 @93cc0bc5`). It backs `info.master_flag` in `src/environment/environment.cpp:Environment::load` and the PluginEditTool master checks. The dirty-plugin ONAM gate in `src/plugin/writer.cpp:write_plugin` uses it too; see that section below.

## src/plugin/load_order.cpp:build_runtime_form_id_context

Runtime classification uses TES4 SmallFile or the case-insensitive `.esl` extension, as the verified OG/AE header paths establish in `docs/RUNTIME_PLUGIN_INDEX_EVIDENCE.md`. The supplied order is already enabled and resolved by the caller. Existing safe table caps and duplicate refusal remain Radicle policy; no unverified engine sorting or overflow behavior is added. `tests/test_load_order.cpp:EslExtensionAssignsALightSlotWithoutChangingHeaderFlags` checks both classification sources and independent positions.

## src/forms/weather.cpp:resolve_master_kind

Weather master insertion uses the same flag-or-extension classification when the target plugin is present in the supplied environment. `tests/test_weather_concept.cpp:MnamExtensionOnlyLightMasterInsertion` checks the resulting master kind and decoded reference without changing the source header. Evidence: `docs/RUNTIME_PLUGIN_INDEX_EVIDENCE.md`.

## src/schema/localized_fields.cpp:read_localized_string

The existing flag-and-schema classification matches the verified OG read direction, summarized by `docs/TES4_LOCALIZATION_EVIDENCE.md`. `tests/test_localized_fields.cpp:BitSevenSelectsRepresentationForTheSameFourBytes` distinguishes identical bytes by plugin state and preserves both representations. `ZeroExternalIdIsPreservedWithoutTextLookup` preserves the empty-ID representation without inventing a sidecar lookup. No production decoder or semantic API rename is required for this evidence slice.

Matches xEdit (owner decision): xEdit sizes a localized element at `Min(4, remaining)` (`TwbLStringDef.GetSize`, Core/wbInterface.pas:20967 at xEdit 93cc0bc5), but `TwbLStringDef.ToStringNative` treats any payload that is not exactly four bytes as `lstring ID is not Int32` and resolves nothing (Core/wbInterface.pas:21006-21010). Radicle loads such a payload, exposes no ID (`localized()` is false, no text), and keeps the bytes in `raw_id`, so `write_localized_string` writes them back unchanged. Editing the field creates a new value with an ID, which encodes as a normal four-byte ID. The environment name paths (`Radicle/src/environment/environment.cpp`, `placed.cpp`) already treat a non-four-byte localized FULL as malformed and resolve nothing. Test: `Radicle/tests/test_localized_fields.cpp:NonFourByteLocalizedPayloadHasNoIdAndRoundTrips`. Quest stages stay fail-closed for a non-four-byte localized CNAM: `Radicle/src/forms/quest_stages.cpp:decode_quest_stages` throws `QUST CNAM is localized and must be exactly four bytes`, as it did on 2981dc0b, because PET's stage edit rebuilds stages from JSON and would drop those bytes. Test: `Radicle/tests/test_quest_stages.cpp:LocalizedLogTextMustBeFourBytes`. Objectives are not changed: their NNAM is written back unmodified, so the raw bytes round-trip.

- Callers of a non-four-byte localized payload (`Radicle/src/forms/armor.cpp:optional_localized` and `optional_localized_before`, `Radicle/src/forms/race.cpp:optional_localized`, `Radicle/src/c_api_quest_structure.cpp:localized`): the typed decoders keep the `raw_id` value without throwing, and their writers return it unchanged. The C quest view reports such an objective NNAM as `RADICLE_NAME_MALFORMED` with its raw bytes; see `src/c_api_quest_structure.cpp:localized`. Tests: `Radicle/tests/test_localized_fields.cpp:ArmorCallerKeepsNonFourBytePayloadWithoutId`, `RaceCallerKeepsNonFourBytePayloadWithoutId`, `QuestStructureCallerReportsNonFourByteObjectiveTextAsMalformed`.

## src/c_api_quest_structure.cpp:localized

GT-226: a localized quest text whose payload is not four bytes (`LocalizedString::raw_id`, see `src/schema/localized_fields.cpp:read_localized_string`) is reported as `RADICLE_NAME_MALFORMED` with its raw bytes, matching the environment view (`src/c_api_env.cpp`). Before this, it showed as `RADICLE_NAME_INLINE` with null bytes and size 0, which hid the bad data. The bytes live in the snapshot's `radicle_quest_structure::strings` deque, so they stay valid until `radicle_quest_structure_destroy`. C# exposes them as `QuestStoredText.MalformedBytes` (`bindings/csharp/Radicle.Interop/QuestStructure.cs:QuestStructure`). Tests: `Radicle/tests/test_localized_fields.cpp:QuestStructureCallerReportsNonFourByteObjectiveTextAsMalformed`, `bindings/csharp/Radicle.Interop.Tests/QuestStructureTests.cs:ShortLocalizedObjectiveTextIsReportedAsMalformed`.

## src/plugin/reader.cpp:inflate_record_payload

Only zlib framing (`MAX_WBITS`) is accepted; the raw-deflate retry is removed after the verified OG inflater record summarized in `docs/COMPRESSION_EVIDENCE.md` established zlib framing. The size guards match PR #462 (GT-138): the declared size is refused above `compressed.size() * 1032 + 64` and above `k_max_record_uncompressed_bytes` (1 GiB) before the single output allocation. `docs/COMPRESSION_EVIDENCE.md` states the source and remaining version/length limits. `tests/test_compression.cpp:Compression.RawDeflateAndGzipFramingLoadsUndecodable` and `BadZlibChecksumLoadsUndecodable` cover framing; `tests/test_reader_limits.cpp:ReaderLimits.CompressedUncompressedSizeBeyondZlibBoundLoadsUndecodable` and `tests/test_compression.cpp:Compression.OversizedDeclaredSizeLoadsUndecodable` cover the guards, which load the record as undecodable (see the `read_record` note). The existing zero-size early return and produced-length behavior remain outside this change.

A compressed record whose content cannot be decoded (zlib failure, either size cap, or a parse failure on the inflated bytes) no longer fails the plugin read unless it is TES4. See `src/plugin/reader.cpp:read_record` below for the behavior and the xEdit comparison.

## Radicle/src/plugin/reader.cpp:read_record / Radicle/src/plugin/writer.cpp:write_record / remap_plugin_masters

GT-205 PR1 (owner decisions D1, D2, D5 and D6, 2026-10-11). `read_record` catches `parse_error` for the compressed branch after the 4-byte framing check. That is the exception `inflate_record_payload` throws for the zlib failure and both size caps (`1032x+64` and 1 GiB, checked before the output allocation), and the one `parse_subrecords` throws on the inflated bytes. `std::bad_alloc` from the output buffer is not caught, so an allocation failure still fails the read. A caught record keeps its header, original range, `compressed` flag and declared `uncompressed_size`, gets `undecodable = true`, and has empty `subrecords`. The framing check (payload under 4 bytes) and every uncompressed record stay strict. A TES4 record is rethrown, so a corrupt compressed TES4 still fails the plugin.

Write-back: `write_record` emits the raw bytes unchanged while the record keeps its original range, so an untouched undecodable record is byte-identical on save. If a path clears that range, `write_record` throws `std::invalid_argument` naming the record instead of serializing an empty record.

Remap: `remap_plugin_masters` refuses the whole operation with `std::invalid_argument`, before any change, when any record is undecodable. The message gives the count and the first record's form key. `sort_plugin_masters` and the C `radicle_plugin_remap_masters` inherit the refusal; the C wrapper maps it to `RADICLE_STATUS_INVALID_ARGUMENT`. Skipping the record would leave master-indexed form IDs in its raw bytes while the rest of the plugin is rewritten.

D6: a stream that inflates to fewer bytes than declared is still accepted. That matches the game evidence in `docs/COMPRESSION_EVIDENCE.md`, but a plausible prefix can then display; this is a follow-up for GT's display. Edit refusal beyond the writer check is GT-205 PR2; see `src/plugin/record_edit.hpp:require_plugin_decodable` and the PR2 section below for what is and is not yet guarded.

xEdit comparison: `TwbMainRecord.DecompressIfNeeded` (source:xEdit Core/wbImplementation.pas:10273-10306 @93cc0bc5) logs `<Error decompressing ...>`, sets its decompressed data pointers (`dcDataBasePtr`, `dcDataEndPtr`) to nil and keeps the record with no data. xEdit re-serializes from those empty subrecords only when the record was modified: the `esModified in eStates` branch (source:xEdit Core/wbImplementation.pas:14806), whose compressed write is at 14826-14838. An untouched record is written from its raw stored bytes (`dcBasePtr` to `dcEndPtr`, source:xEdit Core/wbImplementation.pas:14857-14869, write at 14865), so an untouched undecodable record is byte-identical on save in both tools; Radicle matches xEdit there and is not stricter. For a modified record xEdit would write the emptied record, while Radicle refuses the write (`src/plugin/writer.cpp:write_record`, throw at lines 106-110).

Tests: `tests/test_undecodable_record.cpp` (load, sibling decode, dirty write-back, writer net, remap refusal, C API `INVALID_ARGUMENT`, strict TES4, expansion-bound size). Flipped from strict: `tests/test_compression.cpp:Compression.RawDeflateAndGzipFramingLoadsUndecodable`, `BadZlibChecksumLoadsUndecodable`, `CorruptCompressedPayloadLoadsUndecodable`, `OversizedDeclaredSizeLoadsUndecodable`; `tests/test_reader_limits.cpp:ReaderLimits.CompressedUncompressedSizeBeyondZlibBoundLoadsUndecodable`.

## Radicle/include/fo4/plugin/container.hpp:require_decodable / Radicle/src/plugin/record_edit.hpp:require_plugin_decodable

GT-205 PR2 (owner decisions D3 and D7, 2026-10-12). `require_decodable` throws `std::invalid_argument` ("record <SIG> <form key> is undecodable; edits are refused") for an undecodable record and does nothing otherwise. It lives in the public `container.hpp` because the inline `set_record_conditions` in `include/fo4/plugin/mutation.hpp:set_record_conditions` must call it. `require_plugin_decodable` (`src/plugin/record_edit.hpp:require_plugin_decodable`) is the plugin-wide refusal: while any record in the plugin is undecodable, `delete_local_record` and `renumber_local_record` (`src/plugin/mutation.cpp`) refuse the whole operation. A FormLink stored in raw bytes cannot be seen or rewritten, so a reference scan or a rekey is not proof. The message names the count and the first undecodable form key.

Shared lookups: `detail::find_mutable_record_in` (`src/plugin/record_edit.hpp:find_mutable_record_in`) calls `require_decodable` on the match and skips TES4, so `find_mutable_record` and `locate_mutable_record` inherit the check. The per-domain copies of `find_mutable_record` in `src/forms/{armor,armor_edit,components,eyes,leveled_spell,leveled_spell_creation,proj_expl_hazd,quest_alias_edit,race,race_body,weather}.cpp` and `src/schema/{condition_scopes,nested_formlink_lists}.cpp` were removed and replaced by a `using` of the shared helper. They had the same first-match, skip-TES4 body, and each caller still throws its own not-found error. `mutable_quest` (`src/forms/quest_objectives.cpp:mutable_quest`, `src/forms/quest_stages.cpp:mutable_quest`) keeps its own last-match QUST loop and `QuestError`, and adds `require_decodable` on the match, because a plugin can repeat a form ID across record types and the shared first-match lookup would pick a different record. `require_wthr_record` (`src/forms/weather.cpp:require_wthr_record`) keeps its `WeatherError` and calls the shared helper. `find_top_level_record` (`src/plugin/mutation.cpp:find_top_level_record`) checks its match.

Guarded paths, each before the first write: `set_climate_timing` (`src/environment/base_records.cpp:set_climate_timing`), `set_placed_reference_transform` (`src/forms/placed_reference.cpp:set_placed_reference_transform`), `edit_placed` (`src/forms/placed_reference_edit.cpp:edit_placed`), `set_record_editor_id` and `copy_record_as_override` on both the source and the overwrite target (`src/plugin/mutation.cpp:set_record_editor_id`, `src/plugin/mutation.cpp:copy_record_as_override`), `insert_nested_copy` on the source cell and world and on the existing destination cell and world (`src/plugin/copy_hierarchy.cpp:insert_nested_copy`), `remap_form_links` as a whole-plugin pre-scan so nothing is rewritten first and `rekey_form_links` per record on its working copy (`src/schema/form_links.cpp:remap_form_links`, `src/schema/form_links.cpp:rekey_form_links`), `identical_previous` skipping an undecodable record or an undecodable master record so neither is listed as ITM (`src/plugin/maintenance.cpp:identical_previous`), and `remove_records` refusing an undecodable target (`src/plugin/maintenance.cpp:remove_records`). `records_removed_by_delete` is read-only and is not guarded.

Guarded (D7): `src/forms/placed_reference_copy.cpp:copy_record_for_destination` calls `require_decodable` on the source record, which covers the source REFR, CELL, WRLD and INFO copies of all three copy functions. `src/forms/placed_reference_copy.cpp:replace_record` calls it on the overwrite target before the replacement. `src/forms/placed_reference_copy.cpp:copy_placed_reference_as_override` calls it on the destination WRLD after the world is ensured and on the destination CELL before its children group is ensured; `src/forms/placed_reference_copy.cpp:copy_placed_reference_as_new` calls it on the destination WRLD, when the cell is exterior, and on the destination CELL at the same point; `src/forms/info_copy.cpp:copy_info_as_new` calls it on the destination DIAL before its topic children group is ensured. `ensure_cell_children_group` and `ensure_topic_children_group` take a group rather than the parent record, so the parent check sits at each call site. The split that brought the file under the 750-line gate moved `Radicle/src/forms/info_copy.cpp:copy_info_as_new` and the helpers only it used into `info_copy.cpp`. Helpers both files use are declared in `Radicle/src/forms/placed_reference_copy_internal.hpp` (namespace `fo4::forms::detail`) and defined in `src/forms/placed_reference_copy.cpp`. The public declarations are unchanged.

Tests: `tests/test_undecodable_edit.cpp` covers `SetRecordScalarRefusesUndecodableRecord`, `SetWeatherMnamRefusesUndecodableWeather`, `SetRecordConditionsRefusesUndecodableRecord`, `SetRecordEditorIdRefusesUndecodableRecord`, `CopyAsOverrideRefusesUndecodableSource`, `CopyAsOverrideRefusesUndecodableOverwriteTarget`, `DeleteRefusesUndecodableRecord`, `DeleteRefusesOtherRecordWhilePluginIsUndecodable`, `RenumberRefusesUndecodableRecord`, `RenumberRefusesOtherRecordWhilePluginIsUndecodable`, `RemoveRecordsRefusesUndecodableRecord`, `FindIdenticalToMasterSkipsUndecodableRecord`, `PlacedReferenceCopyRefusesUndecodableSource`, and `PlacedReferenceOverwriteRefusesUndecodableTarget`. Each refusal test checks the throw, that the undecodable record is still undecodable with no subrecords, and that the plugin's output bytes (forced through the dirty write path) equal the original. `FindIdenticalToMasterSkipsUndecodableRecord` checks that an identical valid master override is listed and the undecodable one is not. The two placed-reference tests build an interior CELL with a REFR in its temporary children group; the overwrite test reaches the `replace_record` guard.

## tests/test_compression.cpp:CompressedExtendedSubrecordPreservesPhysicalMarker

The test preserves the physical `XXXX` marker and the expanded payload, while the verified runtime header reader presents only the next logical chunk. `OfficialMastersUseZlibAndPreserveBytes` reads the seven named masters and reconstructs the unmodified plugin from original record/group ranges; it skips when `FO4_DATA_DIR` is unset. Evidence: `docs/COMPRESSION_EVIDENCE.md`.

## src/c_api_records.cpp:radicle_record_topic_children / bindings/csharp/Radicle.Interop/RecordReads.cs:TopicChildren

TopicChildren returns the ordered physical INFO records in the topic-children
group immediately following the selected DIAL version. It does not substitute
winning INFO overrides or collect responses from other plugins or sibling topics.
The hierarchy source is include/fo4/plugin/container.hpp:Group and
src/plugin/child_groups.hpp:owned_child_group; the consumer behavior is verified in
GardenEngine's vendored Mutagen.Bethesda.Fallout4/Records/Major Records/DialogTopic.cs:
DialogTopicBinaryOverlay.CustomEnd. The group label must resolve to that DIAL in
the selected plugin's master context. Missing, empty or differently typed following
groups have no responses; mismatched labels, nested children and non-INFO children
fail closed. Recursive lookup supports DIAL records nested beneath quest groups.

The returned array uses radicle_free_record_array; record handles remain borrowed from
the supplied environment. Managed TopicChildren copies only the handle array via
the existing Records helper. Keep the environment alive while reading the children
and their payloads. No C struct layouts change, but the four new exports move `RADICLE_ABI_VERSION` to 5, so a library built from the ABI-4 headers is refused by the binding's version guard on its first call, before any new export is reached (`AbiGuardTests.cs:ALibraryFromBeforeTheRecordPayloadExportsIsRejected` uses the `radicle_abi_stub_v4` stub). Consumers require a native library containing the new exports. TopicReadTests.cs covers selected and
overridden versions, physical order, sibling separation, nested topics, master/self
keys, empty results, invalid owners/types and disposed-environment access. This
does not validate scene decoding, voice playback or menu fidelity.

The source-linked installed-data comparison passed 92,861 checks across Fallout4,
Robot, workshop01, Coast, workshop02, workshop03 and NukaWorld: exact physical
DIAL keysets, ordered child FormKeys and selected-plugin INFO versions matched
the independent Mutagen overlay for 46,427 topics and 102,462 responses. The
31-test binding suite passed against the candidate native library, including ABI
layout and mismatch tests; generated declaration/probe drift checks passed.
These counts establish hierarchy projection only. Lookup currently walks the
selected plugin hierarchy per request; no voice loading performance claim is made.

## src/c_api_records.cpp / bindings/csharp/Radicle.Interop/RecordReads.cs:ReadPayload, ResolveFormId

The Garden UI importer needs the existing parsed record's ordered subrecords and
the native load order's FormID resolution. These reads copy opaque payload bytes,
including duplicate/unknown fields and extended-size markers, without guessing
their meaning or changing the environment. ReadPayload returns managed-owned
bytes that survive environment disposal. ResolveFormId uses the selected record
version's plugin context, including master and self indices.

Verified against source:include/fo4/formid.hpp:RawFormId and
source:src/formid.cpp:resolve_plugin_link, ResolveFormId rejects an index beyond the source plugin's master table and
created/save IDs; null and the explicit none sentinel return no key. The existing
environment link helper otherwise coerces oversized indices to the source plugin,
so this new strict read validates before invoking it. Existing decoders are unchanged.
Unknown four-byte signatures use reversible Latin1 in managed snapshots.

A `radicle_record` handle is only meaningful to the environment that produced it: it
is a pointer to one of that environment's own `RecordRef` versions, and the plugin
index it carries is an index into that environment's plugin list. All ten exports
therefore call `src/c_api_env_internal.hpp:belongs_to_env` first, which looks the record's
FormKey up in `env->value.versions` and compares addresses. That is the same
pointer-identity test `radicle_record_topic_children` already used, now shared. The
package, combat-style and idle exports (`src/c_api_behavior.cpp:radicle_package_decode`,
`radicle_combat_style_get_info`, `radicle_idle_decode`) run the same check after their
null checks and before decoding, with the same `INVALID_ARGUMENT` status and message. A
rejected package or idle output is left unchanged, and a rejected combat-style output keeps its
caller value.
The dialogue decode exports (`src/c_api_dialogue.cpp:radicle_dialogue_topic_get_info`, `radicle_dialogue_info_decode`,
`radicle_scene_decode`) run the same check before any output is cleared, so a rejected INFO or SCEN output keeps its caller value.
`ReadPayload`, `CopySubrecord` and `ResolveFormId` did not have it. Without it a
handle from a second live environment resolved through the supplied environment:
`ResolveFormId` read `env->value.plugins().at(ref.plugin)` from the wrong plugin list
and returned a key built from another plugin's master table. The wrong answer is
plausible rather than obvious when both environments have a plugin at that index, so
`RecordReadTests.cs:ReadsRejectARecordFromAnotherLiveEnvironment` loads two plugins
that each define a record at local ID 0x800, checks the owner environment resolves it,
and requires the other environment to refuse both `ResolveFormId` and `ReadPayload`.

In `ResolveFormId` the membership test runs before the `RawFormId::is_none()` early
return, so the handle is validated for every `raw_form_id` and not only for the ones
that reach link resolution. `is_none()` is `value_ == 0xFFFFFFFFu`, so with the check
after it a foreign handle plus `0xFFFFFFFF` returned `RADICLE_STATUS_OK` with a null
key instead of being refused; `0` never took that path, because `is_none()` is false
for it. The test asserts refusal for `0x800`, `0` and `0xFFFFFFFF`, which covers both
the ordinary resolution path and the sentinel.

The C caller supplies four writable signature bytes. Copied payload buffers use
radicle_free_bytes; resolved plugin strings are borrowed until environment disposal.
As with existing record readers, keep the environment alive throughout a call;
Garden's environment read lease must cover a snapshot and its link resolutions.
No existing C layout changes. Consumers need a library containing these new exports.
The C# declarations are generated from c_api_records.h by the existing generator
and its drift check. UI field codecs, localization,
condition/script gates and original-payload parity remain separate integration work.

bindings/csharp/Radicle.Interop.Tests/RecordReadTests.cs verifies override choice,
duplicate unknown bytes, empty payloads, master/self/null/invalid FormIDs and
managed byte ownership after disposal. This is not a decoded UI-content claim.

## src/c_api_dialogue.cpp:radicle_dialogue_topic_get_info / radicle_dialogue_info_decode / radicle_scene_decode

These three exports take a `radicle_record` handle like the record readers above, so
they check it with `src/c_api_env_internal.hpp:belongs_to_env` before decoding, and
refuse a handle from another live environment with `RADICLE_STATUS_INVALID_ARGUMENT`
and the same message the record readers use. Without the check a foreign DIAL, INFO
or SCEN handle decoded through the supplied environment's plugin and link tables, the
same wrong-answer path `ResolveFormId` had. The helper moved from
`src/c_api_records.cpp` into the shared internal header so every caller uses one
definition. `Radicle/tests/test_dialogue_writer_c_api.cpp:DecodeRefusesRecordsFromAnotherEnvironment`
loads one plugin into two environments, requires both foreign decodes to refuse with
null outputs, and requires the owner environment to still decode the DIAL.

## src/schema/form_links.cpp:visit_link_offsets

Record-aware classification precedes generic signature-only handling for the current copy work.

## src/schema/form_links.cpp:visit_link_offsets (SCEN action Topic DATA)

`src/schema/form_links.cpp:visit_link_offsets` with `src/schema/non_formid_slots.cpp:scene_action_topic_data`: SCEN `DATA` is a Topic form ID only in a Dialogue (type 0) or Radio (type 6) action, `wbFormIDCk(DATA, 'Topic', [DIAL,NULL])` at source:xEdit Core/wbDefinitionsFO4.pas:9014 and :9096 @93cc0bc5. The action type is the last 2-byte `ANAM` before the subrecord (`src/schema/non_formid_slots.cpp:scene_action_type`), as xEdit reads it through `wbSceneActionTypeDecider` (source:xEdit Core/wbDefinitionsCommon.pas:5403 @93cc0bc5) and the `ANAM` 'Type' field at source:xEdit Core/wbDefinitionsFO4.pas:8956 @93cc0bc5. The empty closing `ANAM` marker (`wbMarkerReq(ANAM)`, source:xEdit Core/wbDefinitionsFO4.pas:9103 @93cc0bc5) has no 2-byte payload, so it ends the type for later subrecords. SCEN has no top-level `DATA`, so no other `DATA` is visited. `DATA` is not in the generic `formid_subrecords.inc` table, so before this rule a cross-plugin remap left topic links pointing at the old master index. Under strict remap a `DATA` in these actions whose payload is not 4 bytes throws instead of being skipped.

## src/schema/form_links.cpp:visit_coed_formids

`COED` is classified before the structured-slot table: owner at offset 0 always, offset 4 only when the owner record in the plugin being remapped is `NPC_`. Faction ranks are never FormIDs. Strict remap fails closed when owner type cannot be determined and the union dword is nonzero.

## src/schema/form_links.cpp:visit_coed_formids

Owner and union dwords are read before `fn(0)` because `remap_raw` rewrites offset 0 into the destination encoding; typing must use the pre-remap bytes with the old context.

## Radicle/src/schema/vmad.cpp:walk_property

The offset walker takes a `nested` flag. A struct (type 7) or struct array (type 17) reached while walking struct members throws `parse_error` ("nested VMAD struct"), so `scan_vmad` reports the record as failed and returns no offsets, the same fail-closed path as other malformed data. Recursion is therefore bounded at two struct levels on the scan path. The typed reader, `read_property`, still walks its own struct members with the default `nested` false, because the existing `test_vmad_array_elements.cpp:StructWithNonScalarMemberStaysDisengagedAndInSync` pins that a nested struct leaves the typed value disengaged and the stream in sync. Test: `Radicle/tests/test_vmad.cpp:NestedStructPropertyFailsClosedInTheWalker`.

## src/schema/vmad.cpp:inspect_quest_vmad

QUST extras after the primary scripts: bind byte, fragment count, optional fragment script, then fragments `{u16 Stage, u16 unused, i32 StageIndex, u8, wstring, wstring}`, then alias extras `{object, i16 version, i16 objFormat, scripts}`. Object format 2 stores unused u16, alias i16, FormID. Rewrite patches alias i16 (format 2 only) and Stage u16. StageIndex bytes are preserved.

## src/forms/weather.cpp:decode_weather (TNAM, DALC)

`src/forms/weather.cpp:decode_weather` keeps each sky-static TNAM slot as `std::optional<FormKey>` in `Weather::tnam`, so a NULL slot is absent rather than an error. This matches `decode_optional_form` for MNAM, NNAM, IMSP and WGDR, and keeps slot positions. xEdit allows NULL in that array (`source:xEdit Core/wbDefinitionsFO4.pas:13090-13092 @93cc0bc5`).

`src/forms/weather.cpp:decode_weather` reads each DALC payload of 24, 28 or 32 bytes into `WeatherAmbientColors`: six directional words always, Specular only when the payload has at least 28 bytes, and Fresnel only at 32. xEdit makes both optional from form version 30 inside the `SetOptionalFrom(1)` struct (`source:xEdit Core/wbDefinitionsCommon.pas:6635-6655 @93cc0bc5`, used by WTHR at `source:xEdit Core/wbDefinitionsCommon.pas:9485-9497 @93cc0bc5`). The reader decides by payload size and does not read the form version, so a 28 byte payload reports Fresnel absent at any version.

## src/forms/quest_vmad.cpp:sync_quest_vmad_stages

xEdit FO4/TES5 name the fragment i32 `Quest Stage Index`; Mutagen `QuestScriptFragment.StageIndex`; UESP VMAD names that slot `logentry`. xEdit PERK uses a separate `Fragment Index` for chronological fragment numbers; QUST chronological ids live in `FragmentName` (`Fragment_N`). Stage u16 is xEdit `Quest Stage` / UESP `fragment.index` = QUST INDX. StageIndex is not range-checked against QSDT count. INDX remap is identity, else a unique stage-body match excluding INDX; array position is never identity. Format-1 alias objects fail closed on rewrite.

## src/forms/quest_alias_edit.cpp:write_quest_aliases

Whole-array replace copies the plugin, rewrites VMAD alias object IDs against the planned alias set, then splices aliases. Duplicate/malformed/trailing/unsupported VMAD throws before commit.

## src/forms/quest_stages.cpp:write_quest_stages

Whole-array replace remaps fragment Stage by INDX identity or unique body match, then splices. StageIndex and unrelated VMAD bytes stay identical.

## src/forms/info_copy.cpp:copy_info_as_new

The contained copy validates its ancestry on a working copy before committing changes.

## form_links.cpp :: visit_link_offsets (WTHR)

WTHR reuses FormID signatures for floats (VNAM/WNAM), bitfields (NAM1), opaque LNAM, color/speed arrays, and mixed SNAM/UNAM payloads. Remap only proven FormLink slots from WTHR_FORMID_SLOTS.md before the global signature table; otherwise float 1.0 (0x3F800000) becomes master index 63.

## src/graph/check_errors.cpp:check_load_order

Whole-load-order "Check for Errors" (#359, xEdit parity). Findings come only from slots Radicle can already prove: `collect_form_links` hits, the generated FormLink field/list target tables, `schema::preflight_plugin`, and the leveled-list graph. Unknown subrecords are never scanned, so a clean result means "no proven problem", not "no problem".

- `invalid_master_index` (warning): `FormLinkHit::key` is empty because resolution failed. A raw index past the MAST table is not an error by itself: `resolve_plugin_link` (source:src/formid.cpp) maps it to the owning plugin, as the game does, and official masters ship such links (#365, `DLCRobot.esm` REFR `XRFG`). Those links are checked like any other link, so they are only reported when the owning plugin has no such record (`unresolved_link`).
- `0xFFFFFFFF` is xEdit's None (`TwbFormID.IsNone`): `collect_form_links` leaves its key empty and the checker skips it like a null link.
- Engine-hardcoded forms: a missing target owned by `Fallout4.esm` (named explicitly, not the first plugin in the load order) with an object ID below `0x800` is skipped, matching xEdit `wbInterface.pas:TwbFormID.IsHardcoded` (`dev-4.1.6`, `_FormID < $800`). The game creates those forms itself (for example `000014`, `000007`), so `Fallout4.esm` has no records for them.
- `unresolved_link` / `deleted_target`: checked against the winning version of every record in the load order (last plugin wins), not against the referencing plugin's own masters. An unresolved link is an error only when `slot_targets` proves the slot (a generated field or list descriptor). Slots found only through the signature-union table (`formid_subrecords.inc`) are reported as warnings, because xEdit declares some of them mixed (TERM `RNAM`/`WNAM`, INNR `WNAM`, MGEF `DNAM`, ...) and their scalar values do not resolve. With that rule vanilla `Fallout4.esm` has no error-level findings (`CheckErrors.VanillaMasterHasNoErrorFindings`).
- `wrong_target_type`: only when `slot_targets` matches a generated field (record + subrecord + offset) or a non-nested list (per-subrecord at offset 0, packed arrays at 4-byte steps, as `formlink_lists.cpp:packed_element_count`). Wildcard lists and nested lists are skipped. `CheckFinding::offset` is `FormLinkHit::offset` (`fo4/schema/form_links.hpp`), the byte offset inside the subrecord.
- `CheckOptions::plugins` limits which plugins' records are checked; the winner index still covers the whole load order.

## Radicle/src/graph/check_errors.cpp:check_preflight

Each `schema::preflight_plugin` issue becomes a `schema_preflight` finding. The message is `path: message` with no code suffix. The issue's `code` string (for example `duplicate_scalar_subrecord`, `scalar_value_out_of_range`) goes in `CheckFinding::detail`, so both problem paths expose the same shape. The C ABI `radicle_env_check_errors` (`src/c_api_check.cpp`) appends ` (<detail>)` to the message when `detail` is set, so its row text is unchanged. PET's Check for Errors (`PluginEditTool/native/src/plugin/RadiclePluginCheck.cpp`) and GetProblems (`PluginEditTool/native/src/plugin/RadiclePluginAnalysis.cpp`) emit it as `Detail`.

## src/graph/check_errors.cpp:slot_targets

Resolves the allowed target signatures for a `(record, subrecord, ordinal, offset)` slot. Generated FormLink fields are tried first through `src/schema/formlink_slots.cpp:formlink_field_at`. An unguarded field matches by its static offset (or stride), as before. A guarded field (variant, `FieldAddress::when`) matches only when `src/schema/field_address.cpp:resolve_field_slots` reports a valid slot at that offset, so the selector byte decides which variant is in force. When the selected variant is not a FormLink, or the selector is absent, no field matches and the slot stays unproven (`unresolved_link` is then only a warning). Generated FormLink lists are tried after fields, unchanged.

Guarded rows now exist (PACK `PTDA` TargetReference/TargetObject/TargetKeyword/TargetUnknownKeyword, selected by the type at offset 0). A guarded field is matched per subrecord instance: the hit's `FormLinkHit::subrecord_ordinal` picks the instance, and the resolved slot's `FieldSlot::subrecord` must be that instance (GT-228). Covered by `CheckErrors.VariantFormLinkSlotIsProvenOnlyForSelectedVariant`, `CheckErrors.RepeatedVariantSubrecordUsesItsOwnSelector` and `CheckErrors.RepeatedVariantSubrecordTargetsAreCheckedPerInstance` in `tests/test_check_errors.cpp`.

## src/graph/check_errors.cpp:find_leveled_list_cycles

Ported from `PluginEditTool/native/src/plugin/RadiclePluginLeveled.cpp:RadiclePluginBackend::checkCircularLeveledLists` and extended to LVSP. Edges are each leveled record's FormLinks in its winning plugin; non-leveled targets are ignored. A cycle is reported once, keyed by its rotation starting at the smallest FormKey, and the returned path repeats its first node at the end.

## src/c_api_check.cpp:radicle_env_check_errors

Rows are tab-separated so the existing `radicle_free_string_array` contract covers ownership. An empty subrecord column means the finding is record-level (preflight, cycle). Adding this export moved `RADICLE_ABI_VERSION` to 3.

## src/c_api_pex.cpp:radicle_pex_parse

The `.pex` C ABI (`include/fo4/c_api_pex.h`): parse bytes or a file into a `radicle_pex` handle, read the container header, per-object counts, object and parent class names and string table entries, and serialize back to bytes. Names and string table entries are handed back as caller-owned byte ranges released with `radicle_free_bytes`. The API never transcodes: the container stores one byte per character and the character encoding above `0x7F` is UNPROVEN, so Latin-1 stays an interpretation the C++ and C# layers apply rather than something the ABI assumes. Malformed bytes map to `RADICLE_STATUS_PARSE_ERROR` carrying the reader's own message, a missing file to `RADICLE_STATUS_IO_ERROR`, and a null argument or out-of-range index to `RADICLE_STATUS_INVALID_ARGUMENT`. Adding these exports took `RADICLE_ABI_VERSION` to 6, one step after the typed record payload exports that hold 5 on main; `RadicleNative.g.cs` was regenerated for the constant and `abi_layout_probe.cpp` is unchanged, because `fo4/c_api_pex.h` is not in the generator's header list yet. Raw PEX interop joins that list with the managed PEX model in A4.

## src/c_api_quest.cpp:radicle_quest_runtime_rows

The quest runtime C ABI (`include/fo4/c_api_quest.h`) splits one QUST record's VMAD and DNAM into structure and values. `radicle_quest_runtime_rows` reports the structure as tab-separated rows (`quest`, `script`, `fragment-script`, `fragment`, `alias`, `alias-script`, `unsupported`) so the existing `radicle_free_string_array` contract covers ownership. `radicle_quest_runtime_properties` reports every script property as a `radicle_quest_property` carrying its owner, owning script, name, type, flags and decoded value, released with `radicle_free_quest_properties`.

Properties are not rows because a VMAD value does not fit the row grammar: a string value can hold a tab or a newline, and an object property carries a FormID and an alias at once, so a tab-separated row cannot carry one losslessly. The reader already decoded every value into `fo4::VmadProperty` and the rows dropped all of them, which made two quests differing only in an integer property decode identically. The regression is pinned at both layers: `Radicle/tests/test_c_api_quest.cpp:RowsCarryNoPropertyFacts` and `ReportsEveryPropertyValue`, and `Radicle.Interop.Tests/QuestRuntimeTests.IntegerPropertyValuesAreDistinguishable`.

The scalar and reference value fields (`integer`, `real`, `boolean`, `object`, `property_alias`, `element_count`) each have a `has_` companion because a property carries at most one value of its type; `element_count` is the leading count of a struct or array property. The quest batch is `RADICLE_ABI_VERSION` 8, after the record payload at 5, PEX at 6 and SEQ at 7, and it is unreleased, the same rule `src/c_api_maintenance.cpp` used to stay at 3 when its exports were added. On the managed side `Radicle.Interop/QuestRuntime.cs` maps the entries onto `ScriptProperties`, `FragmentProperties` and `AliasScriptProperties`, and `DecodeRows` takes the rows and the entries together so the mapping is testable without a native plugin.

`radicle_quest_property.text` is transferred with a length. `text_size` counts the exact raw bytes and `text` points at those bytes plus one trailing NUL for convenience, so a value holding an embedded NUL (the 12 bytes Garden, NUL, Patch) arrives as `text_size` 12 while a NUL-terminated read of `text` sees only 6. A NULL `text` is no string; a non-NULL `text` with a `text_size` of 0 is the empty string. The bytes carry no claimed character encoding. `src/c_api_quest.cpp:copy_text` takes the byte count and `memcpy`s it, never `strlen`, so the transfer is lossless. Script and property names go through `src/c_api_quest.cpp:copy_c_string` instead, which is still `strlen`-based, and the managed side still reads both with `Utf8.Read` (`Marshal.PtrToStringUTF8`), so an embedded NUL in a name would still truncate there; whether a shipped plugin can contain one is UNPROVEN, and this pass did not change either. The regression is pinned by `Radicle/tests/test_c_api_quest.cpp:KeepsEmbeddedNulBytesInAPropertyString`, `EmptyStringPropertyDiffersFromNoString` and `Radicle/tests/test_quest_runtime.cpp:KeepsThePropertyValuesTheCAbiHasToCarry`, and on the managed side the lossless transfer itself is pinned by `Radicle.Interop.Tests/QuestRuntimeTests.EveryValueKindSurvives`, which reads a synthetic plugin through the native library; `QuestRuntimeAttachmentTests.EmbeddedNulBytesSurviveTheRowMapping` builds its entries by hand and covers the row mapping, so it cannot fail if the native transfer regresses. `QuestRuntimeValue.TextBytes` is derived from the stored `Text` with `Encoding.Latin1.GetBytes` rather than stored, so the record struct keeps structural value equality. Adding `text_size` did not bump `RADICLE_ABI_VERSION` past the quest batch's 8, on the same unreleased-batch rule.

A fragment property belongs to the fragment script the `fragment-script` row names, which is the contract `include/fo4/c_api_quest.h` already stated, so its own `script` field is an empty string and the script name is transferred exactly once, through that row and `QuestRuntimeData.FragmentScript`. `src/c_api_quest.cpp:radicle_quest_runtime_properties` had passed `data.fragment_script` for every fragment property and `Radicle.Interop/QuestRuntime.cs:DecodeRows` had substituted the row's name as well, so the name arrived twice and the header described something the code did not do. The non-duplicated contract wins, and it is pinned by `Radicle/tests/test_c_api_quest.cpp:FragmentPropertyCarriesNoScriptNameOfItsOwn` and `Radicle.Interop.Tests/QuestRuntimeAttachmentTests.FragmentPropertiesKeepTheirOwnScriptField`.

## src/schema/non_formid_slots.cpp:corpus_non_formid_slot

`formid_subrecords.inc` is a signature-only union of xEdit's FO4 form ID signatures, so a signature that is a form ID in one record type was treated as one everywhere (DIAL `PNAM` priority float, DIAL `SNAM` subtype code, SNDR `CNAM`, PACK `CNAM`, NPC_ `NAM4`, ...). #365: `remap_plugin_masters` rewrote those bytes whenever the master list changed. The deny list (`include/fo4/schema/non_formid_slots.inc`) is generated by `tools/corpus/non_formid_slots.py` from two sources that must agree for every corpus row, plus a static xEdit pass for the slots the corpus cannot see (next paragraph): an official master stores a non-null value at that (record, subrecord) that resolves to no record in the official load order (its master index is past the file's MAST table, or the target does not exist), AND xEdit `wbDefinitionsFO4.pas` (`dev-4.1.6`) declares that subrecord in that record only as scalar data (`wbFloat`/`wbInteger`, the string types including `wbLStringKC` localized string IDs, the RGBA and color helpers, a struct with no `wbFormID`, or a helper whose whole right-hand side, recursively, has no `wbFormID`/`wbUnion`; `wbPTRN := wbFormIDCk(...)` and the `wbVMAD*` helpers are therefore form IDs). In-table values matter as much as past-table ones: SCEN `WNAM` editor width 350 is `0x0000015E`, index 0, and was rewritten to `0x0100015E` when master 0 moved (`RemapNonFormIdSlots.InTableScalarsSurviveMasterReorder`). Localized string IDs are the same case: TERM `WNAM` welcome text ID `0x0000ABCD` became `0x0100ABCD` (`RemapNonFormIdSlots.LocalizedStringIdsSurviveMasterReorder`). `wbUnknown` slots count as scalar too, because xEdit never remaps bytes it has not decoded (TACT `PNAM`, PACK `BNAM`). Helpers are also read from `wbDefinitionsCommon.pas` (optional fifth generator argument), and a helper is judged by the element that declares the subrecord, not as a whole: `wbWorldLODData` holds NAM3 (water form ID) and NAM4 (`LOD Water Height` float), so only WRLD `NAM4` is denied. `remap_raw` and `rekey_form_links` also leave `0xFFFFFFFF` alone: it is xEdit's None value (`wbInterface.pas:TwbFormID.IsNone`), stored in vanilla EQUP `ANAM`, and was rewritten to `0x01FFFFFF` (`RemapNonFormIdSlots.NoneSentinelAndUndecodedSlotsSurviveMasterReorder`). The corpus evidence alone is not enough either: vanilla holds real links past the table (`DLCRobot.esm` REFR `XRFG`, see `GARDEN-TOOLS.md` on `resolve_plugin_link`), which the game reads as the owning plugin's, so `remap_raw` keeps remapping them. PACK `CNAM` is mixed in xEdit (record-level `Combat Style` form ID, and the `Data Input Values` union of bool/int/float after an `ANAM` type string), so `corpus_non_formid_slot` denies only a PACK `CNAM` whose previous subrecord is `ANAM`. SCEN `TNAM`/`HTID` depend on the scene action (`scene_action_scalar`): an action opens with a 2-byte `ANAM` type and closes with the empty `ANAM` marker. Inside an action `TNAM` is the Timer float and `HTID` is the Dialogue headtracking actor-ID array (or the empty Start Scene marker), so both stay scalar; only `HTID` in a Radio action (type 6, `Play Sound` SNDR) and the record-level `TNAM` (`Template Scene`, after the actions) are form IDs. It only removes slots from the generic branch of `form_links.cpp:visit_link_offsets`; record-specific handlers (WTHR, QUST, NAVM, structured slots) are unchanged. `tools/corpus/non_formid_slots.py:main` also merges `XEDIT_ONLY_SCALAR_SLOTS`, a hand list of slots that no official master value exercises but that xEdit types as strings: FSTP `ANAM` (`source:xEdit Core/wbDefinitionsFO4.pas:8689 @93cc0bc5`, `wbString(ANAM, 'Tag')`), SNDR `NNAM` (`wbDefinitionsFO4.pas:9408`, `wbString`), SNDR `FNAM` (`wbDefinitionsFO4.pas:9446`, `wbString`), and SNDR `ANAM` (`wbDefinitionsFO4.pas:9416` uses `wbSoundDescriptorSounds`, which is `wbRArray('Sounds', wbString(ANAM, 'Sound'))` at `source:xEdit Core/wbDefinitionsCommon.pas:9200 @93cc0bc5`), plus MUST `ANAM` and `BNAM` (`wbDefinitionsFO4.pas:8806-8807`, `wbString`), MUST `FNAM` (`wbDefinitionsFO4.pas:8813`, an array of `wbFloat` cue points) and SOPM `MNAM` (`wbDefinitionsFO4.pas:9505`, `wbInteger` type), checked by `RemapNonFormIdSlots.SopmAndMusicTrackFourByteSlotsStayUnchanged`. Those rows skip the corpus check and are tagged `// xedit` in the output, so a regeneration keeps them. Regenerate it after adding new handlers or when new official files ship.

`tools/corpus/non_formid_slots.py:main` also denies every (record, subrecord) pair that xEdit types as scalar in that record, in every definition of it. Corpus evidence cannot see enum and flag values: values 1 to 7 resolve to vanilla records 000001 to 000007, so the no-record test never fires, and AACT `TNAM`, CLFM `FNAM`, DLBR `DNAM`/`TNAM`, ARTO `DNAM`, COLL `GNAM`, NPC_ `NAM8`, PROJ `VNAM`, REVB `ANAM`, MATT `FNAM`, MESG `DNAM`, SMBN/SMEN `DNAM` were rewritten on master changes. `static_scalar_pairs` needs no game data: it runs `slot_is_scalar` over every block from `record_blocks` and every signature in `formid_subrecords.inc`, and `main` merges the pairs the corpus did not deny as `// xEdit`. `--check-static` lists any pair missing from the tables. `record_blocks` ends each block at its own `wbRecord(` call. Before that, a block ran to the next `wbRecord(`, so the last record absorbed the trailing helper code; the static pass then denied ZOOM `ANAM` and four other slots (EYES `RNAM`, NPC_ `BNAM`, QUST `ZNAM`, RACE `TNAM`) that xEdit defines elsewhere. No corpus row changes verdict. The 120 pairs added by this change are `XEDIT_DENIED` rows with their xEdit line. Tests: `RemapScalarEnumSlots.EnumAndFlagValuesSurviveMasterReorder` (`tests/test_remap_scalar_enum_slots.cpp`) and `tools/corpus/test_non_formid_slots.py`, run by the CI step "Check static xEdit scalar deny pass".

## src/plugin/maintenance.cpp:find_identical_to_master

Walks every group, nested ones included, bottom-up like xEdit `xeMainForm.pas:mniNavRemoveIdenticalToMasterClick` (`dev-4.1.6`): a record is reported only when it is identical to the previous version in load order AND its owned child group (`owned_child_group`: the world, cell or topic children group directly after it) would be emptied, so children are reported before their parent. An identical CELL with a changed REFR is therefore not reported, because removing it would orphan the cell's children. Injected records are not special-cased. A record is never ITM when its placement differs from the previous version (`record_placements`): moving a REFR to another CELL or an INFO to another DIAL is a real edit even when the bytes are identical. `remove_records` erases the reported keys anywhere in the tree and drops only the groups that this call emptied.

## src/graph/plugin_diff.cpp:diff_plugins

`changed_subrecords` holds each differing signature once, sorted. Payloads are first compared per signature in order. When that finds nothing but the ordered subrecord sequence still differs (a pure reorder across signatures), `add_reordered` trims the common prefix and suffix and reports every signature left in either window. The TES4 header is a record here, so master or HEDR changes show up as a changed `TES4`. `placement_changed` marks a record whose placement differs (`record_placements`), so a REFR moved between cells with identical bytes is reported as changed.

## src/c_api_maintenance.cpp:radicle_env_identical_to_master

#360 C ABI for the maintenance APIs, in its own header (`include/fo4/c_api_maintenance.h`) because `c_api_env.h` is near the line limit. `radicle_env_identical_to_master` passes the load order up to and including the plugin to `find_identical_to_master`; rows are `record	signature	previous`. `radicle_env_deleted_counts` wraps `count_deleted_records`. `radicle_env_unused_masters` lists masters missing from `used_masters`; when `scan_master_usage` is incomplete `used_masters` keeps every master, so the list is empty and `out_complete` is 0. `bindings/csharp/tools/generate_interop.py` reads the header and the layout probe now includes every header after `c_api_env.h`. ABI stays 3: these exports ship in the same unreleased version as `radicle_env_check_errors`.

## tools/inspect/main.cpp:print_udr

`fo4inspect udr` lists deleted placed references (the records xEdit's Undelete and Disable References would fix) plus the deleted-record and deleted-navmesh counts from `count_deleted_records`.

## src/graph/reachability.cpp:find_unreachable

Port of xEdit "Build Reachable Info" (#364) for Fallout 4, from `wbImplementation.pas` (`dev-4.1.6`): `TwbFile.BuildReachable` (roots), `TwbMainRecord.Reached`, `TwbGroupRecord.Reached` and `TwbElement.Reached` (propagation).
- Roots: engine-hardcoded forms (`Fallout4.esm` by name via `src/graph/reachability.cpp:k_game_master`, object ID below 0x800); every record in ADDN, ANIO, AVIF, CAMS, COBJ, CPTH, DFOB, DLVW, DOBJ, GMST, IDLE, LSCR, NAVI, RADS; HDPT `DATA` bit 0 (Playable); PERK `DATA` byte 3 (Playable, when the subrecord has 4+ bytes); NPC_ `ACBS` flag 0x4 (CharGen face preset); QUST `DNAM` flag 0x1 (Start Game Enabled); RACE `DATA` flags bit 0 (Playable) at offset 8, or 32 from form version 109. Layouts from `wbDefinitionsFO4.pas`.
- Propagation: every form link of the winning version, and of every version of a complex record (DIAL, WRLD, CELL, DOBJ), because xEdit's `TwbMainRecord.Reached` also reaches a complex record's master and overrides and each version follows its own links; a reached CELL/WRLD/DIAL reaches every record in its child group, and a reached child reaches its parent (xEdit's group `Reached` for types 1 and 6-10). Reverse rules: NPC_ reaches RELA that link it; QUST reaches SCEN whose `PNAM` and DLBR/DIAL whose `QNAM` link it; SMBN/SMQN/SMEN reach story-manager nodes whose `PNAM` links them; a FURN with `WBDT` bench type > 0 reaches COBJ whose `BNAM` is one of its keywords.
- NoReach (`src/graph/reachability.cpp:no_reach`): links xEdit declares with `wbFormIDCkNoReach` / `wbFormIDNoReach` are not followed forward (`TwbElement.Reached` skips them), but stay in the incoming index the reverse rules read. Fallout 4 table, from `wbDefinitionsFO4.pas` and `wbDefinitionsCommon.pas` (`dev-4.1.6`): whole subrecord DIAL `QNAM`, SMBN/SMQN/SMEN `PNAM` and `SNAM`, DLBR `QNAM`, DLVW `QNAM` and `BNAM`, INFO `TPIC` and `PNAM`, COBJ `BNAM`, REGN `WNAM`; CTDA offsets 12, 16 and 24 (both parameters and the Run On reference; the offset-4 comparison global still reaches); COED and XOWN offset 0; XLOC offset 4 (key); ECZN `DATA` and FACT `XNAM` offset 0; MISC `CVPA` and COBJ `FVPA` components (offset 0 of each 8-byte entry); PLDT/PLVD offset 4 for location types 0, 1 and 4, PTDA offset 4 for target types 0 and 1. `wbActorValue` is NoReach too, but every AVIF is a root, so it is not listed. Tests: `test_reachability.cpp:NoReachLinksDoNotReachTheirTarget`, `AComplexRecordFollowsTheLinksOfEveryVersion`.
- Differences from xEdit: SCEN must link this quest in `PNAM` (xEdit accepts any `PNAM` link), and only links Radicle can prove are followed (`collect_form_links`), so slots it cannot decode can make a record look unreachable. Results have not been compared against an xEdit run. Vanilla `Fallout4.esm` alone reports 44,175 records (mostly REFR/INFO in unreached interior cells and topics) in about 80 s.

## src/plugin/maintenance.cpp:esl_eligibility

Native records are the plugin's own (owner is the plugin, TES4 excluded); `out_of_range` counts native object IDs above `k_light_local_mask` (0xFFF) or below the target floor (0x001 when `allows_hardcoded_range` holds, else 0x800), and `ids_fit_light_range()` is true when there are none. That method says nothing about the TES4 light flag, which is why it is not called `already_light`. `capacity` is 0xFFE native records, the limit in xEdit's eligibility script (`Build/Edit Scripts/Find ESP plugins which could be turned into ESL.pas` at `93cc0bc5`: line 7 `iESLMaxRecords = $ffe`, line 8 `iESLMaxFormID = $fff`). xEdit's allocator lets a Fallout 4 plugin use IDs below 0x800 only when its HEDR version is at least 1.0 and it has a master (`Core/wbImplementation.pas` at `93cc0bc5`, `TwbFile.GetAllowHardcodedRangeUse`, lines 4070-4090). `src/plugin/mutation.cpp:allows_hardcoded_range` holds that rule; when it is false `capacity` is 0x800 (IDs 0x800-0xFFF) and `local_form_id_range` gives a light plugin `[0x800, 0xFFF]` (see `Radicle/docs/ESL_FORMIDS.md`). Tests: `test_maintenance.cpp:EslEligibilityKeepsTheFullFloorWithoutHedrOneAndAMaster`, `test_mutation.cpp:LightPluginKeepsTheFullFloorWithoutHedrOneAndAMaster`. The below-floor count lives here, so `radicle_env_esl_eligibility` and `PluginEditTool/native/src/plugin/RadiclePluginEsl.cpp:checkEslEligibility` report the same `out_of_range`, including a masterless plugin's IDs below 0x800. `capacity` follows the xEdit script (0xFFE, line 7 above), while the backend compactor's capacity is the slot count of its target range (4095 for 0x001-0xFFF, 2048 for 0x800-0xFFF); the two answer different questions and are deliberately not unified. Tests: `test_maintenance.cpp:EslEligibilityFollowsTheXEditRecordLimitAndIdCeiling`, `test_maintenance.cpp:EslEligibilityCountsNativeIdsBelowTheTargetFloor`, `test_c_api_maintenance.cpp:EslEligibilityMatchesTheTargetFloorForMasterlessPlugins`, `RadiclePluginEslTests.cpp:Native ESL eligibility counts masterless IDs below the full floor`. C ABI: `radicle_env_esl_eligibility` (layout unchanged).

## src/plugin/placement.cpp:record_placements

A record's placement is its parent record (resolved from the label of the enclosing world, cell or topic children group, as `environment.cpp:Environment::index_group` does) plus the type of the group that directly holds it (persistent, temporary, visible-when-distant, topic children, top level). Labels are resolved in each plugin's own master context, so the same parent compares equal across plugins with different master lists.

## src/plugin/onam.cpp:participates_in_fo4_onam

The record families come from xEdit `Core/wbDefinitionsFO4.pas` (`dev-4.1.6`), TES4 `ONAM` "Overridden Forms": ACHR, DIAL, DLBR, INFO, LAND, NAVM, PARW, PBAR, PBEA, PCON, PFLA, PGRE, PHZD, PMIS, REFR, SCEN. `PARW` (arrow projectile, commonlib_NonVR `ENUM_FORM_ID::kPARW`) was missing and `ACRE` (no Fallout 4 form type) was listed instead, so rebuilding ONAM dropped valid PARW overrides (`Onam.Fo4ParticipantsIncludeParwAndNotAcre`, `Native UpdateOnam stages a missing ONAM for a master-flagged plugin`). The writer calls the same rebuild for every dirty master-flagged plugin, so this affected ordinary saves as well as Update ONAM.

## PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp:RadiclePluginBackend::compactToEsl

The target range comes from `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslSlots`, which applies `fo4::local_form_id_range` to a light-flagged copy of the header: 0x001-0xFFF when `src/plugin/mutation.cpp:allows_hardcoded_range` holds (HEDR at least 1.0 and a master, xEdit `93cc0bc5` `Core/wbImplementation.pas:TwbFile.GetAllowHardcodedRangeUse`, lines 4070-4090), else 0x800-0xFFF. `checkEslEligibility` uses the same helper, so both report the same range text and slot count. The renumber pass runs on a copy with the light flag set, then restores the original header flags before staging, so compaction never sets the serialized ESL flag. A plugin with masters can come out as a full plugin holding IDs 0x001-0x7FF. Existing in-range IDs stay put; IDs below the floor or above 0xFFF are rekeyed in ascending order into free slots by `src/plugin/mutation.cpp:renumber_local_record`. Out-of-range nested records and out-of-range top-level records that have form ID labelled child groups are refused before anything is staged, by `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers`: `NestedRecordCompactionUnsupported` for nested records, `ParentGroupRelabelUnsupported` for parents. A full (not light-flagged) plugin holding own IDs below 0x800 must be flagged light before the game loads it (`PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:lightFlagHint`), because `AddCompileIndex` only remaps IDs >= 0x800 (source: RE repo `ReverseEng/commonlib_NonVR` @ `dbaeca7`, `RE-Work/Runtime/03-FINDINGS.md`, section "FormID compile-index remap decoded"; open question GT-055 on the GARDEN-TOOLS Issue Tracking wiki; RE-016 (GARDEN-TOOLS #520 comment 13469) verifies that AddCompileIndex skips IDs 1 to 0x7FF on OG (0x1549F0), NG (0x2BEFB0) and AE (0x3136E0)).

When the TES4 light flag is not set, the compaction success, HEDR-lowering and already-ESL-compatible messages of `compactToEsl` and `RadiclePluginEsl.cpp:checkEslEligibility` end with `RadiclePluginSupport.hpp:lightFlagHint` (through `lightFlagAdvice`, which adds nothing for a plugin that is already light): "Run masters.SetLight to flag the plugin light; Radicle does not flag it from a .esl file name." (`PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp:RadiclePluginBackend::setLight`). Radicle's save never sets the light flag from the file extension; only `src/plugin/writer.cpp:make_empty_plugin` flags a new .esl, and the reader decides light from the TES4 flag alone.

HEDR next object ID: `src/plugin/mutation.cpp:renumber_local_record` raises it to the new local ID + 1 only when the new ID is at or above the stored value's low 24 bits (`if (new_local.value() >= (next.header.next_form_id & k_full_local_mask))`). `compactToEsl` sets it to the last assigned ID + 1 with `src/plugin/mutation.cpp:set_hedr_next_form_id` (declared in `Radicle/include/fo4/plugin/mutation.hpp`), inside the same transaction. That is xEdit's rule at `93cc0bc5`: `xEdit/xeMainForm.pas`, `TfrmMain.mniNavRenumberFormIDsFromClick` (line 13054), tracks the highest newly assigned ID (`HighFormID`, lines 13216, 13298-13299, 13333-13334), and its nested `UpdateNextObjectID` stores that + 1 (lines 13367-13373). Records that were already in range do not feed it; `reserve_local_form_key` scans upward past them. At full capacity the value is 0x1000. xEdit's increment behaviour for a light FormID at 0xFFF is not in the pinned excerpts (unverified), so the 0x1000 sentinel is Radicle's rule.

`Radicle/src/plugin/flags.cpp:set_plugin_light_flag` with light set calls `Radicle/src/plugin/mutation.cpp:lowered_light_next_form_id`. When every own ID is within the light range, HEDR next (low 24 bits) is above 0xFFF and above the highest own ID + 1, next is lowered to the highest own ID + 1 (or to the light floor when there are no own records). Without this, `src/plugin/mutation.cpp:reserve_local_form_key` refuses every new record in the light plugin. `compactToEsl`'s all-in-range branch and `PluginEditTool/native/src/plugin/RadiclePluginEsl.cpp:checkEslEligibility` use the same helper; eligibility reports "would only lower HEDR nextFormID". xEdit's `Core/wbImplementation.pas` `TwbFile.SetIsLight` (lines 6248-6266 at `93cc0bc5`) sets only the header flag and does not change next. Its `TwbFile.NewFormID` masks next to 12 bits for light files (`Mask := $FFF`, line 5211 at `93cc0bc5`), which Radicle's allocator does not. `PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp:RadiclePluginBackend::setLight` reports the lowering ("lowered HEDR nextFormID to 0x..."). Tests: `Radicle/tests/test_plugin_flags.cpp` `PluginFlags.LightFlagLowersHedrNextToOnePastTheHighestOwnId`, `LightFlagKeepsHedrNextThatIsAlreadyInRange`, `LightFlagLowersMasterlessHedrNextToTheFullFloor`, `ClearingLightFlagNeverChangesHedrNext`; `RadiclePluginCompactTests.cpp` `Native SetLight lowers an out-of-range HEDR next ID to one past the highest own ID`; `RadiclePluginEslTests.cpp` `Native ESL eligibility reports a HEDR-only lowering when every record is already in range`.

`src/plugin/mutation.cpp:reserve_local_form_key` compares the HEDR next value masked to its low 24 bits (the 0xFF prefix shipped plugins store is dropped, RE-016, GARDEN-TOOLS #520 comment 13469) and treats next = ceiling + 1 as a full plugin and reports that no local ID remains; only a next value beyond that is reported as outside the range. Tests: `RadiclePluginMaintenanceTests.cpp`: `Native CompactToEsl remaps top-level records and their in-plugin references` (next value, ESL message), `Native CompactToEsl leaves a light plugin that can still create records`, `Native CompactToEsl at full capacity leaves no ID and creation fails cleanly`; `test_mutation.cpp:ReserveLocalFormKeyReportsAFullLightPluginAndAnOverflowedNextSeparately`; `RadiclePluginEslTests.cpp` covers zero, 0x001, 0x7FF, 0x800, 0xFFF, 0x1000, all 4095 target slots and transactional rejection of 4096 records.

## src/plugin/mutation.cpp:renumber_local_record

A nested record is refused with a `FormIdError` (`mutation.cpp:renumber_local_record`). Top-level records are rekeyed in place, and their `form_key` changes. The child group that follows is not changed by this function. xEdit changes that group in the same step: `Core/wbImplementation.pas` at `93cc0bc5`, `TwbMainRecord.SetLoadOrderFormID`, lines 14294-14295 and 14332-14333.

`src/plugin/mutation.cpp:renumber_local_record` throws `FormIdError` ("renumbering a record with form ID labelled child groups is disabled until parent GRUP label rewriting is proven") when `has_form_id_labelled_child_group` (declared in `include/fo4/plugin/mutation.hpp`; the group types come from `include/fo4/flags.hpp:group_label_is_form_id`: world, cell and topic children and cell persistent, temporary and visible-distant children) finds a group labelled with the record's key. The check runs after the unchanged and target-exists returns and before `rekey_form_links`, so the plugin is unchanged. xEdit relabels the child group in the same step (`Core/wbImplementation.pas` `TwbMainRecord.SetLoadOrderFormID`, lines 14294-14295 and 14332-14333 at `93cc0bc5`); Radicle refuses until relabelling is built and tested. `PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp:compactToEsl` does not call `parentGroupRelabelRefusal`. It uses `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers` and `parentGroupRelabelError`. The two callers of `RadiclePluginSupport.hpp:parentGroupRelabelRefusal` are `PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp:renumberRecord` and `PluginEditTool/native/src/plugin/RadiclePluginRenumber.cpp:renumberPluginFormIds`. Both return `ParentGroupRelabelUnsupported`: "Cannot renumber <key>: it has child groups labelled with its form ID; parent GRUP label rewriting is not supported yet." `renumberRecord` calls the refusal only when the new key differs and is free, so a same-key renumber still reports `changed:false` and an occupied target still reports `RecordFormIdExists`. `renumberPluginFormIds` runs its `RenumberTargetOccupied` check first, then the refusal loop, also on its dry run. That occupied check cannot fire today: the mappings assign consecutive IDs to the sorted own records, so a record that is not moving keeps its own index and no other mapping can target it. Ordering tests: `RadiclePluginCompactTests.cpp` `Native RenumberRecord to the same key reports unchanged for a DIAL with child groups` and `Native RenumberRecord onto an occupied key reports RecordFormIdExists for a DIAL with child groups`; WRLD with world children: `Native CompactToEsl refuses an out-of-range WRLD with world children and leaves the file unchanged` and `Native RenumberRecord refuses a WRLD with world children and leaves the file unchanged`. Tests: `Radicle/tests/test_record_lifecycle.cpp` `RecordLifecycle.RenumberRefusesDialWithTopicChildrenWithoutMutation` and `RecordLifecycle.RenumberRefusesWrldWithWorldChildrenWithoutMutation`; `PluginEditTool/native/tests/unit/RadiclePluginCompactTests.cpp` (CompactToEsl, RenumberRecord and RenumberPluginFormIds refusals).

## PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers

`PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers(plugin, first, last)` returns the own out-of-range records that are nested, or are top-level with a form ID labelled child group. `compactToEsl` and `RadiclePluginEsl.cpp:checkEslEligibility` both run it after the capacity check, so eligibility never says "can fit by remapping" for a plugin compaction refuses. Eligibility reports "would be BLOCKED: N record(s) cannot be remapped here (<count> <SIG> nested | with child groups)". It calls `has_form_id_labelled_child_group` once per out-of-range top-level record (O(records x groups)), bounded by the 0xFFE capacity check and small next to the plugin copy in each `renumber_local_record`. Tests: `RadiclePluginCompactTests.cpp`.

## src/environment/base_records.cpp: the HLOD and water record fields

GardenEngine's object HLOD and water layers read five things Radicle did not decode, so porting them off the removed record library could not compile. They are decoded here rather than in a second record stack, and the exports moved `RADICLE_ABI_VERSION` to 4.

- STAT `MNAM`: up to four 260-byte slots, each a NUL-terminated mesh path followed by unused bytes (`wbDefinitionsFO4.pas`, `wbStruct(MNAM, 'Distant LOD', [wbStringForward('Level 0', 260), ...])`). A slot with no terminator inside its own 260 bytes is a decode error rather than a read past the end into the next slot.
- ACTI `WNAM`: the water type.
- WATR: `FNAM` flags, the `DNAM` visual block through Screen Space Reflections, the three noise layers in their stored interleave (wind direction, speed, amplitude scale, UV scale, falloff, as in `wbDefinitionsFO4.pas`; decoded by `src/environment/base_records.cpp:decode_water`), the `NAM2`/`NAM3`/`NAM4` noise textures, and `NAM0`/`NAM1` velocities. `DNAM` is required from 188 bytes and may end anywhere up to 201, matching `SetOptionalFrom(4)`; the silt and reflection tail is optional, and silt is decoded only when all 12 of its bytes are present (`docs/code-notes/radicle-water-writer.md`). `radicle_water_get_info` no longer reports silt for DNAM sizes 192–199 (it now needs the full member at ≥ 200, as xEdit does); no official record is affected.
- WRLD `NAM3`/`NAM4`: the LOD water and its height (`wbDefinitionsCommon.pas`, `wbWorldLODData`).
- CELL `XCWT`: the cell's water type.

`c_api_env.h` was at the file-size limit, so the typed record decoders moved to `include/fo4/c_api_records.h`, which includes the environment header for the shared types; `generate_interop.py` reads it, and the generated bindings and layout probe are regenerated from the headers, so `radicle_csharp_interop_up_to_date` still holds. Tests: `Radicle.Interop.Tests/HlodFieldTests.cs` builds a plugin with these subrecords and checks every field, including a STAT without `MNAM` and the four-slot ceiling.
# Dialogue and scene read slice

`src/environment/dialogue.cpp:decode_dialogue_topic`, `decode_dialogue_info` and `decode_scene` use the pinned disk layouts and measured marker grammar in `docs/DIALOGUE_SCENES.md`. Scope readers keep repeated scene and response tags separate. Scene action payloads retain ordered opaque subrecord views. Conditions reuse `schema/ctda.cpp:read_ctda_conditions_in` without executing them; reset values and flags retain their encoded values. Unknown data is preserved, and malformed known fields fail closed.

`src/c_api_dialogue.cpp:radicle_dialogue_info_decode` and `radicle_scene_decode` own all returned arrays and copied condition strings until the matching free call; record-backed strings, keys and opaque bytes require the environment to remain alive. `bindings/csharp/Radicle.Interop/Dialogue.cs` copies every returned array, key, string and opaque payload before releasing the holder. `StringTables.ResolveDialogueText` uses the existing source-selection and lookup contract rather than guessing text from IDs. Runtime semantics remain tracked by RE #405 and TES4 bit 7 by #29.

## CMakeLists.txt:RADICLE_CSHARP_TESTS

The two native jobs in `.github/workflows/native-ci.yml` enable the existing
managed test switch, with .NET 8 supplied by setup-dotnet. CI exercises the
sound getters against this branch's native library, ABI probe and stubs.

## Package, combat-style and idle read slice

`src/environment/behavior.cpp:decode_package`, `decode_combat_style` and `decode_idle` follow `docs/PACKAGES_COMBAT_IDLES.md`. Package prelude links/conditions stop at PKCU; post-PKCU streams retain opaque data, and begin/end/change idle links use separate scopes. Combat arrays retain stored lengths and the CSCR integer slot is not read as a float. No scheduler, procedure evaluator or invented defaults are added. RE #407 tracks the remaining behavior and variant evidence.

`src/c_api_behavior.cpp:radicle_package_decode` and `radicle_idle_decode` own condition arrays and converted CIS strings until free; strings, keys and raw payload bytes borrowed from records require a live environment. `bindings/csharp/Radicle.Interop/Behavior.cs` copies these values before freeing native holders. `radicle_combat_style_get_info` uses a fixed output with presence flags and actual variable-array counts. The initial validation was CI-only; subsequent official-master runs exercise the stored readers and byte preservation. Corpus cases still skip when installed data is unavailable.

## Package location and target views

`src/environment/behavior.cpp:decode_package` returns indexed PLDT/PTDA views
without assigning them to an unproven procedure/input scope. The verified
[packages-combat-idle-407-345/README.md](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/packages-combat-idle-407-345/README.md)
sections Q4/Q5 stamp OG addresses 0x726520 and 0x729300: The disk compatibility views accept PLDT 8/12/16 bytes with
signed i32 type, raw u32 value, optional signed i32 radius and optional u32 item
index; PTDA is 12 bytes with signed i32 type, raw u32 value and signed
count-or-distance. These stored fields are not the normalized runtime layout. Only proven form-valued variants
resolve their value through the owning plugin. Raw values and unknown shapes
stay preserved in the original container; input and leaf boundaries remain
opaque. `src/c_api_behavior.cpp:PackageHolder` owns the copied view arrays,
and `bindings/csharp/Radicle.Interop/Behavior.cs:RadicleEnvironment.Package`
copies them before freeing the holder.

`src/schema/behavior_layout.cpp:behavior_formid_offsets` remaps only the PLDT/PTDA branches that xEdit defines as form links, and strict remap refuses the xEdit Unknown branches only when their stored 4-byte value at offset 4 is nonzero. The branch is chosen by the Type integer through `source:xEdit Core/wbDefinitionsFO4.pas:2273 @93cc0bc5` (`wbTypeDecider`). PLDT (`source:xEdit Core/wbDefinitionsFO4.pas:4177-4194 @93cc0bc5`): links are 0 Reference, 1 Cell, 4 Object ID and 6 Keyword; 2 and 3 are four-byte ByteArrays, 5 and 8-11 are integers, 7 is `wbUnused(4)`, and 14 is the Ref Collection Alias integer, all kept numeric and unremapped; 12 and 13 are `wbByteArray('Unknown', 4)` branches; the enum at `source:xEdit Core/wbDefinitionsFO4.pas:4139-4140 @93cc0bc5` names them Near self and Near Editor Location Cell. PTDA (`source:xEdit Core/wbDefinitionsFO4.pas:4223-4245 @93cc0bc5`): links are 0 Specific Reference, 1 Object ID, 3 Keyword and 7 Keyword; 2 Object Type, 4 Alias and 5 Interrupt Data are integers; 6 and 8 are `wbByteArray('Unknown', 4, cpIgnore)` branches; the Type enum names them Self at `source:xEdit Core/wbDefinitionsFO4.pas:4231 @93cc0bc5` and Unknown 8 at line 4233, and the union branch decides. Zero in the 4-byte value means no link and is never refused, so strict remap leaves it unchanged. Non-strict remap leaves every refused branch unchanged. `src/schema/behavior_layout.cpp:behavior_field_payload_valid` is unchanged, so these variants keep their payload-valid status.

## Radicle/src/plugin/flags.cpp:fo4::set_plugin_light_flag

Flagging a plugin light rejects owned FormIDs below the floor that `src/plugin/maintenance.cpp:esl_eligibility` and `src/plugin/mutation.cpp:local_form_id_range` use: 0x001 when `allows_hardcoded_range` holds (HEDR at least 1.0 and a master), else 0x800. IDs above 0xFFF are also rejected, and the message names the actual range. The floor rule is xEdit `93cc0bc5` `Core/wbImplementation.pas:TwbFile.GetAllowHardcodedRangeUse`. RE-016 (GARDEN-TOOLS #520 comment 13469) verifies that the engine keeps a masterless plugin's IDs 0x001-0x7FF in the hardcoded range, where they never resolve to the plugin's own records, so this refusal is correct; see `Radicle/docs/ESL_FORMIDS.md`. Clearing the flag never checks IDs. Tests: `Radicle/tests/test_plugin_flags.cpp:PluginFlags.RejectsLightFlagWhenMasterlessLocalIdIsBelowTheFullFloor`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.RejectsLightFlagBelowTheFloorWhenHedrIsBelowOne`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.AllowsLightFlagForLowLocalIdsWithHedrOneAndAMaster`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.AllowsLightFlagForMasterlessIdsAtTheFullFloor`, and `PluginEditTool/native/tests/unit/RadiclePluginEslTests.cpp:Native SetLight refuses a masterless plugin with owned IDs below 0x800`.

## Radicle/src/plugin/writer.cpp:remap_plugin_masters

`Radicle/src/plugin/writer.cpp:remap_plugin_masters` refuses dropping the last master of a light plugin that owns local IDs below 0x800 (GT-057). Light plugins with a master use the 0x001 floor from `Radicle/src/plugin/mutation.cpp:allows_hardcoded_range`; with no masters that floor reverts to 0x800, so those low own IDs would become invalid. The refusal throws `FormIdError` before any mutation. Full plugins, adds, reorders and light plugins with no low own IDs are unchanged. Tests: `Radicle/tests/test_master_metadata.cpp` `MasterMetadata.RemovingLastMasterOfLightPluginRefusesLowOwnIds`, `MasterMetadata.RemovingLastMasterOfLightPluginAllowsIdsAtTheFloor`, `MasterMetadata.RemovingLastMasterOfFullPluginAllowsLowOwnIds`; `PluginEditTool/native/tests/unit/RadiclePluginAddMastersTests.cpp` `Native CleanUnusedMasters refuses to drop the last master of a light plugin with low own IDs`.

## Radicle/src/c_api.cpp:radicle_plugin_remap_masters

`Radicle/src/c_api.cpp:radicle_plugin_remap_masters` remaps a copy of the plugin and assigns it back only when `fo4::remap_plugin_masters` succeeds, so a failed call (for example a dropped master that an own record or override still uses, which makes `encode_plugin_form_id` throw "is not in the destination master table") leaves the handle unchanged instead of empty (GT-090). Test: `Radicle/tests/test_c_api.cpp` `CApi.FailedRemapMastersLeavesThePluginUnchanged`.

## src/schema/scalar.cpp:scalar_descriptor

Record-agnostic: it resolves a subrecord signature only when exactly one row of `include/fo4/schema/generated_scalar_fields.inc` names it and that row is a whole-payload, non-repeated, non-string field. Any other count returns nullopt, because a signature that two rows share may mean different fields, so the lookup does not choose between them. Only tests call it (`tests/test_scalar.cpp`, `tests/test_scalar_embedded.cpp`, `tests/test_field_writer.cpp`, `PluginEditTool/native/tests/unit/RadiclePluginScalarTests.cpp`); production code goes through `scalar_field_descriptor(record, path)`.

The previous rule returned the first whole-payload row and skipped only embedded rows, so a whole-payload row on a shared signature leaked out. Eight signatures were affected: ANAM, DATA, FNAM, LVLD, LVLF, LVLM, NAM4 and VNAM. DATA started leaking when `FACT\Flags` (a whole-payload u32 on DATA, deliberately so a wrong width throws, see `Radicle/schema/fallout4/fields.json:FACT`) arrived with #551 (`cc0a36daa`), which failed `tests/test_scalar_embedded.cpp:ScalarEmbedded.SignatureOnlyLookupSkipsEmbeddedFields` on Linux and Windows. VNAM had always leaked `PROJ\SoundLevel` although `SCEN` VNAM is a 16-byte struct and `WTHR\VolatilityMult` sits in a 4-byte VNAM, so the old VNAM assertion in that test encoded the leak and now expects no match. XHLT and XSCL each have a single `REFR` row and still resolve. A whole-payload registration on a shared signature is therefore safe; registering a field as embedded is no longer needed to hide it from this lookup.

## Radicle/src/schema/form_link_mixed.cpp:fixed_stride_formid_offsets

Repeated fixed-stride {form ID, value} arrays are visited through `k_repeated_struct_layouts` (PRPS, DAMC, DAMA), which is consulted from `mixed_formid_offsets` before the placed-reference table. Before this entry they had no handler: they were absent from `formid_subrecords.inc`, `structured_formids.inc` and the KWDA/SPLO/XLKR stride-4 branch, so `remap_plugin_masters` and `collect_form_links` both skipped them and a master reorder left stale IDs.

- PRPS: `wbDefinitionsFO4.pas:5266-5272` quotes `wbStructSK([0], 'Property', [wbActorValue, wbFloat('Value')])` and `wbPRPS := wbArrayS(PRPS, 'Properties', wbObjectProperty)`; `wbActorValue` is `wbFormIDCkNoReach('Actor Value', [AVIF, NULL])` (`:3288`). Records: ACTI, CLAS, CONT, FLOR, FURN, LIGH, MSTT, NPC_, RACE, STAT, TERM. Corpus scan of the official masters, 2026-10-11: every PRPS payload size is a multiple of 8.
- DAMC: `wbDefinitionsFO4.pas:4288` quotes `wbArrayS(DAMC, 'Resistances', wbStructSK([0], 'Resistance', [wbFormIDCk('Damage Type', [DMGT]), wbInteger('Value', itU32)]))` inside `wbDEST` (`:4275`). Carried by every record whose definition includes `wbDEST`. Corpus scan, 2026-10-11: one ACTI DAMC payload, 32 bytes.
- DAMA, `wbDefinitionsCommon.pas:wbDamageTypeArray` (`:8454-8463`), element quoted from `wbDefinitionsCommon.pas:8454-8463` as `wbStructSK([0], aItemName, [wbFormIDCk('Type', [DMGT]), wbInteger('Amount', itU32), ...])`. Used by ARMO (`wbDefinitionsFO4.pas:5839`, "Resistance") and WEAP (`:13023`, "Damage Type"). Official masters: ARMO sizes 8 and 16, WEAP size 8, all multiples of 8. The element gains a Curve Table form ID at offset 8 only when `wbDefinitionsCommon.pas:8460` (`wbFromVersion(152, wbFormIDCk('Curve Table', [CURV, NULL]))`) applies, so `form_link_mixed.cpp:k_repeated_struct_layouts` uses the 12-byte, slots {0, 8} DAMA entry when `record.header.form_version >= 152` and the 8-byte, slot {0} entry otherwise. Vanilla FO4 records are form version 131 or lower (the repo's own FO4 version constant is `Radicle/src/forms/race.cpp:k_current_form_version = 131`), and all vanilla DAMA payloads are multiples of 8. The rule is FO4-only, because `wbDefinitionsFO76.pas` (`:15642`) reads DAMA as a different record field.

`fixed_stride_formid_offsets` is the stride-table loop that `placed_reference_formid_offsets` already used for `k_placed_reference_layouts`, lifted out so the record-independent `k_repeated_struct_layouts` table can reuse it. Its malformed-payload error text is now "cannot safely remap malformed <SIG> payload"; no test asserts the old text. `generated_formlink_lists.inc` names PRPS and DAMC, but it only feeds the graph and check tables (`Radicle/src/graph/check_errors.cpp`), not `visit_link_offsets`.

## Radicle/src/material/material_file.cpp:read_material_file / write_material_file

`read_material_file` takes the format from the first `u32` alone (`BGSM` or `BGEM` magic); the file extension is never consulted, because mods ship BGEM content under `.bgsm` names. Any other first word, or fewer than four bytes, throws `fo4::parse_error`. Both directions call one function, `visit_layout`, templated on the access type: `Reader` decodes each field into `MaterialFile::fields` and `Writer` encodes the stored value back. Each field call returns the value it read or wrote, so version gates and conditionals (EmitEnabled, GlassEnabled, Terrain, version 3 UnkInt1) use the same values in both directions and a field cannot be readable without being writable. Bools keep the raw byte (`RawBool`), strings keep every byte including any NUL, TileFlags is a raw `u32`, and bytes after the last field are kept in `MaterialFile::trailing`, so reading then writing a valid file reproduces it exactly. The writer takes a field missing from the list as zero (a conditional block switched on by a setter, for example EmittanceColor after EmitEnabled becomes true), and throws `std::invalid_argument` only when a stored field has a different type than the layout asks for. Tests: `Radicle/tests/test_material_file.cpp` (BGSM versions 0-20 and BGEM versions 2-22 across every version gate, each conditional both ways, raw bool 2, a string without NUL, an empty string, unknown TileFlags bits, trailing bytes, zero-default EmittanceColor), `Radicle/tests/test_material_file_real.cpp` (vanilla archive round trip).

## Radicle/src/c_api_material_file.cpp:radicle_material_file_read

The handle owns a `MaterialFile` copy, so the input buffer can be released after `radicle_material_file_read` returns. `guarded` maps exceptions to statuses: `fo4::parse_error` to `RADICLE_STATUS_PARSE_ERROR`, `std::invalid_argument` to `RADICLE_STATUS_INVALID_ARGUMENT` (this is how a write to Version or Signature is refused, since `set_field` throws), and `std::bad_alloc` to `OUT_OF_MEMORY`. A missing name returns `NOT_FOUND` before any type check, and a type mismatch returns `INVALID_ARGUMENT`. String getters and setters use the text semantics of the core: `radicle_material_file_get_string` returns the value with one trailing NUL dropped, and `radicle_material_file_set_string` appends one NUL, so a caller round-trips text without seeing the NUL. The raw bytes of an unusual string survive only through `radicle_material_file_write`. The new exports moved `RADICLE_ABI_VERSION` from 11 to 12; `bindings/csharp/Radicle.Interop/RadicleNative.g.cs` and `bindings/csharp/tests/abi_layout_probe.cpp` were regenerated from `fo4/c_api_material_file.h`. Tests: `Radicle/tests/test_c_api_material_file.cpp`; the managed round trip is `Radicle/bindings/csharp/Radicle.Interop.Tests/MaterialFileTests.cs`.

`Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION`: ABI 22 (GT-137 slot for #466): material C API added; interop regenerated after #462.

## include/fo4/schema/non_formid_slots.inc: xEdit-only entries

`Radicle/tools/corpus/non_formid_slots.py:main` merges the hand list `XEDIT_DENIED` into the generated deny list, because the corpus scan cannot see two slots that xEdit declares non-form-ID. No vanilla FLOR has RNAM, so the scan never saw it. Vanilla FACT RNAM values are small rank integers, and the generated list does not include FACT RNAM; without the entry every 4-byte rank is remapped as a form ID owned by master index 0. The entries are FACT `RNAM` (rank number, `wbInteger(RNAM, 'Rank#', itU32)` inside `wbFactionRank`, `Core/wbDefinitionsFO4.pas:6771-6773`, used at `:6830` by the FACT record at `:6785`) and FLOR `RNAM` (localized Activate Text Override, `wbLStringKC(RNAM, 'Activate Text Override', 0, cpTranslate)` at `Core/wbDefinitionsFO4.pas:12179`, FLOR record at `:12167`), both at xEdit 93cc0bc5. Entries marked `// xEdit` in `include/fo4/schema/non_formid_slots.inc` are hand-maintained and survive regeneration. The list also covers every FO4 pair whose signature is a global form ID signature in `include/fo4/schema/formid_subrecords.inc` but which xEdit declares in that record only as a string. The corpus rule misses these because vanilla values are never exactly 4 bytes. Examples are CLMT `FNAM` and `GNAM` (climate texture paths) and WATR `NAM2` to `NAM4` (water noise texture paths). Tests: `RemapNonFormIdSlots.FactionRankNumberSurvivesMasterReorder`, `RemapNonFormIdSlots.FloraActivateTextStringIdSurvivesMasterReorder`, `RemapNonFormIdSlots.ClimateSunTexturePathSurvivesMasterReorder` and `RemapNonFormIdSlots.WaterNoiseTexturePathSurvivesMasterReorder`.

## Radicle/src/plugin/writer.cpp:apply_extension_flags

xEdit `93cc0bc5` `Core/wbImplementation.pas` (save path, lines 5300-5306) sets header flags from the module extension: `.esm` sets ESM, and `.esl` sets ESM and Light when light plugins are supported. xEdit never clears them. `apply_extension_flags` applies the same rule in `write_plugin_file` and in the PluginEditTool localized-sidecar save, so a Save As from an `.esp` to `.esm` or `.esl` writes the Master flag (and Light for `.esl`). Flags are only ever set; an existing flag is never cleared. A plugin with every needed flag already set is returned untouched, so it still writes its original bytes. When a flag is added, both `tes4.header.flags` and `header.tes4_flags` are updated and the original byte ranges are cleared so the header re-serializes. The Light flag goes through `set_plugin_light_flag`, which refuses an `.esl` save whose owned form IDs fall outside the light range with `FormIdError`. xEdit sets the flag anyway in that case; keeping the refusal is an owner decision. Tests: `Radicle/tests/test_plugin_flags.cpp:WriteFileToEslSetsMasterAndLightFlags`, `Radicle/tests/test_plugin_flags.cpp:WriteFileToEsmSetsMasterFlagOnly`, `Radicle/tests/test_plugin_flags.cpp:WriteFileToEspKeepsFlags`, `Radicle/tests/test_plugin_flags.cpp:WriteFileToEslRefusesOutOfRangeFormIds`. Setting Light goes through `set_plugin_light_flag`, which also lowers HEDR next object ID to one past the highest owned ID when every owned ID is in 0x800-0xFFF; xEdit's `SetIsLight` (`wbImplementation.pas:6248`) leaves HEDR alone, and the lower value is kept so new records stay in the light range. `write_plugin(const Plugin&, path)` copies the plugin only when a flag is missing.

## src/environment/environment.cpp:Environment::winner

`Environment::winner` skips Partial Form overrides, as xEdit does. xEdit `93cc0bc5` `Core/wbImplementation.pas:GetWinningOverride` (lines 12574-12586) walks overrides from last to first, skips any that is a Partial Form, and falls back to the master record when every override is partial. The check is `fo4::is_partial_form` in `Radicle/include/fo4/plugin/container.hpp`, the same function `Radicle/src/graph/conflict_graph.cpp:ConflictGraph::winner` uses, so the two winners agree. Partial Form is record flag 0x4000 (`RecordFlags::partial_form`) and counts only for CELL, DIAL, QUST and WRLD (`Core/wbDefinitionsFO4.pas` lines 5957, 6311, 10686, 13112). Vanilla has 7 QUST records with the flag. 14 LCTN records carry bit 14, but LCTN is not partial-capable, so they still win. Tests: `Radicle/tests/test_environment.cpp:EnvironmentTest.WinnerSkipsPartialFormOverrideOfPartialCapableRecord`, `EnvironmentTest.WinnerFallsBackToMasterWhenEveryOverrideIsPartialForm`, `EnvironmentTest.WinnerKeepsLastVersionOfRecordTypeWithoutPartialForm`.

## bindings/csharp/tools/run_managed_tests.py:main

GT-065: with `--build-dir`, the script reads the `radicle_csharp_bindings` CTest entry and its `ENVIRONMENT` property. Both are added only when CMake is configured with `-DRADICLE_CSHARP_TESTS=ON` (`Radicle/CMakeLists.txt`), so a build without that option now exits with a message naming the option instead of a bare `StopIteration`. The script's existing `--build-dir` path is otherwise unchanged. `test_run_managed_tests.py` (registered as `radicle_run_managed_tests` in `Radicle/CMakeLists.txt` under the Python3 block) covers both missing-entry cases; CI runs it through the same `ctest --test-dir` steps in `.github/workflows/native-ci.yml`.

## bindings/csharp/tools/run_managed_tests.py:parent_build_files

GT-004: the consumer check copies only `Radicle.Interop`, `Radicle.Interop.Tests`, `global.json`, and the bindings folder's own `Directory.Build.*` and `NuGet.Config` into a temp folder. MSBuild and NuGet also read those files from parent folders up to the repo root, which the temp layout cannot reproduce. `parent_build_files` lists such parent files, stopping at the first ancestor that holds `.git`, and `main` raises `RuntimeError` when any exist so the check cannot pass while a real build would see different settings. No parent files exist in the repo at this commit, so the guard is inert today.

## bindings/csharp/Radicle.Interop/PluginFile.cs:FieldCount

`FieldCount` and `FormLinkListCount` throw `KeyNotFoundException` for a missing record and return 0 when the record exists but the field has no occurrences; before GT-017 both cases returned 0. `TryFieldCount` and `TryFormLinkListCount` give the non-throwing form (false for a missing record, true with `out` 0 for no occurrences), matching the `Edit` family where NOT_FOUND returns false. The old names are kept so callers on open branches still compile. The C side already distinguishes the cases: `Radicle/src/c_api_fields.cpp:radicle_plugin_record_field_count` returns NOT_FOUND when `Radicle/src/plugin/field_writer.cpp:record_field_count` finds no record, and `radicle_plugin_formlink_list_size` does the same through `Radicle/src/schema/formlink_lists.cpp:record_formlink_list_size`. Tests: `Radicle/bindings/csharp/Radicle.Interop.Tests/FieldWriterTests.cs:FieldCountReportsOccurrences` and `FormLinkListOperationsEditByPath`.

>## GT-082 / GT-092 reader limits

Anchored to `src/plugin/reader.cpp:read_group` and `src/plugin/reader.cpp:inflate_record_payload`.

- GRUP nesting: `read_group` takes a depth (top-level groups start at 1, `read_child` passes `depth + 1` for each nested GRUP) and throws `parse_error` once depth exceeds 16. Recursion was unbounded, so a crafted file of nested 24-byte GRUP headers overflowed the stack, which cannot be caught. Real Fallout 4 nesting is about six levels, so 16 leaves headroom.
- GT-138 compressed record size: `inflate_record_payload` checks the declared uncompressed size twice, both before the output buffer is allocated. First it refuses anything above `compressed.size() * 1032 + 64`. 1032 is zlib's maximum expansion ratio; the 64-byte slack covers the stream framing. Then it refuses anything above the absolute cap `k_max_record_uncompressed_bytes` (1 GiB, 2^30). The ratio bound keeps a small record from asking for much more output than its size allows, and the cap keeps any one record under 1 GiB. The output buffer is allocated once, for the zlib inflate attempt. Test: `tests/test_compression.cpp:Compression.OversizedDeclaredSizeLoadsUndecodable` uses a 1,100,000-byte payload declaring 2^30 + 1 bytes; it passes the ratio bound and is caught by the cap, so it loads as undecodable.
- Tests: `tests/test_reader_limits.cpp` covers 16 levels (parses), 17 and 20 levels (refused), and a declared size of `0xFFFFFFFF` on a tiny payload (loads as undecodable). Ordinary compressed reads are still covered by `tests/test_compression.cpp`.

## GT-093 Radicle core hygiene

- `src/formid.cpp:encode_plugin_form_id` (source:include/fo4/formid.hpp, the full and light mask constants): throws `FormIdError` when the value is too large for the destination, instead of truncating it. Tests: `tests/test_formid.cpp:FormIdEncode.EncodeKeepsWidestFullLocal`, `FormIdEncode.EncodeRefusesOversizedLocal`.
- `src/plugin/mutation.cpp:set_record_editor_id`: an embedded NUL throws `FormIdError`. UTF-8 input is converted with `text::utf8_to_windows1252`, the helper `src/c_api_ref_fields.cpp:radicle_plugin_set_placed_editor_id` also uses. Text with no Windows-1252 form throws `FormIdError`; the C ABI returns `RADICLE_STATUS_INVALID_ARGUMENT` for the same input. Native callers in `PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp`, `RadiclePluginLifecycle.cpp` and `RadiclePluginMaintenance.cpp` now pass UTF-8 text; its copy-as-new EDID read goes through `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:editorId`, which converts Windows-1252 to UTF-8 first. Tests: `tests/test_editor_id_text.cpp:EditorIdText.RefusesEmbeddedNul`, `EditorIdText.StoresUtf8AsWindows1252WhenReplacing`, `EditorIdText.StoresUtf8AsWindows1252WhenInserting`, `EditorIdText.RefusesCharactersOutsideWindows1252`.
- `src/plugin/tes4.cpp:parse_tes4_header`: a `MAST` with no `DATA` is pushed before the next `MAST` is read, so it is no longer overwritten. The trailing unpaired `MAST` and the paired path both decode through `named_master`, which applies `text::windows1252_to_utf8`. Tests: `tests/test_tes4_masters_hedr.cpp:Tes4Masters.UnpairedMasterIsKeptWhenAnotherMasterFollows`, `Tes4Masters.TrailingUnpairedMasterDecodesWindows1252`.
- `src/plugin/writer.cpp:patch_hedr_record_count`: a HEDR of 12 bytes or more is accepted on rebuild. Only the record count bytes are written; bytes after the first 12 are kept as stored. `src/plugin/mutation.cpp:set_hedr_next_form_id` accepts 12 bytes or more and patches only bytes 8 to 11 (the next form ID). Test: `tests/test_tes4_masters_hedr.cpp:HedrRebuild.LongerHedrKeepsTrailingBytesAcrossRebuild`, `HedrRebuild.NextFormIdUpdateKeepsTrailingBytes`.
- `src/archive/ba2_hash.cpp:normalize_ba2_name`: name-table keys and lookups use this normalizer, which has no 260-character cutoff. `normalize_ba2_path` keeps the `"."` result for 260+ characters, so the name hashes from `compute_ba2_hashes` are unchanged. The key is built in `src/archive/ba2_reader.cpp:Ba2Reader::open` and `lookup_key`, and the texture lookup uses it in `src/archive/ba2_texture.cpp:normalized_path`. Before this change every long name keyed to `"."`, so later entries were dropped and `find` returned the first one. Names under the cutoff normalize as before. Tests: `tests/test_ba2_long_names.cpp:Ba2LongNames.DistinctLongNamesKeepSeparateEntries`, `Ba2LongNames.TextureLookupKeepsDistinctLongNames`.

## GT-081 CTDA alias and packdata params are not form IDs

Anchored to `src/schema/form_links_ctda.cpp:ctda_formid_offsets`, with its per-function table `src/schema/form_links_ctda.cpp:k_ctda_reference_params` and lookup `src/schema/form_links_ctda.cpp:reference_param_mask`. `form_links.cpp:visit_link_offsets` only forwards the offsets this walker returns.

The walker decided param1 (CTDA offset 12) and param2 (offset 16) from the form ID flags of the function table alone. The CTDA Type byte changes what those params hold. With Type bit 0x02 (use aliases) a ptReference, ptActor or ptPackage param is an alias index, and with 0x08 (use packdata) it is a package data index. Neither is a form ID, so `remap_plugin_masters` rewrote them when master 0 moved or a masterless plugin gained a master. Pinned by `Radicle/tests/test_ctda_alias_formids.cpp:CtdaReferenceParams.AliasParamSurvivesMasterZeroMoving` and the neighbouring tests in that suite.

The rule is TES5Edit at commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`:

- `wbConditionParam1Decider` (lines 808-834): when the xEdit param type is ptReference, ptActor or ptPackage, Type & 0x02 turns it into ptAlias, except GetIsCurrentPackage (function index 161) with Run On = 5 (Quest Alias), which stays ptPackage. Otherwise Type & 0x08 turns it into ptPackdata. The two flags are exclusive: the 0x08 branch is only reached when 0x02 is clear.
- `wbConditionParam2Decider` (lines 837-858): the same type set, with 0x02 giving ptAlias and otherwise 0x08 giving ptPackdata. There is no GetIsCurrentPackage exception for param2.

`k_ctda_reference_params` lists the 63 function indexes whose xEdit ParamType1 or ParamType2 is in that set, read from the `wbConditionFunctions` rows (lines 261-741) of the same commit; bit 1 is param1 and bit 2 is param2. The form ID flags in `ctda_functions.inc` are separate and still decide whether a param is a form ID at all. A param that is form ID-flagged and not in the set (ptFaction, ptGlobal and similar) is remapped whatever the Type byte says. The Run On check reads the u32 at offset 20, so a payload shorter than 24 bytes is never treated as Quest Alias. Alias and packdata params are no longer reported by `collect_form_links` or by the reachability CTDA rule, because they were never form IDs.

## GT-092 schema corrections

Anchored to the four GT-092 items. The xEdit reference is TES5Edit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`.

- PROJ DNAM `Flags` and `Type`: anchored to `include/fo4/forms/proj_expl_hazd.hpp:ProjectileCreation`, `src/forms/proj_expl_hazd.cpp:encode_projectile_dnam_with` and `src/forms/proj_expl_hazd.cpp:decode_projectile`. xEdit Core/wbDefinitionsFO4.pas:7115-7147 @93cc0bc5: `wbRecord(PROJ` at 7115, `wbStruct(DNAM` at 7122, `wbInteger('Flags', itU16, wbFlags` at 7123-7138 and `wbInteger('Type', itU16, wbEnum` at 7139, with values `$01` Missile through `$40` Arrow at 7140-7146. `Flags` is a u16 at +0 and `Type` a u16 at +2, so the old u32 `Flags` covered both. The default is `Type` 1 (Missile) and `Flags` 0, which keeps the bytes the old `0x00010000` default wrote. `schema/fallout4/fields.json:PROJ\Type` is new, and `generated_scalar_fields.inc` was regenerated from it. Public effect: the scalar path `PROJ\Flags` is now bounded to 0..65535 and `PROJ\Type` is a new path; no C# accessor reads these fields. Tests: `tests/test_proj_expl_hazd_creation.cpp:ProjExplHazdCreation.ProjectileFlagsAndTypeOccupySeparateLeadingWords` and `ProjExplHazdCreation.ProjectileTypeDefaultsToMissile`.
- CTDA 32-byte layout: anchored to `src/schema/ctda.cpp:parse_ctda_condition` and `src/schema/ctda.cpp:serialize_ctda_condition`. xEdit lines 5183-5219: `wbConditions` declares one fixed 32-byte `wbStructSK(CTDA` at 5186 for every function, with `Function` at 5193, `wbUnused(2)` at 5194 and `Parameter #3`, the Event Data enum, at 5215. xEdit has no 28-byte variant. The 28-byte GetEventData shape that the parser used to take for function 4672 moved the run-on, reference and unknown3 fields four bytes early, so a 32-byte payload lost its tail on rewrite. The parser now takes exactly 32 bytes and refuses any other length with `std::out_of_range`, which is the same error the truncated-payload path already used. Repo evidence conflicts on the 28-byte shape: `PluginEditTool/Mutagen/Mutagen.Bethesda.Fallout4/Records/Common Subrecords/Condition.xml` lines 44-50 define GetEventData with `Function`, `Member` and `Parameter3` and no trailing padding. A scan of 18 plugins (the official Fallout 4 masters and DLC) found 131436 CTDA subrecords, all 32 bytes, and none with function index 4672; GetEventData is function 576 (`wbDefinitionsFO4.pas:554`, GT-198), so the codec accepts only 32-byte CTDA and keeps the trailing padding. Tests: `tests/test_ctda_model.cpp:CtdaModel.GetEventDataLayoutParsesAndSerializes`, `CtdaModel.TwentyEightBytePayloadIsRefused` and `CtdaModel.OverlongPayloadThrows`. GT-142: a QUST log entry or alias run holding a 28-byte CTDA makes `src/forms/quest_stages.cpp:lossless_quest_log_entry_conditions` return false, and PET reports `QuestAuthoringCoverageIncomplete` from `PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:existingStagesUnrepresentable` (log entries) and `aliasConditionsUnrepresentable` (aliases).
- NPC_ `NAM7`: anchored to `src/schema/non_formid_slots.cpp:corpus_non_formid_slot` and to `include/fo4/schema/non_formid_slots_manual.inc`. xEdit line 10286 starts `wbRecord(NPC_` and line 10424 declares `wbFloat(NAM7, 'Unused', cpNormal, True)`, a scalar. `NAM7` is in the global form ID signature list `include/fo4/schema/formid_subrecords.inc`, so without the deny entry a float whose bits look like a form ID was remapped. The entry was added by hand because the generator needs the game data to measure a count, so the comment carries no count; regenerate with `tools/corpus/non_formid_slots.py` to confirm it. The entry now lives in `include/fo4/schema/non_formid_slots_manual.inc`; see GT-143. Test: `tests/test_remap_non_formid_slots.cpp:RemapNonFormIdSlots.NpcNam7FloatSurvivesMasterRemoval`.
- CTDA padding: anchored to `include/fo4/schema/ctda.hpp:CtdaCondition` (field `padding`), `src/schema/ctda.cpp:parse_ctda_condition` and `src/schema/ctda.cpp:serialize_ctda_condition`. xEdit line 5188 declares `wbUnused(3)` at +1, so bytes 1-3 are unused on disk. They are now parsed into `CtdaCondition::padding` and written back, so a condition edit no longer zeroes them. This also changes `src/forms/quest_stages.cpp:lossless_quest_log_entry_conditions`, which compares the rebuilt bytes: a log-entry CTDA with non-zero padding now passes that check. Test: `tests/test_ctda_model.cpp:CtdaModel.PaddingBytesSurviveAConditionEdit`. GT-142: the Radicle codec keeps these bytes, but PET's `PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition` builds a fresh `CtdaCondition`, so a condition that PET rebuilds has zero padding.

## GT-091 / GT-093 C API safety

Anchored to `src/c_api.cpp:radicle_plugin_remap_masters`, `src/c_api_env_internal.hpp:belongs_to_env`, `include/fo4/c_api_env.h:radicle_script_property` and `src/c_api_placed.cpp:build_view`.

- GT-091: a record handle points into one environment's version table, so passing it with another environment made the call resolve that record's plugin index and form links against the wrong master table. `belongs_to_env` now moved to `src/c_api_env_internal.hpp` and every entry point that takes a record and an env, plugin or strings handle calls it before reading, returning `RADICLE_STATUS_INVALID_ARGUMENT` with "record does not belong to this environment". Entry points changed: `src/c_api_env.cpp:radicle_record_get_info`; `src/c_api_placed.cpp:radicle_placed_ref_decode` and `radicle_record_primary_model_path`; `src/c_api_base_records.cpp:require`, which guards light, bendable spline, layer parent, lighting template, climate, weather, static collection, static distant LOD, activator water type and water info; `src/c_api_materials.cpp:require`, which guards material swap decode, placed reference material swap, primary model material swap and primary model color remapping index; `src/c_api_terrain.cpp:radicle_landscape_decode`, `radicle_landscape_texture_get_set` and `radicle_texture_set_get_info`; `src/c_api_world.cpp:radicle_worldspace_get_info`, `radicle_cell_get_info` and `radicle_record_get_container`; `src/c_api_writeback.cpp:radicle_plugin_copy_placed_ref_override` and `radicle_plugin_copy_placed_ref_new`; `src/c_api_strings.cpp:radicle_strings_resolve_name`, checked against the env the strings handle was loaded from. `src/c_api_records.cpp` keeps its four existing checks and now uses the shared helper. Test: `tests/test_c_api.cpp:CApi.RecordFromAnotherEnvironmentIsRejected` covers all 30 entry points that take a record from another environment: `radicle_activator_get_water_type`, `radicle_bendable_spline_get_info`, `radicle_cell_get_info`, `radicle_climate_decode`, `radicle_landscape_decode`, `radicle_landscape_texture_get_set`, `radicle_layer_get_parent`, `radicle_light_get_info`, `radicle_lighting_template_get_info`, `radicle_material_swap_decode`, `radicle_placed_ref_decode`, `radicle_placed_reference_material_swap`, `radicle_plugin_copy_placed_ref_new`, `radicle_plugin_copy_placed_ref_override`, `radicle_record_copy_subrecord`, `radicle_record_get_container`, `radicle_record_get_info`, `radicle_record_payload_info`, `radicle_record_primary_model_color_remapping_index`, `radicle_record_primary_model_material_swap`, `radicle_record_primary_model_path`, `radicle_record_resolve_form_id`, `radicle_record_topic_children`, `radicle_static_collection_decode`, `radicle_static_get_distant_lod`, `radicle_strings_resolve_name`, `radicle_texture_set_get_info`, `radicle_water_get_info`, `radicle_weather_decode` and `radicle_worldspace_get_info`.
- GT-093 placed text: the placed script property `text` was a NUL-terminated pointer, so a string with an embedded NUL (`"Garden\0Patch"`, 12 bytes) reached the caller as 6 bytes. `radicle_script_property` now has `text_size` after `text`, as `radicle_quest_property` does, and `build_view` sets it from the converted UTF-8 length. The C# reader `bindings/csharp/Radicle.Interop/PlacedReferences.cs:RadicleEnvironment` decodes `TextSize` bytes instead of stopping at NUL. Test: `tests/test_c_api.cpp:CApi.PlacedScriptTextKeepsEmbeddedNulBytesWithItsLength`.
- ABI: `RADICLE_ABI_VERSION` moves from 11 to 12 in `include/fo4/c_api.h` because `radicle_script_property` changed layout (a field inserted before the `has_*` fields). `bindings/csharp/Radicle.Interop/RadicleNative.g.cs` and `bindings/csharp/tests/abi_layout_probe.cpp` were regenerated by `bindings/csharp/tools/generate_interop.py`, a pure Python generator that reads the headers and writes both files. `tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsTwelve` asserts 12. A native library built from the ABI-11 headers is refused by the binding's version guard.

## GT-079 delete removes the owned child group

Anchored to `src/plugin/mutation.cpp:delete_local_record`, `src/plugin/mutation.cpp:erase_subtree` (the erase both `delete_local_record` and `src/plugin/mutation.cpp:records_removed_by_delete` run), `src/plugin/mutation.cpp:erase_record`, and the shared `src/plugin/child_groups.hpp:owned_child_group`.

- Removal: `erase_record` removes the matched record and, when the GRUP directly after it is an owned child group (world, cell or topic children) whose label equals the record's raw form ID, that GRUP with every record nested in it. Before this change only the record was removed, so the orphaned children GRUP attached to the previous sibling on reload. xEdit removes the child group first: `TwbMainRecord.Remove` calls `lGroup.Remove` before `_File.RemoveMainRecord` (TES5Edit 93cc0bc5a1251936c3c7859eee3150eda12a62d7, `Core/wbImplementation.pas:13545-13553`), and `TwbGroupRecord.Remove` removes its elements before detaching itself (`Core/wbImplementation.pas:18192-18205`).
- Label match: xEdit finds the children GRUP with `TwbGroupRecord.FindChildGroup`, which compares the group type and `GroupLabel` with the record's form ID (`Core/wbImplementation.pas:17721-17743`). `erase_record` applies the same label test, so a following GRUP that names another record stays in place instead of being removed with this one. `owned_child_group` keeps its positional rule, so `remove_records` in `src/plugin/maintenance.cpp` is unchanged.
- Refusal: `delete_local_record` returns `RecordDeleteStatus::referenced` when a schema-proven in-plugin FormLink from a record outside the removed subtree targets any record in that subtree. A link from inside the subtree does not block, and a link to the deleted record itself still blocks as before. The erase runs on the scratch copy, so the caller's plugin is unchanged on refusal. The existing status is returned instead of throwing, so the PET caller maps it to `RecordReferenced` (`PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp:deleteRecord`).
- Pruning: a group that lost its last child to this delete is removed, and the check repeats upward through ancestors that this delete emptied, up to and including top-level groups. Groups that were already empty are not touched. xEdit prunes emptied groups when records move (`Core/wbImplementation.pas:14612-14613` and `:25187-25188`), in the identical-to-master path (`TwbFile.RemoveIdenticalDeltaFast`, `:5638-5642`) and at load (`:6124-6129`). Its plain delete does not prune the parent: `TwbContainer.RemoveElement` (`:8421-8453`) only removes the element.
- Counts: the HEDR record count is not edited here. `write_plugin` rebuilds it from the tree whenever the structure fingerprint changes, and a delete always changes that fingerprint.
- Cross-plugin: `delete_local_record` scans only the plugin being edited. `PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp:deleteRecord` also refuses (`RecordReferenced`) when a record in another loaded plugin links to any key that `src/plugin/mutation.cpp:records_removed_by_delete` lists for the delete, which covers the child records inside the removed group. Same-plugin links into that group are left to `delete_local_record`. Overrides in another loaded plugin of a record inside the removed group are refused too; see GT-141.
- Owned types (GT-146): `src/plugin/child_groups.hpp:is_owned_child_group_type` owns only child-group types 1, 6 and 7, so a QUST type-10 child group is not owned and would be left behind on delete.
- Tests: `tests/test_delete_child_groups.cpp` covers the DIAL, CELL and WRLD removals, pruning, both in-plugin refusals and the foreign-label rule; `PluginEditTool/native/tests/unit/RadiclePluginDeleteChildGroupTests.cpp` runs a DIAL delete through the backend, save and reload, and refuses a DIAL whose topic child is linked from a second loaded plugin.

## GT-091 / GT-096 C# bindings

- GT-091 record ownership: `bindings/csharp/Radicle.Interop/RadicleEnvironment.cs:Record` now carries `Owner`, the environment that produced it. `bindings/csharp/Radicle.Interop/RadicleEnvironment.cs:RadicleEnvironment.Use` is the one check every record-taking method calls: `Use(Record)` throws `ArgumentException` when `Owner` is not this environment (so `default(Record)`, whose owner is null, is rejected), then takes the handle scope, which throws `ObjectDisposedException` once the environment is disposed. Callers include `bindings/csharp/Radicle.Interop/MaterialSwaps.cs:RadicleEnvironment.MaterialSwap`, `bindings/csharp/Radicle.Interop/PlacedReferences.cs:RadicleEnvironment.PlacedReference`, `bindings/csharp/Radicle.Interop/RecordReads.cs:RadicleEnvironment.ReadPayload` and `bindings/csharp/Radicle.Interop/Localization.cs:StringTables.ResolveName`, which checks the record against the environment the tables were loaded from. Records are created with their owner in `RadicleEnvironment.Records`, `RadicleEnvironment.Winner` and `RadicleEnvironment.CellChildren`.
- GT-096 handle lifetime: `bindings/csharp/Radicle.Interop/Core.cs:EnvironmentHandle` and `bindings/csharp/Radicle.Interop/Core.cs:PluginHandle` are the `SafeHandle` subclasses; their `ReleaseHandle` calls `EnvDestroy` and `PluginDestroy`. `bindings/csharp/Radicle.Interop/Core.cs:HandleUse` holds a `DangerousAddRef` for the length of one call. `Dispose` during a call only closes the handle, and the native free runs when the last holder releases. Every environment method, `bindings/csharp/Radicle.Interop/PluginFile.cs:PluginFile.Edit`, the other `PluginFile` methods, `bindings/csharp/Radicle.Interop/QuestRuntime.cs:QuestRuntime.Rows` and `bindings/csharp/Radicle.Interop/QuestRuntime.cs:QuestRuntime.Properties`, and `bindings/csharp/Radicle.Interop/Localization.cs:StringTables.Load` go through it. The P/Invoke signatures in `RadicleNative.g.cs` are unchanged; only the wrappers changed.
- GT-096 index: `bindings/csharp/Radicle.Interop/PluginFile.cs:PluginFile.SetActivateParentDelay` now validates the index through `PluginFile.Index`, so a negative index throws `ArgumentOutOfRangeException` as the other indexed edits do.
- GT-096 embedded NUL: superseded by GT-140. The per-method checks in `bindings/csharp/Radicle.Interop/PluginFile.cs` (`SetScalar`, `SetEditorId`, `SetComments`, `SetString` and `CreateRecord`) are gone; `Utf8NoNulMarshaler` now refuses the NUL.
- Remaining wrappers: `bindings/csharp/Radicle.Interop/Core.cs:PexHandle`, `SeqHandle`, `StringsHandle`, `AssetsHandle` and `Ba2Handle` are the `SafeHandle` subclasses of `bindings/csharp/Radicle.Interop/PexFile.cs:PexFile`, `bindings/csharp/Radicle.Interop/SeqFile.cs:SeqFile`, `bindings/csharp/Radicle.Interop/Localization.cs:StringTables`, `bindings/csharp/Radicle.Interop/Archives.cs:AssetResolver` and `bindings/csharp/Radicle.Interop/Archives.cs:Ba2Archive`. Each `ReleaseHandle` calls the matching native destroy. Every native call goes through `HandleUse`, including `bindings/csharp/Radicle.Interop/PexCode.cs:PexFile.ReadCode`. Pex and Seq construction still calls the native parser on the raw pointer before the wrapper exists, as `RadicleEnvironment.Load` does. A borrowed archive (`Ba2Archive` with an owner) takes its owner's scope on every call, so it still throws `ObjectDisposedException` after its `AssetResolver` is disposed; `Ba2Handle` closes the native archive only when it owns it. `StringTables.Load` and `Ba2Archive` pass a `default` `HandleUse` when there is no assets or owner, and `HandleUse.Dispose` ignores that default. Tests: `Radicle.Interop.Tests/OwnershipTests.cs:DisposeDefersTheNativeFreeUntilAnInFlightCallReturns`, with the same test for `PexFile`, `SeqFile`, `Ba2Archive`, `AssetResolver` and `StringTables`, all through `AssertDisposeDefersNativeFree`.

## GT-139 typed creators store EditorIDs as Windows-1252

Anchored to `src/plugin/mutation.cpp:stored_editor_id`, which `src/plugin/mutation.cpp:set_record_editor_id` shares, and to the typed creators `src/forms/cell.cpp`, `src/forms/eyes.cpp`, `src/forms/race.cpp`, `src/forms/armor.cpp` and `src/forms/proj_expl_hazd.cpp`.

The creators convert the EditorID with `stored_editor_id`. A character outside Windows-1252 makes `src/plugin/mutation.cpp:stored_editor_id` throw `FormIdError` before the plugin changes. An embedded NUL is still refused by each creator's own string check, with that family's error type; `set_record_editor_id` checks NUL itself before converting.

GT-150: PET's `PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp:createRecord` maps any `fo4::FormIdError` on create to the family's own code (`CellCreateFailed`, `EyesCreateFailed`, `RaceCreateFailed`, `ArmorCreateFailed`, `LeveledSpellCreateFailed` or `ProjExplHazdCreateFailed`), the same code the family uses for an embedded NUL. This also covers other `FormIdError`s on create, such as no free local form ID from `src/plugin/mutation.cpp:reserve_local_form_key`, pinned in `PluginEditTool/native/tests/unit/RadiclePluginMaintenanceTests.cpp:Native CompactToEsl at full capacity leaves no ID and creation fails`.

GT-147: every PET EditorID reader goes through `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:editorId`, which decodes Windows-1252 to UTF-8. `PluginEditTool/native/src/plugin/RadiclePluginQueries.cpp:subrecordValue` decodes EDID the same way. Lookups by EditorID compare the decoded UTF-8 with the request. Test: `PluginEditTool/native/tests/unit/RadiclePluginEditorIdTextTests.cpp:Native EditorId reads decode Windows-1252 for listings and lookups`.

GT-150: an EditorID stored as raw UTF-8 before the conversion was applied reads back garbled, because each UTF-8 byte decodes as one Windows-1252 character. Before `ab27e845` the typed creators in `src/forms/` wrote the caller's UTF-8 bytes unchanged, so a non-ASCII EditorID that PET created with them now reads back garbled. There is no migration.

Tests: `tests/test_exterior_cell_creation.cpp:ExteriorCellCreation.StoresEditorIdAsWindows1252`, `tests/test_exterior_cell_creation.cpp:ExteriorCellCreation.EditorIdOutsideWindows1252IsRefusedBeforeAnyMutation`, `tests/test_proj_expl_hazd_creation.cpp:ProjExplHazdCreation.StoresEditorIdAsWindows1252` and `tests/test_proj_expl_hazd_creation.cpp:ProjExplHazdCreation.EditorIdOutsideWindows1252IsRefusedBeforeAnyMutation`, next to the EYES ones in `tests/test_eyes_creation.cpp:EyesCreation.StoresEditorIdAsWindows1252` and `tests/test_eyes_creation.cpp:EyesCreation.EditorIdOutsideWindows1252IsRefusedBeforeAnyMutation`.

## GT-140 C# string arguments refuse an embedded NUL

Anchored to `bindings/csharp/Radicle.Interop/Core.cs:Utf8NoNulMarshaler`, `bindings/csharp/Radicle.Interop/RadicleNative.g.cs` and `bindings/csharp/tools/generate_interop.py`.

The generator maps every `const char*` parameter to `Utf8NoNulMarshaler`, so `RadicleNative.g.cs` uses it for every string parameter. `Utf8NoNulMarshaler.Allocate` throws `ArgumentException` on an embedded NUL during marshalling, before the native call. This replaces the five per-method checks that `PluginFile.cs` used under GT-096.

## PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp:deleteRecord

GT-141: `deleteRecord` also refuses with `RecordHasExternalVersions` when another loaded plugin overrides a record inside the removed subtree. The message names that plugin, the overridden record inside the subtree and the target. The `RecordReferenced` refusal for references from another plugin names the referencing record, its plugin, the record inside the subtree and the target.

GT-148: the external-versions check runs only for removed records owned by the deleting plugin (`FormKey::owner()`). A record owned by a master that the plugin only overrides is skipped, because deleting drops only the override. References from other plugins to such a record are also not refused when the owner's own version is still loaded, since the record stays. Test: `PluginEditTool/native/tests/unit/RadiclePluginDeleteChildGroupTests.cpp:Native delete removes a cell whose child group overrides a master record`.

## src/schema/non_formid_slots.cpp:k_non_formid_slots

GT-143: the NPC_ `NAM7` exclusion lives in the hand-written `include/fo4/schema/non_formid_slots_manual.inc`. Its source is xEdit `Core/wbDefinitionsFO4.pas:10424` at `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `wbFloat(NAM7, 'Unused')`. `k_non_formid_slots` includes it alongside the generated `non_formid_slots.inc`, so regenerating the corpus file does not drop it.

## src/c_api_placed.cpp:radicle_record_primary_model_path

GT-145: `radicle_record_primary_model_path` is wrapped in `guarded()` like its neighbours, so an exception cannot cross `extern "C"`.

GT-149: the entry points in `src/c_api_fields.cpp`, `src/c_api_ref_fields.cpp`, `src/c_api_writeback.cpp` and `src/c_api_authoring.cpp` build form keys, convert text and fill vectors inside `guarded(...)`. The placed-reference setters in `src/c_api_ref_fields.cpp` wrap their argument checks in `guarded` around `run`, so invalid input still returns before the plugin is copied. Status codes and messages are unchanged. Other entry points catch `std::bad_alloc` inline instead of using `guarded`: `src/c_api.cpp`, `src/c_api_pex.cpp`, `src/c_api_quest.cpp`, `src/c_api_seq.cpp` and `src/c_api_env.cpp:radicle_env_load`.

`src/c_api_records.cpp:radicle_record_copy_subrecord` allocates with `std::malloc` and is wrapped in `guarded` after its argument checks, like the other allocating entries. The unwrapped entries in `src/c_api_archive.cpp`, `src/c_api_records.cpp`, `src/c_api_strings.cpp`, `src/c_api_placed.cpp` and `src/c_api_world.cpp` do not allocate or throw: `radicle_ba2_close`, `radicle_assets_destroy`, `radicle_strings_destroy`, `radicle_placed_ref_free` and `radicle_free_cell_children` only `delete` or `free` (destructors are noexcept); `radicle_ba2_get_info`, `radicle_ba2_get_entry`, `radicle_assets_archive_count`, `radicle_assets_get_archive`, `radicle_record_payload_info`, `radicle_string_source_for`, `radicle_strings_table_path` and `radicle_strings_lookup` only read noexcept accessors and assign plain values; `radicle_strings_load`, `radicle_strings_load_with_assets`, `radicle_env_worldspace_top_cell` and `radicle_env_cell_landscape` only delegate to `load_strings` or `resolve_one`, which are guarded. `belongs_to_env` and `fail` are non-throwing.

## src/nif/nif.cpp:read_nif

read_nif checks the FO4 stream gate (version 20.2.0.7, user version 12, BS 130), then the block-type table, block sizes, string table and footer roots, all bounded by the remaining bytes before any reserve. It builds the child and parent maps once, skipping null child references and keeping the first parent seen. The layout facts with their F4LODGEN and NifFile.cs citations, and the parity-pending list for values the sources do not pin (rotation order, billboard layout, BSDynamicTriShape, vertex encodings, strict footer), are in `Radicle/docs/NIF.md`.

## Radicle/src/plugin/writer.cpp:write_plugin

The dirty-plugin ONAM rebuild gate is `src/formid.cpp:is_master_plugin` (TES4 master flag or an `.esm`/`.esl` name). xEdit gives an extension-only module its master state at save: `.esm` sets ESM (`source:xEdit Core/wbImplementation.pas:5301-5302 @93cc0bc5`), and `.esl` sets ESM and light (`source:xEdit Core/wbImplementation.pas:5304-5306 @93cc0bc5`). The ONAM rebuild runs when `FileHeader.IsESM` or the `mfHasESMExtension` module flag holds (`source:xEdit Core/wbImplementation.pas:5401 @93cc0bc5`); `source:xEdit Core/wbLoadOrder.pas:332-334 @93cc0bc5` sets that flag for `.esm` and `.esl` on Fallout 4. So a dirty `.esm` or `.esl` without the master flag rebuilds ONAM. Radicle does not set the TES4 master bit on write, while xEdit's save path sets it for `.esm` (`source:xEdit Core/wbImplementation.pas:5301-5302 @93cc0bc5`); the written bytes keep the flag clear, and that difference is open. The writer likewise does not set the Small (light) bit for a loaded extension-only `.esl`, while xEdit sets ESM and Small on save for `.esl` names (`source:xEdit Core/wbImplementation.pas:5304-5306 @93cc0bc5`); the runtime treats such a file as light by name through `src/formid.cpp:classify_plugin_kind`. `Radicle/docs/ONAM.md` states the same rule. Tests: `tests/test_extension_light_kinds.cpp:DirtyEslWithoutMasterFlagRebuildsOnam`, `tests/test_extension_light_kinds.cpp:DirtyEsmWithoutMasterFlagRebuildsOnam`.

## Radicle/src/forms/placed_reference_copy.cpp:owner_kind

The owner-is-source branch now calls `src/formid.cpp:classify_plugin_kind` with the source header and key, so a source named `.esl` without the TES4 SmallFile flag yields a light master entry, as the load-time sites do. Test: `tests/test_extension_light_kinds.cpp:OwnerEqualToEslSourceIsLightMaster`.

## Radicle/src/environment/sound.cpp:resolve_sound_files

`resolve_sound_files` (C ABI `radicle_sound_resolve_files`) maps a SOUN marker or an SNDR descriptor to the sound file paths it plays. A marker follows SDSC to its SNDR. A Standard descriptor (CNAM `1EEF540A`, or no CNAM, which the decoder reports as an absent kind) returns its ANAM files in stored order. A Compound (`54651A43`) resolves each DNAM child in stored order and concatenates the results; its own ANAM files are not read. An AutoWeapon (`ED157AE3`) resolves its BNAM base descriptor, then appends its ITMS/INTV/FNAM rate files.

Fail closed: the recursion throws `WorldDecodeError` instead of returning a partial list when a link is missing or does not resolve in the load order, a marker has no SDSC, the target is not an SNDR, the kind is unknown, the chain goes past eight descriptor levels, or a descriptor reaches one of its own ancestors. The ancestor check looks only at the chain being resolved, so a child shared by two branches resolves twice instead of failing. The C ABI maps the throw to a non-OK status and a null list. Tests: `Radicle/tests/test_sound.cpp:MarkerToStandardResolvesFilesNativelyAndThroughTheCAbi`, `Radicle/tests/test_sound.cpp:CompoundConcatenatesChildFilesInDescriptorOrder`, `Radicle/tests/test_sound.cpp:AutoWeaponResolvesBaseFilesThenRateFiles`, `Radicle/tests/test_sound.cpp:DescriptorCyclesFailClosed`, `Radicle/tests/test_sound.cpp:UnresolvedLinksFailClosed`, `Radicle/tests/test_sound.cpp:UnknownKindAndNonSoundTargetsFailClosed`, `Radicle/tests/test_sound.cpp:ChainsStopAtTheDepthLimitOfEightDescriptors`.

## Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION

The sound file-list exports `radicle_sound_resolve_files` and `radicle_sound_file_list_free` moved `RADICLE_ABI_VERSION` from 33 to 34, and `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsThirtyFour` replaces `AbiVersionIsThirtyThree` as the pin. ABI 35 renames that test to `CApiQuest.AbiVersionIsThirtyFive` and moves its literal to 35; that bump is the #340 sound playback functions (`radicle_sound_select_rate_file`, `radicle_sound_select_standard_file`). The #343 climate helpers (`radicle_climate_moon_phase`, `radicle_climate_volatility_hold_hours`, `radicle_weather_transition_hours`, in `c_api_records.h`) were first numbered 35 on their branch and moved to ABI 36 when nomad/dev merged, so they arrive in ABI 36. The test is now `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsThirtySix`. The #344 dialogue reset hours fields (`has_reset_hours` and `reset_hours` on `radicle_dialogue_info`) and the `radicle_scene_action_type_name` export moved it to ABI 37; the test is now `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsThirtySeven`. ABI 38 adds the #339 navmesh extra-edge accessor `radicle_navmesh_triangle_edge_is_extra` and the NVPP path-list functions `radicle_navi_path_list_decode` and `radicle_navi_path_list_free`; the test is now `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsThirtyEight`. ABI 39 adds the #345 package procedure-tree decode (`radicle_package_procedure_decode` and `radicle_package_procedure_free`) and the package location and target type name functions (`radicle_package_location_type_name` and `radicle_package_target_type_name`); the test is now `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsThirtyNine`.

## Radicle/tests/test_water_real.cpp:DecodesEveryWaterAndPreservesMasterBytes

The `populated_*` properties count WATR records by `fo4::env::decode_water` output. `populated_DNAM` counts records whose `WaterData::visual` is present, `populated_NAM0` and `populated_NAM1` count a non-zero component in `linear_velocity` or `angular_velocity`, and `populated_NAM2` to `populated_NAM4` count a non-empty `noise_textures` entry. There is no `populated_ANAM` because `WaterData` does not expose ANAM, and FNAM is decoded as `flags` and is not counted. The counts are recorded only and carry no EXPECT.

## Radicle/src/environment/sound.cpp:collect_descriptor

Every call counts one visit against `k_max_sound_visits` (4096) and the emitted file list is checked against `k_max_sound_files` (4096) after each call, so a fan-out whose paths multiply cannot run unbounded. Both limits throw `WorldDecodeError` through `Reader::fail`, the same refusal as the depth and cycle checks, and the C ABI returns a non-OK status with a null list. Shipped data peaks at depth 2 and fan-out 10. Test: `Radicle/tests/test_sound.cpp:WideFanOutChainsStopAtTheVisitLimit`.

## Radicle/src/environment/package_procedure.cpp:decode_package_procedure

Reads the subrecords after PKCU in three stages. Data inputs come first: each ANAM whose zstring is a registered input type runs to the next ANAM, UNAM or XNAM; then one bare UNAM u8 per input (an older shape with a BNAM and a 4-byte PNAM after each UNAM is also accepted); then an optional XNAM kept once per package. The tree follows, if present: a root ANAM Procedure, Sequence, Stacked, Simultaneous or Random. A container reads CITC with that many CTDA (each with optional CIS1 and CIS2), then PRCB (child count, flags), then its children, and then one optional PFO2 or PFOR. A leaf (ANAM Procedure) reads CITC and CTDA, then PNAM naming a registered procedure; the following subrecords up to ANAM, PFO2, PFOR, POBA, POEA, POCA, UNAM or XNAM are an opaque index range, and one optional PFO2 or PFOR directly after that range is consumed. After the tree, the UNAM, BNAM and PNAM triples are consumed as the template input-name table and not exposed; in shipped data they come only after the tree (RE-015, #520 comment 13144). The registered leaf list holds 30 names: the RE #407 comment 7894 names plus Wait and Say from comment 9746. A real-data walk of OG 1.10.163, AE 1.11.240 and NG 1.10.984 found every shipped leaf name inside this list, with 0 fallbacks. Any boundary that does not reconcile throws the internal Unreconciled marker, which the function catches and reports as decoded false with one opaque region from after PKCU to the first POBA, POEA or POCA. The depth limit of 64 bounds recursion on hostile records. Evidence: RE #407 comments 7894 (sections 3 and 5) and 9746, and RE-015.

## Radicle/src/environment/package_procedure.cpp:expected_input_count

Per-leaf input slot counts from RE #407 comment 9746 (Travel 8, Sandbox 16, Sit 1, Sleep 1, Follow 6, Wait 2, Say 5). They describe the leaf's slots, not the package's data-input list, so the corpus test records mismatches and asserts nothing about them.

## Radicle/src/environment/package_procedure.cpp:package_location_type_name

Names for PLDT types 0 to 14 follow RE #407 comment 7894, with corrections from RE-015: type 9 is labeled "location handle (derived)" rather than quest alias, type 11 is "input ID matched against the data-input set (RE-015)", and type 7 is marked unproven because no type-7 arm was found (RE-015; never shipped). PTDA types 0 to 8 follow comment 7894. Types 4 and 5 are never shipped and have descriptive labels only. Raw values stay in PackageLocation and PackageTarget.

## Radicle/src/c_api_behavior.cpp:radicle_package_procedure_decode

Flattens the tree into a node array in pre-order and an input array. Node children are pointers into per-node index arrays owned by the holder, and names are owned strings, so the pointers stay valid until radicle_package_procedure_free. A non-PACK record returns `RADICLE_STATUS_PARSE_ERROR` with "expected a PACK record", the same status and text `radicle_package_decode` gives through `Reader`; it previously returned OK with `decoded` 0. Test: `Radicle/tests/test_behavior.cpp:ProcedureCAbiRefusesANonPackRecordLikePackDecode`.

## Radicle/tests/test_behavior_real.cpp:EveryOfficialPackageCombatStyleAndIdleDecodesAndMasterIsByteIdentical

The procedure-tree corpus check expects every PACK to decode and records PACK_procedure_total and PACK_procedure_decoded, plus one PACK_LEAF_<name> count per leaf name. PACK_LEAF_INPUT_COUNT_MISMATCH and PACK_PKCU_INPUT_COUNT_MISMATCH are recorded only, because the leaf counts are not proven to apply to a package's data-input list. PLDT_nonzero_padding is asserted to be zero: bytes 1 to 3 of every shipped PLDT are zero, consistent with the one-byte type in a 16-byte block (RE #407 comment 7894), while the reader still reads the type as i32 at offset 0.

## Radicle/include/fo4/environment/navmesh.hpp:NavmeshTriangle::edge_is_extra

`edge_is_extra(i)` reports whether flags bit i (0 to 2) is set. A set bit means `edges[i]` indexes the extra-edge array instead of the triangle array. `decode_navmesh` in `Radicle/src/environment/navmesh.cpp` checks every flagged index after the extra-edge table is read, and throws `WorldDecodeError` when an index is at or past the extra-edge count, so the mesh is refused rather than partly read. An unflagged `0xFFFF` is still the open-edge value and is kept as read.

`set_navmesh_triangle` refuses -1 on a flagged slot (`Radicle/tests/test_navmesh_authoring.cpp:FlaggedEdgeSlotRefusesMinusOneWithoutChangingBytes`). The C ABI exposes the same bit test as `radicle_navmesh_triangle_edge_is_extra` in `Radicle/src/c_api_navmesh.cpp`, which returns `RADICLE_STATUS_INVALID_ARGUMENT` for an edge above 2. The C# wrapper is `NavmeshTriangle.EdgeIsExtra` in `Radicle/bindings/csharp/Radicle.Interop/Navmesh.cs`. Tests: `Radicle/tests/test_navmesh.cpp:ExtraEdgeFlagIndexesExtraEdgeArray`, `Radicle/tests/test_navmesh.cpp:FlaggedEdgeIndexPastExtraEdgeCountFailsClosed`, `Radicle/tests/test_navmesh.cpp:NavmeshAbi.TriangleEdgeAccessorReadsFlagBitsAndRefusesOutOfRange`, and `Radicle/tests/test_navmesh_real.cpp:NavmeshReal` (flagged bits 0 to 2 equal the extra-edge count per master).

## Radicle/src/environment/navmesh.cpp:decode_navmesh_paths

`decode_navmesh_paths` reads the NVPP subrecord of a NAVI record. It takes the NAVI record like `decode_navmesh_info` does and shares its NVER 15 check through `require_navi_version`. The payload is validated by `validate`, which calls `navmesh_mixed_formid_offsets`, before the lists are read. A truncated payload or trailing bytes therefore throw `WorldDecodeError`. A record without an NVPP subrecord returns empty lists. Form IDs resolve through the same `link` helper as the other navmesh links. The layout (u32 N, N lists of u32 k and k form IDs, u32 M, M pairs of form ID and u32 value) is in `docs/NAVM_NAVI.md`.

The C ABI reads it through `radicle_navi_path_list_decode`, which returns an owned list released by `radicle_navi_path_list_free` (`Radicle/src/c_api_navmesh.cpp`). The C# binding is `RadicleEnvironment.NavmeshPaths` in `Radicle/bindings/csharp/Radicle.Interop/Navmesh.cs`. These exports, together with the accessor above, move `RADICLE_ABI_VERSION` from 34 to 35, pinned by `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsThirtyFive`. Tests: `Radicle/tests/test_navmesh.cpp:PathPayloadDecodesListsAndAnchorsAndFailsClosed` and `Radicle/tests/test_navmesh_real.cpp:NavmeshCorpus.Fallout4FixturesBoundaryCountsAndNvppSize`, which asserts N = 196 and M = 14 on Fallout4.esm.
## Radicle/include/fo4/environment/dialogue.hpp:DialogueInfo::reset_hours

ENAM's second u16 is a reset time in units of 1/65535 of a day (RE #405 comment 7891, reaffirmed in #344 comment 7892), so the derived hours are raw * 24 / 65535 and raw 65535 is exactly 24.0. The method returns an empty optional when ENAM is absent, rather than 0, so an absent reset is not confused with a zero reset. The writer field `INFO\ResetHours` in `include/fo4/schema/generated_scalar_fields.inc` edits the raw u16 and keeps its historical name; it is not the derived value.

## Radicle/include/fo4/environment/dialogue.hpp:SceneActionType

The enumerators are the OG 1.10.163 ANAM action type ids 0 to 6 from RE #405 comment 7891. Only ids 0 to 6 are named. The stored `SceneAction::type` stays the raw u16, so unknown ids such as 7 or 0xFFFF are preserved and report no name rather than being coerced. RE #405 does not state that every shipped id is in 0 to 6, so the corpus test records a histogram instead of asserting it.

## Radicle/src/environment/dialogue.cpp:scene_action_type_name

Unknown ids return an empty view built from a literal, not a default `string_view`, so `data()` is a valid pointer for the C ABI's `radicle_scene_action_type_name`, which must return a non-null C string.

## Radicle/include/fo4/c_api_dialogue.h:radicle_dialogue_info

`has_reset_hours` and `reset_hours` follow the `has_` plus value pattern of the other optional fields. They were added in ABI 37. The C# `DialogueInfo` record gained a `ResetHours` positional parameter after `ResetTimeEncoded`.

## Radicle/src/c_api_dialogue.cpp:radicle_scene_action_type_name

Static names are returned for the named ids and an empty string for every other id. The caller does not free the result, which points into the library's static data.

## Radicle/tests/test_dialogue_real.cpp:DialogueReal

The per-type `SCEN_action_type_<id>` properties are a histogram of the shipped action type ids for the record-level CI report. They are not asserted, because RE #405 only states the meaning of ids 0 to 6 and does not state that every shipped id is in that range.

## Radicle/include/fo4/environment/base_records.hpp:ClimateTiming
`moon_phase` decodes the packed moon byte as in `days_per_phase`: the phase is `(days passed % (days * 8)) / days`, so there are eight phases (0 to 7) driven by the game day count. Zero days per phase means the phase is never updated, so the helper returns no value rather than 0. Packing and phase formula: #397 comment 7866.

`volatility_hold_hours` takes the multiplier and the hold-time bounds as inputs. The engine's `x = clamp(byte * volatilityMult, 0, 255)` and the result `max(0.1, (max - min) * (255 - x) / 255 + min)` are the proven formula (0x656270, #397 comment 7866). The 0.1 floor is part of the formula; the engine returns 0.1 directly when there is no climate or no current weather, which the helper does not model. The compiled defaults are min 1.0 and max 23.0 game hours. The meaning of the result as the weather hold time is verified from `Sky::UpdateWeather` at 0x64E530 (#397 comment 9745).

## Radicle/src/environment/base_records.cpp:transition_hours
Computes `byte / 255 * (max - min) + min` in float32 from the WTHR DATA transition byte (`weatherData[3]`, read unsigned by the engine). The bounds are game settings `fWeatherTransMin` and `fWeatherTransMax`, not record fields, so the caller supplies them; the compiled OG defaults are 0.01 and 1.0, and the shipped Fallout4.esm on OG 1.10.163, NG 1.10.984 and AE 1.11.240 sets fWeatherTransMax 3.0 and fWeatherTransAccel 16.0 (#397 comments 7866 and 9763 for the GMST routing). Its min and max come from GMSTs where the last matching plugin wins, and the accelerated 5x (compiled) or 17x (shipped Fallout4.esm) multiplier from fWeatherTransAccel is not applied (RE-010 on #520, comment 13142).

## Radicle/src/schema/preflight.cpp:climate_sun_window_issues
Flags a CLMT TNAM whose sunrise (bytes 0 to 1) or sunset (bytes 2 to 3) window has end minus begin equal to 144 raw units, which is 24 hours at 1/6 hour per unit. The engine divides by zero for that window in 0x647CE0 (#397 comment 9745). Reversed and zero-width windows (end minus begin at or below 0) are flagged as `climate_sun_window_never_opens`, because the engine never opens them (RE-010 on #520, comment 13142); the record is not reordered or clamped.

## Radicle/src/c_api_base_records.cpp:radicle_climate_moon_phase
C ABI wrappers for `ClimateTiming::moon_phase`, `ClimateTiming::volatility_hold_hours` and `transition_hours`. They take the stored bytes as scalars, check null out-pointers, and use the same `RADICLE_STATUS_INVALID_ARGUMENT` convention as the other scalar getters. The moon wrapper reports absence with `out_has_phase` (0 when days per phase is 0).

## Radicle/tests/real_data_build.cpp:detect_game_build

Reads `<data_dir>/../Fallout4.exe` and scans its bytes for the VS_FIXEDFILEINFO signature 0xFEEF04BD followed by struct version 0x00010000. The version is the high and low file-version words, printed as major.minor.build.revision. The last match in file order wins: `scan_file_version_backward` walks from the end of the file, and the struct-version check is what rejects stray constants. A non-empty `FO4_GAME_BUILD` takes precedence and skips the file read. `game_build_from_version` maps 1.10.163 to OG, 1.10.980 and 1.10.984 to NG, and 1.11.x to AE. `exact_counts_known` is true only for 1.11.240.0, the build the pinned counts were measured on. `scan_file_version_backward` reads the exe in 1 MiB windows (`k_version_scan_chunk`) from the end and returns at the first match, so it reads only the tail when a match is near the end and the whole file only when nothing matches. There is no cache: each call scans again.

## Radicle/src/environment/sound.cpp:select_rate_file_index

Walks the rate list in stored order and skips INTV 0 entries, so the returned index is the stored index; storage keeps the zero-rate entries. Ties go to the higher entry. The rule is written for ascending rates because the RE gives no rule for an unsorted list. Source: #399 comment 7869 section 3 (AutoWeapon rate choice) and section 5 (boundary values). Tests: `Radicle/tests/test_sound.cpp:AutoWeaponRateChoosesTheNearestEntryWithTiesToTheHigherEntry`, `Radicle/tests/test_sound.cpp:AutoWeaponRateSkipsZeroEntriesAndReturnsTheStoredIndex`.

## Radicle/src/environment/sound.cpp:select_standard_file_index

The random draw is an input, so the chooser stays deterministic and testable; the engine's uniform index is the caller's job. A draw equal to the previous pick moves to `(draw + 1) % count`, and only when count is above one. Source: #399 comment 7869 section 3 (Standard selection). Test: `Radicle/tests/test_sound.cpp:StandardChooserReturnsTheOnlyFileAndNeverRepeatsAConsecutivePick`.

## Radicle/src/environment/sound.cpp:sound_frequency_factor

Derived from BNAM byte 0 (signed) as `1 + shift * 0.01`, clamped to [0.1, 2.0], per OG 1.10.163 playback (#399 comment 7869). Test: `Radicle/tests/test_sound.cpp:DerivedFrequencyFactorClampsAtBothEnds`.

## Radicle/src/environment/sound.cpp:sound_is_looping

Tests the mask `0x38` on LNAM byte 1, which is the LNAM dword bits `0x800`, `0x1000` and `0x2000` (#399 comment 7869). This is the any-loop test only. The Envelope Fast and Slow bits have their own stop behavior (#399 comment 9761) and are not separated here. `sound_sidechain_send`, `sound_small_rumble_send` and `sound_big_rumble_send` read LNAM bytes 2 and 3 from the same source. Per RE 7869 the engine applies rumble sends only when the rumble-enabled flag is set and the value is nonzero; `sound_small_rumble_send` and `sound_big_rumble_send` return only the scaled nibble, so the caller must gate on that flag. Test: `Radicle/tests/test_sound.cpp:DerivedLoopingIsAnyOfTheLoopBitsInByteOne`.

## Radicle/src/c_api_sound.cpp:radicle_sound_select_standard_file

Checks `draw_index < count` before calling the native chooser and returns INVALID_ARGUMENT. Left to the native throw, `guarded` would map the WorldDecodeError to PARSE_ERROR. Test: `Radicle/tests/test_sound.cpp:CAbiMatchesTheNativeRulesAndRejectsBadArguments`.

## Radicle/include/fo4/archive/ba2.hpp:is_supported_ba2_general_version

GNRL versions 1, 7 and 8 are accepted on read and write. The GNRL record layout is the same for all three; only the header version field differs, matching the C# oracle which passes the version through. Version 7 is seen on NG/AE installs and version 8 on the AE 1.11.240.0 build (`docs/BA2.md`). An earlier "Skyrim SE-style" label on version 7 had no source and was removed. Versions 0, 2, 3, 9 and 0xFFFFFFFF are rejected on read and write (`Radicle/tests/test_ba2.cpp:Ba2GeneralUnprovenVersion`).

## Radicle/include/fo4/archive/ba2_texture.hpp:is_supported_ba2_texture_version

DX10 versions 1, 7 and 8 are accepted on read and write, with the same version set and the same reasoning as the GNRL gate in `Radicle/include/fo4/archive/ba2.hpp:is_supported_ba2_general_version`. Rejection of 0, 2, 3, 9 and 0xFFFFFFFF is pinned by `Radicle/tests/test_ba2_texture.cpp:Ba2TextureUnprovenVersion`.

## Radicle/include/fo4/plugin/mutation.hpp:local_form_id_range

Inclusive [min, max] local form ID range this plugin may allocate for its own records, selected by the TES4 SmallFile flag. Light plugins are 0x001..0xFFF. `TESFile::AdjustFormIDFileIndex` in Fallout4.exe 1.10.163 is recorded at RVA 0x131fa0 in `docs/ESL_FORMIDS.md` (lines 19, 43); the 0x131f90 value that earlier comments gave is the older 1.10.155 image, labeled Tool-Build at `docs/ESL_FORMIDS.md:48`. The RVA is documentation only and is not verified in this repository. Evidence: `RE-Work/Runtime/03-FINDINGS.md`, section "FormID compile-index remap decoded", summarized in `docs/ESL_FORMIDS.md`.

The recorded routine rebuilds a light record's runtime form ID as

    *id &= 0xFFF;  *id |= 0xFE000000;  *id |= u16 [this+0x372] << 12;

so bits 12..23 of the on-disk local are discarded before the slot is OR'd in. A local above 0xFFF does not fail to load; it silently aliases onto another record. That is why the ceiling is enforced here, since the engine never reports the violation. See `docs/ESL_FORMIDS.md` and issue #26.

## Radicle/src/plugin/mutation.cpp:reserve_local_form_key

Shipped Bethesda plugins store a 0xFF-prefixed HEDR next object ID. The seven official masters and the cc ESLs on the AE 1.11.240.0 install all do (`docs/FORMAT_NOTES.md`, TES4 / HEDR). The game discards that prefix when it opens a file: it clamps a stored value at or below 0x7FF to 0x800, keeps the low 24 bits and ORs in the plugin's compile index. The Creation Kit applies the same clamp on header read (RE-016, GARDEN-TOOLS #520 comment 13469, evidence commit `78e013b2`). Radicle therefore reads the field as its low 24 bits (`k_full_local_mask`) wherever it compares or allocates from it: in `reserve_local_form_key`, `lowered_light_next_form_id` and `renumber_local_record`. `reserve_local_form_key` takes the larger of that masked value and the plugin's own floor (0x800 for a full plugin, 0x001 for a light plugin that meets `allows_hardcoded_range`). A light plugin whose masked next is above 0x1000 is still refused with a `FormIdError` before any record is created, and the written value is unprefixed, so the 0xFF byte is dropped on the first allocation. Tests: `Radicle/tests/test_mutation.cpp:FullPluginMasksFfPrefixedHedrNext`, `Radicle/tests/test_mutation.cpp:FullPluginClampsMaskedHedrNextBelowTheFloor`, `Radicle/tests/test_mutation.cpp:LightPluginMasksFfPrefixedHedrNextInRange`, `Radicle/tests/test_mutation.cpp:LightPluginRefusesFfPrefixedHedrNextAboveTheLightCeiling`, `Radicle/tests/test_mutation.cpp:LoweredLightNextIgnoresTheFfPrefix`. One deliberate difference from the CK: the CK clamps a stored next at or below 0x7FF to 0x800 for every plugin, while Radicle lets a light plugin that meets `allows_hardcoded_range` allocate from 0x001 (xEdit's behavior; six of the nine shipped ESLs in `docs/ESL_FORMIDS.md` use IDs below 0x800). The engine and CK compare the raw value before masking (RE-016, #520 comment 13469, evidence commit `78e013b2`, `RE-Work/subsystems/plugin-io-garden-tools/small-file-compile-index-389/`), so a stored `0xFF000001` is not clamped and becomes local 1; Radicle masks first and then clamps to the plugin's own floor, the safer choice.

## Radicle/tests/test_header_versions_real.cpp:ReadsEveryOfficialMasterAndEslHeaderAndRewritesItIdentically

The test reads the plugins from `installed_master_and_cc_plugins` in `Radicle/tests/real_data_plugins.hpp`: the seven official masters present in `FO4_DATA_DIR`, plus the installed cc*.esl and cc*.esm Creations listed in `Fallout4.ccc` when that file is present, or all of them otherwise (names matched case-insensitively). It does not scan other files. then asserts the TES4 signature, a finite HEDR version and a byte-identical `write_plugin` result. Form version, HEDR version, flags and next object ID are recorded as properties only. Form version 131 and the 0x81/0x281 flags were observed on AE 1.11.240.0 and are not asserted, so the test runs on other builds without changes.

## Radicle/tools/corpus/ba2_measure.py:exe_file_version

`--json` output includes the Fallout4.exe file version read from the VS_FIXEDFILEINFO block (signature 0xFEEF04BD, then the two version words after the struct-version field). The executable is looked up in the parent folder of the Data folder. The signature search takes the first match in the file; no other match was checked. Without `--json` the Markdown table is unchanged.

## Radicle/tools/corpus/form_versions.py:walk

The census walks TES4 and then each GRUP recursively, reading only the 24-byte record and group headers and seeking past every payload, so it never decompresses. A child that overruns its parent or a truncated header raises and fails that plugin. `Radicle/docs/FORMAT_NOTES.md` (AE 1.11.240.0, 2026-10-12) holds the table it produced over the install's Data folder. The `Fallout4.exe` version comes from the VS_FIXEDFILEINFO block next to the Data folder.

## Radicle/tools/corpus/form_versions.py:expand

In data-folder mode `expand` adds the seven official masters found in the folder, then every cc*.esm and cc*.esl file. When `Fallout4.ccc` exists in the parent of the folder, only the cc files named in it are added (names trimmed, lowercased and read as `utf-8-sig`), so user copies are skipped; when the file is missing or lists no names, every cc file is added. This mirrors `Radicle/tests/real_data_plugins.hpp:installed_master_and_cc_plugins`. On the AE Data folder the script reports the 16 plugins of the census and no `ccOTMFO4001-Remnants - Copy.esl`.

## Radicle/src/forms/race.cpp:decode_race

The RACE form version 131 gate stays exact, and `src/forms/race.cpp:create_race` keeps the same gate. The AE census in `Radicle/docs/FORMAT_NOTES.md` found all 110 RACE records in the seven official masters at form version 131, plus 2 in `ccBGSFO4046-TesCan.esl` and 3 in `ccOTMFO4001-Remnants.esl` (NG and AE), all at form version 131, so the gate is unchanged. `Radicle/tests/test_race_real.cpp:RaceReal.EveryRaceDecodesWithoutFailure` decodes every official RACE under FO4_DATA_DIR. The census was also run on OG 1.10.163.0 and NG 1.10.984.0 on 2026-10-12: the minimum and maximum form version per signature match AE for all 138 signatures, and RACE is 131 on all three releases (`Radicle/docs/FORMAT_NOTES.md`, Releases).

## Radicle/tests/real_data_plugins.hpp:installed_master_and_cc_plugins

Returns the seven official masters present in the Data folder (Fallout4.esm, DLCRobot, DLCworkshop01-03, DLCCoast, DLCNukaWorld) followed by every `cc*.esl` and `cc*.esm` file, matched case-insensitively. When `Fallout4.ccc` exists in the game root (`dir / ".." / "Fallout4.ccc"`), only cc files whose lowercased name is listed there are returned, so user copies such as `ccOTMFO4001-Remnants - Copy.esl` are skipped; when the file is missing or lists no names, the prefix rule applies alone. Listed names are trimmed and lowercased, and the helper strips a UTF-8 BOM. The Radicle readers do not strip it or trim (RE-020, `Radicle/src/loadorder/plugin_list.cpp:parse_ccc`), so the helper is looser than the engine. Missing masters are skipped rather than reported, so the corpus tests keep their skip behavior on partial installs. `archive_load_order` then pairs each name with its `<plugin> - *.ba2` archives, so a `cc` archive is opened only when its plugin is present in Data. Shared by `test_nif_corpus.cpp`, `test_ba2_real.cpp` and `test_pex_corpus.cpp`.

## Radicle/tests/test_nif_corpus.cpp:ReadsEveryVanillaNifAndHandlesGeometryTypes

Reads NIFs from every general archive of the masters and cc plugins from `installed_master_and_cc_plugins`. Per-archive NIF counts and per-BS-version counts are recorded with `RecordProperty`. `read_nif` only accepts BS 130, so a NIF with another BS version fails as a parse error and is counted under the failure, not under a `bs_version_<n>` key; the BS number of a rejected file is not exposed by the reader.

## Radicle/tests/test_ba2_real.cpp:ParsesEveryVanillaStringTableInMastersAndCcArchives

Parses every `Strings/*.STRINGS`, `*.DLSTRINGS` and `*.ILSTRINGS` entry in the general archives of the masters and cc plugins with `fo4::localization::read_string_table`. Path matching normalizes separators and case. Counts are recorded per extension and per language suffix (the text after the last `_` in the file stem, `none` when absent). Only `.STRINGS` presence is asserted.

## Radicle/tests/test_pex_corpus.cpp:EveryDataDirArchivePexRoundTripsByteIdentically

Fallback for `FO4_DATA_DIR` when neither `FO4_PEX_CORPUS` nor `FO4_PEX_ARCHIVES` is set. It scans every general archive from `archive_load_order` for `.pex` entries instead of naming `Fallout4 - Misc.ba2`. On AE 1.11.240, `Fallout4 - Misc.ba2` holds 7875 PEX entries, and the DLC and some cc Main archives hold more, so the scan covers all of them. Found and identical counts are recorded per archive, and the same `roundTrip` and `expectClean` checks apply.

## Radicle/include/fo4/environment/vegetation.hpp:GrassData::water_filter_active

The stored water discriminator is a raw u32. Modes 0 through 5 mean a water filter applies; any larger value, including 6, 7 and 0xFFFFFFFF, means no filter, as RE-013 (#403 c7887) proves for the GRAS water mode. The method is a bare predicate. Its only caller in this repo is `Radicle/tests/test_vegetation.cpp`, so no runtime path applies the filter here; the runtime consumer is not decoded (RE-013 on #520). See `Radicle/docs/VEGETATION.md`.

## Radicle/src/environment/vegetation.cpp:landscape_grasses_runtime_view

Filters the stored LTEX GNAM list from `decode_landscape_grasses`. A null link is one whose owner is empty, as produced by `resolve_plugin_link` for a zero raw form ID. The stored list keeps every entry, including nulls. The view applies no slope, density or placement logic.

## Radicle/include/fo4/loadorder/load_order_files.hpp:assemble_load_order

Returns the loaded-file list for a data directory, a parsed plugin list and the `Fallout4.ccc` names. The stage order, master hoist and checked pass are in `Radicle/src/loadorder/assemble.cpp:assemble_load_order`. `failed` is set on the first checked-file failure and on a hoist cycle; entries show the state reached at that point. `PluginList::duplicates` carries the dropped repeats so `duplicate_entry` can be reported.

## Radicle/src/loadorder/assemble.cpp:assemble_load_order

The data directory overload lists the directory and passes the entries to the span overload, which holds the one assembly implementation. The span overload takes already-resolved plugin files, such as a mod manager's virtual-data winners. `discover` applies the same filter as the directory scan (plugin extension, regular file, non-zero size) and keeps the first path for each plugin key through `try_emplace`, so a repeated name resolves to the earlier path. The directory overload's output is unchanged. Tests: `Radicle/tests/test_load_order_files.cpp:ResolvedFilesFromSeveralFoldersAssembleLikeOneDataFolder`, `Radicle/tests/test_load_order_files.cpp:ResolvedFilesKeepTheFirstPathForADuplicateName`, `Radicle/tests/test_load_order_files.cpp:ResolvedFilesSkipNonPluginAndZeroByteFiles`. No C ABI change; `RADICLE_ABI_VERSION` stays 40.

## Radicle/src/loadorder/plugin_list.cpp:parse_plugin_list

Engine rules from RE-020 (#520 c14028). The text is first cut at the first 0x1A byte (Ctrl+Z; text mode ends the read there, PARTIAL: measured on the CRT), then put through text-mode translation (CRLF to LF, a lone CR kept), then split into `fgets` chunks of at most 511 bytes, each ending at its LF; a chunk of one byte or less is skipped, so `A` plus LF is the name `A` and a final unterminated `B` is skipped. The UTF-8 BOM is not stripped: it stays on line 1 and blocks the `*` and `#` tests there. `#` and `*` count only at byte 0, `*` after the `#` test, so `*#x` is the active name `#x`. Nothing is trimmed. Names are decoded from Windows-1252 by `Radicle/src/text/encoding.cpp:windows1252_to_utf8`, so undefined bytes map to C1 code points; other ANSI code pages are not modeled. A name with a byte below 0x20 goes to `invalid_lines` before decoding; an embedded NUL makes the engine stop the name at the NUL, while Radicle reports the whole line there. The engine never matches those names; Radicle reports them through `invalid_lines`, which `assemble_load_order` turns into `invalid_name`. Duplicates use the `PluginKey` map: the first line keeps its position and becomes active if a later duplicate is starred, and each duplicate goes to `duplicates`. `PluginKey` folds Unicode; the engine folds ASCII only (RE-020, INFERRED), which differs only for non-ASCII names. Tests: `Radicle/tests/test_load_order_files.cpp:StarredDuplicateActivatesFirstEntryAndIsRecorded`, `Radicle/tests/test_load_order_files.cpp:MixedCaseDuplicateKeepsActiveFirstEntry`, `Radicle/tests/test_load_order_files.cpp:NamesWithControlCharactersAreSkippedAndReported`, `Radicle/tests/test_load_order_files.cpp:Utf8BomStaysOnLineOneAndBlocksStarAndComment`, `Radicle/tests/test_load_order_files.cpp:TrailingSpaceIsKeptAndTabMakesAnInvalidLine`, `Radicle/tests/test_load_order_files.cpp:HashAndStarCountOnlyAtByteZero`, `Radicle/tests/test_load_order_files.cpp:OneByteChunksAreSkippedUnlessTheyEndInLf`, `Radicle/tests/test_load_order_files.cpp:LongLineIsReadInFgetsChunks`, `Radicle/tests/test_load_order_files.cpp:Windows1252HighByteDecodesToUtf8`, `Radicle/tests/test_load_order_files.cpp:UndefinedWindows1252ByteDecodesToC1Code`, `Radicle/tests/test_load_order_files.cpp:LoneCrInsideALineMakesAnInvalidLine`, `Radicle/tests/test_load_order_files.cpp:CtrlZEndsThePluginListText`, `Radicle/tests/test_load_order_files.cpp:FullChunkWithLfGivesOneName`, `Radicle/tests/test_load_order_files.cpp:OverlongChunkThenOneByteNameWithLf`, `Radicle/tests/test_load_order_files.cpp:BomOnlyFileIsOneUnstarredEntry`.

## Radicle/bindings/csharp/Radicle.Interop/LoadOrder.cs:SplitTrailing

Splits a row into a fixed number of trailing fields counted from the right, so a TAB inside the first field stays there and cannot throw. `ReadPluginList` uses it for `name\tactive\tline` and `Assemble` for `name\tchecked\tmaster\tlight`. The native plugin-list parser already refuses control characters in names, so this is a second guard. Test: `Radicle/bindings/csharp/Radicle.Interop.Tests/LoadOrderTests.cs:ReadPluginListSkipsNamesWithControlCharactersWithoutThrowing`.

## Radicle/src/loadorder/plugin_list.cpp:parse_ccc

Engine rule from RE-020 (#520 c14028): the engine reads the stream and `strtok_s`-splits on CR and LF, so empty tokens are skipped and there is no comment, star, BOM or trim handling. This parser splits the same way, keeps spaces and tabs, and decodes each token with `Radicle/src/text/encoding.cpp:windows1252_to_utf8`; a token with a control character below 0x20 (such as a TAB) is dropped, because the engine can never match it, it would break the TAB-joined C API rows, and `parse_ccc` has no line report. Names keep their case. The assembly matches names case-insensitively, so the engine's lower-casing does not change the result. Tests: `Radicle/tests/test_load_order_files.cpp:CccKeepsSpacesAndSkipsEmptyTokens`, `Radicle/tests/test_load_order_files.cpp:CccDropsTokensWithAControlCharacter`, `Radicle/tests/test_load_order_files.cpp:CccSplitsOnCarriageReturn`, `Radicle/tests/test_load_order_files.cpp:CccBomStaysOnFirstNameAndUndefinedByteDecodesToC1`.

## Radicle/src/loadorder/assemble.cpp:read_tes4_header

Reads the 24-byte record header and the TES4 data only, passes those bytes to `read_plugin`, so no groups are parsed. A data size above 16 MiB is rejected before the file size check and before the buffer is resized, so a corrupt header cannot force a large allocation. A data size that overruns the file is rejected the same way. Any throw is reported by `assemble_load_order` as `unreadable_header`.

## Radicle/src/loadorder/assemble.cpp:find_master_cycle

Depth-first search over present master-flag files, following edges only to present master-flag masters. A file that lists itself closes a one-file cycle. The `path` stack holds the current chain, so the cycle is the chain from the repeated file to the end.

## Radicle/src/loadorder/assemble.cpp:master_cycle

Runs `find_master_cycle` from every unvisited master-flag file and returns the first cycle found, or an empty vector. `assemble_load_order` calls it before the hoist: a cycle is reported as `hoist_cycle` naming its files, sets `failed`, and skips the hoist and the checked pass.

## Radicle/src/loadorder/assemble.cpp:hoist_masters

Inner loop per the RE record section 4.4 (`RE-Work/subsystems/radicle-re-garden-tools/light-plugin-index-389-28/README.md`). For a master-flag dependent at `pos`, each MAST name in order that is present at or after `pos` is moved to `pos`, ahead of the dependent and of masters moved earlier in the same scan. Independent masters therefore end in reverse MAST order. The walk does not advance after a move, so the moved masters are processed next. The move count is capped at `n*n + n` as a backstop; past it the function records `hoist_cycle` and returns false. `master_cycle` runs first, so the cap only catches inputs the search does not.

## Radicle/src/loadorder/assemble.cpp:settle_checked

Recursive checked pass. A checked file above HEDR 1.0 is unchecked with `version_unchecked` and is not recursed. Otherwise every master must be present and at or below 1.0; a missing or too-new master fails the assembly. Unchecked masters are checked and recursed. The comparison is a float against `1.0f`.

## Radicle/src/c_api_loadorder.cpp:radicle_load_order_assemble

Rows are `name\tchecked\tmaster_flag\tlight`; findings are `code\tplugin\tdetail`. `*out_failed` is 1 when the assembly aborted, and the call still returns OK. It is written only after both arrays are copied. A data directory that is not a directory returns IO_ERROR, and so does a plugin list or `Fallout4.ccc` that cannot be read (`std::exception`); `bad_alloc` maps to OUT_OF_MEMORY and anything else to INTERNAL_ERROR. If the findings copy fails, the rows already copied are freed with `std::free`, not `radicle_free_string_array`, so the error message set by the failed copy is kept.

## Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION

ABI 40 adds `radicle_plugin_list_read`, `radicle_ccc_read` and `radicle_load_order_assemble` (`Radicle/include/fo4/c_api_loadorder.h`) and the `radicle_official_table` enum. The pin is `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsForty`.

## Radicle/tests/test_load_order_real.cpp:InstalledDataFollowsFallout4OfficialThenCccOrder

Skips unless `FO4_DATA_DIR` is set. Picks the AE table for 1.11.x builds and the OG/NG table otherwise. Asserts that `Fallout4.esm` is first, that installed official plugins follow in table order, and that present `Fallout4.ccc` entries follow after them in file order. `order.failed` is asserted false only when no `missing_master` finding is present.

## Radicle/tests/test_c_api_loadorder.cpp:CApiLoadOrder

Covers the C ABI for the load order: null arguments return INVALID_ARGUMENT, an unknown table value is rejected with the outputs cleared, a non-directory data directory and a missing plugin list return IO_ERROR with the outputs cleared, and the happy path frees its rows and findings with `radicle_free_string_array`. Outputs start as non-null sentinels so a failure that does not clear them is caught.

## Radicle/tests/temp_data_dir.hpp:TempDataDir

Creates a directory named with the process id and a process-wide atomic counter, so parallel test binaries and repeated fixtures do not collide. `write` sets each file's time to one hour ago plus `rank` seconds, which gives the name tie-break and the timestamp order distinct, deterministic values.

## Radicle/src/forms/static_collection.cpp:decode_static_collection_parts

The in-progress part is a plain local `StaticCollectionPartView` with an `open` flag instead of a `std::optional`. GCC 15 at -O3 reported a false -Wmaybe-uninitialized through the optional's inlined vector destructor, so the optional was replaced. The behavior is unchanged.

## Radicle/src/forms/npc.cpp:replace_run

source:xEdit NPC_ lists are contiguous runs in the record layout, so one edit replaces one run as a unit. A run with other subrecords between its entries is refused rather than reordered. An encoded run equal to the stored run returns false before the record is marked modified, so a no-op set leaves a parsed plugin clean. An empty list drops its count subrecord (PRKZ or COCT) along with its entries.

## Radicle/src/forms/npc.cpp:insertion_index

Places a missing run after the last subrecord whose rank in `k_order` is below the run's anchor. `k_order` follows the top-level order of the NPC_ record at xEdit Core/wbDefinitionsFO4.pas:10286-10489. It throws when no earlier subrecord exists instead of guessing a position.

## Radicle/src/forms/npc.cpp:encode_perks

source:xEdit PRKZ is a count path for PRKR (FO4:10393). The count is written with the run, so PRKZ equals the entry count. Removing the last perk drops PRKZ with the run.

## Radicle/src/forms/npc.cpp:encode_inventory

source:xEdit COCT is a count path for CNTO (xEdit 93cc0bc5 Core/wbDefinitionsFO4.pas:3315-3316). COCT is tolerated: decode returns the CNTO entries present without checking COCT, and edits rewrite COCT to the CNTO count. Fallout4.esm NPC_ 0x0003D62A is the example, with COCT=2 and one CNTO. Removing the last item drops COCT with the run. COED is emitted only directly after its CNTO, matching its nesting at FO4:3300-3314.

## Radicle/src/forms/npc.cpp:decode_npc_inventory

source:xEdit COED owner allows NULL (FO4:3291), so owner 0 decodes to no owner. A COED that does not directly follow a CNTO throws, because xEdit nests COED inside the CNTO group.

## Radicle/src/forms/npc.cpp:decode_npc_perks

source:xEdit PRKZ is checked against the PRKR count when PRKZ is present (FO4:10393). An absent PRKZ is accepted.

## Radicle/schema/fallout4/fields.json:NPC_

source:xEdit NPC_ is FO4:10286-10489. Fixed structs (ACBS 20 bytes, DNAM 8, OBND 12, MWGT 12, MRSV 20, QNAM 16) carry payload_size, so a wrong-size payload hides its fields. AIDT uses minimum_payload_size 21 for AINoSlowApproach in place of xEdit's record-version gate (FO4:6229). Level and Level Mult are one raw field because the schema has no union; ACBS Flags bit 7 selects which one xEdit shows (Common:5437-5449). PRKZ, COCT, SPCT and CS2H are left out of the scalars because a generic write would desync them from their lists. Configuration and template flag names are the `flags` names of `NPC_\ConfigurationFlags` and `NPC_\TemplateFlags` (see the `records[NPC_] — enum and flags names` note below).

## Radicle/include/fo4/forms/npc.hpp:NpcItemExtraData

source:xEdit global_or_rank is stored raw. xEdit reads it as a Global FormID or as a rank depending on the owner (Common:5451-5469); this layer does not choose between them.

## Radicle/tests/test_npc_real.cpp:NpcReal

Runs only when FO4_DATA_DIR is set. For every NPC_ in each official master it decodes each scalar that is present and the three lists. The PRKZ count check is enforced by the decoder, so a mismatch fails here. COCT is tolerated: Fallout4.esm NPC_ 0x0003D62A has COCT=2 with one CNTO, and decode returns the entries present.

## Radicle/src/forms/npc.cpp:decode_npc_factions

Measured 2026-10-13 on the official masters, not from memory: every NPC_ SNAM is a 5-byte payload (Fallout4.esm 6571 SNAM, DLCRobot.esm 371, DLCworkshop01.esm 84, DLCCoast.esm 773, DLCworkshop02.esm 2, DLCworkshop03.esm 93, DLCNukaWorld.esm 1193). CI job 22303 first failed on DLCRobot.esm NPC_ 0x002A9F, whose SNAM is 5 bytes. The 8-byte check was wrong. The NPC_ record holds one SNAM kind, the faction list, so the edit run is the whole SNAM group. Test: `tests/test_npc_lists.cpp:NpcLists.FactionEditsTouchOnlyTheSnamRun`.

## Radicle/include/fo4/forms/faction.hpp:FactionCombatReaction

xEdit FO4 `wbFactionRelations` (`Core/wbDefinitionsCommon.pas` lines 8540-8557) gives the
Group Combat Reaction enum as 0 Neutral, 1 Enemy, 2 Ally, 3 Friend (lines 8549-8553). The
Radicle names are `neutral`, `enemy`, `ally` and `friendly`. A value above 3 is malformed
and decode throws `FactionError`. xEdit's `IsTES4` branch (line 8547) omits this member
for TES4 only, so FO4 always carries it.

## Radicle/include/fo4/forms/faction.hpp:FactionRelation

XNAM is a 12-byte relation: form ID at 0, Modifier as int32 at 4, and reaction as u32 at 8
(xEdit lines 8544-8553). The form ID is allowed to be a FACT or a RACE (line 8545). Radicle
treats a null form ID as malformed. The schema's `FACT\RelationFaction` formlink row uses
the same two targets, so `check` validates them as well. The layout is pinned at xEdit
93cc0bc5 `Core/wbDefinitionsCommon.pas:8543-8553` (XNAM, reaction 0..3), and
`Radicle/tests/test_faction_real.cpp:FactionReal` is the check against the masters.

## Radicle/include/fo4/forms/faction.hpp:FactionRank

RNAM is a u32 rank number (`Core/wbDefinitionsFO4.pas` line 6773). MNAM and FNAM are
localized strings (lines 6774-6775, `cpTranslate`), so they go through
`schema::read_localized_string`. INAM (line 6776) is an unused string. Radicle keeps its raw
bytes so an edit does not drop them. A rank is an RNAM-started group (`wbRStructSK([0], ...)`,
line 6772) with members in the order RNAM, MNAM, FNAM, INAM. A member that is out of order,
or is not preceded by an RNAM in the same group, is malformed. The layout is pinned at xEdit
93cc0bc5 `Core/wbDefinitionsFO4.pas:6771-6776` (RNAM/MNAM/FNAM/INAM), and
`Radicle/tests/test_faction_real.cpp:FactionReal` is the check against the masters.

## Radicle/src/forms/faction.cpp:rank_member_stage

The stage order RNAM, MNAM, FNAM, INAM is the xEdit member order at
`Core/wbDefinitionsFO4.pas` lines 6772-6776. A member is accepted only when its stage is
higher than the previous member of the same rank group. Any other record subrecord closes the
group, so a later MNAM cannot attach to an earlier rank.

## Radicle/src/forms/faction.cpp:add_faction_relation

Relations are inserted after the last XNAM. With none, the new XNAM goes before the first
DATA, which is where xEdit places the Relations array (line 6788, before the DATA struct
at line 6789). Ranks are inserted after the last rank group. With none, they go before the
first VEND, VENC, VENV, PLVD, CITC or CTDA, which follow the CRVA struct and the Ranks
array in xEdit (lines 6830-6845).

## Radicle/src/forms/faction.cpp:add_faction_rank

Edits build the next plugin state on a copy and replace the caller's plugin only after every
check passes, so a rejected write leaves the plugin unchanged. Title strings must match the
plugin's localized flag: a localized plugin takes string IDs, an inline plugin takes text.
Text containing a NUL is refused.

## Radicle/schema/fallout4/fields.json:FACT

CRVA is gated by minimum payload sizes 16, 18 and 20 on the Steal Multiplier, Escape and
Werewolf fields (xEdit `wbStruct(CRVA, ...).SetOptionalFrom(7)`, `wbDefinitionsFO4.pas`
line 6828). The first seven members are required, so their offset plus width is the gate.
VENV has no optional tail (`SetRequired`, line 6842), so each VENV field carries
`payload_size` 12, which hides every VENV field in a payload of any other size. Flags is a
whole-payload u32 (DATA, lines 6789-6810), so a wrong width throws `ScalarError`.
The formlink rows for JAIL, WAIT, STOL, PLCN, CRGR, JOUT, VEND and VENC are not nullable
because xEdit lists no NULL among their targets (lines 6811-6816 and 6831-6832). The
XNAM relation row is repeated with payload size 12 and targets FACT and RACE (line 8545).
Localized subrecords are FULL, MNAM and FNAM (lines 3692, 6774, 6775).

## Radicle/src/schema/form_link_mixed.cpp:faction_formid_offsets

FACT XNAM is a 12-byte relation with the faction form ID at offset 0. Modifier and reaction are not form IDs, so only offset 0 is remapped. The table is qualified by record signature because XNAM means other things elsewhere: PACK XNAM is handled by `behavior_formid_offsets` in `src/schema/behavior_layout.cpp`, and it is checked before this function. A payload that is not exactly 12 bytes throws under strict remap and is skipped otherwise. Test: `Radicle/tests/test_packed_formlink_arrays.cpp:FactionXnamRelationFormIdRemapsAndKeepsModifierAndReaction` and `FactionXnamWrongSizeFailsClosedUnderStrictRemap`.

## Radicle/include/fo4/forms/leveled_list.hpp:LeveledListEntry

Layout from xEdit `Core/wbDefinitionsCommon.pas:8791-8802` (`wbLeveledListEntry`, FO4 branch through `IsFO4Plus`): level u16 at 0, two bytes at 2-3, item form ID at 4, count u16 at 8, chance none u8 at 10, one byte at 11. The two byte pairs are kept as read. TODO-verify: a one-off byte check of the shipped `Fallout4.esm` on 2026-10-12 (scratch script, not a repo test) found non-zero values in those bytes and no LVLO other than 12 bytes; `LeveledListReal` is the repo check; it runs in the `native-windows-real-data` job, and no pass is recorded in these notes.

## Radicle/include/fo4/forms/leveled_list.hpp:LeveledListExtraData

Layout from xEdit `Core/wbDefinitionsFO4.pas:3290-3298` (`wbCOED`): owner FormID at 0 (NULL allowed), a union at 4, item condition float at 8. The union stays raw because `Core/wbDefinitionsCommon.pas:5451-5469` (`wbCOEDOwnerDecider`) picks its meaning from the owner's signature, which Plugin decode cannot resolve across masters. TODO-verify, same status as LeveledListEntry.

## Radicle/src/forms/leveled_list.cpp:decode_leveled_list

Reads the LVLI, LVLN and LVSP header and entries in one pass. A COED attaches only when it directly follows an LVLO, and it is an error otherwise, because xEdit's `wbRStructExSK([0], [1], ...)` (`Core/wbDefinitionsFO4.pas:9998-10004`, `10021-10027`) only accepts it there. Entries with no LLCT are an error, and a present LLCT must equal the entry count (`Core/wbDefinitionsFO4.pas:3278`). LVLG and LVSG with a NULL FormID decode to no value; the shipped masters have none. ONAM goes through `schema::read_localized_string`, so a localized plugin keeps its string ID. Unmodeled subrecords (LLKC, the model group) are skipped, not rejected.

## Radicle/src/forms/leveled_list.cpp:replace_leveled_list_entries

Rebuilds the entry region. Every LVLO, and the COED directly after each LVLO, is dropped. The new entries go at the first existing LVLO; with no LVLO they go at the existing LLCT; with neither they go after the last LVLD, LVLM, LVLF or LVLG, or at the end. LLCT is written as the entry count. Unmodeled subrecords keep their place. A replacement equal to the current entries returns before any write, so a parsed plugin is not dirtied. The old LVSP code placed the new entries after LVLF only, so a record with LVLG and no LLCT now gets them after LVLG, which is the xEdit order.

## Radicle/src/forms/leveled_list.cpp:insert_leveled_list_entry

Copy, edit, replace. The record is decoded, the entry vector is changed, and `replace_leveled_list_entries` writes it, so LLCT always follows the count and an out-of-range index throws before anything is written. `remove_leveled_list_entry` and `set_leveled_list_entry` follow the same path.

## Radicle/src/forms/leveled_list.cpp:set_leveled_list_settings

The LVSP settings logic, moved here unchanged in behavior: a missing LVLD, LVLM or LVLF is inserted before the first LLCT or LVLO, and existing ones are rewritten in place. LVLG and LVSG are untouched.

## Radicle/src/forms/leveled_spell.cpp:decode_leveled_spell

The LVSP public API is now a thin layer over `decode_leveled_list`. A COED after an LVLO is attached to the shared entry but has no field in `LeveledSpellEntry`, so an LVSP replacement drops it. xEdit defines no COED on LVSP and the shipped masters have no LVSP, so nothing real is lost. LVSP also now fails when entries exist without LLCT, where the old code accepted that.

## Radicle/include/fo4/forms/leveled_spell.hpp:LeveledSpellError

An alias of `LeveledListError`. The LVSP tests and `leveled_spell_creation.cpp` keep catching `LeveledSpellError`, so the public name is unchanged.

## Radicle/schema/fallout4/fields.json:records[WEAP]

WEAP is declared only in the generated tables, as `schema/fallout4/README.md` requires. The authoring rows follow xEdit's definition order in `wbDefinitionsFO4.pas` 12882-13025, with line-by-line citations in `Radicle/docs/WEAPONS.md`. FULL is left out of the authoring list: the object-template combinations also write FULL (`wbDefinitionsFO4.pas:5525`), and the authoring order cannot hold a signature twice. OBND (`wbDefinitionsCommon.pas:9089` SetRequired) and DNAM are min 1. DNAM min 1 follows the PROJ, HAZD and ARMA DNAM precedent, which xEdit does not mark. No authoring default is registered, so create and add of these subrecords are not claimed.

Embedded scalars carry `payload_size` 132 (DNAM), 41 (FNAM) and 12 (CRDT), which are the sums of their xEdit member widths. xEdit has no version condition on them. A subrecord of another size makes its scalars absent to the reader, and `Radicle/tests/test_weapon_real.cpp:WeaponReal` reports that size as a failure.

## Radicle/src/forms/weapon.cpp:decode_weapon_damage_types

DAMA is a count-free array of 8-byte entries (`type`, `amount`). From form version 152 each entry is 12 bytes with a Curve Table FormID after them, as `wbDamageTypeArray` in `Core/wbDefinitionsCommon.pas:8454-8463` gates it with `wbFromVersion(152, ...)` at 8460. The FormLink remap already has this layout at `Radicle/src/schema/form_link_mixed.cpp:k_repeated_struct_layouts`. A payload that is not a whole number of entries throws `WeaponError`, and an absent DAMA returns an empty list. The layout is xEdit-derived; `Radicle/tests/test_weapon_real.cpp:WeaponReal` is the check against the masters.

## Radicle/tests/test_scalar_embedded.cpp:ScalarEmbedded.ReadsWeaponDnamAtMeasuredOffsetsAndTreatsAShortPayloadAsAbsent

A synthetic WEAP DNAM is written at the offsets xEdit gives (Speed at 4, OnHit at 36, Capacity at 52, AnimationType at 54, Stagger at 124). A payload shorter than the Stagger offset reads as absent, not as an error.

## Radicle/tests/test_weapon.cpp:WeaponDamageTypes

Covers the 8-byte form, the 12-byte form from version 152 with the Curve Table, the fail-closed size check, an absent DAMA (empty list) and a key with no WEAP record (throws). Its layout is xEdit-derived and marked TODO-verify in the file until CI runs `WeaponReal`.

## Radicle/tests/test_weapon_real.cpp:WeaponReal

Skips unless `FO4_DATA_DIR` is set, and matches the RaceReal pattern. For every WEAP in each official master it decodes DAMA and every WEAP scalar. A subrecord whose size differs from the scalar's `payload_size` is a failure, because the reader would otherwise report the field as absent. Fallout4.esm must contain at least one WEAP.

## .github/scripts/check_real_data_regex.py:real_data_tests

Read-only guard for GT-229. It finds every gtest `TEST`, `TEST_F`, `TEST_P` and Catch `TEST_CASE` that reads game data, and each one's `Suite.Name` (or Catch title) must match `REAL_DATA_TEST_REGEX`, which the script reads from the `native-ci.yml` env block. The check runs in the Linux native job, so a game-data test that the real-data job would never select fails CI. Comments and string literals are blanked before brace matching, so a skip message that names `FO4_DATA_DIR` does not count as a read.

It scans `.cpp`, `.hpp` and `.h` under both test roots. Helpers are function heads and lambdas named by their variable (`name = [...](...) -> T {`). A constructor, or a `SetUp` or `SetUpTestSuite` inside a `class` or `struct`, marks that class as a fixture when its span reads `FO4_DATA_DIR` or calls a helper. The helper set is a fixpoint across all scanned files: chains of any depth are followed, and a helper defined in a header counts everywhere it is included. The script still fails when no game-data test is found. Known gaps: out-of-line `Name::SetUp()` definitions are not tied to their class, and functions with a trailing return type (`auto f() -> T {`) are not detected as helpers. The script's own test is `.github/scripts/test_check_real_data_regex.py`.

## Radicle/tests/test_real_data_build.cpp:InstalledDataBuildIsKnown

GT-247. Runs only when `FO4_DATA_DIR` is set and asserts `detect_game_build` does not return `Unknown`, so the real-data job fails when `Fallout4.exe` is not next to the Data folder instead of reporting green. The suite name `RealDataBuildReal` matches the `[A-Za-z0-9_]*Real\.` alternative.

GT-275. It also asserts `exact_counts_known`, which is true only for AE 1.11.240.0. A Steam update changes the file version, which skips every exact-count assert while the job stays green; this test instead fails on any other build and prints `build.file_version`, so the pinned counts get re-checked. An exported `FO4_GAME_BUILD` overrides the exe read; only `1.11.240.0` satisfies both assertions.

## Radicle/tests/test_real_data_build.cpp:MissingExeIsUnknown

GT-245. Clears `FO4_GAME_BUILD` first, the same way `DetectReadsFalloutExeNextToDataFolder` does, so an exported override cannot turn the missing-exe case into a known build.

## Radicle/tests/test_navmesh_authoring.cpp:UneditedPluginWithNavmeshAndNaviRecordsWritesBackByteIdentical

GT-244. `write_plugin` returns the source bytes whenever `plugin_original_bytes` is non-empty, so the byte comparison could never fail. Clearing `plugin.original_range` (the pattern other round-trip tests use) forces the rebuild path before the comparison. The NAVI record in `navmesh_writer_fixture` is still a `ZZZZ` sentinel: a valid NAVI needs NVER and NVMI payloads, and the builder for those lives in an anonymous namespace in `test_navmesh.cpp`, so it was not copied here. Once the clear forced the rebuild, the first differing byte was the stored HEDR record count (file offset 34, 4 stored, 10 canonical); the fixture is fixed in `Radicle/tests/navmesh_writer_fixture.hpp:navmesh_writer_fixture`.

## Radicle/tests/test_water_authoring.cpp:UnmodifiedWaterRecordsRoundTripByteIdentical

GT-244. The plugin's original range is cleared before `write_plugin`, so the WATR bytes are rebuilt and compared, not copied from the source. The rebuild rewrites TES4 HEDR bytes 4..7 (record count), so the fixture must store the canonical count; `Radicle/tests/water_record_fixture.hpp:build` now writes it. Before the fix the first differing byte was file offset 34 (0x00 in the source, 0x03 rebuilt).

## Radicle/tests/water_record_fixture.hpp:build

HEDR record count is the number of distinct top-level GRUPs plus the number of entries (the WATR round-trip fixture stores 1 + 2 = 3). It was 0. `write_plugin` recomputes this count whenever TES4's original range is cleared.

## Radicle/tests/navmesh_writer_fixture.hpp:navmesh_writer_fixture

HEDR record count is 10: five top-level GRUPs (NAVM, two REFR, MISC, NAVI) plus five major records. It was 4. Same rule as `water_record_fixture.hpp:build`.

## Radicle/src/forms/placed_reference_copy.cpp:copy_placed_reference_as_new

Refuses an undecodable source reference, source parent CELL, or source parent WRLD before `reserve_local_form_key` runs. The reservation rewrites the TES4 HEDR next form ID of the working copy, so before this fix an undecodable source on a target with a short HEDR surfaced as `FormIdError` instead of the refusal. `copy_record_for_destination` keeps its own check for the other copy paths. Test: `tests/test_undecodable_edit.cpp:UndecodableEdit.CopyPlacedReferenceAsNewRefusesUndecodableSource`.

## Radicle/src/forms/info_copy.cpp:copy_info_as_new

Refuses an undecodable source INFO and an undecodable source parent DIAL (its QNAM is read by the copy) right after containment resolves, before `reserve_local_form_key`. Same ordering cause and test as `placed_reference_copy.cpp:copy_placed_reference_as_new`: `tests/test_undecodable_edit.cpp:UndecodableEdit.CopyInfoAsNewRefusesUndecodableSource`.

## Radicle/tests/test_sound_real.cpp:DecodesEverySoundRecordThroughNativeAndAbiAndPreservesSubrecords

GT-244. The test copies `env.plugin(0)` so it can clear the copy's original range; the environment's plugin stays read-only. The copy duplicates the decoded official master in memory, which matters on the real-data runner. The rewritten file is not compared byte for byte. Compressed records re-encode at zlib level 6 (`Radicle/src/plugin/writer.cpp:zlib_compress`, default `compress()`), while the official masters were compressed at levels 7 to 9 per record (FLEVEL 3 throughout; first 400 compressed records of DLCRobot.esm: 331 match level 7, 63 level 8, 6 level 9; Fallout4.esm: 268 level 7, 132 level 8), so no single level reproduces them. Re-encoding every compressed Fallout4.esm record at level 6 adds exactly 378074 bytes, the whole gap the CI reported (331154650 against 330776576). Only compressed bytes change; the data does not. The test therefore compares every record except TES4 (header flags, form version, compressed flag, and each subrecord's signature, extended size and data) against the source record. The record-range clear skips TES4: the official masters store HEDR counts that differ from the recomputed count (see `docs/HEDR_RECORD_COUNT.md`), so a TES4 rebuild would change their bytes. HEDR rebuild is covered by `tests/test_hedr_count.cpp`.

## Radicle/src/schema/form_links.cpp:remap_record_form_links — single-record refusal (GT-232)

`remap_record_form_links` calls `require_decodable` on entry, so each single-record caller refuses an undecodable record before any FormLink is rewritten. Callers: `src/plugin/copy_hierarchy.cpp:clone_record`, `src/plugin/mutation.cpp:copy_record_as_override`, `src/forms/placed_reference_copy.cpp:copy_record_for_destination`, `src/plugin/maintenance.cpp:previous_normalizes_equal` (its previous record is already screened by `identical_previous`), and the PET copy path in `PluginEditTool/native/src/plugin/RadiclePluginMaintenance.cpp:RadiclePluginBackend::invokeLegacyBackend`. `remap_form_links` still refuses the whole plugin in its pre-scan, so its behavior is unchanged. Test: `tests/test_undecodable_edit.cpp:UndecodableEdit.RemapRecordFormLinksRefusesUndecodableRecord`.

## Radicle/src/forms/quest_vmad.cpp:sync_quest_vmad_aliases, Radicle/src/forms/quest_vmad.cpp:sync_quest_vmad_stages — entry refusal (GT-232)

Both call `require_decodable` on the record before reading its VMAD. Their callers, `src/forms/quest_alias_edit.cpp` and `src/forms/quest_stages.cpp:write_quest_stages`, already pass a record from a decodable lookup, so the check is a guard for direct callers. Test: `tests/test_undecodable_edit.cpp:UndecodableEdit.SyncQuestVmadRefusesUndecodableRecord`.

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in — entry refusal (GT-232)

`write_ctda_conditions` goes through this function, so both are refused for an undecodable record before the condition list is scanned. `set_record_conditions` (`include/fo4/plugin/mutation.hpp`) and the scoped condition writer already check earlier. Test: `tests/test_undecodable_edit.cpp:UndecodableEdit.WriteCtdaConditionsRefusesUndecodableRecord`.

## Radicle/src/plugin/master_usage.cpp:scan_master_usage — undecodable records (GT-233)

`scan_master_usage` counts each undecodable non-TES4 record in `MasterUsage::undecodable_records`, and `MasterUsage::complete()` (`include/fo4/plugin/master_usage.hpp:MasterUsage::complete`) is false while that count is non-zero. The FormLinks of such a record are raw bytes the scan cannot see, so a master's use there is unproven. `radicle_env_unused_masters` (`src/c_api_maintenance.cpp:radicle_env_unused_masters`) therefore reports `complete` as 0, and `used_masters` keeps every master. Test: `tests/test_undecodable_edit.cpp:UndecodableEdit.MasterUsageIsIncompleteWhileRecordIsUndecodable`.

## Radicle/src/forms/cell.cpp:find_exterior_cell — undecodable CELL (GT-237)

A non-persistent CELL in an exterior sub-block that is undecodable throws `CellError` ("undecodable CELL inside an exterior sub-block") before its XCLC is read, so it is no longer reported as missing XCLC. The lookup still throws rather than skipping the cell, which keeps it fail-closed. Test: `tests/test_exterior_cell_creation.cpp:ExteriorCellCreation.UndecodableExistingCellFailsClosedWithDistinctMessage`.

## Radicle/src/plugin/reader.cpp:read_record — GT-236 coverage

Tests added in `tests/test_compression.cpp`: `Compression.ShortInflatedOutputLoadsAtProducedLength` pins the D6 behavior (a stream that inflates short loads at the produced length, not undecodable), and `Compression.PayloadUnderFourBytesFailsThePluginRead` pins the strict framing check, which throws `fo4::parse_error` outside the compressed-record `try`. Tests added in `tests/test_undecodable_edit.cpp`: `UndecodableEdit.FindIdenticalToMasterSkipsWhenMasterRecordIsUndecodable` covers the previous-record screen in `src/plugin/maintenance.cpp:identical_previous`; `UndecodableEdit.CopyPlacedReferenceAsNewRefusesUndecodableSource` and `UndecodableEdit.CopyPlacedReferenceAsNewRefusesUndecodableParentCell` cover the guards in `src/forms/placed_reference_copy.cpp:copy_placed_reference_as_new`; `UndecodableEdit.CopyInfoAsNewRefusesUndecodableSource` and `UndecodableEdit.CopyInfoAsNewRefusesUndecodableDestinationDial` cover the guards in `src/forms/info_copy.cpp:copy_info_as_new`.

## Radicle/tools/parity_matrix.py:render

`render` builds the per-signature table, the field-kind table and the opaque list that `docs/FO4_PARITY.md` carries between the parity-matrix markers. Inputs: `schema/fallout4/xedit_signatures.txt` (signature set and xEdit registration line), `schema/fallout4/fields.json` (scalar and string rows, FormLink fields, FormLink lists, authoring, keyword lists, localized subrecords), the `TYPED_MODULES` map (each cited file must name the signature, checked by `typed_modules`), the record signatures named in `src/schema/form_link_mixed.cpp` (`mixed_record_signatures`; subrecord-only handlers such as `obts_formid_offsets` and `behavior_formid_offsets`, and the record-specific checks in `src/schema/form_links.cpp`, are not attributed), and the registered `tests/*_real.cpp` files listed in `CMakeLists.txt` that name the signature in quotes (`real_test_sources`). Tier rule in `tier_of`: L4 has rows, FormLink fields or authoring; L2 has a typed module; L3 has FormLink lists only; L1 has a keyword list only; L0 has none. The dedicated PET route (old L5) is not derived. `main --check` compares the block with the doc and exits 1 when it is stale; `main --write` replaces it. The semantic letters stay hand-kept in `docs/FO4_PARITY.md` (Record semantic status).

## Radicle/tools/parity_matrix.py:load_signatures — xedit_signatures.txt pin and method

Pin: TES5Edit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`, SHA-256 `f0bacc90a7dbd62bf90ff570d1974932bd4b99488bd0da9bbcf4e01abdfb9c5e` with LF line endings (same hash as `docs/FO4_PARITY.md`). Method: strip Pascal `{...}`, `(* ... *)` and `//` comments while keeping string literals and line count, then take the bare four-character identifier that is the first argument of `wbRecord(`, `wbRefRecord(` or `ReferenceRecord(`. The first registration line is kept per signature. The result is 147 unique signatures; names that occur only in commented-out definitions (for example `WOOP`, `SCPT`, `TLOD`) are excluded. The lines match the xEdit line numbers cited in `docs/FO4_PARITY.md`.

## Radicle/tools/schema_codegen.py:parse_names

A scalar row declares names through its existing `presentation` key: `"presentation": "enum"` with `"names": [{ "value": N, "name": "..." }]`, or `"presentation": "flags"` with `"names": [{ "bit": N, "name": "..." }]`. `NAME_KEYS` maps each presentation to its entry key, and `parse_names` returns `{value, name}` entries for the generator; a flags entry becomes the mask `1 << bit`, so one lookup serves both presentations. Names are a list of entries rather than a JSON object because `json.loads` drops a repeated key without complaint, and the generator has to be able to reject a repeated value. Rules, each covered in `tools/test_field_address_codegen.py:SchemaCodegenTests`: only integer storage may carry names, and flags also need unsigned storage so a mask is never negative (`test_names_need_an_integer_enum_or_flags_row`, which also refuses `names` on `identity`, `percent_0_100` and string rows); an entry has exactly its key and `name`, the key is an integer (not a bool or a float) and the name is non-empty printable ASCII (`test_names_entries_are_checked`); names are unique, and so are values or bits (`test_names_reject_duplicates`); a flag bit is below 8 times the storage width, an enum value fits the storage, and the resulting value or mask lies inside the row's `minimum` and `maximum`, so no name stands for a value the writer would reject (`test_names_reject_values_outside_the_field`). The names are display metadata. They do not narrow the write bounds, a stored value with no name stays readable, and a table need not cover every storable value (the boolean rows are 0 to 255 with two names).

## Radicle/tools/schema_codegen.py:render

`render` emits one `constexpr ScalarName k_scalar_names_N[]` per distinct table, numbered in first-use order over the rows sorted by record and path, and then declares `k_scalar_fields` itself. `names_key` is the sharing key, so the two `SoundLevel` rows use one array and the seven boolean rows another. Before this change the generated file held only the row initializers and `src/schema/scalar.cpp` declared the array around its `#include`. The tables must precede the array that points at them, so the declaration moved into the generated file and `scalar.cpp` includes it at namespace scope. The five CMake `--check` commands and their arguments are unchanged, and the other four generated files are byte-identical. A row without names renders as before, with the trailing `minimum_payload_size` argument only when it is non-zero. A row with names always writes `minimum_payload_size` (0 when unset) because `names` follows it positionally. `CPP_PRESENTATIONS` writes the JSON presentation `enum` as `ScalarPresentation::enumeration`, since `enum` is a C++ keyword. Tests: `tools/test_field_address_codegen.py:SchemaCodegenTests.test_flags_names_become_single_bit_masks`, `test_enum_names_keep_their_values`, `test_identical_name_tables_are_emitted_once` and `test_rows_without_names_render_as_before`.

## Radicle/tools/schema_codegen.py:names_key

The sharing key of a name table: the tuple of `(value, name)` pairs in row order. Two rows share one `k_scalar_names_N` array only when their pairs match exactly, order included, whatever their presentation. `render` keys a dict by it, so the array numbers follow first use. Test: `tools/test_field_address_codegen.py:SchemaCodegenTests.test_identical_name_tables_are_emitted_once`.

## Radicle/tools/schema_codegen.py:NAME_KEYS

`PRESENTATIONS` is the accepted set. `NAME_KEYS` maps `enum` to the entry key `value` and `flags` to the entry key `bit`; a presentation with no entry there takes no names, which is how `parse_names` refuses `names` on `identity` and `percent_0_100`. `CPP_PRESENTATIONS` maps a JSON presentation to its C++ enumerator where the spelling differs (`enum` to `enumeration`). String rows list `names` in `STRING_FORBIDDEN`, so `load_fields` rejects them there.

## Radicle/src/schema/scalar.cpp:k_scalar_fields

`k_scalar_fields` and the `k_scalar_names_N` tables now come from one `#include "fo4/schema/generated_scalar_fields.inc"` at namespace scope inside the anonymous namespace. The include used to sit inside a braced array initializer, which cannot hold the tables (see the `render` note above).

## Radicle/include/fo4/schema/scalar.hpp:ScalarName

One entry of a name table. `value` is the stored integer for an `enumeration` row and the single-bit mask for a `flags` row; `int64_t` holds every u32 mask and every i32 enum value. The tables are the generated `k_scalar_names_N` arrays in `src/schema/scalar.cpp`, which have static storage, so a copied `ScalarDescriptor` stays valid for the life of the process.

## Radicle/include/fo4/schema/scalar.hpp:ScalarPresentation

`enumeration` and `flags` follow `identity` and `percent_0_100`. Nothing switches on this enum: encode, decode, the range checks and the field writer read `storage`, `minimum` and `maximum` only, so an enum or flags row is read and written exactly like an `identity` row of the same storage. A caller that shows names reads `ScalarDescriptor::names`.

## Radicle/include/fo4/schema/scalar.hpp:ScalarDescriptor

`names` is the last member and defaults to an empty span, so the aggregate initializers that already exist keep compiling without it: generated rows without names, `tests/test_field_writer.cpp:scalar_field` and `src/plugin/scene_writer.cpp:descriptor_for`. It is empty for every `identity` and `percent_0_100` row; `tests/test_scalar.cpp:ScalarSchema.OnlyEnumAndFlagsRowsCarryNames` checks that for FACT, NPC_, WEAP and REFR.

## Radicle/src/schema/scalar.cpp:scalar_value_name

Linear search of `ScalarDescriptor::names` for the entry whose `value` equals the argument, `std::nullopt` when there is none or the row has no names. For a flags row the match is an exact single-bit mask, so a multi-bit raw value has no name; a caller that wants every set flag walks `names` and tests `raw & value` itself. Tests: `tests/test_scalar.cpp:ScalarSchema.EnumRowExposesItsXEditNames` and `ScalarSchema.FlagsRowExposesOneMaskPerNamedBit`.

## Radicle/src/schema/scalar.cpp:scalar_name_value

The reverse of `scalar_value_name`: an exact, case-sensitive name match that returns the enum value or the flag mask, `std::nullopt` otherwise. Neither lookup throws. Tests as for `scalar_value_name`.

## Radicle/schema/fallout4/fields.json:records[FACT] — enum and flags names

Every name and value in the enum and flags tables comes from xEdit at pin 93cc0bc5 (`Core/wbDefinitionsFO4.pas`, `Core/wbDefinitionsCommon.pas`, `Core/wbInterface.pas`). `wbFlags([...])` takes the list position as the bit (`TwbFlagsDef.Create`, Core/wbInterface.pas:14421-14422) and `wbEnum([...])` takes it as the value (`TwbEnumDef.Create`, Core/wbInterface.pas:15020-15024). An empty string is an unnamed slot and is left out of the tables. The `{0x...}` and `{n}` comments in the source agree with the list position at every entry used here. Placeholder names such as `Unknown 3` and `Unused 21` are ordinary xEdit names and are kept. The `names` do not change any row's bounds.

- `FACT\Flags` (`flags`, u32, bits 0 to 16): Core/wbDefinitionsFO4.pas:6790-6808, names at 6791-6807.
- `FACT\CrimeArrest` (FO4:6818), `FACT\CrimeAttackOnSight` (6819), `FACT\VendorBuysStolenItems` (6838), `FACT\VendorSellsEverythingNotInList` (6839) and `FACT\VendorBuysNonStolenItems` (6840): `enum` with `wbBoolEnum`, Core/wbDefinitionsCommon.pas:7240-7247 (0 `False`, 1 `True`).

## Radicle/schema/fallout4/fields.json:records[NPC_] — enum and flags names

Same xEdit rules as the `records[FACT]` note above.

- `NPC_\ConfigurationFlags` (`flags`, u32 at ACBS +0, bits 0 to 31): Core/wbDefinitionsFO4.pas:10299-10332, names at 10300-10331.
- `NPC_\TemplateFlags` (`flags`, u16 at ACBS +14, bits 0 to 12): FO4:10342 uses `wbTemplateFlags`, Core/wbDefinitionsCommon.pas:8155-8178 (list at 8159-8177). For FO4, `IsFO3`, `IsFO76` and `IsSF1` return their second argument (Common:6173-6178, 6285-6290 and 6320-6325), so bit 3 is `Spell List`, bits 10 to 12 are `Def Pack List`, `Attack Data` and `Keywords`, and bits 13 to 15 are empty and have no entry.
- AIDT, all `enum` on u8: `NPC_\AIAggression` FO4:6216 with `wbAggressionEnum` Common:6973-6982; `NPC_\AIConfidence` 6217 with `wbConfidenceEnum` Common:7249-7259; `NPC_\AIMorality` 6219 with `wbMoralityEnum` Common:7691-7700; `NPC_\AIMood` 6220 with `wbMoodEnum` Common:7676-7689 (its `IsFO3` pairs resolve to the second name, `Angry` through `Disgusted`); `NPC_\AIAssistance` 6221 with `wbAssistanceEnum` Common:7023-7031; `NPC_\AIAggroRadiusBehavior` 6223 and `NPC_\AINoSlowApproach` 6229 with `wbBoolEnum` Common:7240-7247.
- `NPC_\SoundLevel` (`enum`, u32, NAM8): FO4:10431 with `wbSoundLevelEnum`, FO4:3424-3430.

## Radicle/schema/fallout4/fields.json:records[WEAP] — enum and flags names

Same xEdit rules as the `records[FACT]` note above.

- `WEAP\Flags` (`flags`, u32 at DNAM +48, bits 0 to 31): Core/wbDefinitionsFO4.pas:12924-12957, names at 12925-12956.
- `WEAP\ModelFlags` (MODF, u8) and `WEAP\FirstPersonModelFlags` (MO4F, u8): `wbMODF` (FO4:4260, listed at 1038 in `wbGenericModel`, 1030-1045) and `wbMO4F` (4263, listed at 12909) both use `wbModelFlags`, FO4:4256-4259: bit 0 `Has FaceBones Model`, bit 1 `Has 1stPerson Model`.
- `enum`: `WEAP\OnHit` (u32 at DNAM +36) FO4:12921 with `wbHitBehaviourEnum` 3677-3682; `WEAP\AnimationType` (u8 at +54) 12959-12972; `WEAP\SoundLevel` (u32 at +69) 12977 with `wbSoundLevelEnum` 3424-3430; `WEAP\Stagger` (u32 at +124) 12992 with `wbStaggerEnum` 5106-5112; `WEAP\Pattern` (u32 at FNAM +33) 13005-13010; `WEAP\MeleeSpeed` (u32, MASE) 13025-13031. The rows' existing bounds are exactly these enum ranges, so every name lies inside them.

## Radicle/tests/test_scalar.cpp:ScalarSchema.EnumRowExposesItsXEditNames

`WEAP\Stagger` (FO4:12992, 5106-5112) exposes its five names in value order, and `scalar_value_name` and `scalar_name_value` map both ways. A value outside the table and a name in the wrong case have no match.

## Radicle/tests/test_scalar.cpp:ScalarSchema.FlagsRowExposesOneMaskPerNamedBit

`FACT\Flags` (FO4:6790-6808) exposes 17 names whose values are the masks `1 << bit`. A single-bit mask maps to its name and back; a two-bit value and an unnamed bit have no name.

## Radicle/tests/test_scalar.cpp:ScalarSchema.EmbeddedFlagsRowKeepsItsRawU16Encoding

`NPC_\TemplateFlags` is a flags row embedded at ACBS +14 with 13 names. `decode_scalar` and `encode_scalar` still treat it as a plain u16 (bytes `01 10` are "4097" and back), which is the check that a named row keeps the existing read and write behavior.

## Radicle/tests/test_scalar.cpp:ScalarSchema.OnlyEnumAndFlagsRowsCarryNames

For every row of FACT, NPC_, WEAP and REFR, `names` is non-empty exactly when the presentation is `enumeration` or `flags`.
## tests/test_environment_strings.cpp:StringsEnvironmentTest::write

`StringsEnvironmentTest.RejectsMalformedTablesAndLanguages` failed once on the Windows MSVC job of the nomad/dev run at `9c1b3ae6` (job 22302): `Environment::load` threw "failed to load ...\radicle_strings_<n>\Base.esm: plugin parse error at 0x0: unable to open ..." for the first plugin the fixture had just written. The same test passed in the earlier Windows jobs 22085, 22121, 22247 and 22275, and its five sibling `StringsEnvironmentTest` processes passed beside it in the same job. Nothing on that path changed recently: the open in `src/plugin/reader.cpp:read_plugin_file` and `src/environment/environment.cpp:Environment::load` date from 2026-08-20 to 2026-09-30, and the test file from 2026-09-28. Names cannot collide, since ctest runs each test in its own process and the directory name is a steady clock count in 100 ns ticks. No product or fixture defect explains the failure, so the cause is unproven; it looks like a transient file-system error on the runner, whose default TEMP sits under its reparse-point home.

The fixture's `write` did not check its stream, so a failed write would have surfaced only as that "unable to open" at load. It now closes the stream and throws "cannot write <path>" on any stream failure. A recurrence then names the stage: a SetUp failure means the write failed, and the original message means the file existed but could not be opened.

## Radicle/src/forms/leveled_list.cpp:create_leveled_list — minimal LVLI, LVLN and LVSP

The record is EDID (only when an editor ID is given), OBND (12 zero bytes), LVLD, LVLM, LVLF and LLCT set to 0, in that order. xEdit requires OBND in LVLN, LVLI and LVSP (`wbObjectBounds` at Core/wbDefinitionsFO4.pas:9988, 10011, 10035, required per Core/wbDefinitionsCommon.pas:9074-9090), so the creator writes it. OBND is minimum 1 for LVLI, LVLN and LVSP in Radicle/schema/fallout4/fields.json (12-byte zero default), so the generated rows in `k_generated_subrecord_authoring_constraints` (Radicle/include/fo4/schema/generated_record_constraints.inc) require it too; that table is generated and is not edited here. The generic `create_record` path in Radicle/src/plugin/subrecord_authoring.cpp builds from those rows, so it writes OBND as well as the typed creators. The LVSP order is the one `create_leveled_spell` (Radicle/src/forms/leveled_spell_creation.cpp:create_leveled_spell) proves, with OBND first because there is no EDID. The authoring rows require LVLD and LVLF exactly once, and the minimal record has no LVLG, LVSG or ONAM. The xEdit record definitions are Core/wbDefinitionsFO4.pas:9986 (LVLN), 10009 (LVLI) and 10033 (LVSP), and the LVLF names for LVLI are at 10014-10018. Flags above 0x07 are refused, as in `create_leveled_spell`. Entries are not part of the creator: they are added later through insert or set, which validate them.

## Radicle/src/forms/leveled_list.cpp:validate_entry_target — LVLO target types

Allowed local entry types, from the xEdit LVLO definitions:

- LVLI: the 44 non-NULL signatures of `sigBaseObjects` (Core/wbDefinitionsFO4.pas:74-83), used by the LVLI entry at line 10023.
- LVLN: `LVLN` and `NPC_` (Core/wbDefinitionsFO4.pas:10000).
- LVSP: `LVSP` and `SPEL` (Core/wbDefinitionsFO4.pas:10046).

Every entry uses `wbLeveledListEntry` (Core/wbDefinitionsCommon.pas:8788-8794), whose `wbFormIDCk(aObjectName, aSigs)` at 8794 is the only signature check. `validate_entry_target` runs from `replace_leveled_list_entries`, which insert and set both call. It checks only entries whose item FormKey is not already in the list (`replace_leveled_list_entries` matches on `item`, not on the whole entry), so a level or count change to an existing entry does not re-check its target, and a remove does not check at all. A local FormKey that does not resolve is refused as `LeveledListError`, as `validate_local_entry_targets` in leveled_spell_creation.cpp does. A foreign FormKey is not checked, because `fo4::Plugin` carries no master records and the signature is unknown here. PET's LVSP creation checks foreign spells against the load-order graph (PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp, LVSP branch), but the LVLI and LVLN entry edits in PluginEditTool/native/src/plugin/RadiclePluginLeveled.cpp do not yet. The fixture `standard_plugin` in Radicle/tests/test_leveled_list.cpp carries a local NPC_ at 000810, because two replacement tests point an entry there.

## Radicle/src/forms/faction.cpp:create_faction — minimal FACT

The record is EDID (optional), FULL (optional), DATA (u32 flags), CRVA (20 bytes) and VENV (12 bytes), in that order. In xEdit, the FACT record is at Core/wbDefinitionsFO4.pas:6785, and DATA (6789, `SetRequired` at 6810), CRVA (6817, `SetRequired` at 6829) and VENV (6833, `SetRequired` at 6842) are all required. The 20 and 12 bytes are the FACT CRVA and VENV authoring defaults, looked up by signature from the generated constraints in generated_record_constraints.inc, which match the xEdit defaults: Arrest and Attack On Sight set, End Hour 24, and Buys Stolen Items and Buys NonStolen Items set. The flag mask 0x0001FFFF is the set of names in the DATA flags list (Core/wbDefinitionsFO4.pas:6790-6807). A full name is refused in a localized plugin, as `create_race` refuses DESC, because a localized FULL holds a string-table ID and not inline text.

## Radicle/src/graph/check_errors.cpp:info_topics

An INFO's DIAL topic is the label of its nearest enclosing topic-children group (GRUP type 7). Records outside such a group map to no topic. The map covers only the plugin being checked. A label that is null or uses index 0xFF maps to an empty key, which resolves to no loaded DIAL. A label whose master index is past the plugin's master table maps to no topic, so no finding is raised for it. No xEdit validation proves the group-to-DIAL mapping: `Core/wbDefinitionsFO4.pas:1017-1025` is inside `wbConditionQuestOverlay`, a display callback for a condition's quest.

## Radicle/src/graph/check_errors.cpp:check_dialogue_links

Dialogue stored-link checks. Each rule reads only stored subrecords. A stored form ID whose master index is past the plugin's master table is skipped, so neither rule below fires for it.

- `branch_starting_topic_not_dial` (error): DLBR `SNAM` (Starting Topic) must target a loaded DIAL, including a missing target. Cites `Core/wbDefinitionsFO4.pas:8792` (`wbFormIDCk(SNAM, 'Starting Topic', [DIAL]).SetRequired`). DLBR has no record row in the generated schema, so the slot is unproven. The generic `check_links` pass does see the `SNAM`, because `formid_subrecords.inc` lists it and a 4-byte payload is remapped as a form ID. For a missing target that pass is skipped (see `check_links` below), so this rule is the only finding.
- `deleted_target` (warning): a DLBR `SNAM` whose loaded winner is deleted. This comes only from the generic `check_links` `deleted_target` check, which already covers the DLBR SNAM link; `check_dialogue_links` adds no second warning for it.
- `topic_children_not_dial` (warning): the label of an INFO's topic-children group must be a loaded DIAL. This is a structural warning without xEdit enforcement: `Core/wbDefinitionsFO4.pas:1017-1025` is `wbConditionQuestOverlay`, a display callback that does nothing when ChildrenOf is not a DIAL, so it is not a validation.
- Not added: DIAL `BNAM` (Branch, `wbDefinitionsFO4.pas:6318`) and DIAL `QNAM` (Quest, `:6319`) must target DLBR and QUST. The generated schema already proves both (`generated_formlink_lists.inc` `DIAL\Branch` and `DIAL\Quest`), so `wrong_target_type` covers them. DLBR `QNAM` (Quest, `:8780`) is also unproven, and it is not covered here.
- No INFO merge-order rule is included; that needs engine evidence.
- Only FO4_DATA_DIR vanilla (`VanillaMasterHasNoErrorFindings`) can show whether vanilla holds these invariants. Unit tests here use synthetic fixtures only (`tests/test_check_dialogue.cpp`).

## Radicle/include/fo4/schema/scalar.hpp:CountedArrayDescriptor

A counted array-of-struct kind: one payload subrecord holds `count` elements of `stride` bytes, and a whole-payload u32 sibling (`count_subrecord`) stores the count. Members are ordinary scalar rows at `FieldAddress{false, stride, 0, ...}`, so reads and writes reuse the stride addressing that already exists and no new storage is added. The array itself has no row.

Not supported yet: a count stored inside another subrecord or in a non-u32 type; header-then-array payloads (no array_offset); repeating subrecord groups such as CNTO+COED or EFID+EFIT+CTDA; prefix-count arrays; members with enum or flags names, strings or links; per-record duplication for ACHR/CELL XWCU.

## Radicle/src/schema/scalar.cpp:counted_array_descriptor

Looks up a counted array by record and path in the generated `k_counted_arrays` table. `is_counted_array_count_subrecord` answers whether a subrecord is the count of any array in that record.

## Radicle/src/plugin/field_writer.cpp:write_scalar

Refuses a generic scalar write to a counted array's count subrecord (REFR `XWCN`) with `ScalarError`. A raw write would desynchronize the count from the payload, so insert and remove are the only writers of the count. The check runs before any byte changes.

## Radicle/src/plugin/field_writer.cpp:counted_array_subrecords

Finds the payload and count subrecords. It throws `CountedArrayError` on a duplicate subrecord, a count that is not four bytes, or a payload that is not a whole number of stride elements or whose element count differs from the stored count. A missing subrecord returns nullopt, which the mutators report as `subrecord_missing`. Every throw happens before the plugin changes.

## Radicle/src/plugin/field_writer.cpp:insert_record_counted_element

Inserts a zero-filled element at `index` and writes count+1. An `index` equal to the count appends; a larger index returns `index_out_of_range`. Modeled on the packed-array insert in `Radicle/src/schema/formlink_lists.cpp`.

## Radicle/src/plugin/field_writer.cpp:remove_record_counted_element

Removes the element at `index` and writes count-1. An index at or above the count returns `index_out_of_range`. Removing the last element keeps both subrecords, the payload empty and the count zero, so a later insert does not have to create subrecords. Missing-subrecord insertion is not generated; see the insertion policy in `Radicle/tools/schema_codegen.py:parse_counted_array`.

## Radicle/tools/schema_codegen.py:parse_counted_array

Validates a `counted_array` entry. Keys are limited to `COUNTED_ARRAY_KEYS` and `MEMBER_KEYS`; the stride must be a positive integer; insertion must be `existing_only`; members carry no names. Members expand into scalar rows with the array's subrecord and stride, so the shared address and overlap rules check that each member fits one stride element and does not overlap another.

## Radicle/tools/schema_codegen.py:load_schema

After the rows are expanded, requires exactly one whole-payload u32 row for each array's count subrecord, and rejects two arrays that declare the same payload subrecord or the same count subrecord in one record. The count rule is what lets `write_scalar` refuse plain writes while insert and remove remain the only count writers. The same pass checks each `subrecord_group`: exactly one whole-payload u32 row for its count subrecord, no member declared by two groups in one record, no count shared by two groups, no group count that equals a member of another group in that record, and no group member or count subrecord equal to a counted array's payload or count subrecord in the same record.

## Radicle/tools/parity_matrix.py:scalar_rows

Expands counted arrays with `schema_codegen.parse_counted_array`, the same function the codegen uses, so the parity counts cannot drift from the generated rows. Subrecord groups expand through `schema_codegen.parse_subrecord_group`, the function `load_schema` uses for their member rows, and `formlink_rows` counts their links the same way.

## Radicle/src/schema/preflight.cpp:preflight_record

REFR XWCU members now run through the generic checks. A payload that is not a whole number of 16-byte elements reports `invalid_scalar_storage` once for each member row. A non-finite member is reported through the `ScalarError` from `decode_scalar`, which preflight maps to `invalid_scalar_storage`.

## Radicle/src/plugin/mutation.cpp:set_record_scalar

The overload that takes only a Signature selects the first scalar row for that subrecord. Rows sort by path, so for the counted payload (REFR `XWCU`) the first row is `REFR\WaterCurrentUnnamed` at offset 12, not X. Counted members should be written with the descriptor and index overload.

## Radicle/include/fo4/schema/generated_scalar_fields.inc:k_counted_arrays

xEdit proof for REFR `XWCU`/`XWCN` against the pinned `Core/wbDefinitionsFO4.pas` @93cc0bc5 (SHA-256 `f0bacc90a7dbd62bf90ff570d1974932bd4b99488bd0da9bbcf4e01abdfb9c5e`, matched on a build copy). `wbRefRecord(REFR, 'Placed Object', ...)` starts at line 11347 and the next record, REGN, at 11870. Its `Water Current Velocities` block is at lines 11568-11575: `wbInteger(XWCN, 'Velocity Count', itU32, ...)` at 11569 is the u32 count row, and `wbArray(XWCU, 'Velocities', wbStruct('Current', [wbVec3('Velocity'), wbFloat]))` at 11570-11574 is the element. `.SetCountPathOnValue(XWCN, False)` is at 11575. `wbVec3` is three floats X, Y, Z (`Core/wbDefinitionsCommon.pas` lines 6456-6459), and the trailing unnamed `wbFloat` is the fourth float, so an element is 16 bytes at offsets 0, 4, 8, 12, as generated. The same block appears in ACHR at 4583-4590 and CELL at 6052-6059; only REFR is in this schema. The 4-byte width of `wbFloat` is not cited: its definition is in `wbInterface.pas`, which is outside the hash pin.

## Radicle/src/forms/leveled_list.cpp:reset_leveled_list — wrapper override reset (#362)

Resets a LVLI, LVLN or LVSP to the required subrecords only: the EDID is dropped, then OBND (12 zero bytes), LVLD 0, LVLF 0 and LLCT 0 are written. The wrapper override takes the picked source record's EDID, or none if the source has none, and `copyAsWrapper` sets it after the reset. LVLM, LVLG, LVSG, ONAM, LLKC and every LVLO are dropped, as xEdit's required-member reset does (`wbImplementation.pas:9404-9407`). Unlike `create_leveled_list`, it writes no LVLM. It returns false when the FormKey is not in the plugin, and throws `LeveledListError` for a record that is not a leveled list.

## Radicle/src/forms/placed_reference_create.cpp:create_placed_reference — REFR creation (#182)

Creates a REFR over a base in a CELL this plugin owns. Refused unless the CELL is owned by this plugin (the message says to copy the cell as an override first), is a decodable CELL record inside a GRUP, and the base is either a record of this plugin or owned by a master already in its header. A local base that is not in the plugin is refused. No master is added here (TODO-verify whether the managed editor's Mutagen layer adds one on save). Work runs on a copy, and the form ID is reserved only after every refusal passes, so a refused request leaves the plugin unchanged and consumes no FormID. Tests: `tests/test_placed_reference_create.cpp:PlacedReferenceCreate.PlacesTemporaryReferenceInsideExteriorCellGridAndRoundTrips`, `PlacedReferenceCreate.PlacesPersistentReferenceInInteriorCellAndRoundTrips`, `PlacedReferenceCreate.RefusesPersistentAndOutOfGridReferencesInExteriorCellsWithoutMutation`, `PlacedReferenceCreate.PersistentBucketIsInsertedBeforeExistingTemporaryBucket`, `PlacedReferenceCreate.InitiallyDisabledSetsRecordFlagAndDefaultsOff`, `PlacedReferenceCreate.RefusesForeignCellsAndUnownedBasesWithoutMutation`, `PlacedReferenceCreate.RefusesMasterCellWithOverrideHint` and `PlacedReferenceCreate.RefusesLocalBaseThatIsNotInThisPlugin`.

Exterior cells are the CELL records whose DATA bit 0 is clear. For those, `require_placement_in_cell` refuses `persistent = true`, because xEdit moves an exterior persistent reference to the worldspace's own persistent CELL (`Core/wbImplementation.pas` `TwbMainRecord.UpdateCellChildGroup`, :14553-14558, the `CELL[P]` branch). It also refuses a position whose grid (floor(x/4096), floor(y/4096)) differs from the cell's XCLC grid (`UpdateCellChildGroup`, :14570). Interior cells have no grid check. The 4096-unit span is the exterior cell size used by the grid rule; `wbPositionToGridCell` (called at :14570) has no body in the pinned copy, so the floor rule is not confirmed against it. Interior cells accept persistent and temporary references alike.

TODO-verify: the grid rule for exact negative multiples of 4096 (`wbPositionToGridCell` body not in the pinned copy).

Record layout, from the pinned `Core/wbDefinitionsFO4.pas`:
- REFR is `wbRefRecord(REFR, 'Placed Object', ...)` at line 11347. Flag `0x00000400` Persistent is at line 11351 and `0x00000800` Initially Disabled at 11352. `persistent` and `initially_disabled` (default false) set these two bits, and nothing else in the header flags is written.
- NAME (base FormID, 4 bytes) is `wbFormIDCk(NAME, 'Base', ...)` at line 11450.
- DATA is `wbVec3PosRot(DATA)` at line 11864: position x,y,z then rotation x,y,z as six float32 (24 bytes).
- The rotation members are `wbFloatAngle` (`wbDefinitionsCommon.pas:6507-6509`). Their definition is not in the pinned copy, so the stored unit is TODO-verify. The tool text says radians, and the value is written unchanged, as the managed C# does.

GRUP placement follows xEdit's `TwbMainRecord.UpdateCellChildGroup` (`Core/wbImplementation.pas:14489`): a persistent reference belongs in the type 8 bucket and a temporary one in type 9. `TwbGroupRecord.GetSortKeyInternal` (`Core/wbImplementation.pas:17999`) sorts child groups by group type first, so type 8 precedes type 9. `detail::ensure_reference_bucket` in `Radicle/src/forms/placed_reference_copy.cpp` therefore inserts a new bucket before any later-typed child group.

## Radicle/src/forms/placed_reference_copy.cpp:ensure_cell_children_group — CELL child GRUP placement

The CELL children GRUP (type 6) is inserted directly after the CELL record in the CELL's parent GRUP. Its label is the CELL form ID. `create_placed_reference` and the copy paths share this function and `ensure_reference_bucket`, both in `detail::`. Type-and-label identity is `flags.hpp:group_label_is_form_id`. Existing groups are found through `detail::unique_child_group`, which throws on a duplicate, so no second bucket is created. `detail::u32_label`, declared in `placed_reference_copy_internal.hpp`, is the little-endian label encoder for these paths and for `Radicle/src/forms/cell_create.cpp`.

## Radicle/src/forms/cell_create.cpp:create_interior_cell — interior CELL creation (#182)

Creates a local interior CELL from an editor ID. Only the subrecords xEdit's CELL definition needs for an interior cell are written. EDID holds the editor ID as a zstring, stored as Windows-1252 through `stored_editor_id`. DATA is a two-byte flags value with bit 0 (`Is Interior Cell`) set, so the stored value is `0x0001`. In `Core/wbDefinitionsFO4.pas` the CELL record starts at line 5953, and its flag list with `{0} 'Is Interior Cell'` is at line 5968. Its `wbEDID` (`wbDefinitionsFO4.pas:3691`, `wbStringKC(EDID, 'Editor ID', 0, cpOverride)`) is not marked required. The empty-ID refusal is this creator's rule, not an xEdit one. XCLC, XCLL and the other optional CELL subrecords are not written.

Grouping: top-level GRUP type 0 labeled `CELL`, then interior block GRUP type 2 with label `ObjectID % 10`, then sub-block GRUP type 3 with label `(ObjectID / 10) % 10`, where ObjectID is the low 24 bits of the form ID (`form ID & 0xFFFFFF`). Measured on every interior CELL in the seven vanilla masters (Fallout4.esm and the six DLC .esm files): 1419 of 1419 match block = (form ID & 0xFFFFFF) % 10 and sub-block = ((form ID & 0xFFFFFF) / 10) % 10; blocks and sub-blocks ascend. xEdit's `TwbMainRecord.UpdateInteriorCellGroup` (`Core/wbImplementation.pas:14625-14721`) applies only when DATA bit 0 is set (:14656). It takes the last two decimal digits of ObjectID (`s := '00' + IntToStr(ObjectID)`, :14681), block from `s[2]` (:14686) and sub-block from `s[1]` (:14687). It creates the groups with `TwbGroupRecord.Create(TopGroup, 2, Block)` (:14704) and `TwbGroupRecord.Create(NewBlockGroup, 3, SubBlock)` (:14721), and sorts them by `GetSortKeyInternal` (:17999). CELL records inside a sub-block sort by full form ID (`TwbMainRecord.GetSortKeyInternal`, `Core/wbImplementation.pas:12525-12527`); only the block and sub-block labels use the low 24 bits.

Placement. The top-level CELL group comes from `Radicle/src/plugin/mutation.cpp:find_or_create_top_group`, the path other top-level creates use, so a missing group is appended. Blocks and sub-blocks are inserted in ascending label order. The CELL goes before the first CELL in its sub-block whose masked ObjectID is larger. Its type 6 children group, when present, follows the CELL, so the two stay together.

Refusals happen before the form ID is reserved: an empty editor ID, an embedded NUL, and an editor ID that matches (ASCII case-insensitively) any CELL record in this plugin, overrides included. Exterior cells keep `create_exterior_cell`. The declaration is in `Radicle/include/fo4/forms/cell.hpp`. The duplicate check covers this plugin's CELL records only, not its masters, and skips undecodable CELLs, whose subrecords are not decoded. Tests: `tests/test_interior_cell_create.cpp` (`InteriorCellCreate.*`).

## Radicle/schema/fallout4/fields.json:R2 audio records

Batch R2 (#287 C2) adds schema rows and authoring for AECH, ASPC, REVB, SNCT, SOPM, SCSN, STAG and MUST. The block sits right after the FSTS entry. Layouts and authoring defaults come from `wbDefinitionsFO4.pas` at pin 01a11729 (`build/xedit`), and `wbDefinitionsCommon.pas` for `wbObjectBounds` (`:9074`, `SetRequired`) and `wbBoolEnum` (`:7240`). Corpus counts come from a read-only scan of Fallout4.esm and the six official DLC masters: records per signature, subrecord presence, occurrences per record and payload sizes. Required means xEdit marks the subrecord required and every vanilla record of that signature has it. xEdit `wbDiv(n)` fields are stored raw with identity presentation; the label names the scale. Enum and flag names are the xEdit `wbEnum` and `wbFlags` lists.

- AECH (`wbDefinitionsFO4.pas:12206`): no rows, authoring EDID 0..1. The Effects array (`:12208`, one KNAM type plus a DNAM data union per effect) is not covered. No `when` is used: `record_discriminator_matches` in `src/schema/field_address.cpp` requires exactly one record-level discriminator subrecord, and KNAM occurs once per effect (corpus: 31 records, 1 to 5 KNAM each, 92 in total; types 2252866750 BSOverdrive 23, 4015480703 BSStateVariableFilter 59, 411269967 BSDelayEffect 10).
- ASPC (`:7057`): `XTRI` u8 enum False/True (98 of 98, `SetRequired` via `True`, min 1). `WNAM` u16 raw, x100 dB (83 of 98, `SetRequired`, min 0). FormLinks `SNAM` to SNDR (79), `RDAT` to REGN (61) and `BNAM` to REVB (92), all nullable false. `OBND` min 1 with 12 zero bytes (98 of 98, `wbObjectBounds` `SetRequired`).
- REVB (`:9593`): `DATA` is 14 bytes in all 59 records (required, min 1). Twelve scalar rows sit at offsets 0 to 13, each with payload_size 14; `Decay HF ratio` is raw x100 and `Diffusion`, `Density` are raw percent. `ANAM` u32 enum with `wbReverbClassEnum` (`:3668`: Default, Class A to Class E; corpus 0:6, 1:3, 2:18, 3:16, 4:12, 5:4; required, 59 of 59, min 1). Defaults follow the xEdit `SetDefaultEditValue` calls (1250, 800, 100, 100, 100).
- SNCT (`:9469`): `FNAM` u32 flags, bits 0 to 6 at `:9473-9479` (required, 50 of 50, min 1). FormLinks `PNAM` and `ONAM` to SNCT (49 and 43 of 50, nullable false). `FULL` is a localized subrecord (44 of 50, min 0). `VNAM` and `UNAM` u16 raw, x65535 (50 and 5 of 50). `MNAM` and `CNAM` f32 (50 and 5 of 50). None of these four is required in xEdit, so all have min 0.
- SOPM (`:9489`): `NAM1` (`:9491`) is four bytes in all 402 records. Flags u8 with eight bits at `:9492-9501`, and reverb send % u8 at offset 3. `MNAM` u32 enum Uses HRTF / Defined Speaker Output (402 of 402; values 0:285, 1:117). `VNAM` i16 raw, x100 dB (402 of 402). `ATTN` (`:9526`, 24 bytes in 377 records) is flattened: four f32 distances at offsets 0 to 12, then eight u8 curve values at offsets 16 to 23, payload_size 24; the nested Fade In and Fade Out curve structs are read as fixed offsets, not as a nested kind. `ENAM` FormLink to AECH (72 of 402, nullable false). `ONAM` (`:9510`, output channels, array of 8-byte structs in 24 bytes, 117 records) is not covered. All SOPM authoring minimums are 0, because xEdit marks none of these subrecords required.
- SCSN (`:12650`): `PNAM` u16 priority (11 of 11, xEdit not required, min 0). `CNAM` (`:12653`, array of struct with a SNCT FormLink and a float, 2 to 18 entries in 11 records, 8 bytes each) is not covered.
- STAG (`:12663`): no rows, authoring EDID 0..1. `TNAM` (`:12665`, array of struct with a sound FormLink and an action string, 36 of 37 records with 1 to 42 entries) is not covered.
- MUST (`:8795`): `CNAM` u32 enum Palette Track 603355331, Single Track 1859641416, Silent Track 2712257749 (`:8797-8801`; corpus 41, 575, 8; required, 624 of 624, min 1; default bytes c378f623 for Palette Track). `FLTV` f32 (`:8803`, `SetRequired`, 49 of 624, min 0). `DNAM` f32 (41 of 624). `ANAM` and `BNAM` zstrings (575 and 20 of 624). `LNAM` (`:8808`) is 12 bytes in 27 records, flattened to three scalars at offsets 0, 4 and 8. `SNAM` (`:8816`) is a packed FormLink list with stride 4 and nullable true, targets MUST, like the MUSC `TNAM` row (41 of 624; payloads 28 to 248 bytes, so one subrecord holds many IDs). Not covered: `FNAM` cue points (`:8813`, float array, 20 of 624), `CITC` and `CTDA` conditions (`:8814-8815`, 103 of 624), and `CIS2` (11 records, 28 bytes), which the xEdit MUST definition does not list.

## Radicle/tests/test_audio_records.cpp:EveryDeclaredRowDecodesFromTheFixture

Synthetic plugin with one top-level group per record. Every scalar and string row of the eight records decodes to non-empty text, and every FormLink row has its subrecord present. AECH and STAG have no rows, so only their records are read.

## Radicle/tests/test_audio_records.cpp:OneEditPerRecordRoundTripsAndTouchesNoOtherSubrecord

One generic edit per record that has rows: `set_record_scalar` for scalars, `set_record_string` for the MUST string and `set_record_formlink_field` for links. The plugin is written and re-read. The edited value must decode back, and every other subrecord payload must match the unedited fixture byte for byte.

## Radicle/tests/test_audio_records_real.cpp:AudioRecordsReal.EveryRowDecodesAndAuthoringMinimumsHold

Over each official master, every batch record must have no `preflight_record` error and every scalar row that is present must decode. Each authoring subrecord's count must fall inside its minimum and maximum. Defaults are not compared against the corpus.

## Radicle/schema/fallout4/fields.json:records[ALCH..IDLM]

Batch K1 (#287 C8) moves ALCH, AMMO, BOOK, INGR, KEYM, ARTO and IDLM from keyword-only (L1) to L4. The `keyword_records` entries are unchanged, and the new `records` block sits after EYES. Each citation below quotes the pinned xEdit definition text (pin 01a11729, `wbDefinitionsFO4.pas`). Corpus counts come from a read-only scan of the official masters: ALCH 354, AMMO 106, BOOK 470, INGR 1, KEYM 246, ARTO 258, IDLM 51 records.

- Radicle/schema/fallout4/fields.json:ALCH: `wbFloat(DATA, 'Weight', cpNormal, True)` (:5710) and `wbStruct(ENIT, 'Effect Data', [` (:5711) are required; corpus 354/354 each, OBND 354/354. The effect list (`wbEffectsReq`) is not covered: it is an array of structs. Also not covered: the Addiction `wbFormID('Addiction')` (no target list), MODT, MODF, DEST, DAMC, VMAD.
- Radicle/schema/fallout4/fields.json:AMMO: `wbStruct(DATA, 'Data', [` (:5756) carries `.SetRequired` (:5760), corpus 106/106. `wbStruct(DNAM, '', [` (:5761) is required, 106/106. NAM2 (`wbModelInfo(NAM2)`) is authoring-only with no row.
- Radicle/schema/fallout4/fields.json:BOOK: `wbDESCreq,` (:5900) is required, corpus 470/470. `wbStruct(DATA, 'Data', [` (:5906) and `wbStruct(DNAM, '', [` (:5910) are required, 470/470 each. The DNAM `wbUnion('Teaches', wbBOOKTeachesDecider, [` is not covered (union decided by a decider callback).
- Radicle/schema/fallout4/fields.json:INGR: `wbStruct(DATA, '', [` (:9802) and `wbStruct(ENIT, 'Effect Data', [` (:9806) are required; INGR has one vanilla record, so these minimums rest on xEdit plus that record. `wbEffectsReq` (:9820) is not covered.
- Radicle/schema/fallout4/fields.json:KEYM: `wbFULLReq,` (:9832) is required, corpus 246/246. Its FULL default is a single NUL (`"00"`), as in FLOR. Not covered: VMAD, MODT, MODF, DEST, DAMC.
- Radicle/schema/fallout4/fields.json:ARTO: `wbInteger(DNAM, 'Art Type', itU32, wbEnum([` (:9368) is an enum with three names; it is not marked required, and corpus has it in 258/258. OBND is required in xEdit, 258/258. Not covered: MODT, MODF.
- Radicle/schema/fallout4/fields.json:IDLM: `wbObjectBounds,` (:7103) is required, 51/51. `wbIdleAnimation.SetRequired,` (:7105): the group is required, but IDLF, IDLC and IDLT are present in 42, 39 and 39 of 51 records, so their minimums are 0. `wbIdleAnimation` Flags (IDLF, bits 0-5 named per `wbDefinitionsCommon.pas` lines 8630-8637, FO4 branch of each IsFO3/IsFO76/IsSF1) is covered; bit 1 stays unnamed. Not covered: IDLA (array of structs), MODT, MODF.
- Radicle/tests/test_base_items.cpp:make_plugin: fixture subrecords follow the authoring order (`wbDefinitionsFO4.pas:5896-5900` puts MODL before DESC), since `preflight_authoring_record` rejects out-of-order subrecords (`Radicle/src/schema/preflight.cpp:preflight_authoring_record`).
- Adding subrecords to records with unlisted subrecords (ALCH/INGR effects, IDLM IDLA) fails closed with `unknown_order` in `Radicle/src/plugin/subrecord_authoring.cpp:add_with_rules`.
- Radicle/schema/fallout4/fields.json:K1 keyword overlap: the records entries sit beside the keyword_records entries. All six codegen `--check` commands pass with both present; KSIZ and KWDA are authoring rules marked coupled.
- Radicle/tests/test_base_items.cpp:EditsOneRowPerRecordAndRereads: fixture with one record per signature and SNDR targets; one scalar, one string and one sound link edit per record, then re-read and compared.
- Radicle/tests/test_base_items_real.cpp:RowsDecodeAndAuthoringLimitsHold: official masters; scalar and FormLink payload sizes and decodes, and authoring minimums and maximums per record.

## Radicle/schema/fallout4/fields.json

Batch T1 adds schema rows and authoring constraints for STAT, LIGH, BNDS, LAYR, LGTM, MSWP and TXST. LAND is excluded because its payloads are grids and arrays. The rows are one block placed after TREE. Layouts come from the xEdit pin 01a11729 (`build/xedit/wbDefinitionsFO4.pas`): STAT at line 12045, LIGH at 9871, BNDS at 12280, LAYR at 12400, MSWP at 12437, LGTM at 8629 and TXST at 6984. The `DODT` struct is at line 3400, and `wbObjectBounds` is at `wbDefinitionsCommon.pas:9074`.

Vanilla corpus (Fallout4.esm plus the six DLC masters, read-only) has STAT 23579, LIGH 1065, BNDS 12, LAYR 4595, LGTM 72, MSWP 3937 and TXST 511 records. Authoring minimum 1 is set only where xEdit marks the subrecord required and every vanilla record has it: LIGH `DATA`, `FNAM` and `OBND` (1065 of 1065 each), STAT `DNAM` and `OBND` (23579 of 23579), BNDS `OBND` (12 of 12), LGTM `DATA` and `DALC` (72 of 72), and TXST `DNAM` and `OBND` (511 of 511). No record in the corpus exceeds a maximum or misses a minimum. The TXST authoring order is xEdit's (see the TXST note); no vanilla TXST has both TX02 and TX05, so that pair's order is not observed in the corpus.

Rows per record: STAT 3 scalar, 1 string and 1 FormLink. LIGH 19 scalar, 1 string and 3 FormLinks. BNDS 8 scalar and 1 FormLink. LAYR 1 FormLink. LGTM 16 scalar and 1 FormLink. MSWP none. TXST 13 scalar (9 `DODT` members, 3 `DODT` color bytes and 1 `DNAM` flags field) and 9 strings. The six FormLinks that xEdit's target list does not allow NULL for (LIGH Sound, Lens and God rays, LGTM God rays, BNDS Texture and LAYR Parent) are `nullable: false`; STAT Material keeps `[MATO, NULL]`.

Optional tail members carry `minimum_payload_size` at their end offset, matching the reader thresholds.

Not covered (for the array, struct and blob lane):
- STAT `MNAM` (distant LOD, four 260-byte mesh slots), `NVNM` (navmesh blob), `VMAD`, `PTRN`, `FTYP`, `PRPS` and the `MOD*` model blobs. Record flags are not rows.
- LIGH `DATA` byte 11 (color unused byte), `VMAD`, `PTRN`, `PRPS`, `DEST`, `ICON` and `MICO`. `KWDA` and `KSIZ` stay with `keyword_records`.
- LGTM `DATA` ambient-unused block at 40-71 (six directional colors, specular and fresnel), `DATA` byte 88 (unused), the color unused bytes, and `DALC` (32-byte directional ambient block).
- LGTM `DATA` offsets 72 to 132 (21 members: fog far color, fog max, light fade begin and end, near and far height mid and range, high fog near and far color, and the five scales). Their position depends on the ambient block, which is 24 bytes below form version 30 and 32 bytes from it, so a row would misread older records. They stay out until a form-version gate exists. The corpus has 46 records at 92 bytes, 4 at 128 and 22 at 136.
- MSWP `BNAM`, `SNAM`, `CNAM` and the substitution `FNAM`. The flat `FNAM` (Tree Folder, the first `FNAM`) has no row. 207 vanilla records contain more than one `FNAM` because xEdit's obsolete `FNAM` sits in the substitution array, so a row would fail closed on them. MSWP therefore has authoring (`EDID`) only.
- TXST `DODT` byte 30 (`Alpha Threshold?`, marked as unverified by xEdit), the unused color byte 35. `TX06` has no vanilla occurrence and is kept in authoring only.

## Radicle/src/environment/base_records.cpp:decode_light

`LIGH DATA` rows match the reader offsets: time `i32` at 0, radius `u32` at 4, color bytes 8 to 10, flags at 12, then falloff, FOV, near clip, flicker, constant, scalar, exponent (48, minimum payload 52), god-ray near clip (52, minimum 56), value (56, minimum 60) and weight (60, minimum 64). The reader accepts sizes 48 to 64 in multiples of 4. xEdit sets `SetOptionalFrom(10)` on `DATA` (line 9889 onward), so a 48-byte payload has no exponent. The corpus has 107 records at 48 bytes, 174 at 60 and 784 at 64. `LIGH FNAM` is a 4-byte fade value (`payload_size` 4). The flags names are the 22 xEdit `wbFlags` names in sequential bit order (bit 0 "Unknown 0" through bit 21 "Unknown 21"). The sound (`SNAM`), lens (`LNAM`) and god rays (`WGDR`) FormLinks match `decode_light`. `SNAM` is absent from vanilla but is defined at xEdit line 9941.

## Radicle/src/environment/base_records.cpp:decode_bendable_spline

`BNDS DNAM` rows use the reader offsets: tiles `f32` at 0, slices `u16` at 4 (minimum 6), relative `u16` enum at 6 (minimum 8, names False and True from `wbBoolEnum`), default color at 8, 12 and 16 (minimum 20 for each channel, matching the reader's `size >= 20` gate), wind sensibility at 20 (minimum 24) and wind flexibility at 24 (minimum 28). `TNAM` is a TXST FormLink. xEdit has no optional member in `DNAM` (line 12283), so the reader's 20 and 24-byte sizes are a leniency beyond xEdit, not a row conflict. No change was needed.

## Radicle/src/environment/base_records.cpp:decode_layer_parent

`LAYR PNAM` is a 4-byte LAYR FormLink (`wbFormIDCk(PNAM, 'Parent', [LAYR])` at line 12402). It is optional. `decode_layer_parent` reads the same link at the same offset.

## Radicle/src/environment/base_records.cpp:decode_static_distant_lod

`STAT DNAM` (xEdit line 12075, `SetOptionalFrom(2)` and `SetRequired`) has max angle `f32` at 0 (30 to 120 by its xEdit name; vanilla is 30 to 90), material FormLink at 4 (MATO or NULL, `payload_sizes` 8 and 16), leaf amplitude at 8 (minimum 12) and leaf frequency at 12 (minimum 16). The FormLink schema takes at most two payload sizes (`formlink_schema_codegen.py` rejects three), so 12 is not listed. xEdit allows a 12-byte `DNAM` (members from index 2 are optional), and no vanilla record has one, so the material link has no 12-byte entry. The corpus has 23164 DNAM records at 16 bytes and 415 at 8 bytes. `MODL` is a string row. The reader reads `MNAM` only, and its check (up to four 260-byte slots) is more lenient than xEdit's fixed four slots. No change was made. The STAT authoring order is kept as listed: xEdit marks the record `SetUnordered` (`wbDefinitionsFO4.pas:12089`, "NVNM can be before or after MNAM"), but the listed order matches both vanilla orderings found in the corpus (2 of 2). A mod that writes NVNM before MNAM would fail the order check.

## Radicle/src/environment/world.cpp:decode_lighting_template

`LGTM DATA` (xEdit line 8631, `SetRequired`) uses the XCLL-shaped offsets from `decode_lighting`: directional at 4, fog near color at 8, fog near and far at 12 and 16, rotation at 20 and 24, directional fade at 28, fog clip at 32 and fog power at 36. These 16 rows read the same bytes in both layouts. The ambient block after offset 40 is 24 bytes below form version 30 and 32 bytes from it (`wbDefinitionsCommon.pas:6648-6653`). The reader handles both layouts (`Radicle/src/environment/world.cpp:50-52`, which picks 24 or 32 by form version), and the 21 members past offset 72 are not rows (see Not covered). `DALC` (line 8661) is a required 32-byte struct, and `WGDR` (line 8662) is a GDRY FormLink with no NULL target, so it is `nullable: false`.

## Radicle/src/environment/material_swap.cpp:decode_material_swap

The reader reads only the repeated `BNAM`, `SNAM` and `CNAM` substitution array (xEdit line 12437). It ignores the flat `FNAM` (line 12442). That flat `FNAM` cannot be a row because 207 vanilla MSWP records contain more than one `FNAM`, and a non-repeated row fails closed on duplicates (`field_address.cpp:resolve_field_slots` sets `duplicate`). MSWP therefore has authoring (`EDID`) and no rows. The array stays with the array lane.

## Radicle/src/environment/terrain.cpp:decode_texture_set

`TX00` to `TX07` are strings (`wbString` at lines 6988 to 6995). The authoring order is xEdit's order, `TX00, TX01, TX03, TX04, TX05, TX02, TX06, TX07` (the xEdit comment at line 6993 flags the TX05 and TX02 swap). No vanilla TXST has both TX02 and TX05, so that order is not observed in the corpus. `MNAM` is the material string at line 7003. `DNAM` is a `u16` flags field with names "No Specular Map", "Facegen Textures" and "Has Model Space Normal Map" (bits 0 to 2, line 6998), and it is exactly 2 bytes in the corpus (`payload_size` 2). `DODT` (`wbDODT`, line 3400, used at line 6997) is 36 bytes: min and max width and height, depth, shininess, parallax scale at 24, parallax passes at 28, flags at 29 (names "POM Shadows", "Alpha - Blending", "Alpha - Testing", "No Subtextures", "Multiplicative Blending"), alpha threshold at 30 (not covered), and color bytes 32 to 34. The reader reads the six floats of `DODT` (`size >= 16`) and `DNAM` (`sized 2`), and agrees with these offsets.

## Radicle/tests/test_static_light_records.cpp:StaticLightSynthetic.EditsOneFieldPerRecordAndRereadsIt

A synthetic plugin holds one record per batch signature, each in its own top-level GRUP. The test edits one scalar, string or FormLink per record through `set_record_scalar`, `set_record_string` and `set_record_formlink_field`, writes, re-reads, and compares the changed payload bytes. `StaticLightSynthetic.EveryBatchRowDecodesOnItsRecord` decodes every scalar row on the fixture (at least 59 decodes). `StaticLightSynthetic.MaterialSwapArraysStayUntouchedAndHaveNoRows` checks that MSWP has no rows and that its substitution array is unchanged.

## Radicle/tests/test_static_light_records_real.cpp:StaticLightReal.BatchRowsDecodeAndAuthoringMinimumsHold

Over each official master (`FO4_DATA_DIR`), every batch record decodes every scalar row for its subrecord, every FormLink row resolves to valid slots (`resolve_field_slots`), and `preflight_authoring_record` reports no error. Authoring is sampled at 64 records per master because the preflight record lookup is a linear scan. The full-corpus order, cardinality and minimum sweep was run read-only over all masters outside the repo.

## Radicle/schema/fallout4/fields.json:records[GLOB..DLVW]

Batch R1 adds rows and authoring for GLOB, GMST, KYWD, KSSM, CLFM, LCRT, VTYP, DMGT, AACT, DFOB, OTFT, CMPO, DLBR and DLVW, placed directly after CLMT. Layouts are xEdit `Core/wbDefinitionsFO4.pas` at pin 01a11729: GLOB 6921-6934, GMST 6936-6945, KYWD 6947-6958, LCRT 6962-6966, AACT 6970-6981, KSSM 12387-12397, CLFM 9572-9588, VTYP 7950-7955, DMGT 12310-12318, DFOB 12304-12307, OTFT 9357-9360, CMPO 12294-12302, DLBR 8778-8793, DLVW 8819-8824. The keyword type enum is `wbKeywordTypeEnum` (4844-4866); the bool names are `wbBoolEnum` (`Core/wbDefinitionsCommon.pas:7240`).

Corpus counts come from a read-only scan of Fallout4.esm and the six official DLC masters that walks every nested GRUP (re-measured 2026-10-13; the first scan read top-level groups only and reported DLBR as 0). Records per type: GLOB 1918, GMST 2051, KYWD 4337, KSSM 197, CLFM 166, LCRT 913, VTYP 732, DMGT 8, AACT 175, DFOB 446, OTFT 477, CMPO 33, DLBR 185, DLVW 1. Every batch record sits in a top-level group except DLBR, which all sit in type-10 (quest children) groups. A subrecord is `minimum 1` only when xEdit requires it and every record has it: GLOB FLTV (1918 of 1918), GMST EDID and DATA (2051 of 2051), CLFM FNAM (166 of 166), LCRT CNAM (913 of 913), VTYP DNAM (732 of 732). xEdit requires these but the corpus does not, so they stay at 0: GLOB FNAM (784 of 1918), KYWD CNAM (4305 of 4337) and TNAM (4290 of 4337), LCRT TNAM (904 of 913), AACT CNAM (107 of 175) and TNAM (94 of 175). DLBR QNAM and SNAM are `SetRequired` in xEdit (`wbDefinitionsFO4.pas:8780`, `:8792`) and are present in 185 of 185 DLBR records, so both are `minimum 1` with no default. Every subrecord the corpus holds is listed in its record's authoring, which the real-data test checks.

GLOB: `Type` is the FNAM u8 enum (`wbInteger(FNAM, 'Type', itU8, wbEnum`, line 6926) with Unknown 0, Short 's', Long 'l', Float 'f', Boolean 'b'. `Value` is FLTV f32 (line 6933, `wbFloat`). xEdit reads FLTV as a float for every type, so FNAM does not change its width and no `when` gate is needed. Corpus FNAM values: 's' 743, 'f' 40, 'l' 1.

GMST: DATA is a union chosen by the first character of EDID (`wbGMSTUnionDecider`, `Core/wbDefinitionsCommon.pas:5620-5640`): 's' is a string, 'i' Int32, 'f' float, 'b' a u32 bool, and any other first character falls back to Int32. The rows are `FloatValue` (f32, `when` EDID byte 0 equals 102), `IntegerValue` (i32, equals 105) and `BooleanValue` (u32 enum False/True, equals 98). Corpus first letters: s 1216, f 744, i 80, u 10, b 1. Not covered: the 's' branch (localized string DATA, with no gate kind for localized values) and the Int32 fallback for every letter other than i. The fallback would need more than the 8 `when` values, so the 10 'u' records in the corpus get no integer row.

KYWD, AACT, LCRT: `Type` is TNAM u32 enum from `wbKeywordTypeEnum` (0..18; KYWD corpus values stay in that range). KYWD `Notes` is DNAM and `Display name (legacy)` is NNAM, both strings; every corpus string is NUL-terminated. `AttractionRule` is DATA, a FormLink to AORU (line 6957), not nullable because the xEdit list has no NULL. AACT `Notes` (DNAM) and `AttractionRule` (DATA, line 6980) follow the same pattern. Not covered: CNAM colour (`wbByteRGBA`, a 4-byte colour with no colour kind).

KSSM: `PrimaryDescriptor`, `ExteriorTail` and `VATSDescriptor` are FormLinks to SNDR (DNAM 12389, ENAM, VNAM). `VATSThreshold` is TNAM f32 (line 12392). `Keywords` is a KNAM FormLink list to KYWD (`wbRArray`, line 12393) with insertion `existing_neighbors`. KNAM must stay before RNAM: in all 197 corpus KSSM records every KNAM precedes every RNAM, so appending would put a KNAM after RNAM against the xEdit order (line 12394). Not covered: RNAM `Sounds` (`wbRArrayS` of `wbStructSK`, line 12394), a struct array.

CLFM: `ColorIndex` is CNAM u32 identity (`wbInteger(CNAM, 'Color/Index', itU32`, line 9580). The colour and remapping union is commented out at 9579-9581, so CNAM is read as a plain integer. `Flags` is FNAM u32 with Playable bit 0, Remapping Index bit 1 and Extended LUT bit 2 (lines 9582-9585). Corpus values are 0, 1, 3 and 7, all inside those bits. Not covered: CTDA conditions (`wbConditions`, line 9588).

VTYP: `Flags` is DNAM u8 with Allow Default Dialog bit 0 and Female bit 1 (lines 7952-7955). Corpus values are 0 to 3.

DMGT: no rows. DNAM is `wbUnion` on `wbFormVersionDecider(78)` (line 12313): an AVIF array before form version 78 and an AVIF plus SPEL struct array after it. Not covered.

DFOB: `Object` is DATA, a plain `wbFormID` with no target list (line 12306). It is modeled as a FormLink list with the wildcard target `*` (the same wildcard FLST uses), nullable because `wbFormID` has no NULL check.

OTFT: no rows. INAM is one packed-array subrecord (`wbArrayS(INAM, 'Items', ...)`, line 9359), so its authoring maximum is 1. Corpus: 477 of 477 records hold exactly one INAM, 4 to 36 bytes long. Not covered: the packed form ID array.

CMPO: `AutoCalcValue` is DATA u32. `ScrapItem` (MNAM, `wbFormIDCk(MNAM, 'Scrap Item', [MISC])`, line 12300), `ModScrapScalar` (GNAM, GLOB) and `CraftingSound` (CUSD, SNDR, line 5263) are FormLinks. Not covered: OBND, which is the `wbObjectBounds` struct.

DLBR: `Category` is TNAM u32 enum (Player 0, Command 1, `wbDefinitionsFO4.pas:8782-8784`). `Flags` is DNAM u32 with Top-Level, Blocking and Exclusive at bits 0, 1 and 2; the names come from `wbDefinitionsFO4.pas:8786-8790`, whose `{1}` to `{3}` markers are Pascal comments. Corpus DNAM values are 0, 1 and 2; TNAM values are 0 and 1. `Quest` (QNAM, line 8780) and `StartingTopic` (SNAM, line 8792) are required FormLinks. All 185 records are in type-10 groups under a QUST, so placement is `quest_children`, the placement DIAL uses.

DLVW: `Quest` (QNAM) and `Branch` (BNAM, DLBR) are FormLinks. ENAM and DNAM are `wbUnused` (line 8823) and are not modeled.

## Radicle/tests/test_config_records.cpp:ConfigRecordsSynthetic

One synthetic plugin with no masters holds a record for each batch type plus the AORU, SNDR, KYWD, MISC, QUST, DIAL and DLBR targets. The two DLBR records sit in a type-10 group labelled with QUST 2066. Each test edits one scalar, string or link per record through `set_record_scalar`, `set_record_string`, `set_record_formlink_field` or `set_record_formlink_list_element`. Every link is retargeted to a second target record of its type, so the re-read bytes differ from the fixture. Each test then writes, re-reads, compares the payload bytes and reads the count of each FormLink and string row. The GMST case also checks that a `when`-gated row returns `subrecord_missing` when the EDID letter does not match. DMGT and OTFT have no rows, so the last test checks that they expose none.

## Radicle/tests/test_config_records_real.cpp:ConfigRecordsReal

For every batch record in each official master, the test checks that every subrecord is listed in the authoring table and that each subrecord count falls within its minimum and maximum. It also decodes every scalar row against its subrecord payloads and calls `record_field_count` for every FormLink row. For each GMST record, each when-gated row has one slot, and that slot is valid exactly when the EDID first letter matches its gate (f, i, b). Records of these types are counted, and Fallout4.esm must contain at least one. The seven masters hold 185 DLBR records in quest-children groups, so the real test covers DLBR too.

## Radicle/src/plugin/field_writer.cpp:record_field_count

Count is positional for gated and ungated fields so indexes match the setters; a slot whose when-condition fails still counts, and setting it returns `subrecord_missing`; callers check slot validity with `schema::resolve_field_slots`.

## Radicle/schema/fallout4/fields.json:R4 placed reference variants

Batch R4 (#287 C4) adds schema rows and authoring for the placed-reference variants PARW, PBAR, PBEA, PCON, PFLA, PGRE, PHZD and PMIS, plus PLYR and the base record ADDN. Layouts are from xEdit at pin 01a11729 (`build/xedit/wbDefinitionsFO4.pas`). The eight variants use the `ReferenceRecord` helper (`wbDefinitionsFO4.pas:3200`, applied at `wbDefinitionsFO4.pas:5944-5951`), which is the same helper family as REFR (`wbDefinitionsFO4.pas:11347`) and ACHR (`wbDefinitionsFO4.pas:4483`). The variants therefore mirror REFR's two existing rows (HealthPercent from XHLT, Scale from XSCL) and add NAME, DATA, XESP and XOWN rows. The shared helper's Enable Parent is `wbXESP` (`wbDefinitionsFO4.pas:4330`), ownership is `wbOwnership` (`wbDefinitionsCommon.pas:9093`), and the reference target set is `sigReferences` (`wbDefinitionsFO4.pas:67-70`, which includes NULL, so EnableParent is nullable).

Rows per variant: 10 scalar (XHLT percent, XSCL, DATA position X/Y/Z and rotation X/Y/Z as f32 at offsets 0..20 of a 24-byte payload, XESP flags as u8 at offset 4 of an 8-byte payload, XOWN No Crime as u8 at offset 8 of a 12-byte payload) and 3 FormLink (NAME Base targets PROJ and HAZD per `wbDefinitionsFO4.pas:3213`, not nullable; XESP EnableParent, nullable; XOWN Owner targets FACT and NPC_ per `wbDefinitionsCommon.pas:9099`, not nullable). ADDN has 3 scalar rows (DATA Index u32, DNAM cap u16 at 0, DNAM Flags u16 enum at 2 with the four names from `wbDefinitionsFO4.pas:7808-7812`) and 2 FormLink rows (SNAM Sound SNDR, LNAM Light LIGH). PLYR has no rows (see Not covered).

Vanilla counts from a read-only survey of the seven official masters (zero counts are tolerated by the real test):
- ADDN 369 (Fallout4.esm 255, DLCRobot 10, DLCworkshop01 29, DLCCoast 25, DLCworkshop02 16, DLCworkshop03 2, DLCNukaWorld 32). DATA is 4 bytes on 369 of 369, DNAM 4 bytes on 369 of 369.
- PGRE 522 (Fallout4.esm 395, DLCCoast 77, DLCNukaWorld 50). DATA 24 bytes and NAME 4 bytes on 522 of 522. Its NAME resolves to PROJ for the 395 Fallout4.esm records.
- PHZD 2404 (Fallout4.esm 981, DLCCoast 1249, DLCNukaWorld 100, DLCRobot 12, DLCworkshop03 60, DLCworkshop01 1, DLCworkshop02 1). DATA on 2403 of 2404. The one without DATA is a deleted record, so the DATA authoring minimum for PHZD is 0. Its NAME resolves to HAZD for the 981 Fallout4.esm records.
- PMIS 2 (Fallout4.esm 1, DLCCoast 1). DATA and NAME on 2 of 2; NAME resolves to PROJ for the Fallout4.esm record.
- PARW, PBAR, PBEA, PCON, PFLA and PLYR have zero records in all seven masters. Their rows and authoring come from xEdit alone. DATA is minimum 1 there because the shared helper marks it SetRequired (`wbDefinitionsFO4.pas:3263`) and every vanilla record of that signature has it, vacuously.

Authoring minimums and defaults: variants list EDID, VMAD, NAME, XESP, XHLT, XSCL and DATA (min 1 with 24 zero bytes, except PHZD min 0), max 1 each. ADDN lists EDID, OBND, MODL (coupled), MODT, DATA (min 1, SetRequired at `wbDefinitionsFO4.pas:7802`, default `1c010000` = the Index 284 set at `wbDefinitionsFO4.pas:7801`), SNAM, LNAM and DNAM (min 1, SetRequired at `wbDefinitionsFO4.pas:7813`). Checked against every vanilla record of these signatures with the same order and count rules as `preflight_authoring_record`: zero violations.

Authoring order: XOWN is left out of the variant authoring list. `ReferenceRecord` is `SetUnordered` (`wbDefinitionsFO4.pas:3266`) and so is its `wbOwnership` member (`wbDefinitionsCommon.pas:9113`), so xEdit does not fix the order of XOWN against XESP. Vanilla PGRE has both orders (25 records with XESP first, 4 with XOWN first), so an authoring row for XOWN would reject real records. XOWN keeps its field rows, and `preflight_authoring_record` skips subrecords that have no authoring rule.

Placement: the eight variants use `cell_reference_children` (`src/schema/preflight.cpp:preflight_authoring_record`), which accepts a direct child of a cell persistent children GRUP (group type 8) or a cell temporary children GRUP (group type 9). Vanilla has 64 PHZD in type 8 and 2,340 PHZD, 522 PGRE and 2 PMIS in type 9 (read-only scan of the seven masters; no variant sits elsewhere). The real test asserts no `record_placement_invalid` for them. Tests: `test_preflight.cpp:CellReferenceVariantAcceptsPersistentAndTemporaryChildren`, `CellReferenceVariantAtTopLevelFails`. PLYR and ADDN are `wbRecord` (`wbDefinitionsFO4.pas:12129`, `wbDefinitionsFO4.pas:7796`), not cell helpers, so they use `top_level`.

PLYR: xEdit defines the PLYR subrecord as `wbFormID(PLYR, 'Player', cpNormal, True).SetDefaultNativeValue($7)` (`wbDefinitionsFO4.pas:12131`) with no target list, so no FormLink row is added. Its authoring row has minimum 1 and default `07000000`. Not covered: the variant subrecords XEZN, XLKR, XPWR, XAPD, XAPR, XASP, XATP, XLYR, XMSP, XRFG, XRGD, XEMI, XMBR, XLRT, XLRL, XLOD, XCVR and MNAM, which are arrays, unions, or payloads that have no typed row in this batch. XRNK is a plain `i32` scalar row per variant (`<variant>\OwnerRank`, `wbDefinitionsCommon.pas:9105`). The `XGLB` branch of `wbOwnership` is behind `IsTES4` (`wbDefinitionsCommon.pas:9106`) and is not modelled.

## Radicle/include/fo4/forms/placed_reference.hpp:PlacedReferenceKind

`PlacedReferenceKind::variant` marks the eight ReferenceRecord variants so consumers can tell them apart from REFR (`object`) and ACHR (`npc`). The enum is set by `decode_placed_reference` and read by `PluginEditTool/native/src/plugin/RadiclePluginCell.cpp:placedReferencesJson`.

## Radicle/src/forms/placed_reference.cpp:decode_placed_reference

The decoder used to accept only REFR and ACHR. It now accepts every signature that `is_placed_reference_signature` (`src/schema/form_link_mixed.cpp:is_placed_reference_signature`) already names, and sets the kind from the signature. REFR and ACHR decode as before, except deleted records without DATA (below). The variants read NAME, DATA, XSCL, EDID and the deleted flag the same way; xEdit gives them one layout. A live record without DATA throws `PlacedReferenceError`. A deleted record without DATA decodes as a view with `deleted` set and a zero `transform`, because a deleted view only erases the master's copy and never reads the transform. Measured read-only over the seven masters: 548 deleted REFR, 3 deleted ACHR and 1 deleted PHZD (DLCworkshop03.esm, form ID 001A31E8) have no DATA. Tests: `test_placed_reference.cpp:DeletedReferenceWithoutDataDecodesAsDeleted`, `LiveReferenceWithoutDataIsRejected`.

Consumer effect: `placed_references_for_cell` now returns variants. `PluginEditTool/native/src/plugin/RadiclePluginCell.cpp:placedReferencesJson` maps object to `PlacedObject`, variant to `PlacedTrap` (as `CellService.cs` does for `IAPlacedTrapGetter`) and npc to `PlacedNpc`. Test: `PluginEditTool/native/tests/unit/RadiclePluginCellTests.cpp` (`Native cell takes each placed base's model from its type-aware primary model`, which lists a PHZD as `PlacedTrap`).

## Radicle/tests/test_placed_variants.cpp:PlacedVariants

Synthetic fixture: one ADDN in a top-level group, and one REFR in persistent children and the eight variants in temporary children of one CELL. Covers the variant kind and base, transform and scale of each variant (`CellListingDecodesEveryVariantAsVariant`); ADDN is not a placed reference (`DecodeSkipsAddonNodeBecauseItIsNotAReference`); every scalar and FormLink row reads its subrecord (`EveryScalarAndFormLinkRowReadsItsSubrecord`); one scalar and one link per record edit through `set_record_scalar` and `set_record_formlink_field`, then write and re-read (`EditsOneScalarAndOneLinkPerRecordAndRoundTrips`, `EditsAddonIndexAndSoundLinkAndRoundTrips`).

## Radicle/tests/test_placed_variants_real.cpp:PlacedVariantsReal

For each official master and each tracked signature (variants, PLYR, ADDN): every scalar row decodes, every variant (deleted ones included) decodes as variant through `decode_placed_reference`, no variant has a `record_placement_invalid` preflight issue (`preflight_authoring_record`), and each record meets the authoring minimum, maximum and order rules (`check_authoring`). Counts go to `RecordProperty`. Signatures with zero records pass without assertions. Fallout4.esm must contain ADDN, PGRE and PHZD. Runs only with FO4_DATA_DIR set. Placement preflight is sampled at 64 records per signature per master because the preflight record lookup is a linear scan.

## Radicle/schema/fallout4/fields.json:records[EFSH..GDRY]

Batch R3a adds rows and authoring for EFSH, IMGS, IMAD, ZOOM, CAMS, SPGD and GDRY, placed directly after GRAS. Layouts are xEdit `Core/wbDefinitionsFO4.pas` at pin 01a11729: SPGD 6128-6161, EFSH 6510-6724, IMGS 7393-7464, IMAD 7466-7572, CAMS 7880-7928, GDRY 12322-12335, ZOOM 13180-13189. Bool names are `wbBoolEnum` (`Core/wbDefinitionsCommon.pas:7240`). Zoom overlay names are `wbZoomOverlayEnum` (`Core/wbDefinitionsCommon.pas:7941`); FO4 names 0 to 16 are modeled, and entries 17 to 21 resolve to empty names in FO4, so they are not modeled.

Corpus counts come from a read-only scan of Fallout4.esm and the six official DLC masters that walks every nested GRUP: EFSH 70 (FO4 63, DLCRobot 4, DLCCoast 3), IMGS 334 (FO4 293, DLCCoast 32, DLCworkshop03 2, DLCNukaWorld 7), IMAD 269 (FO4 247, DLCRobot 3, DLCCoast 8, DLCworkshop02 3, DLCworkshop03 1, DLCNukaWorld 7), ZOOM 18 (FO4 16, DLCRobot 1, DLCCoast 1), CAMS 772 (FO4 only), SPGD 4 (FO4 3, DLCNukaWorld 1), GDRY 69 (FO4 61, DLCCoast 6, DLCNukaWorld 2). Every batch record sits in a top-level group, so placement is `top_level`.

Flattening: a nested struct whose members are all fixed-size scalars is expanded into scalar rows at absolute byte offsets. This covers vec3 and RGB color members, CAMS `Time Multipliers`, the IMGS ENAM, HNAM, CNAM and TNAM structs, and the IMAD HDR and Cinematic mult/add pairs. Arrays, unions, interpolator lists and version-gated members are not modeled. `wbUnused` padding is skipped.

Minimum 1 only where xEdit marks the subrecord required and the corpus has it on every record: EFSH ICON, NAM7, NAM8, DATA (70 of 70 each) and DNAM (70 of 70; the `wbUnion` required argument at `wbDefinitionsFO4.pas:6722`, no default, since its size is 157 or 395 bytes by form version); IMAD EDID, DNAM, TNAM and NAM3 (269 of 269); ZOOM GNAM (18 of 18); CAMS DATA (772 of 772); SPGD DATA (4 of 4). Kept at 0: CAMS MNAM (771 of 772), GDRY DATA (69 of 69, but xEdit has no `SetRequired`, `wbDefinitionsFO4.pas:12324`), and IMGS HNAM, CNAM, TNAM and DNAM (333 of 334, the ENAM-only record has none).

Defaults use xEdit's native or edit defaults and are sized to the fixed layout: CAMS DATA (Action Hit, Location Target, Global 1.0; `wbDefinitionsFO4.pas:7885`), ZOOM GNAM (FOV multiplier 1.0), SPGD DATA (Box Size 4096), GDRY DATA (Intensity 1.0, Air Color Scale 3.0, Back Color Scale 2.0, Forward Color Scale 4.0, Forward Phase 0.75).

Short-payload rows use `minimum_payload_size` (the xEdit `SetOptionalFrom` tails). CAMS DATA is 64 bytes in 102 records, 52 bytes in 472 (no Rotation Offset) and 44 bytes in 198 (no Location Spring, Target Spring or Rotation Offset). Rows: LocationSpring 48, TargetSpring 52, RotationOffsetX, Y and Z 56, 60 and 64. SPGD DATA is 96 bytes in all 4 records. xEdit's `SetOptionalFrom(10)` (`wbDefinitionsFO4.pas:6128-6161`) counts the `wbUnused(4)` members, so its optional tail starts at CenterOffsetMax (offset 40): CenterOffsetMax uses minimum 44, InitialRotation 52, SubtexturesX 60, SubtexturesY 68, Type 76, BoxSize 84 and ParticleDensity 92. IMGS DNAM is 24 bytes in 280 records and 16 bytes in 53 (no vignette); VignetteRadius uses minimum 20 and VignetteStrength 24.

Not covered, by record:
- EFSH: the DNAM union on `wbFormVersionDecider(106)` (`wbDefinitionsFO4.pas:6519-6722`). Its Data arm holds the flat block and the Ambient Sound FormLink. Vanilla DNAM sizes are 157 bytes (67 records) and 395 bytes (3 records). A form-version gate is needed before rows exist. DATA is `wbUnknown` with 0 bytes in all 70 records and has no row. MODL, MODT, MODC, MODS and MODF are the generic model (`wbDefinitionsFO4.pas:1030`) and appear in authoring only.
- IMAD: BNAM, VNAM, TNAM, NAM3, RNAM, SNAM, UNAM, NAM1, NAM2, WNAM, XNAM, YNAM, NAM5, NAM6 and NAM4 are time-interpolator and color-interpolator lists. Each is listed in authoring with maximum 1, because the corpus never repeats it. The 21 `@IAD`..`TIAD` interpolator subrecords (0x40 to 0x54) are listed the same way. The 21 `\x00IAD`..`\x14IAD` subrecords (0x00 to 0x14) are not listed because authoring signatures must be printable ASCII. DNAM Vignette Radius and Strength (offsets 244 and 248) are gated by `wbFromVersion(116)` and are not modeled; 49 vanilla records (244-byte DNAM) lack them.
- ZOOM: overlay values 17 to 21 (empty FO4 names) are not modeled. The corpus holds only 0 to 16.
- CAMS: CTDA conditions (`wbConditions`, 78 records, up to 8 per record) are listed in authoring with maximum 65535 and have no row.
- IMGS ENAM occurs once in the corpus (56 bytes) and is modeled as flat rows. Its one record is not a basis for a required rule.

## Radicle/tests/test_image_fx_records.cpp:ImageFxSynthetic

One synthetic plugin without masters holds 10 records, each in its own top-level group. IMAD 3001 has a 252-byte DNAM and IMAD 3002 a 244-byte DNAM. ZOOM 3003, CAMS 3004 (64-byte DATA) and 3005 (44-byte DATA), SPGD 3006, GDRY 3007, IMGS 3008 (full) and 3009 (16-byte DNAM) and EFSH 3010 (DATA with 0 bytes, DNAM as a 157-byte block) cover the batch. The first test reads every scalar row that has its subrecord. The second checks that minimum-payload rows are absent on short CAMS and IMGS subrecords. The third edits one scalar, string or link per record through `set_record_scalar`, `set_record_string` and `set_record_formlink_field`, writes, re-reads and compares the payload bytes. The fourth retargets the ZOOM and CAMS image-space links to the second IMAD record and checks the stored bytes. The fifth shows that writes to rows beyond a short payload return `subrecord_missing` and leave the plugin bytes unchanged.

## Radicle/tests/test_image_fx_records_real.cpp:ImageFxRecordsReal

For each batch record in each official master the test checks that every printable subrecord is listed in its authoring entry and that each subrecord count is within its minimum and maximum. It decodes every scalar row against its subrecord payload. For each signature it calls `record_field_count` on every FormLink row for the first 64 records only, because the lookup is linear. Non-printable IMAD interpolator signatures are skipped by design. Fallout4.esm must hold at least one batch record.

## Radicle/schema/fallout4/fields.json:records[MATO..RFCT]

Batch R3b (#287 C3) moves MATO, MATT, IPCT, IPDS, LENS and RFCT from preservation-only to field rows and authoring. The block sits right after LVSP. Each citation quotes the pinned xEdit definition (pin 01a11729, `wbDefinitionsFO4.pas`). Corpus counts come from a read-only scan of Fallout4.esm and the six DLC .esm masters: MATO 26, MATT 156, IPCT 865, IPDS 371, LENS 49 and RFCT 53 records. All six sit directly under a top-level GRUP of their own signature (`placement: top_level`); no nested group holds them.

- Radicle/schema/fallout4/fields.json:MATO: `wbRecord(MATO` (:9375). `wbStruct(DATA, 'Directional Material Data', [` (:9381) is required (`.SetRequired` :9390); corpus 26/26. DATA sizes are 28 (1 record), 32 (2) and 48 (23), so the layout is version-gated. Rows cover offsets 0 to 24: Falloff Scale, Falloff Bias, Noise UV Scale and Material UV Scale (:9382-9385), and `wbVec3('Projection Vector')` (:9386) as three f32 rows. The required default is 48 bytes with xEdit's edit defaults (Falloff Bias 0.4, UV scales 512, Normal Dampener 1.0). Not covered: offsets 28 and up, which are `wbFromVersion(19)` Normal Dampener (:9387), `wbFromVersion(25)` Single Pass Color (:9388) and `wbFromVersion(31)` Single Pass (:9389). The hidden `DNAM` property-data array (:9378-9380) is listed in authoring at 0..65535 with no row; corpus has it in 6/26 records, at most 2 each. MODL is coupled; MODT, MODC, MODS and MODF are authoring-only (`wbGenericModel` :1030).
- Radicle/schema/fallout4/fields.json:MATT: `wbRecord(MATT` (:7958). Required and corpus 156/156: `wbFloatColors(CNAM, 'Havok Display Color')` (:7962-7963), which is three f32 values Red, Green and Blue (`wbFloatColors` at `wbDefinitionsCommon.pas:6810-6830`; its 0-255 normalizer is display only); BNAM Buoyancy (:7964-7966, default 1.0); FNAM Flags (:7967-7972, names at bits 0 to 2; corpus values {0, 2, 3, 6, 7}). Not required: PNAM Material Parent (:7960, `[MATT, NULL]`, so nullable; 154/156 present, no NULL seen), HNAM Havok Impact Data Set (:7973, `[IPDS]`, not nullable; 138/156 present, no NULL seen), MNAM (:7961, string, 156/156 but not required by xEdit), ANAM Breakable FX (:7974, 1 record) and MODT (:7975, 1 record). MNAM and ANAM are zero-terminated in every corpus record. MATT HavokColor rows (`MATT\HavokColorRed`, `MATT\HavokColorGreen`, `MATT\HavokColorBlue` in fields.json) store the raw float; xEdit displays it multiplied by 255 (`wbDefinitionsCommon.pas:6818`).
- Radicle/schema/fallout4/fields.json:IPCT: `wbRecord(IPCT` (:7978). DATA (`wbStruct(DATA, '', [` :7981) is required (`cpNormal, True` :8003); corpus 865/865, 24 bytes. Rows: Effect Duration (:7982), Effect Orientation enum 0 to 2 (:7983-7987; corpus {0, 1, 2}), Angle Threshold (:7988), Placement Radius (:7989), Sound Level enum (:7990-7991, names from `wbSoundLevelEnum` at `wbDefinitionsFO4.pas:3424-3430`; corpus {1, 4}), Flags u8 with bit 0 'No Decal Data' (:7992-7994; corpus {0, 1}) and Impact Result enum 0 to 4 (:7995-8001; corpus {0, 4}). Not covered: `wbUnknown(2)` at DATA offset 22 (:8002), and DODT Decal Data (:8004), a nested struct from `wbDefinitionsFO4.pas:3400-3412` (36 bytes, corpus 240/865); DODT is authoring-only. Form links: DNAM Texture Set (:8005, `[TXST]`; corpus 257), ENAM Secondary Texture Set (:8006, `[TXST]`; 23), SNAM Sound 1 (:8007, `[SNDR]`; 801), NAM1 Sound 2 (:8008, `[SNDR]`; 412), NAM3 Footstep Explosion (:8009, `[EXPL]`; 0 corpus occurrences, so the row is from xEdit alone), and NAM2 Hazard (:8010, `[HAZD]`; 11). None of these lists has NULL, so none is nullable, and no corpus link is zero. FNAM Footstep Particle Max Dist (:8011, f32; 3 records).
- Radicle/schema/fallout4/fields.json:IPDS: `wbRecord(IPDS` (:8014). PNAM is `wbRArrayS` of `wbStructSK(PNAM)` (:8016-8019): 8-byte pairs of a Material (MATT) and an Impact (IPCT). Corpus has PNAM in 361/371 records, up to 93 pairs each. The pairs are an array, so they get no row; they are listed in authoring at 0..65535. The PNAM pair remap is the `k_ipds` and `k_pnam` branch in Radicle/src/schema/form_links.cpp, not a table in form_link_mixed.cpp.
- Radicle/schema/fallout4/fields.json:LENS: `wbRecord(LENS` (:12405). `wbFloat(CNAM, 'Color Influence')` (:12407) is an f32 row; corpus 28/49. Not covered: `wbFloat(DNAM, 'Fade Distance Radius Scale')` (:12408). The sprite array reuses DNAM for the sprite ID string (:12412). The top-level DNAM is absent in 21 of 49 records, and in those the first DNAM is a sprite string, so a scalar row would decode text as a float. Also not covered: `LFSP` count (:12409) and the sprite array (:12410-12426: DNAM, FNAM, LFSD), corpus LFSP 1 to 20 per record. LFSP is kept out of authoring because the sprite DNAM follows it in vanilla, and the order check in Radicle/src/schema/preflight.cpp:preflight_authoring_record would reject that. LENS authoring is EDID, CNAM and DNAM (0 to 65535, since sprites repeat DNAM).
- Radicle/schema/fallout4/fields.json:RFCT: `wbRecord(RFCT` (:6163) has EDID and DATA only. DATA (`wbStruct(DATA, 'Effect Data', [` :6165) is required (:6173); corpus 53/53, 12 bytes. Form links: Effect Art ARTO (:6166, `[ARTO, NULL]`, nullable; no zero in corpus) and Shader EFSH (:6167, `[EFSH, NULL]`, nullable; zero in 53/53). Flags u32 (:6168-6172) has names at bits 0 to 2; corpus {0, 1, 4, 6}. RFCT DATA links are not visited by remap (`Radicle/src/schema/form_links.cpp:remap_record_form_links`) until the schema FormLink remap change lands.

- Radicle/tests/test_material_impact_records.cpp:EditsOneRowPerRecordAndRereads: a code-built fixture of top-level groups. Each record gets one scalar edit, and MATT and IPCT get string or link edits. Each link is retargeted to a second fixture record, and the stored bytes are compared after re-read.
- Radicle/tests/test_material_impact_records_real.cpp:EveryBatchRecordMatchesItsAuthoringAndDecodesEveryRow: over the official masters, every batch record meets its authoring minimums and maximums. Unlisted subrecords are allowed only for the LENS sprite array members (LFSP, FNAM, LFSD). `record_field_count` is sampled at 64 records per signature per master, because it is a linear lookup.

## Radicle/schema/fallout4/fields.json:R5 actor support records

Batch R5 adds rows and authoring for CLAS, AVIF, RELA, HDPT, ECZN, EQUP, MOVT, COLL, DUAL and ANIO, placed directly after PROJ. Layouts are xEdit `wbDefinitionsFO4.pas` at pin 01a11729: CLAS 6102-6113, AVIF 7826-7878, RELA 8848-8877, HDPT 7006-7055, ECZN 8022-8037, EQUP 8835-8846, MOVT 9393-9404, COLL 9547-9570, DUAL 9452-9467, ANIO 5777-5784. The generic model is `wbGenericModel` (1030-1046). Localized helpers are `wbFULL`, `wbFULLReq`, `wbDESCReq` (3692-3695).

Corpus counts come from a read-only scan of Fallout4.esm and the six official DLC masters, walking every nested GRUP. Every batch record sits in a top-level group (group type 0). Records per type: CLAS 32, AVIF 433, RELA 67, HDPT 420, ECZN 365, EQUP 47, MOVT 103, COLL 57, ANIO 168. DUAL has 0 vanilla records, so its rows have no corpus evidence and its real-data check is vacuous. A subrecord is `minimum 1` only when xEdit requires it and every record has it: CLAS DESC (32 of 32) and DATA (32 of 32), AVIF DESC (433 of 433) and NAM0 (433 of 433), RELA DATA (67 of 67), HDPT DATA (420 of 420) and PNAM (420 of 420), ECZN DATA (365 of 365), EQUP DATA (47 of 47), COLL DESC, BNAM, GNAM, MNAM and INTV (57 of 57 each). xEdit requires these but the corpus does not, so they stay at 0: CLAS FULL (31 of 32) and EQUP ANAM (44 of 47). DUAL DATA also stays `minimum 0` although xEdit requires it (`wbDefinitionsFO4.pas:9466`), because no vanilla DUAL record exists to confirm it. The ECZN DATA minimum 1 follows xEdit's `wbStruct` rule: the fifth argument of `wbStruct` is `aRequired`, after `aPriority` (`Core/wbInterface.pas`, not in the local pin copy; the call sites `wbDefinitionsFO4.pas:8036` and `:9466` pass `cpNormal, True`). Each vanilla subrecord order agrees with the xEdit member order (checked per record).

CLAS: `BleedoutDefault` is DATA f32 at offset 4 (line 6110; offset 0 is `wbUnknown(4)`). `InventoryImage` is the ICON string (line 6106, `wbICON` at 5256); no vanilla record has ICON, so its row has no corpus evidence. Not covered: PRPS (`wbArrayS` of `wbObjectProperty`, line 6107, `wbPRPS` at 5272) is an array of structs. Corpus PRPS sizes are 56 and 64 bytes.

AVIF: `DefaultValue` is NAM0 f32 (line 7831, required). `Flags` is AVFL u32 with the 32 xEdit names (lines 7832-7865; the 0x20000000 name `Unknown 39` is xEdit's spelling and is kept). `Type` is NAM1 u32 enum (lines 7866-7877). ANAM (`wbLString`, line 7830), FULL and DESC are localized.

RELA: DATA is a 16-byte struct (line 8850). `Parent` (offset 0) and `Child` (offset 4) are NPC_ links, nullable because xEdit lists NULL. `Rank` is the u8 enum at offset 8 (lines 8853-8863). `Flags` is the u8 flags at offset 11 (lines 8865-8874). `AssociationType` at offset 12 is a FormLink to ASTP, nullable (line 8875). ASTP is out of scope as a record, but the link is a RELA field, so its row stays and it has the same target-only dependency. Not covered: the two `wbByteArray` unknown bytes at offset 9 (line 8864).

HDPT: DATA is a u8 flags field (lines 7013-7021) and PNAM is the u32 `Type` enum (lines 7024-7036). `TextureSet` (TNAM, line 7051), `Color` (CNAM, line 7052) and `ValidRaces` (RNAM, line 7053) are non-nullable FormLinks. `ExtraParts` is the HNAM repeated-subrecord FormLink list to HDPT (`wbRArrayS`, lines 7038-7040, 4-byte entries, 184 records with 1 to 3), `existing_only`. Not covered: `Parts` (NAM0/NAM1 struct array, lines 7041-7050, 190 records with 1 to 2 of each) and the generic model (MODL/MODT/MODC/MODS/MODF, authoring only). CTDA (`wbConditions`, line 7054) is counted in authoring, with no condition scope, as CLFM does.

ECZN: DATA is a 12-byte struct (lines 8024-8036). `Owner` (offset 0, NPC_ and FACT) and `Location` (offset 4, LCTN) are nullable FormLinks. `Rank` (offset 8) and `MinLevel` (offset 9) are i8; `Flags` (offset 10, bits 0 to 3) is u8; `MaxLevel` (offset 11) is i8. Names from lines 8030-8033.

EQUP: `Flags` is DATA u32 (lines 8838-8842). Not covered: ANAM `ConditionActorValue` (line 8844, a FormLink to AVIF). Fallout4.esm EQUP 0001A02F has ANAM = 0x0000000E, which is a CONT, and 16 records hold 0xFFFFFFFF, so an AVIF target would fail `CheckErrors.VanillaMasterHasNoErrorFindings`. ANAM stays in the authoring table (minimum 0). PNAM `Slot Parents` (line 8837, `wbArray`) is a packed array of EQUP links in one subrecord. Corpus sizes are 4, 8 and 12 bytes, one PNAM per record. The schema's FormLink lists read one entry per subrecord (`src/schema/formlink_list_read.cpp:record_formlink_list_values`, `whole_payload` for `per_subrecord`), and `packed_array` storage is generated only for keyword records, so a packed list would fail to decode.

MOVT: `Name` is MNAM string (line 9395). INAM is a 12-byte struct of three f32 values at offsets 0, 4 and 8 (lines 9397-9401, 21 records). The 180/Pi display scale on `Directional` does not change the stored bytes. `FloatHeight` (JNAM, line 9402) and `FlightAngleGain` (LNAM, line 9403) are f32. Not covered: SPED (`wbUnion` on `wbFormVersionDecider([28, 60, 104])`, line 9147, referenced at 9396). Its size varies with form version (corpus sizes 112, 68, 44 and 40).

COLL: `Index` is BNAM u32 (line 9550). `Flags` is GNAM u32 with Trigger Volume, Sensor and Navmesh Obstacle at bits 0 to 2 (lines 9552-9556). `InteractablesCount` is INTV u32 (line 9558). `Name` is the MNAM string (line 9557). DESC is localized. Not covered: FNAM debug color (`wbByteColors`, line 9551, a 4-byte color with no color kind, like CLFM `ColorIndex` (CNAM), which is also read as a plain integer) and CNAM `Collides With` (`wbArrayS`, line 9559, a packed array of COLL links in one subrecord, the same storage gap as EQUP PNAM).

DUAL: `InheritScale` is DATA u32 flags at offset 20 (lines 9461-9465). `Projectile`, `Explosion`, `EffectShader`, `HitEffectArt` and `ImpactDataSet` are DATA FormLinks at offsets 0, 4, 8, 12 and 16 (lines 9456-9460), all nullable. The DATA struct is 24 bytes in xEdit only, since the corpus has no DUAL records. Not covered: OBND (`wbObjectBounds`, authoring only).

ANIO: `UnloadEvent` is the BNAM string (line 5783). The generic model (line 5782) is authoring only: MODL is coupled, as in HAZD, and MODT/MODC/MODS/MODF are plain counts.

## Radicle/tests/test_actor_support_records.cpp:ActorSupportSynthetic

One synthetic plugin with no masters holds a target record for each link (NPC_, FACT, LCTN, AVIF, ASTP, PROJ, EXPL, EFSH, ARTO, IPDS, TXST, CLFM, FLST, HDPT) plus one record per batch type. Each test edits scalars, strings and links through `set_record_scalar`, `set_record_string`, `set_record_formlink_field` and `set_record_formlink_list_element`, retargets every edited link to a second fixture record of the same type, then writes, re-reads and compares each subrecord payload byte for byte.

## Radicle/tests/test_actor_support_records_real.cpp:ActorSupportReal

For every batch record in each official master, the test checks that every subrecord is listed in the authoring table and that each subrecord count falls within its minimum and maximum. It decodes every scalar row. It calls `record_field_count` for FormLink rows on at most 64 records per signature per master, because that lookup is linear. Only Fallout4.esm must contain batch records. DUAL has none, so the Fallout4.esm check is satisfied by the other nine types.

## Radicle/schema/fallout4/fields.json:R6 item and mod records

Batch R6 (#287 C6) moves NOTE, ENCH, AMDL, AORU, PKIN, DOBJ and INNR to schema rows and authoring. The new `records` block sits after SNDR. Each citation quotes the pinned xEdit definition (pin 01a11729, `wbDefinitionsFO4.pas` unless a file is named). Corpus counts come from a read-only scan of the seven official masters (Fallout4.esm and the DLC .esm files, nested GRUPs walked): NOTE 252, ENCH 416, AMDL 60, AORU 9, PKIN 1009, DOBJ 1, INNR 20 records. Every vanilla record of these signatures sits directly under its top-level GRUP (`top_level` placement).

- Radicle/schema/fallout4/fields.json:NOTE: `wbInteger(DNAM, 'Type', itU8,` (:12480) is an enum with four names (DNAM present in 252/252 vanilla records). `wbStruct(DATA, 'Attributes'` (:12488) gives Value u32 at 0 and Weight f32 at 4, payload 8 (252/252). OBND is required by `wbObjectBounds` (wbDefinitionsCommon.pas:9089), corpus 252/252, so its authoring minimum is 1 with 12 zero bytes. EDID, VMAD, PTRN (:12474), FULL (:12475), YNAM and ZNAM (:12478-12479, SNDR, 239/252 each) and PNAM (:12498, 8/252) are not required in xEdit, so their minimums are 0. PTRN targets TRNS and is not nullable. ICON (:12477) has no vanilla record and is kept as a row, labeled "Inventory Image" (`wbICON`, wbDefinitionsFO4.pas:5256). Not covered: SNAM union (`wbUnion(SNAM, '', wbNoteTypeDecider` :12492, 244/252), MODT (model info, binary), MODC, MODS, MODF (no row, 0 in vanilla), VMAD (struct, authoring only).
- Radicle/schema/fallout4/fields.json:ENCH: the ENIT struct `wbStruct(ENIT, 'Effect Data', [` (:6730) with `.SetRequired` (:6748) is required, corpus 416/416 at 36 bytes (all 36). Members: Enchantment Cost i32 (:6731), Flags u32 (:6732-6735; bit 1 is unnamed in xEdit and is named "Unknown 2" here), Cast Type (:6737, `wbCastEnum` at wbDefinitionsFO4.pas:4822), Enchantment Amount (:6738), Target Type (:6739, `wbTargetEnum` at :4829), Enchant Type (:6740-6743, values 6 and 12), Charge Time f32 (:6744), Base Enchantment (:6745, `[ENCH, NULL]`, nullable) and Worn Restrictions (:6746, `[FLST, NULL]`, nullable). Corpus values: flags 0/1/4, cast 0/1/2, target 0/1, enchant type always 6, both FormLinks always 0. xEdit sets no ENIT default. The authored default sets Enchant Type to 6 (Enchantment), because 6 is a named value present in 416/416 vanilla records, and everything else to 0. Not covered: a 28-byte ENIT (`SetOptionalFrom(8)`, wbDefinitionsFO4.pas:6747) does not decode with the payload_size 36 rows, and vanilla ENITs are all 36 bytes. OBND is required (wbObjectBounds, corpus 416/416). FULL is optional (306/416). EDID is optional. EFID, EFIT and CTDA (`wbEffectsReq` at :5608, 614 effect entries and 89 CTDA records) are an array of structs and are not covered; K1's ALCH effect list is handled the same way.
- Radicle/schema/fallout4/fields.json:AMDL: `wbStruct(DNAM, 'Data', [` (:12248) has 16 members at 64 bytes (60/60). Offsets follow xEdit member order: 0 to 12 f32, 16 u32 (Decrease Delay MS), 20 to 40 f32, 44 u32 (Runaway Recoil Shots), 48 to 60 f32. DNAM is not required in xEdit, so its minimum is 0.
- Radicle/schema/fallout4/fields.json:AORU: `wbStruct(AOR2, 'Data', [` (:12270) ends with `], cpNormal, True)` (:12277), so it is required; corpus 9/9 at 16 bytes. Radius, Min Delay and Max Delay are f32 at 0, 4 and 8. Requires Line of Sight and Combat Target are u8 bool enums at 12 and 13 (`wbBoolEnum`, wbDefinitionsCommon.pas:7240). Offsets 14 and 15 are `wbUnused(2)` and have no row. The AOR2 authoring default is xEdit's Radius 600 and Max Delay 30 (`SetDefaultEditValue` at wbDefinitionsFO4.pas:12271 and :12273), with the other members zero.
- Radicle/schema/fallout4/fields.json:PKIN: `wbFLTR` (wbDefinitionsFO4.pas:5274) is a string at FLTR (1008 of 1009 records have EDID; FLTR 252/1009). `wbFormIDCk(CNAM, 'Cell', [CELL])` (:12578) is a FormLink that is not nullable (1008/1009). `wbInteger(VNAM, 'Version', itU32)` (:12579) is a non-embedded u32 (877/1009). OBND is not 100% in vanilla (9/1009), so it is authoring-optional even though xEdit marks it required (wbObjectBounds). Record flag 0x200 (Prefab) is a record flag and has no subrecord row.
- Radicle/schema/fallout4/fields.json:DOBJ: the only vanilla record (1) has DNAM at 3168 bytes. `wbArrayS(DNAM, 'Objects', ...)` (wbDefinitionsFO4.pas:8620) is required (`.SetRequired` at :8626) and holds `Use` u32 plus `Object ID` form ID pairs. It is not covered: it is an array of structs, and its form ID remap is in `src/schema/form_link_mixed.cpp` and `src/schema/form_links.cpp:visit_link_offsets` (DOBJ DNAM branch). DNAM is a required authoring subrecord (minimum 1, maximum 1) with no default: vanilla is 1/1 and xEdit requires it, and the codegen parses a default only when one is present (record_schema_codegen.py:98-100), so create returns no_default. The unit fixture writes 8 zero bytes for it. EDID is optional.
- Radicle/schema/fallout4/fields.json:INNR: `wbInteger(UNAM, 'Target', itU32, wbEnum([...` (:12339-12345) has five names: 0 None, 29 Armor, 45 Actor, 42 Furniture, 43 Weapon. Corpus values are 29 (11) and 43 (9). UNAM is not required in xEdit, so its minimum is 0. The `wbRArray('Naming Rules', ...)` (:12346) with VNAM, WNAM, KSIZ, KWDA, XNAM and YNAM is an array of structs and is not covered.
- Radicle/tests/test_item_mod_records.cpp:EditsOneRowPerRecordAndRereads: synthetic fixture with one record per signature; one scalar, one string or one FormLink edit per record, then re-read and compared. Each FormLink is retargeted to a second fixture record (SNDR, ENCH, CELL) and checked in `collect_form_links`.
- Radicle/tests/test_item_mod_records.cpp:EveryScalarRowDecodesOnTheFixture: every scalar and string row decodes on the fixture (scalar_fields includes string rows).
- Radicle/tests/test_item_mod_records.cpp:FlagsRowNamesComeFromXEdit: `ENCH\Flags` bit 2 name from `scalar_value_name`.
- Radicle/tests/test_item_mod_records_real.cpp:RowsDecodeAndAuthoringLimitsHold: official masters; scalar and FormLink payload sizes and decodes, and authoring minimums and maximums per record.

## Radicle/schema/fallout4/fields.json:records[SMBN..CPTH]

Batch R7a adds rows and authoring for SMBN, SMEN, SMQN, SCCO, TRNS and CPTH, placed directly after FLST. Layouts are xEdit `Core/wbDefinitionsFO4.pas` at pin 01a11729: CPTH 7930-7948, SMBN 8720-8733, SMQN 8735-8760, SMEN 8762-8776, SCCO 12593-12621, TRNS 12774-12786. The ENAM event list is `wbQuestEventEnum` (`Core/wbDefinitionsCommon.pas:7845-7897`).

Corpus counts come from a read-only scan of Fallout4.esm and the six official DLC masters: SMBN 136, SMEN 18, SMQN 346, SCCO 231, TRNS 1409, CPTH 447 records. Every record sits in a top-level group labelled with its own signature, so every record uses `placement: top_level`. No nested group placement was seen.

Required means xEdit-required and present in every vanilla record. CITC is required in SMBN (136 of 136), SMEN (18 of 18) and SMQN (346 of 346) through `wbCITCReq` (`wbDefinitionsFO4.pas:3280`). SMQN QNAM (Quest Count, line 8753) is 346 of 346; its `wbInteger` sixth argument is aRequired (xEdit wbInterface.pas), set True there. SCCO VNAM is an unknown that xEdit requires twice (lines 12609 and 12618) and it appears exactly twice in all 231 records, so authoring is min 2 max 2. TRNS DATA (SetRequired, line 12785) is 1409 of 1409. CPTH ANAM (SetRequired, line 7936) and DATA (line 7937, required) are 447 of 447. Optional subrecords stay minimum 0 even at 100 percent (PNAM, SNAM, DNAM, XNAM, ENAM, MNAM, HNAM, SCCO QNAM).

Authoring order follows xEdit member order and every vanilla record shape: SMBN 3 sequences, SMEN 3, SMQN 5 distinct sequences of listed subrecords (counted over the 346 vanilla SMQN records, with CTDA excluded), SCCO 1, TRNS 2, CPTH 4.

SMBN and SMEN DNAM: Random bit 0 and Warn if no child quest started bit 1 (lines 8726-8731 and 8768-8772). Corpus values are 0 and 1 for SMBN and 0 and 2 for SMEN. SMQN DNAM uses wbSparseFlags with bits 0, 1, 16, 17 and 18 (lines 8741-8749). Corpus bits used are 0, 1, 16, 17 and 18.

SMEN ENAM Type uses 38 names. xEdit gives FO4 the else branch of IsTES5, IsFO76 and IsSF1 (`Core/wbDefinitionsCommon.pas:6201-6205`, `6285-6289`, `6320-6324`). Entries that resolve to an empty name are dropped, so ADBO, CBGN, DOCK, ILOC, LAND, LCPG, PCON, QPMT, SKIL and XPLL have no row name. The 18 corpus ENAM codes are all in the list.

SMEN PNAM: all 18 vanilla SMEN records point at form ID 0x0000005B in Fallout4.esm. No record in the scanned masters defines that object ID, so the link does not resolve to a record. SMEN PNAM holds 0x5B in all 18 vanilla records; that is a Fallout4.esm ID below 0x800, and `src/graph/check_errors.cpp:check_links` skips unresolved targets in that range. Make no claim about what 0x5B is. Parent links point to SMQN, SMBN or SMEN (SMBN parents 83 SMBN and 53 SMEN; SMQN parents 312 SMBN and 34 SMEN). SNAM NULL appears 33 times on SMBN, 1 on SMEN and 114 on SMQN, so PNAM and SNAM are nullable with targets [SMQN, SMBN, SMEN] (xEdit's list adds NULL, which is not a record signature).

SCCO QNAM targets QUST in all 231 records and is not nullable (xEdit list has no NULL). Scene Layout SNAM targets SCEN (797 links; 3 links point at targets not in the masters).

TRNS DATA is 36 bytes in 1109 records and 28 bytes in 300. xEdit makes Zoom Min and Zoom Max optional from member index 2 (SetOptionalFrom(2), line 12784). The Zoom rows use minimum_payload_size 32 and 36, the same gate WTHR uses. The gate is by payload size, not form version: 28-byte payloads occur at form versions 53-130 and 36-byte payloads at 130-131; version 130 has both (16 records at 28 bytes, 61 at 36). Position and rotation are wbVec3PosRot (`Core/wbDefinitionsCommon.pas:6536`, `wbVec3` at 6451): Pos X, Y, Z at offsets 0, 4, 8 and Rot X, Y, Z at 12, 16, 20. Scale is at offset 24. The authoring default is 24 zero bytes, Scale 1.0, Zoom Min -1.0 and Zoom Max 1.0 (SetDefaultEditValue, lines 12781-12783).

CPTH ANAM holds two FormIDs at offsets 0 (Parent) and 4 (Previous). Both targets are [CPTH] with NULL allowed (xEdit list includes NULL). Corpus Parent is CPTH 442 times and NULL 5 times; Previous is CPTH 341 times and NULL 106 times. CPTH DATA is a u8 flags field with Disable bit 0 through Not Must Have Camera Shots bit 7 (lines 7938-7945). Corpus values are 0, 1, 4, 5, 64 and 65.

Not covered:
- CTDA condition content on SMBN, SMEN, SMQN and CPTH. `condition_scopes` in fields.json covers only nested containers (ALCH, ENCH, INGR and SPEL Effects; PACK Branches; PERK Tabs; QUST Aliases and Targets), so these top-level conditions have no scope row. CTDA is listed in authoring as 0 to 65535, the same as FACT. CIS1 and CIS2 are not listed: corpus has CIS2 on 2 SMBN, CIS1 on 7 SMQN and CIS2 on 14 SMQN, and no CPTH or SMEN CIS subrecords.
- SMQN Quests array: NNAM (QUST, 1468 links) and RNAM (hours, stored with xEdit scale 1/24, line 8757). QNAM is the array count, so it is listed in authoring (min 1, max 1) with no value row. This matches LLCT, which is derived from its entry count.
- SCCO Scene Layout array (SNAM SCEN and XNAM X/Y s32, lines 12596-12608) and the Original Coordinates XNAM array (lines 12610-12617). Both VNAM unknowns are listed in authoring without rows.
- CPTH Camera Shots: SNAM CAMS array (line 7947, 824 entries across 329 records). Not listed in authoring.
- TRNS record header flag 0x8000 (Around Origin, lines 12775-12776). It is a record header flag, not a subrecord, and the schema has no kind for it.

## Radicle/tests/test_story_manager_records.cpp:StoryManagerRecordsSynthetic

One synthetic plugin with no masters holds one record per type in its own top-level group, plus QUST targets for SCCO. Each test edits one link, scalar or enum per record through `set_record_formlink_field` and `set_record_scalar`, then writes, re-reads and compares the payload bytes. A 28-byte TRNS DATA reports `subrecord_missing` for Zoom Min because the minimum_payload_size gate rejects it, and it still accepts Scale.

## Radicle/tests/test_story_manager_records_real.cpp:StoryManagerRecordsReal

For every batch record in each official master, the test checks each subrecord count against its authoring minimum and maximum, decodes every scalar row, and calls `record_field_count` for FormLink rows. It does not require every subrecord to be listed, because CIS1, CIS2 and the array subrecords are not listed. `record_field_count` is a linear lookup, so FormLink rows are sampled to 64 records per signature per master. Fallout4.esm must contain at least one batch record.

## Radicle/schema/fallout4/fields.json:R7b loading screen, message and debris records

Batch R7b adds rows and authoring for LSCR, OVIS, NOCM, DEBR, RFGP and MESG as one block directly after PACK. Layouts are xEdit `Core/wbDefinitionsFO4.pas` at pin 01a11729: LSCR 9946-9965, OVIS 12546-12567, NOCM 12453-12468, DEBR 7388-7391, RFGP 12582-12587, MESG 8179-8204. REGN is out of scope.

Corpus counts come from a read-only scan of Fallout4.esm and the six official DLC masters. Records per type: LSCR 448, MESG 1990, RFGP 9368, DEBR 5, OVIS 1, NOCM 1. Every record of the six types sits directly under a top-level GRUP, so all six use `placement` `top_level`. Required means xEdit requires the subrecord and every record has it: LSCR DESC (`wbDESCReq`, 9952; 448 of 448), MESG DESC (8181; 1990 of 1990), MESG INAM (`SetRequired`, 8183; 1990 of 1990), MESG DNAM (`SetRequired`, 8191; 1990 of 1990), DEBR DATA (the Models array is required at 7390, and the DATA struct is `SetRequired` at `Core/wbDefinitionsCommon.pas:8479`; each of the 5 records has 4 to 11 DATA entries). NOCM EDID is `SetRequired` (12456), but its one vanilla record has no EDID, so its minimum is 0. MODT is not required in xEdit (`wbModelInfo` has no `SetRequired`) and is left out of authoring (see DEBR below).

LSCR: ONAM (9956-9959, `Rotation`) is two i16 scalars, `LSCR\RotationMin` at offset 0 and `LSCR\RotationMax` at offset 2, payload 4; all 448 records are 4 bytes. ZNAM (9960-9963, `Zoom`) is two f32 scalars, `LSCR\ZoomMin` at offset 0 and `LSCR\ZoomMax` at offset 4, payload 8; 290 records are 8 bytes. MOD2 (9964) is a string row; no corpus record has it. NNAM (9954) is a FormLink to STAT or SCOL with NULL in the xEdit list, so it is nullable. TNAM (9955) targets TRNS only, so it is not nullable. DESC is localized. Not covered: the record-level conditions (`wbConditions`, 9953; CTDA in 278 records, up to 8 each), the record flags at 9947-9950, and the CIS2 parameter string inside each condition (`Core/wbDefinitionsFO4.pas:5250-5251`; 6 records). CIS2 sits after CTDA inside each condition, so it repeats interleaved with CTDA; it is left out of authoring because preflight would reject the order and skips unlisted subrecords.

OVIS: no rows. Authoring lists EDID (plain `wbEDID`; absent from the one vanilla record) and INDX (0 to 65535). DATA is left out: it repeats interleaved with INDX (INDX, DATA, INDX), and preflight would reject the listed INDX after a listed DATA. Preflight skips unlisted subrecords, so DATA is unlisted. Not covered: the unnamed array of INDX and DATA entries (12548-12567).

NOCM: no rows. Authoring lists EDID (0 to 1) and INDX (0 to 65535). DATA, INTV and NAM1 are left out: they repeat interleaved with INDX in the entry array, so listing any of them with INDX would fail preflight's order rule. Preflight skips unlisted subrecords. Not covered: the nested arrays at 12458-12466.

DEBR: no rows. Its only member is the Models array (7390), built by `wbDebrisModel` (`Core/wbDefinitionsCommon.pas:8466`). Each model holds a DATA struct with a percentage, a variable-length model filename and a collision flag, then MODT texture hashes. The variable-length filename makes later offsets unstable and an array is not a row kind, so nothing is modeled. Authoring lists EDID (0 to 1) and DATA (1 to 65535). MODT is left out: it repeats interleaved with DATA (DATA, MODT, DATA), so listing it would fail preflight's order rule, and preflight skips unlisted subrecords.

RFGP: NNAM (12584) is a string row. RNAM (12585) is a FormLink to `sigReferences` (`Core/wbDefinitionsFO4.pas:67-70`), which lists NULL, so it is nullable. Its targets are the eleven non-NULL signatures in xEdit order (PLYR, ACHR, REFR, PGRE, PHZD, PMIS, PARW, PBAR, PBEA, PCON, PFLA), the same list PACK `LocationReference` uses. PNAM (12586) targets PKIN only and is not nullable. Authoring lists EDID, NNAM, RNAM and PNAM, each 0 to 1. Nothing is under Not covered.

MESG: DNAM (8185-8192) is the `Flags` u32 with `Message Box` at bit 0 and `Delay Initial Display` at bit 1 (8186-8189). TNAM (8193-8196, `Display time`) is optional, so xEdit's native default of 2 (8194) is not an authoring default. SNAM (8197, `SWF`) is a string row. QNAM (8184) is a FormLink to QUST, not nullable. Localized subrecords, all `wbLStringKC` in xEdit: DESC (3695), FULL (3692), NNAM (8198, `Short Title`) and ITXT (8201, menu button text). INAM (8183, `Icon (unused)`) has the target list `[NULL]` only, and NULL is not a record signature, so no FormLink row is written; its authoring default is the zero payload. Authoring lists EDID, DESC, FULL, INAM, QNAM, DNAM, TNAM, SNAM, NNAM and ITXT (ITXT 0 to 65535). CTDA, CIS1 and CIS2 are left out: they repeat interleaved with ITXT in the Menu Buttons group (ITXT, CTDA, CIS1, CIS2), so listing them would fail preflight's order rule, and preflight skips unlisted subrecords. Not covered: the Menu Buttons group (8199-8203: ITXT followed by `wbConditions`) and the CIS1 and CIS2 condition strings inside each condition (`Core/wbDefinitionsFO4.pas:5184-5251`).

Authoring order follows xEdit member order for the listed subrecords. Preflight (`src/schema/preflight.cpp`) errors when a listed subrecord's rule index goes down within a record and skips unlisted subrecords, so an interleaved group may keep at most one listed member. The real-data test checks that order rule over every vanilla record of the six types, and the min and max counts. LSCR lists CTDA then NNAM, with CIS2 left out.

## Radicle/tests/test_world_misc_records.cpp:WorldMiscRecordsSynthetic

One synthetic plugin with no masters holds the six batch records plus STAT, TRNS, QUST, PKIN and REFR targets, each record in its own top-level group. The LSCR, RFGP and MESG tests edit one scalar, string or FormLink per record through `set_record_scalar`, `set_record_string` and `set_record_formlink_field`. They retarget each edited link to a second fixture record, then write, re-read and compare payload bytes. They also check that the untouched PNAM and INAM payloads are unchanged. The last test checks that OVIS, NOCM and DEBR expose no scalar or FormLink rows.

## Radicle/tests/test_world_misc_records_real.cpp:WorldMiscRecordsReal

For every batch record in each official master, the test checks the preflight order rule: each listed subrecord's rule index never goes down within a record, and unlisted subrecords are skipped. It also checks that each subrecord count falls within its minimum and maximum. It decodes each scalar row against its subrecord payloads, and it calls `record_field_count` for the FormLink rows of at most 64 records per signature per master. Fallout4.esm must contain at least one batch record.

## Radicle/schema/fallout4/fields.json:K3 magic effect, spell and location

Batch K3 (#287 C10) moves MGEF, SPEL and LCTN from keyword-only (L1) to L4. The `keyword_records` entries are unchanged, and the new `records` block sits right after WEAP. Citations quote the pinned xEdit definitions (pin 01a11729, `wbDefinitionsFO4.pas` unless marked `wbDefinitionsCommon.pas`). Corpus counts come from a read-only scan of the seven official masters: MGEF 832, SPEL 430 and LCTN 501 records, all directly under top-level GRUPs (`placement`: `top_level`).

- Radicle/schema/fallout4/fields.json:MGEF: `wbRecord(MGEF` (:10052). The DATA struct (`wbStruct(DATA, 'Data'`, :10058) ends with `.SetRequired` (:10203). Rows come from its flat members: Flags (`wbFlags`, :10059), Base Cost (:10095), Resist Value (:10109), Taper Weight (:10113), Archetype (:10124), Actor Value (:10177) and Second Actor Value (:10182), which are `wbActorValue` (:3288), Casting Type (:10180), Delivery (:10181), Perk to Apply (:10196), Casting Sound Level (:10197) and Script Effect AI (:10198).
- Radicle/schema/fallout4/fields.json:MGEF not covered: Assoc. Item (wbDefinitionsFO4.pas:10096, offset 8, archetype union), Counter Effect Count (:10110, offset 20, count for ESCE), Counter Effects (ESCE, :10204; 0 vanilla records), Sounds (SNDD, wbMagicEffectSounds, :10207 and wbDefinitionsCommon.pas:8896; 832 of 832, not xEdit-required, unlisted), Conditions (:10209), VMAD, and MDOB (:10056; 0 vanilla records).
- Radicle/schema/fallout4/fields.json:MGEF corpus: DATA 832 of 832 records; FULL 830; DNAM 832; VMAD 244; CTDA 132; KSIZ and KWDA 208. Local ids of 51 Actor Value values match no record in the seven masters; the row is kept because xEdit defines the field as an AVIF link (:3288). All 51 unmatched Actor Value links are Fallout4.esm IDs below 0x800 (engine-hardcoded range).
- Radicle/schema/fallout4/fields.json:MGEF (localized): FULL (:10055) and DNAM (:10208).
- Radicle/schema/fallout4/fields.json:SPEL: `wbRecord(SPEL` (:12030). Required by xEdit and present in 430 of 430 corpus records: DESC (`wbDESCReq`, :3695 and :12036), OBND (`wbObjectBounds`, wbDefinitionsCommon.pas:9089) and SPIT (`wbSPIT`, :11972, required at :12028). Not required: FULL (:3692; 346 of 430), EDID (:3691) and ETYP (`wbETYP`, :4867 and :12035). Rows: the SPIT members (`wbFlags` :11974 for Flags), plus Casting Perk and Equipment type (ETYP). Not covered: EFID, EFIT and CTDA (`wbEffectsReq`, :12038), the K1 ALCH precedent.
- Radicle/schema/fallout4/fields.json:SPEL (localized): FULL (:3692) and DESC (:3695).
- Radicle/schema/fallout4/fields.json:LCTN: `wbRecord(LCTN` (:8039). Not covered: the arrays at :8040-8166. Rows: Parent Location (PNAM, :8169), Music (NAM1, :8170), Unreported Crime Faction (FNAM, :8171; no vanilla record has it, so the row rests on xEdit alone), World Location Marker Ref (MNAM, :8172), World Location Radius (RNAM, :8173) and Actor Fade Mult (ANAM, :8175). Authoring only: CNAM (:8176), KSIZ and KWDA (`wbKeywords`, wbDefinitionsCommon.pas:8661). FULL (:8167) is localized and not required. Corpus: PNAM 437 records, NAM1 68, MNAM 238, RNAM 487, ANAM 486, KSIZ and KWDA 427, CNAM 1.
- Authoring defaults: MGEF DATA and SPEL SPIT are zero-filled at their xEdit sizes; SPEL OBND is 12 zero bytes; SPEL DESC is "00", as in the K1 BOOK rows.
- Radicle/tests/test_magic_location_records.cpp:EditsOneRowPerRecordAndRereads: three fixture records (MGEF, SPEL, LCTN), one scalar edit and one FormLink retarget to a second fixture record of the same type, with the stored bytes asserted after a re-read. No string rows: MGEF, SPEL and LCTN have no string-kind subrecord.
- Radicle/tests/test_magic_location_records_real.cpp:RowsDecodeAndAuthoringLimitsHold: official masters; scalar and FormLink payload sizes and decodes, and the authoring minimums and maximums for MGEF, SPEL and LCTN.

## Radicle/schema/fallout4/fields.json:records[ACTI..TERM]

Batch K2 (#287 C9) moves ACTI, CONT, DOOR, FURN, MSTT, TACT and TERM from L1/L2 to L4. The `keyword_records` entries are unchanged, and the new `records` block sits after INFO. Layouts and required flags are from the pinned xEdit definitions (pin 01a11729, `wbDefinitionsFO4.pas`). Corpus counts come from a read-only scan of the seven official masters, walking every GRUP: ACTI 2196 records, CONT 635, DOOR 444, FURN 783, MSTT 1106, TACT 74, TERM 1074. All of them sit in top-level groups (`placement: top_level`); no nested-group copies were found.

Required rule: `minimum` is 1 only when xEdit marks the member required and every vanilla record of the signature carries it. OBND (`wbObjectBounds`, `wbDefinitionsCommon.pas:9074`, `SetRequired` at 9089) is required in all seven records: 2196/2196, 635/635, 444/444, 783/783, 1106/1106, 74/74 and 1074/1074. The other required members are CONT DATA (`wbDefinitionsFO4.pas:6196`, `True` at 6203, 635/635), DOOR FNAM (6496, `True` at 6504, 444/444), FURN FNAM (6875, `SetRequired` at 6879, 783/783), FURN PNAM (6872, `SetRequired`, 783/783), FURN WBDT (6885, `SetRequired` at 6900, 783/783), MSTT DATA (7092, `SetRequired` at 7094, 1106/1106), and TACT PNAM (5685) and FNAM (5687), both `wbUnknown(..., cpIgnore, True)` with 74/74 occurrences. The `True` argument is read from those call sites; the `wbUnknown` helper itself is not in the pinned tree we have. Every other member is optional with minimum 0.

- Radicle/schema/fallout4/fields.json:ACTI: OBND bounds rows (`wbDefinitionsFO4.pas:5637`), MODL (`wbGenericModel` at 5641, def 1030), FNAM flags (5652, five names), MODS, PTRN (`wbDefinitionsFO4.pas:5258`, TRNS), NTRM (5260, TERM), FTYP (5276, LCRT), and the links SNAM/VNAM (SNDR, 5648/5649), WNAM (WATR, 5650), KNAM (KYWD, 5659). FULL and ATTX (5277, localized) are in `localized_subrecords`. Corpus: FNAM 2196/2196 (size 2), SNAM 2, VNAM 26, WNAM 50, KNAM 0. KNAM has no vanilla occurrence, so its row is layout-only.
- Radicle/schema/fallout4/fields.json:CONT: OBND bounds, MODL, the DATA flags row (`wbDefinitionsFO4.pas:6197-6201`, bits 0-2) and the DATA weight row (offset 1, f32), both `payload_size` 5 (635/635). Links: SNAM 6208, QNAM 6209, TNAM 6210 (SNDR) and ONAM 6211 (FLST), plus PTRN, MODS, FTYP 6205 and NTRM 6206. FULL is localized. Corpus: SNAM 328, QNAM 328, TNAM 87, ONAM 15, DATA 635/635.
- Radicle/schema/fallout4/fields.json:DOOR: OBND bounds, MODL, the FNAM flags row (`wbDefinitionsFO4.pas:6496`, u8, bit 0 unnamed as in xEdit, bits 1-6 named) and links SNAM 6493, ANAM 6494 (close) and BNAM 6495 (loop), all SNDR, plus PTRN, MODS and NTRM 6492. FULL, ONAM 6505 and CNAM 6506 are localized. Corpus: FNAM 444/444, SNAM 327, ANAM 321, BNAM 0, ONAM 6, CNAM 1. BNAM has no vanilla occurrence, so its row is layout-only.
- Radicle/schema/fallout4/fields.json:FURN: OBND bounds, MODL, the FNAM flags row (`wbDefinitionsFO4.pas:6875`, u16, bits 0-1), the WBDT bench type enum row (`wbDefinitionsFO4.pas:6886-6897`, values 0-9; embedded at offset 0 with `minimum_payload_size` 1, because vanilla has 782 records of 1 byte and 1 record of 2 bytes, so no exact `payload_size`), the XMRK string row (6914, corpus 603), the MNAM active-markers flags row (`FURN\ActiveMarkers`, u32, the 32 names of `wbMNAMFurnitureMarker` at `wbDefinitionsFO4.pas:5279-5311`, member at 6884; MNAM is 783/783 with exactly one per record, so it is `minimum` 0 and `maximum` 1, listed between COCT and WBDT), and links PTRN, MODS, NTRM, FTYP (6871), WNAM (WATR, 6873, layout-only, no vanilla occurrence), and NAM1 (6901, [ARMO, WEAP, PERK, SPEL, HAZD], 2 records). FULL and ATTX (6874, localized) are in `localized_subrecords`.
- Radicle/schema/fallout4/fields.json:MSTT: OBND bounds, MODL, and the DATA row as an enum with the False/True names used by FACT (`wbDefinitionsFO4.pas:7092`, `wbBoolEnum`, `SetDefaultNativeValue(1)` at 7093), plus SNAM (7095, SNDR, corpus 27), PTRN and MODS. FULL is localized.
- Radicle/schema/fallout4/fields.json:TACT: OBND bounds, MODL, SNAM (5686, SNDR, layout-only: 0 vanilla occurrences) and VNAM (5688, VTYP, 69/74). FULL is localized. PNAM (5685, 4 bytes, default `00000000`) and FNAM (5687, 2 bytes, default `0000`) are authoring-only with minimum 1 and maximum 1, placed after KWDA and between SNAM and VNAM; they have no rows.
- Radicle/schema/fallout4/fields.json:TERM: OBND bounds, MODL, the FNAM flags row (`wbDefinitionsFO4.pas:12690-12697`, u16, seven names; bit 6 is "Unknown 6" as in xEdit), the MNAM active-markers flags row (`TERM\ActiveMarkers`, u32, the same 32 names, member at 12710; 1074/1074, one per record, `minimum` 0 and `maximum` 1, listed between COCT and WBDT), the XMRK string row (12712, corpus 713), PTRN (12681) and MODS. NAM0 (12682), WNAM (12683) and FULL (12684) are localized.

Authoring order: each listed rule keeps `wbDefinitionsFO4.pas` member order. An order check over all 6312 vanilla records of the seven signatures (ACTI 2196, CONT 635, DOOR 444, FURN 783, MSTT 1106, TACT 74, TERM 1074) found no record where a listed subrecord's authoring index goes down. The real test asserts the same rule (see `Radicle/tests/test_object_base_records_real.cpp:RowsDecodeAndAuthoringLimitsHold`).

Interleaving: no listed member sits inside a repeating group. CONT CNTO, COCT and COED are not listed and are left to the subrecord-groups kind. The listed count members (FURN and TERM COCT, BSIZ, ISIZ, KSIZ, KWDA) are coupled, and the repeated items they count (CNTO, BTXT, ITXT, RNAM, CTDA, CIS1, CIS2, TNAM, UNAM, marker arrays) are not listed, so generic add cannot reach them.

Not covered (no rows, not listed, or listed as authoring-only):
- All: PRPS (`wbPRPS` at 5272, an array of structs; its remap is already handled by `k_repeated_struct_layouts` in `Radicle/src/schema/form_link_mixed.cpp`, which is not reused here), VMAD, the destruction groups DEST/DMDL/DMDT/DMDS/DSTA/DSTD/DSTF (DEST is authoring-only and coupled), MODT, MODC and MODF (model metadata: authoring-only). KSIZ/KWDA are coupled; the keyword list comes from `keyword_records`, which is left alone.
- ACTI: PNAM marker color (`wbByteColors`, 4 bytes, 2196/2196, authoring-only, not required in xEdit), RADR (5660, radio receiver struct with a Sound Model FormLink; 114 records), CITC and CTDA (5667-5668; CTDA is an array of conditions, 139 records; not in `condition_scopes`), STCP (8 records), NVNM (navmesh blob), DAMC (1 record).
- CONT: CNTO (6194, 4579 items in 476 records), COCT (6193, coupled count) and COED are not listed and are left to the subrecord-groups kind; no rows are added. `src/forms/npc.cpp` has a typed CNTO/COCT handler, but it is NPC_-only and is not reused.
- DOOR: TNAM random teleport destinations (6507, `wbRArrayS`, 87 records, array of FormLinks).
- FURN: the marker arrays ENAM, NAM0 and FNPR (6902-6912), SNAM marker parameters (`wbSNAMMarkerParams`, 5314, array of 24-byte structs), APPR (5275, array of keywords), OBTE, OBTS and STOP (object template), CNTO (67 records, 145 items), CITC and CTDA (1 record each), PRPS (144 records), NVNM (34 records), DSTA/DSTD/DSTF.
- MSTT: PRPS (2 records), DMDL and DMDT.
- TERM: CNTO holotape items (12700, 23 records), COCT as authoring-only (12699), the menu and body arrays (BTXT 12717, ITXT 12729, RNAM 12730, ANAM, ITID, CTDA, CIS1, CIS2, TNAM, UNAM), SNAM marker parameters (12713, 24 bytes, 1073 of 1074 records; this is why SNAM is not listed), and PRPS (9 records). The 4-byte looping sound SNAM (12689) has no vanilla occurrence, so it has no row and is not listed. WBDT (12711, unsized (`wbByteArray(..., 0)`); vanilla holds one byte 00 in 1074/1074) and PNAM (12688, color) are authoring-only, min 0.

## Radicle/tests/test_object_base_records.cpp:EditsOneRowPerRecordAndRereads

Fixture: one record per signature. SNDR records 0x800 and 0x808 and TRNS records 0x809 and 0x80A are the link targets. Each item edits one scalar row, one MODL string and one link row. The SNDR links (ACTI, CONT, DOOR, MSTT, TACT) retarget from 0x800 to 0x808; the FURN and TERM PreviewTransform (PTRN) links retarget from 0x809 to 0x80A. After re-read the test checks the stored bytes directly: scalar bytes against `encode_scalar`, the string against zstr, and the link subrecord data against the raw u32 of the target (`u32_bytes(raw_id(...))`). It does not go through `collect_form_links`. The TACT fixture lists PNAM (4 zero bytes) before SNAM and FNAM (2 zero bytes) after it; the FURN and TERM fixtures put PTRN directly after OBND. `FixtureLinksStartOnTheirFirstTarget` checks the starting bytes first.

## Radicle/tests/test_object_base_records_real.cpp:RowsDecodeAndAuthoringLimitsHold

Over the seven official masters (`FO4_DATA_DIR`): every scalar row's payload size and decode, every FormLink row's payload size, every authoring minimum and maximum, and the authoring order rule (a listed subrecord's authoring index never goes down within a record). The order check looks only at listed rules; unlisted subrecords are skipped. The test checks each record inside the `visit_records` callback, on the record it is given, so it makes no (plugin, key) lookups and needs no sampling. A post-fix re-run of the order check over CONT (635), FURN (783), TERM (1074) and TACT (74) found no violations.

## Radicle/include/fo4/schema/scalar.hpp:SubrecordGroupDescriptor

A repeating subrecord group. `members[0]` is the leader, which must be required and non-repeatable and starts each run; the other members may be required, optional or repeatable. `count_subrecord` names the whole-payload u32 row that stores the run count, and codegen requires it. `successors` lists the subrecords that follow the group in the record, from `build/xedit/wbDefinitionsFO4.pas` @01a11729 (for CONT, the successor list in `fields.json`). `SubrecordGroupMemberDescriptor` carries the signature, `required`, `repeatable`, `payload_size` and the `default_payload` that insert writes. The only source of these descriptors is the generated `k_subrecord_groups` table. `schema_codegen.py` enforces the rules; `field_writer.cpp` relies on them and also checks each record it touches.

Not supported yet:

- Insert creates only the required members. It never creates an optional member such as COED, so a run keeps COED only when it already had one.
- Insert creates the count subrecord only for a record with no count and no runs. A record with runs but no count returns `subrecord_missing`.
- Only `existing_only` insertion is accepted. Runs cannot be reordered, and a repeatable leader is rejected.
- Only CONT is generated. The LVLI leveled-list entries use the same COED shape in vanilla (see `generated_scalar_fields.inc:k_subrecord_groups`) and are not wired.
- This change adds no C API or editor entry point for groups; insert and remove exist only as C++ functions in `mutation.hpp`.
- The group check covers only the `field_writer.cpp` entry points: scalar and FormLink writes through the `scalar_slots` and `formlink_slots` helpers, insert, remove and `subrecord_group_spans`. `formlink_list_read.cpp`, `src/schema/formlink_slots.cpp` and `preflight` do not run it yet.

## Radicle/src/schema/scalar.cpp:subrecord_group_for_member

Returns the first group in the generated table, within the record's own groups, whose members name the subrecord. The codegen rejects a member declared by two groups in one record, so the first match is the only match. `is_subrecord_group_count_subrecord` answers whether a subrecord is a group count in its record, and `write_scalar` consults it to refuse plain writes.

## Radicle/src/plugin/field_writer.cpp:group_spans

Splits a record's subrecords into runs. A run starts at a leader and continues while subrecords are members of the group, stopping at the next leader or the first non-member. It throws `SubrecordGroupError` for a member outside a run (an orphan), a member out of order or repeated when its descriptor is not repeatable, a run missing a required member, and a member whose payload size differs from its descriptor. It does not check the count. Scalar and formlink reads and writes reach it through `scalar_slots` and `formlink_slots` and convert the error to `ScalarError` or `FormLinkFieldError`, so a broken run fails closed for member access. A count that disagrees with the run count can still be read and written at member level, as counted arrays allow; insert and remove refuse it through `group_counter`.

## Radicle/src/plugin/field_writer.cpp:group_counter

Finds the one `count_subrecord` in the record, requires four bytes, and requires its value to equal the number of runs. A duplicate, a wrong size or a mismatch throws `SubrecordGroupError`. Codegen makes the count mandatory, so there is no count-less path. A missing count subrecord returns nullopt. Insert then creates it only when the group has no runs; otherwise insert and remove report `subrecord_missing` without changing the plugin.

## Radicle/src/plugin/field_writer.cpp:subrecord_group_spans

Enumerates the runs as `SubrecordGroupSpan{first, end}`, half-open indexes into `Record::subrecords`. It reports structure only. The count is not compared, so a record whose count disagrees still enumerates; insert and remove are the calls that refuse it.

## Radicle/src/plugin/field_writer.cpp:insert_record_subrecord_group

Inserts run `index`. The new run goes before run `index`, or after the last run's end when `index` equals the run count. An empty group whose count is present takes the run straight after the count. When the count is missing and there are no runs, insert creates a count subrecord of 1 followed by the new run, placed before the first present successor (`first_successor_index`), or at the end when none is present. Each required member is created with zero bytes and then its default bytes are copied in. Optional members are never created. All checks run before the subrecords change. The count is written after the insert, at its index shifted past the new subrecords, so a throw leaves the plugin untouched. An `index` above the run count returns `index_out_of_range`. For CONT, the CNTO default `00000000 01000000` is item 0 with count 1, from xEdit's `wbCNTO` (`build/xedit/wbDefinitionsFO4.pas` @01a11729, lines 3300-3313): the item form ID at offset 0 and `wbInteger('Count', itS32).SetDefaultNativeValue(1)` at offset 4. xEdit creates the count when items go above zero (`build/xedit/wbImplementation.pas` @01a11729, lines 21144-21172), which is why an item-less CONT gets COCT=1 here.

## Radicle/src/plugin/field_writer.cpp:remove_record_subrecord_group

Erases the run's subrecords, its optional and repeated members included, and writes count-1. Removing the last run keeps the count subrecord at zero rather than deleting it, so a later insert does not have to create it.

## Radicle/src/plugin/field_writer.cpp:write_scalar

Refuses a plain write to a group count (CONT `COCT`, field `CONT\ItemTotal`) with `ScalarError` before any byte changes, the same way counted arrays refuse their count. Member rows such as `CONT\ItemCount` go through `scalar_slots`, so a broken run is refused here too.

## Radicle/tools/schema_codegen.py:parse_subrecord_group

Validates one `subrecord_group` row and returns `(group, rows, links)`. Member scalar rows become scalar rows with `repeated: true` on their member subrecord, so they pass through the existing scalar loop and get its alignment, payload and overlap checks. Member FormLink rows come back as links, and `formlink_schema_codegen.load_lists` appends them to the record's FormLink rows. `count_subrecord` is required and must be four printable ASCII characters. `successors` is an optional list of distinct four-character signatures that may not name a member or the count. Rejected: a leader that is optional or repeatable; a member signature repeated or equal to the count subrecord; member keys outside `GROUP_MEMBER_KEYS`; link keys outside `GROUP_LINK_KEYS`, which refuses `payload_sizes` on a link; rows on an optional or repeatable member; a row that does not fit its member, where a whole-payload row must match the member size; a default whose length differs from the payload size; unknown group keys; a path not qualified by the record; and an insertion policy other than `existing_only`.

## Radicle/tools/formlink_schema_codegen.py:load_lists

Appends each group member link to the record's FormLink rows before the usual checks, so a group link gets the same path, offset, target and overlap rules as an ordinary FormLink field. The FormLink/scalar overlap check runs over the expanded group scalars too, so a link and a member scalar in one CNTO payload are checked together.

## Radicle/tools/parity_matrix.py:formlink_rows

Expands group links through `parse_subrecord_group` as `load_lists` does, so the FO4_PARITY FormLink counts and kind table include group links.

## Radicle/include/fo4/schema/generated_scalar_fields.inc:k_subrecord_groups

The CONT `CONT\Items` group. Leader `CNTO` is required, 8 bytes, with default `0000000001000000`. Optional `COED` is 12 bytes with no rows. Count `COCT` is the whole-payload u32 row `CONT\ItemTotal`. Member scalar row: `CONT\ItemCount`, CNTO offset 4, i32, repeated. Member link: `CONT\ItemObject` in `generated_formlink_lists.inc`, CNTO offset 0, targets `sigBaseObjects`, nullable. NULL is item 0, which insert writes, and xEdit's `sigBaseObjects` includes NULL (`build/xedit/wbDefinitionsFO4.pas` @01a11729, line 75) and CNTO uses it (line 3303). `successors` is DEST, DAMC, DSTD, DSTA, DMDL, DMDC, DMDT, DMDS, DSTF, DATA, KSIZ, KWDA, FTYP, NTRM, PRPS, SNAM, QNAM, TNAM, ONAM.

xEdit citations are to `build/xedit/wbDefinitionsFO4.pas` @01a11729. `wbCOED` is at 3290 with the owner form ID at offset 0, a global-or-rank union at 4 and a float at 8, so 12 bytes. `wbCNTO` is at 3300-3313, `wbCOCT` at 3315 and `wbCNTOs` at 3316, which sets `SetCountPath(COCT)`. The CONT record at 6176 lists `wbCOCT` before `wbCNTOs`, then `wbDEST` and `DATA`. COED sits in the same repeated struct as CNTO, so a run is CNTO plus its optional COED.

Vanilla survey, read-only over the seven local masters under `D:/SteamLibrary/steamapps/common/Fallout 4/Data`: 635 CONT records, 476 with items. In those 476, COCT is the subrecord directly before the first CNTO, is four bytes, and equals the CNTO count in every record (476 of 476). The other 159 have no COCT and no items, so insert gives them COCT=1 with the new run before the first successor. All 4579 CNTO subrecords are 8 bytes. No CONT record has a COED (0 of 4579 items), no record has more than one COCT, and no non-member subrecord sits inside an item run. Item targets resolve to ALCH 322, LVLI 1558, MISC 1521, ARMO 501, KEYM 31, BOOK 314, WEAP 151, NOTE 168 and AMMO 13, all in `sigBaseObjects` (4579 of 4579). COED has no CONT sample. The only vanilla COED is in LVLI leveled-list entries: 66 subrecords, all 12 bytes, where xEdit's LVLI array is at `build/xedit/wbDefinitionsFO4.pas` lines 10021-10027 with `wbCOED` at line 10024. The CONT COED member therefore rests on the xEdit layout and the LVLI samples, not on CONT data.

## Radicle/include/fo4/schema/generated_formlink_lists.inc:k_generated_sig_base_objects

The 44 signatures of xEdit's `sigBaseObjects` union (`build/xedit/wbDefinitionsFO4.pas` @01a11729, lines 75-82). ENCH was added last, at line 82, so `is_sig_base_object(ENCH)` is true, as `tests/test_kwda_formlink_lists.cpp` expects. Leveled-list entries do not read this table: `src/forms/leveled_list.cpp:k_lvli_targets` keeps its own list, which already had ENCH.

## Radicle/tests/test_subrecord_group.cpp:SubrecordGroup

The fixtures build a CONT record and use the generated group, so the vanilla shape is tested directly. A hand-built descriptor (`custom_group`) covers the leader, required, repeatable, order and payload rules, which the generated table does not express. The remap test uses the same three-master header as `FormLink.CntoRemapsItemLeavesCount` in `test_formlinks.cpp` and leaves COED out, so it checks only the two CNTO item IDs; each one must move to master index 1 exactly once.

## Radicle/include/fo4/plugin/mutation.hpp:SubrecordGroupSpan

`SubrecordGroupSpan{first, end}` is the half-open range of one run in `Record::subrecords`. `SubrecordGroupMutationStatus` mirrors `CountedArrayMutationStatus` with `updated`, `record_missing`, `index_out_of_range` and `subrecord_missing`. Structural failures are `SubrecordGroupError` throws instead of statuses.

## Radicle/src/schema/form_link_schema.cpp:schema_formlink_offsets

Schema FormLink rows (`k_generated_formlink_fields`) that the global table cannot reach, such as WEAP DNAM with 132-byte payloads, are resolved here with `resolve_field_slots` and returned as byte offsets per subrecord. The global table applies only to 4-byte payloads (see the `is_listed_formid_signature` checks in `src/schema/form_links.cpp`). A slot is kept only when it is valid, its payload size matches the row's `payload_sizes` (any size when `payload_sizes[0]` is zero), and the subrecord is not deliberately non-form ID. Slots on subrecords that `struct_payload_formid_offsets` or `mixed_formid_offsets` own (EXPL DATA, PROJ DNAM and HAZD DNAM among them) are dropped, so those handlers keep their malformed-size rejection. INFO returns nothing because `visit_info_link_offsets` in `src/schema/form_links.cpp` owns that record. NAVI NVMI slots are valid only when the navmesh mixed-offset parse succeeds and NVER is 4 bytes. PACK and IDLE rows also pass the behavior payload rules inside `resolve_field_slots` in `src/schema/field_address.cpp`. Callers compute this once per record, so the row scan is not repeated for each subrecord.

## Radicle/src/schema/form_link_schema.cpp:deliberate_non_formid_subrecord

Excludes subrecords that are deliberately not form IDs from the schema path: any `XXXX` subrecord, RACE `SRAF`, the QUST signatures in `qust_non_formid_signature`, and the corpus rules in `corpus_non_formid_slot` (PACK `CNAM` after `ANAM`, SCEN action scalars, and the static pair list). Without this predicate the schema path would revive links that the existing visitors reject on purpose.

## Radicle/src/schema/form_link_schema.cpp:qust_non_formid_signature

Signatures that QUST reuses for scalar or string data although they are form ID signatures in other record types: DNAM (General quest data), NAM2 (stage note), CNAM (stage log-entry text), FNAM (objective/alias flags), ANAM (next alias ID), NNAM (objective text or quest description), and SNAM (SWF filename). They must never reach the global signature-only form ID table. The list is unchanged from the former `qust_non_formid_signature` in `src/schema/form_links.cpp`.

## Radicle/src/schema/form_links.cpp:visit_formlink_offsets

Wraps `visit_link_offsets` and merges in the schema offsets for the same subrecord. Subrecords with no schema offsets skip the buffer and go straight to `visit_link_offsets`. Otherwise the union is sorted and deduplicated before the callback runs, so a link that both the global table and a schema row list, such as FACT VEND with a 4-byte payload, is visited once (`tests/test_form_link_schema_remap.cpp:LinkAlsoListedByGlobalTableIsVisitedOnce`). Strict mode passes through unchanged. Sorting can reorder output from a handler that did not emit ascending offsets; the remap and rekey callbacks do not depend on order.

## Radicle/src/plugin/subrecord_authoring.cpp:editable — coupled count subrecords

Count subrecords are marked `coupled` in `schema/fallout4/fields.json` (NPC_ SPCT, DEST, PRKZ, COCT, KSIZ, KWDA, OBTE, CS2H; LVLI and LVLN LLCT; FACT CITC; LIGH KSIZ and KWDA). Older records mark KSIZ and KWDA together and DEST alone, and `editable` reads the flag, so an unmarked count with minimum 0 could be removed while its entries stay. The entries the counts describe (PRKR, CNTO, LVLO, CTDA, SPLO, OBTF, OBTS, CS2K) are repeated without `uncounted`, so `editable` already refuses them.

LVSP LLCT (minimum 1) and SMBN, SMEN and SMQN CITC (minimum 1) are not coupled. Removal already stops at the minimum and add stops at the maximum. Coupling them would make `create_with_rules` refuse those records, because it requires every minimum-1 rule to be editable, and `tests/test_c_api_authoring.cpp` creates an LVSP through that path.

## Radicle/schema/fallout4/fields.json:NPC_ — NAM7 minimum 0

NPC_ NAM7 has minimum 0 (it was 1). xEdit marks it required (`Core/wbDefinitionsFO4.pas:10424`, `wbFloat(NAM7, 'Unused', cpNormal, True)`), but the audit count found it on none of the 3015 vanilla NPC_ records, so the minimum follows the corpus. This is a deliberate deviation from the xEdit flag.

## Radicle/src/graph/check_errors.cpp:check_links — DLBR SNAM with a missing target

`check_links` skips a DLBR `SNAM` whose target is in no loaded plugin. Before this, that link also got an `unresolved_link` finding, a warning because the DLBR `SNAM` slot has no schema proof, next to the `branch_starting_topic_not_dial` error from `check_dialogue_links`. The dialogue rule is kept because it names the requirement (a loaded DIAL). Deleted targets still come from `check_links`. Test: `tests/test_check_errors.cpp:MissingBranchStartingTopicIsReportedOnce`.

## Radicle/src/schema/form_link_mixed.cpp:npc_formid_offsets — NPC_ SNAM and PRKR

NPC_ `SNAM` (faction) is 5 bytes: form ID u32 at 0, rank s8 at 4. NPC_ `PRKR` (perk) is also 5 bytes: form ID u32 at 0, rank u8 at 4. Both layouts are exact, so any other size throws under strict remap and is skipped otherwise. The sizes match `encode_factions` and `encode_perks` in `src/forms/npc.cpp`, which write 5 bytes, and `decode_npc_factions` and `decode_npc_perks`, which require 5. xEdit defines the perk struct at `Core/wbDefinitionsFO4.pas:10386-10388`. Before this layout, the generic `SNAM` entry in `formid_subrecords.inc` remapped only 4-byte payloads, and `PRKR` was in no remap table, so the perk form ID was never remapped. Tests: `tests/test_packed_formlink_arrays.cpp:NpcFactionSnamRemapsFormIdAndKeepsRank` and `NpcPerkPrkrRemapsFormIdAndKeepsRank`.

## Radicle/src/schema/form_link_mixed.cpp:leveled_list_formid_offsets — LVLI and LVLN LLKC

LLKC is an uncounted array of 8-byte structs: keyword form ID u32 at 0 and a u32 chance at 4 (`Core/wbDefinitionsFO4.pas:9979-9984`, `wbArrayS` with `wbStructSK([0], ...)`). The layout is non-exact, `size % 8 == 0`, like `XPOD` in `k_placed_reference_layouts`. Reason: the shipped byte check found 8, 16 and 24 byte payloads, and xEdit accepts any element count. The chance is not remapped. Tests: `tests/test_packed_formlink_arrays.cpp:LeveledListLlkcKeywordsRemapAndKeepChances` and `LeveledListLlkcWrongSizeFailsClosedUnderStrictRemap`.

## Radicle/src/forms/npc.cpp:require_local_target — typed NPC_ targets

`add_npc_faction` and `set_npc_faction` require a local SNAM target to be FACT. `add_npc_perk` and `set_npc_perk` require a local PRKR target to be PERK. A local key that does not resolve throws `NpcError`. Foreign keys are not resolved here and are not checked, the same rule as `validate_local_entry_targets` in `src/forms/leveled_spell_creation.cpp` and `validate_entry_target` in `src/forms/leveled_list.cpp`. xEdit uses `wbFormIDCk('Faction', [FACT])` for SNAM and `wbFormIDCk('Perk', [PERK])` for PRKR (`Core/wbDefinitionsFO4.pas:10387`). Test: `tests/test_npc_lists.cpp:LocalFactionAndPerkTargetsMustHaveTheirSignature`.

## Radicle/src/forms/faction.cpp:require_relation_target — FACT XNAM targets

`add_faction_relation` and `set_faction_relation` require a local XNAM faction to be FACT or RACE, before the plugin is copied. A local key that does not resolve throws `FactionError`. Foreign keys are not checked, as in `require_local_target` (`src/forms/npc.cpp`). xEdit defines the relation struct with `wbFormIDCkNoReach('Faction', [FACT, RACE])` (`Core/wbDefinitionsCommon.pas:8818-8819`). Test: `tests/test_faction.cpp:RelationTargetOfThisPluginMustBeFactOrRace`.

## Radicle/src/forms/leveled_list.cpp:set_leveled_list_settings — LVLF bits

LVLF flags may use only bits 0x01, 0x02 and 0x04 (mask 0x07). `set_leveled_list_settings` applies the same check as `create_leveled_list`, before any read or write, so a rejected call leaves the plugin unchanged. Test: `tests/test_leveled_list.cpp:SettingsRejectFlagBitsOutsideTheDefinedMask`.

## Radicle/src/schema/form_link_mixed.cpp:k_weapon_layouts — WEAP embedded form ID slots

WEAP DNAM (132 bytes), FNAM (41) and CRDT (12) are fixed-size structs with no version condition in xEdit (`Core/wbDefinitionsFO4.pas` WEAP record, lines 12877-13032; the DNAM struct is at 12911). Slots: DNAM Ammo @0, Skill @40, Resist @44, eight Sound slots @73 to @101 (lines 12912, 12922, 12923, 12978-12985); FNAM Override Projectile @29 (line 13004); CRDT Crit Effect @8 (line 13016). Sizes are exact, so a payload of another size fails closed under strict remap. The layouts are interpreted by `fixed_stride_formid_offsets`, like the FACT XNAM and NPC_ SNAM layouts.

## Radicle/src/schema/form_link_mixed.cpp:faction_plvd_formid_offsets — FACT PLVD location slot

xEdit `wbPLVD` (`Core/wbDefinitionsFO4.pas` lines 4200-4221) is a struct whose first field is a s32 Type that selects the union member at @4. Types 0 (Reference), 1 (Cell), 4 (Object ID) and 6 (Keyword) hold a form ID; the others (2 and 3 byte arrays, 5 Object Type, 7 unused, 8 to 14 aliases and targets) do not. Collection Index is optional from member 3 (`SetOptionalFrom(3)`), so the payload is 12 or 16 bytes. `Radicle/src/graph/reachability.cpp` checks PLVD types 0, 1 and 4 only; this remap includes type 6 as xEdit lists it. The PACK PLDT decider in `Radicle/src/schema/behavior_layout.cpp` uses the same four types as this function.

## Radicle/src/forms/leveled_list.cpp:zeroes_chance_none — LVLO chance on form versions below 69

`decode_leveled_list` reports entry chance none as 0 and `replace_leveled_list_entries` writes 0 for LVLI and LVLN records whose `header.form_version` is below 69. xEdit's `wbLLEAfterLoad` zeroes the entries' Chance None under the same condition (`Core/wbDefinitionsFO4.pas:2215-2250`), attached to LVLN (10007) and LVLI (10031) only. LVSP has no after-load (10044-10049), so LVSP chance is decoded as stored. Unedited records are not rewritten. Tests: `tests/test_leveled_list.cpp:LegacyFormVersionReportsZeroChanceNoneForLvliAndLvln`, `LegacyFormVersionEditsWriteZeroChanceNoneAndNoOpStaysIdentical`.

## Radicle/src/forms/leveled_list.cpp:sorted_entries — LVLO order for typed writes

Typed writes store LVLO entries in xEdit's order. The entry struct's sort key is Level, then the reference, then Count (`Core/wbDefinitionsCommon.pas:8788-8802`, the extended key from `wbStructExSK([0, 2], [3])`). The reference is compared as the raw form ID in this plugin's context, which matches load order for the plugin's masters. Keys are compared as fixed-width hex strings, so the order is numeric. `Core/wbImplementation.pas:2151-2175` (`CompareSortKeys`) and `:20916` (`TwbSubRecordArray.DoInit`) sort the array. `std::stable_sort` keeps the caller's order for equal keys. The sort runs in `replace_leveled_list_entries`, so insert, set and remove all write sorted lists. A list that is already sorted is still a no-op when the caller's entries equal the decoded entries.

Index meaning: `insert_leveled_list_entry` and `set_leveled_list_entry` take an index into the list as decoded before the call. The new entry lands at that index first, and then the list is sorted, so its final index is set by its key. Among equal keys, the index decides the order. Tests: `tests/test_leveled_list.cpp:EntriesSortByLevelThenReferenceAfterTypedEdits`, `EqualSortKeysKeepTheirOrderAndSetResortsTheEdit`.

## Radicle/src/forms/leveled_list.cpp:replace_leveled_list_entries — LVSP COED refusal

An LVSP entry with `extra` is refused before any write. xEdit's LVSP entry is LVLO only (`Core/wbDefinitionsFO4.pas:10044-10049`). A COED-carrying LVSP entry from a file is still decoded, and any write that carries it is refused. Test: `tests/test_leveled_list.cpp:SpellListEntriesRefuseExtraData`.

## Radicle/src/forms/leveled_list.cpp:zstring — editor ID NUL

`create_leveled_list` refuses an editor ID with an embedded NUL before the plugin is copied. A NUL would write an early terminator into the zstring. The same local check is in `Radicle/src/forms/cell.cpp:zstring`. The LVSP creator takes no editor ID, so it has no check to match. Test: `tests/test_leveled_list_creation.cpp:RefusesEmbeddedNulInEditorId`.

## Radicle/src/forms/faction.cpp:zstring — FACT editor ID and full name NUL

`create_faction` refuses an editor ID or full name with an embedded NUL. The local check matches the one in `Radicle/src/forms/leveled_list.cpp:zstring`. Test: `tests/test_faction_creation.cpp:RefusesEmbeddedNulInEditorIdOrFullName`.

## Radicle/src/forms/npc.cpp:k_order — NPC_ run boundaries

`insertion_index` places a missing run after the last known subrecord that orders before the anchor. An unknown subrecord between two known ones can therefore be split off. The order table lists each group's members in xEdit's `NPC_` definition order: `wbDEST` (`Core/wbDefinitionsFO4.pas:4275-4300`: DEST, DAMC, DSTD, DSTA, DMDL, DMDT, DMDC, DMDS, DSTF), `wbAttackData` (`:4073-4121`: ATKD, ATKE, ATKW, ATKS, ATKT), and the SPCT count before SPLO (`:3283-3285`). Without the full groups, a new PRKZ or COCT run landed between DEST and DSTD, or between ATKD and ATKE. Test: `tests/test_npc_lists.cpp:InsertedRunsStayOutsideTheDestAndAttackGroups`.

## Radicle/src/forms/npc.cpp:require_local_base_object — CNTO item targets

`add_npc_inventory_item` and `set_npc_inventory_item` require a local CNTO item to be a base object, using `schema::is_sig_base_object` (the generated xEdit `sigBaseObjects` list, `Core/wbDefinitionsFO4.pas:74`). Foreign keys are not checked, as in `require_local_target`. Test: `tests/test_npc_lists.cpp:InventoryItemsMustBeBaseObjectsAndCoedOwnersNpcOrFact`.

## Radicle/src/forms/npc.cpp:require_local_coed_owner — COED owner targets

A local COED owner must be NPC_ or FACT, as xEdit's `wbCOED` owner (`Core/wbDefinitionsFO4.pas:3291`, `[NPC_, FACT, NULL]`). A null owner is allowed. Foreign keys are not checked. Test: `tests/test_npc_lists.cpp:InventoryItemsMustBeBaseObjectsAndCoedOwnersNpcOrFact`.

## Radicle/src/forms/npc.cpp:encode_perks — count kept at zero

`encode_perks` and `encode_inventory` always write PRKZ or COCT, including 0 when the last entry is removed. xEdit's `TwbSubRecordArray.BeforeActualRemove` sets the count element to 0 and keeps it (`Core/wbImplementation.pas:20774-20797`). Test: `tests/test_npc_lists.cpp:RemovingTheLastEntryKeepsItsCountSubrecordAtZero`.

## Radicle/src/forms/npc.cpp:replace_run — NPC_ SNAM, PRKR and CNTO keep append order

`replace_run` writes the caller's order and does not sort. This deviates from xEdit, which declares the CNTO array with `wbRArrayS` (`Core/wbDefinitionsFO4.pas:3316`). The reason is shipped-corpus evidence: vanilla Fallout4.esm stores 1,011 SNAM, 1,468 CNTO and 127 XNAM lists unsorted, so a sorted write would reorder records that Bethesda stored in another order. LVLO is sorted (see `sorted_entries`).

## Radicle/src/forms/faction.cpp:add_faction_relation — FACT XNAM append order

`add_faction_relation` inserts after the last XNAM and does not sort. The reason is the same shipped-corpus evidence as `replace_run`: a byte scan of the shipped Fallout4.esm found 127 unsorted XNAM lists in Fallout4.esm.

## Radicle/src/forms/faction.cpp:require_editable — undecodable refusal before index checks (GT-276)

The index-based FACT, NPC_ and LVLI/LVLN edits (`set_faction_relation`, `remove_faction_rank`, `set_npc_perk`, `remove_leveled_list_entry` and the other set/remove entry points) call `require_editable` before any index check or decode. `Radicle/src/forms/faction.cpp:require_editable`, `Radicle/src/forms/npc.cpp:require_editable` and `Radicle/src/forms/leveled_list.cpp:require_editable` call `require_decodable` on the target record. The reason: an undecodable record has empty `subrecords`, so the decoders return empty lists and the old check reported "index is out of range" instead of the undecodable refusal.

The whole-record write points also refuse, so add, replace and settings edits do not write into an undecodable record: `Radicle/src/forms/faction.cpp:mutable_faction`, `Radicle/src/forms/npc.cpp:replace_run`, and `Radicle/src/forms/leveled_list.cpp:replace_leveled_list_entries`, `set_leveled_list_settings` and `reset_leveled_list`. Before this change an empty `replace_leveled_list_entries` on an undecodable LVLN returned true without touching the record, because both sides decoded to no entries. The `decode_*` readers are unchanged, so reads of an undecodable record behave as before. Tests: `Radicle/tests/test_undecodable_edit.cpp:FactionIndexedEditsRefuseUndecodableRecord`, `NpcIndexedEditsRefuseUndecodableRecord`, `LeveledListIndexedEditsRefuseUndecodableRecord` and `LeveledListWholeEditsRefuseUndecodableRecord`.

## src/schema/form_link_mixed.cpp:mixed_formid_offsets (struct payloads, #602 WP-2)

Each rule below was checked against the xEdit source at pin 93cc0bc5 before coding. Strict remap throws `FormIdError` on a malformed size; lenient remap returns no offsets. Tests are in `Radicle/tests/test_struct_payload_remap.cpp`.

The xEdit lines were re-checked by a second reader, and a scan of the seven official masters found no payload these rules reject, with every non-null form ID at the new slots resolving to the type xEdit expects. Rules are listed by table and function, not by offset.

Packed form ID arrays (`packed_formid_array_offsets`), test `StructPayloadRemap.PackedArraysRemapAndKeepNeighbors`:
- OTFT INAM, `Core/wbDefinitionsFO4.pas` line 9359.
- COLL CNAM, `Core/wbDefinitionsFO4.pas` line 9559.
- RACE HCLF, `Core/wbDefinitionsFO4.pas` line 11205.
- CELL XCLR, `Core/wbDefinitionsFO4.pas` line 6046.
- COBJ FNAM, `Core/wbDefinitionsFO4.pas` line 10279.

Fixed-stride rows (`k_record_stride_layouts`), tests in the same file:
- KSSM RNAM, `Core/wbDefinitionsFO4.pas` lines 12394-12397. Test `StructPayloadRemap.SoundAndCategoryStructsRemapAndKeepScalars`.
- MGEF SNDD, `Core/wbDefinitionsCommon.pas` lines 8896-8912, used by MGEF at `Core/wbDefinitionsFO4.pas` line 10207. Same test.
- SCSN CNAM, `Core/wbDefinitionsFO4.pas` lines 12653-12656. Same test.
- COBJ FVPA, `Core/wbDefinitionsFO4.pas` lines 10251-10264. Test `StructPayloadRemap.ComponentPairsRemapAndKeepCounts`.
- MISC CVPA, `Core/wbDefinitionsFO4.pas` lines 10235-10238. Same test.
- FURN and TERM SNAM, `Core/wbDefinitionsFO4.pas` lines 5314-5332, used at lines 6915 (FURN) and 12713 (TERM). Test `StructPayloadRemap.MarkerParamsKeywordRemapsByFormVersion`. xEdit marks the marker keyword optional (`SetOptionalFrom(4)`), no vanilla payload omits it, and a mod payload whose last element lacks it is not a multiple of the stride, so strict remap refuses it.
- ALCH ENIT, `Core/wbDefinitionsFO4.pas`: Addiction is line 5733 and Consume Sound line 5735. Test `StructPayloadRemap.SingleSlotsAndAlchEnitRemapAndKeepScalars`. Offset 16 is already a `fields.json` row, and `form_link_schema.cpp:schema_formlink_offsets` drops schema rows for an owned subrecord, so the rule lists both. ENCH ENIT (line 6730) and INGR ENIT (line 9806) are not in this rule.

Fixed single slots (`structured_formids.inc`), same test set:
- ACTI RADR, `Core/wbDefinitionsFO4.pas` lines 5660-5666.
- CELL XILW, `Core/wbDefinitionsFO4.pas` line 6071.
- REFR and ACHR XTEL, `Core/wbDefinitionsFO4.pas` lines 11550-11561.

Strict mode: `StructPayloadRemap.WrongStrideSizeFailsClosedWithoutMutation`.
## Radicle/src/forms/placed_reference_edit.cpp:set_placed_reference_record_flags — Deleted bit kept

`set_placed_reference_record_flags` keeps the record's compressed, persistent and Deleted bits and takes the other bits from the caller. Deleted is never set or cleared here, because deleting needs the content stripped (xEdit `SetDeleted` in `Core/wbImplementation.pas`). Reason: #602 D-6. Test: `Radicle/tests/test_environment_ref_fields.cpp:RecordFlagsNeverToggleDeleted`.

## src/schema/form_links.cpp:is_listed_formid_signature (generator gaps, #602 WP-3)

Each row below was read in xEdit at pin 93cc0bc5 (`Core/wbDefinitionsFO4.pas` and `Core/wbDefinitionsCommon.pas`) before coding, and every definition of the signature in both files was checked. Tests are in `Radicle/tests/test_generator_gap_remap.cpp` (`GeneratorGapRemap.*`). The static deny pass (`non_formid_slots.py --check-static`) reports no scalar pair for any added signature, so `XEDIT_DENIED` is unchanged.

Global signatures (`formid_subrecords.inc`, 4-byte payload, any record):
- EITM: the only definition is `wbEnchantment` (`wbDefinitionsCommon.pas:8486-8489`, form ID when not TES4). Used by ARMO (`wbDefinitionsFO4.pas:5798`), EXPL (:7339) and WEAP (:12891).
- CS2K and CS2D: NPC_ sound pairs in `wbActorSounds` (`wbDefinitionsCommon.pas:8399-8405`), used at `wbDefinitionsFO4.pas:10434`.
- PLYR: the PLYR record's own subrecord, `wbFormID(PLYR, 'Player')` (`wbDefinitionsFO4.pas:12131`).
- HEAD: `wbHeadPart` (`wbDefinitionsCommon.pas:8583`), used only for RACE head parts (`wbDefinitionsFO4.pas:11274` and :11292). No other record defines HEAD.
- MDOB: `wbMDOB` (`wbDefinitionsCommon.pas:8877`), used only by MGEF (`wbDefinitionsFO4.pas:10056`).

Keyword array (`is_formid_array_signature`):
- APPR: `wbArrayS(APPR, ..., wbFormIDCk('Keyword', [KYWD]))` (`wbDefinitionsFO4.pas:5275`), used by ARMO (:5841), FURN (:6916), NPC_ (:10404), RACE (:11068) and WEAP (:12901). Every element is a keyword form ID.

Record-qualified (`packed_formid_array_offsets`, not global, because MODL and INDX are not form IDs in other records):
- ARMO MODL: the Armor Addon form ID inside the Models struct (`wbDefinitionsFO4.pas:5823-5825`, after INDX).
- ARMA MODL: one RACE form ID per repeated subrecord (`wbDefinitionsFO4.pas:5885`).
- MODL is a model path string through `wbGenericModel` (`wbDefinitionsFO4.pas:1034`). ARMO and ARMA do not use that helper: its users between lines 5786 and 5890 are none, and ANIO (:5782) and BOOK (:5897) sit outside that range.
- NPC_ TPTA: 13 form IDs, 52 bytes (`wbDefinitionsFO4.pas:10353-10366`, entries from `wbNPCTemplateActorEntry`, `wbDefinitionsCommon.pas:8274-8277`).
- OVIS INDX: `wbFormIDCk(INDX, 'Object', [STAT])` (`wbDefinitionsFO4.pas:12550`). INDX is an integer in NOCM (:12462) and in other records.

Structured slot (`structured_formids.inc`, any record, payload at least 12 bytes):
- ATKD attack spell at offset 8: `wbAttackData` (`wbDefinitionsFO4.pas:4073-4119`), two floats, then the Attack Spell form ID, then attack flags. The struct is 44 bytes by its member sizes. Used by NPC_ (:10377) and RACE (:11211). ATKD is defined only in `wbDefinitionsFO4.pas`.

Schema formlink rows (`schema/fallout4/fields.json`) hold none of these signatures as remap fields; they appear there only as authoring rows. `visit_formlink_offsets` also dedupes any overlap, so no offset is visited twice.

## src/schema/form_link_mixed.cpp:mixed_formid_offsets (unions and deciders, #602 WP-4)

Each rule below was checked against the xEdit source at pin 93cc0bc5 (`Core/wbDefinitionsFO4.pas`) before coding. Strict remap throws `FormIdError` on a malformed size; lenient remap returns no offsets. Tests are in `Radicle/tests/test_union_payload_remap.cpp` (`UnionPayloadRemap.*`).

Decider unions (`book_teaches_formid_offsets`, `omod_data_formid_offsets`):
- BOOK DNAM (`book_teaches_formid_offsets`): 13 bytes. Flags u8 at 0 decides the u32 at +1 (`wbBOOKTeachesDecider`, `wbDefinitionsFO4.pas:1618-1636`; union `Teaches` at :5918-5923). Flags bit 0x01 (Advance Actor Value) is an AVIF, 0x04 (Add Spell) a SPEL, 0x10 (Add Perk) a PERK; with none of those the value is unused and not remapped. Test `UnionPayloadRemap.BookTeachesRemapsOnlyWhenFlagsSayFormId`.
- OMOD DATA (`omod_data_formid_offsets`, layout in `omod_data_layout_offsets`): `wbDefinitionsFO4.pas:12510-12537`. Header: Include Count u32 at 0, Property Count u32 at 4, Form Type u32 at 10 (an enum, never remapped), Attach Point form ID at 16. Then a u32 count and 4-byte KYWD slots (Attach Parent Slots), a u32 count and 8-byte items (no FormIDs), I rows of 7 bytes (Mod form ID at row +0), and P rows of 24 bytes. Property Value 1 at row +12 is a form ID when the value type byte (row +0) is 4 or 6 (`wbOMODDataPropertyValue1Decider`, `:2382-2406`). The total must equal the sum of the parts, otherwise the payload fails closed. Test `UnionPayloadRemap.OmodDataRemapsAttachSlotsIncludesAndFormValueTypes`.

Property rows on OBTS (`obts_formid_offsets`): the same 24-byte rows follow the includes. Value 1 at row +12 is remapped under the same value-type rule. Property union at `wbDefinitionsFO4.pas:5479`, `wbObjectModProperties` at :5458. Test `UnionPayloadRemap.ObtsPropertyValueRemapsOnlyFormIdValueTypes`.

MGEF DATA (`mgef_assoc_item_formid_offsets`, called from `form_links.cpp:visit_link_offsets`): the Assoc. Item union at DATA +8 is a form ID when the Archetype u32 at DATA +64 (`:10124`) is 12, 17, 18, 25, 34, 35, 36, 39, 40, 45 or 46 (`wbMGEFAssocItemDecider`, `:1367-1389`; Assoc. Item union at :10096-10110). The rule applies only to 152-byte payloads, the size the schema rows require, and other sizes are left unchanged. It is non-owning on purpose: `mixed_formid_offsets` returns nothing for MGEF DATA, so `form_link_schema.cpp:schema_formlink_offsets` keeps its 18 DATA rows (offsets 16 to 136). `visit_formlink_offsets` merges the extra offset with those rows. Test `UnionPayloadRemap.MgefAssocItemFollowsArchetypeAndKeepsSchemaRows`.

Form-version gates (`k_record_stride_layouts`):
- DMGT DNAM at form version 78 or later: an {AVIF, SPEL} 8-byte pair per entry, slots 0 and 4 (`wbDefinitionsFO4.pas:12313-12320`). Below 78 the payload is a u32 index array, and an empty 4-byte row claims it so the global 4-byte `formid_subrecords.inc` table does not remap a single index. Test `UnionPayloadRemap.DamageTypeAndRaceBipedRemapByFormVersion`.
- RACE RBPC at form version 78 or later: one AVIF form ID per 4-byte entry, slot 0 (`wbDefinitionsFO4.pas:11011-11022`). Below 78 the entries are integers and not remapped. Same test.
- EFSH DNAM (`wbFormVersionDecider(106)`, `wbDefinitionsFO4.pas:6519-6722`): at form version 106 or later the flat Data arm is 157 bytes and its Ambient Sound SNDR is at 108 (members counted from :6652-6722; the same 157-byte size is in the Radicle.md EFSH note). Below 106 the old-format arm is 395 bytes, with Addon Models DEBR at 241 (:6582) and Ambient Sound SNDR at 305 (:6598). Both sizes match the corpus counts in the EFSH note above. Test `UnionPayloadRemap.EfshAmbientSoundFollowsFormVersionLayout`.
- Malformed size on any of the above fails closed in strict mode. Test `UnionPayloadRemap.MalformedUnionPayloadsFailClosedWithoutMutation`.
