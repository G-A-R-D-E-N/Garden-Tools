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
data only through `FO4_DATA_DIR` and runs only when it is set. CI sets it from
`FO4_DATA_DIR_WINDOWS` in the Windows Release pass of `native-ci`; the Windows Debug
pass clears it. The Linux job passes `FO4_DATA_DIR_LINUX`, which is currently unset,
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

## src/plugin/reader.cpp:inflate_record_payload

Only zlib framing (`MAX_WBITS`) is accepted; the raw-deflate retry is removed after the verified OG inflater record summarized in `docs/COMPRESSION_EVIDENCE.md` established zlib framing. The size guards match PR #462 (GT-138): the declared size is refused above `compressed.size() * 1032 + 64` and above `k_max_record_uncompressed_bytes` (1 GiB) before the single output allocation. `docs/COMPRESSION_EVIDENCE.md` states the source and remaining version/length limits. `tests/test_compression.cpp:RejectsValidRawDeflateAndGzipFraming` and `RejectsBadZlibChecksum` cover framing refusal; `tests/test_reader_limits.cpp:ReaderLimits.CompressedUncompressedSizeBeyondZlibBoundIsRefusedBeforeAllocation` and `tests/test_compression.cpp:Compression.OversizedDeclaredSizeIsRefusedBeforeAllocating` cover the guards. The existing zero-size early return and produced-length behavior remain outside this change.

Current behavior: a decompression failure, or a size-cap refusal, throws and fails the whole plugin read. xEdit instead logs `<Error decompressing ...>` and loads that one record with no subrecords: `TwbMainRecord.DecompressIfNeeded` wraps the inflate in a `try` (source:xEdit Core/wbImplementation.pas:10277-10291 @93cc0bc5), and its `except` handler sets the data pointers to nil (source:xEdit Core/wbImplementation.pas:10300-10305 @93cc0bc5). The 1 GiB and `1032x+64` caps are Radicle limits with no xEdit equivalent; the official masters peak at 554,674 bytes and a 75.6x ratio. All 76,895 compressed records in the survey above decompress, so vanilla loads are unaffected either way.

Owner decision (2026-10-11), not yet implemented: one undecodable compressed record must not fail the plugin. Such a record keeps its original compressed bytes, is written back byte-identically on save, exposes no subrecords, reports as undecodable, and any edit to it is refused with a clear error. A record over either cap is handled the same way. The writer already emits a record's original bytes verbatim (`src/plugin/writer.cpp:write_record`), so the save path needs no change. Edit refusal does: every mutable access path must check the flag, and plugin-wide rewrites must refuse while such a record exists. `src/plugin/writer.cpp:remap_plugin_masters` calls `clear_original_bytes`, which drops the original bytes that would preserve the record, so that record would be written with no subrecords.

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

## src/forms/placed_reference_copy.cpp:copy_info_as_new

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

## src/graph/check_errors.cpp:slot_targets

Resolves the allowed target signatures for a `(record, subrecord, offset)` slot. Generated FormLink fields are tried first through `src/schema/formlink_slots.cpp:formlink_field_at`. An unguarded field matches by its static offset (or stride), as before. A guarded field (variant, `FieldAddress::when`) matches only when `src/schema/field_address.cpp:resolve_field_slots` reports a valid slot at that offset, so the selector byte decides which variant is in force. When the selected variant is not a FormLink, or the selector is absent, no field matches and the slot stays unproven (`unresolved_link` is then only a warning). Generated FormLink lists are tried after fields, unchanged.

Every entry in `include/fo4/schema/generated_formlink_lists.inc` is `FieldAddress{}`, so no real row is guarded yet (GT-018). The variant branch is covered only by `CheckErrors.VariantFormLinkSlotIsProvenOnlyForSelectedVariant` in `tests/test_check_errors.cpp`, which uses a synthetic guarded copy of `EXPL\Light`. `check_load_order` itself still reads the generated tables, so an end-to-end wrong-signature variant error has no test until a guarded row exists.

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

`formid_subrecords.inc` is a signature-only union of xEdit's FO4 form ID signatures, so a signature that is a form ID in one record type was treated as one everywhere (DIAL `PNAM` priority float, DIAL `SNAM` subtype code, SNDR `CNAM`, PACK `CNAM`, NPC_ `NAM4`, ...). #365: `remap_plugin_masters` rewrote those bytes whenever the master list changed. The deny list (`include/fo4/schema/non_formid_slots.inc`) is generated by `tools/corpus/non_formid_slots.py` from two sources that must agree for every corpus row: an official master stores a non-null value at that (record, subrecord) that resolves to no record in the official load order (its master index is past the file's MAST table, or the target does not exist), AND xEdit `wbDefinitionsFO4.pas` (`dev-4.1.6`) declares that subrecord in that record only as scalar data (`wbFloat`/`wbInteger`, the string types including `wbLStringKC` localized string IDs, the RGBA and color helpers, a struct with no `wbFormID`, or a helper whose whole right-hand side, recursively, has no `wbFormID`/`wbUnion`; `wbPTRN := wbFormIDCk(...)` and the `wbVMAD*` helpers are therefore form IDs). In-table values matter as much as past-table ones: SCEN `WNAM` editor width 350 is `0x0000015E`, index 0, and was rewritten to `0x0100015E` when master 0 moved (`RemapNonFormIdSlots.InTableScalarsSurviveMasterReorder`). Localized string IDs are the same case: TERM `WNAM` welcome text ID `0x0000ABCD` became `0x0100ABCD` (`RemapNonFormIdSlots.LocalizedStringIdsSurviveMasterReorder`). `wbUnknown` slots count as scalar too, because xEdit never remaps bytes it has not decoded (TACT `PNAM`, PACK `BNAM`). Helpers are also read from `wbDefinitionsCommon.pas` (optional fifth generator argument), and a helper is judged by the element that declares the subrecord, not as a whole: `wbWorldLODData` holds NAM3 (water form ID) and NAM4 (`LOD Water Height` float), so only WRLD `NAM4` is denied. `remap_raw` and `rekey_form_links` also leave `0xFFFFFFFF` alone: it is xEdit's None value (`wbInterface.pas:TwbFormID.IsNone`), stored in vanilla EQUP `ANAM`, and was rewritten to `0x01FFFFFF` (`RemapNonFormIdSlots.NoneSentinelAndUndecodedSlotsSurviveMasterReorder`). The corpus evidence alone is not enough either: vanilla holds real links past the table (`DLCRobot.esm` REFR `XRFG`, see `GARDEN-TOOLS.md` on `resolve_plugin_link`), which the game reads as the owning plugin's, so `remap_raw` keeps remapping them. PACK `CNAM` is mixed in xEdit (record-level `Combat Style` form ID, and the `Data Input Values` union of bool/int/float after an `ANAM` type string), so `corpus_non_formid_slot` denies only a PACK `CNAM` whose previous subrecord is `ANAM`. SCEN `TNAM`/`HTID` depend on the scene action (`scene_action_scalar`): an action opens with a 2-byte `ANAM` type and closes with the empty `ANAM` marker. Inside an action `TNAM` is the Timer float and `HTID` is the Dialogue headtracking actor-ID array (or the empty Start Scene marker), so both stay scalar; only `HTID` in a Radio action (type 6, `Play Sound` SNDR) and the record-level `TNAM` (`Template Scene`, after the actions) are form IDs. It only removes slots from the generic branch of `form_links.cpp:visit_link_offsets`; record-specific handlers (WTHR, QUST, NAVM, structured slots) are unchanged. `tools/corpus/non_formid_slots.py:main` also merges `XEDIT_ONLY_SCALAR_SLOTS`, a hand list of slots that no official master value exercises but that xEdit types as strings: FSTP `ANAM` (`source:xEdit Core/wbDefinitionsFO4.pas:8689 @93cc0bc5`, `wbString(ANAM, 'Tag')`), SNDR `NNAM` (`wbDefinitionsFO4.pas:9408`, `wbString`), SNDR `FNAM` (`wbDefinitionsFO4.pas:9446`, `wbString`), and SNDR `ANAM` (`wbDefinitionsFO4.pas:9416` uses `wbSoundDescriptorSounds`, which is `wbRArray('Sounds', wbString(ANAM, 'Sound'))` at `source:xEdit Core/wbDefinitionsCommon.pas:9200 @93cc0bc5`), plus MUST `ANAM` and `BNAM` (`wbDefinitionsFO4.pas:8806-8807`, `wbString`), MUST `FNAM` (`wbDefinitionsFO4.pas:8813`, an array of `wbFloat` cue points) and SOPM `MNAM` (`wbDefinitionsFO4.pas:9505`, `wbInteger` type), checked by `RemapNonFormIdSlots.SopmAndMusicTrackFourByteSlotsStayUnchanged`. Those rows skip the corpus check and are tagged `// xedit` in the output, so a regeneration keeps them. Regenerate it after adding new handlers or when new official files ship.

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

The target range comes from `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslSlots`, which applies `fo4::local_form_id_range` to a light-flagged copy of the header: 0x001-0xFFF when `src/plugin/mutation.cpp:allows_hardcoded_range` holds (HEDR at least 1.0 and a master, xEdit `93cc0bc5` `Core/wbImplementation.pas:TwbFile.GetAllowHardcodedRangeUse`, lines 4070-4090), else 0x800-0xFFF. `checkEslEligibility` uses the same helper, so both report the same range text and slot count. The renumber pass runs on a copy with the light flag set, then restores the original header flags before staging, so compaction never sets the serialized ESL flag. A plugin with masters can come out as a full plugin holding IDs 0x001-0x7FF. Existing in-range IDs stay put; IDs below the floor or above 0xFFF are rekeyed in ascending order into free slots by `src/plugin/mutation.cpp:renumber_local_record`. Out-of-range nested records and out-of-range top-level records that have form ID labelled child groups are refused before anything is staged, by `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers`: `NestedRecordCompactionUnsupported` for nested records, `ParentGroupRelabelUnsupported` for parents. A full (not light-flagged) plugin holding own IDs below 0x800 must be flagged light before the game loads it (`PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:lightFlagHint`), because `AddCompileIndex` only remaps IDs >= 0x800 (source: RE repo `ReverseEng/commonlib_NonVR` @ `dbaeca7`, `RE-Work/Runtime/03-FINDINGS.md`, section "FormID compile-index remap decoded"; open question GT-055 on the GARDEN-TOOLS Issue Tracking wiki).

When the TES4 light flag is not set, the compaction success, HEDR-lowering and already-ESL-compatible messages of `compactToEsl` and `RadiclePluginEsl.cpp:checkEslEligibility` end with `RadiclePluginSupport.hpp:lightFlagHint` (through `lightFlagAdvice`, which adds nothing for a plugin that is already light): "Run masters.SetLight to flag the plugin light; Radicle does not flag it from a .esl file name." (`PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp:RadiclePluginBackend::setLight`). Radicle's save never sets the light flag from the file extension; only `src/plugin/writer.cpp:make_empty_plugin` flags a new .esl, and the reader decides light from the TES4 flag alone.

HEDR next object ID: `src/plugin/mutation.cpp:renumber_local_record` raises it to the new local ID + 1 only when the new ID is at or above the stored value (`if (new_local.value() >= next.header.next_form_id)`). `compactToEsl` sets it to the last assigned ID + 1 with `src/plugin/mutation.cpp:set_hedr_next_form_id` (declared in `Radicle/include/fo4/plugin/mutation.hpp`), inside the same transaction. That is xEdit's rule at `93cc0bc5`: `xEdit/xeMainForm.pas`, `TfrmMain.mniNavRenumberFormIDsFromClick` (line 13054), tracks the highest newly assigned ID (`HighFormID`, lines 13216, 13298-13299, 13333-13334), and its nested `UpdateNextObjectID` stores that + 1 (lines 13367-13373). Records that were already in range do not feed it; `reserve_local_form_key` scans upward past them. At full capacity the value is 0x1000. xEdit's increment behaviour for a light FormID at 0xFFF is not in the pinned excerpts (unverified), so the 0x1000 sentinel is Radicle's rule.

`Radicle/src/plugin/flags.cpp:set_plugin_light_flag` with light set calls `Radicle/src/plugin/mutation.cpp:lowered_light_next_form_id`. When every own ID is within the light range, HEDR next is above 0xFFF and above the highest own ID + 1, next is lowered to the highest own ID + 1 (or to the light floor when there are no own records). Without this, `src/plugin/mutation.cpp:reserve_local_form_key` refuses every new record in the light plugin. `compactToEsl`'s all-in-range branch and `PluginEditTool/native/src/plugin/RadiclePluginEsl.cpp:checkEslEligibility` use the same helper; eligibility reports "would only lower HEDR nextFormID". xEdit's `Core/wbImplementation.pas` `TwbFile.SetIsLight` (lines 6248-6266 at `93cc0bc5`) sets only the header flag and does not change next. Its `TwbFile.NewFormID` masks next to 12 bits for light files (`Mask := $FFF`, line 5211 at `93cc0bc5`), which Radicle's allocator does not. `PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp:RadiclePluginBackend::setLight` reports the lowering ("lowered HEDR nextFormID to 0x..."). Tests: `Radicle/tests/test_plugin_flags.cpp` `PluginFlags.LightFlagLowersHedrNextToOnePastTheHighestOwnId`, `LightFlagKeepsHedrNextThatIsAlreadyInRange`, `LightFlagLowersMasterlessHedrNextToTheFullFloor`, `ClearingLightFlagNeverChangesHedrNext`; `RadiclePluginCompactTests.cpp` `Native SetLight lowers an out-of-range HEDR next ID to one past the highest own ID`; `RadiclePluginEslTests.cpp` `Native ESL eligibility reports a HEDR-only lowering when every record is already in range`.

`src/plugin/mutation.cpp:reserve_local_form_key` treats next = ceiling + 1 as a full plugin and reports that no local ID remains; only a next value beyond that is reported as outside the range. Tests: `RadiclePluginMaintenanceTests.cpp`: `Native CompactToEsl remaps top-level records and their in-plugin references` (next value, ESL message), `Native CompactToEsl leaves a light plugin that can still create records`, `Native CompactToEsl at full capacity leaves no ID and creation fails cleanly`; `test_mutation.cpp:ReserveLocalFormKeyReportsAFullLightPluginAndAnOverflowedNextSeparately`; `RadiclePluginEslTests.cpp` covers zero, 0x001, 0x7FF, 0x800, 0xFFF, 0x1000, all 4095 target slots and transactional rejection of 4096 records.

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

Flagging a plugin light rejects owned FormIDs below the floor that `src/plugin/maintenance.cpp:esl_eligibility` and `src/plugin/mutation.cpp:local_form_id_range` use: 0x001 when `allows_hardcoded_range` holds (HEDR at least 1.0 and a master), else 0x800. IDs above 0xFFF are also rejected, and the message names the actual range. The floor rule is xEdit `93cc0bc5` `Core/wbImplementation.pas:TwbFile.GetAllowHardcodedRangeUse`. Whether the engine loads masterless IDs below 0x800 in a light plugin is unverified; see `Radicle/docs/ESL_FORMIDS.md`. Clearing the flag never checks IDs. Tests: `Radicle/tests/test_plugin_flags.cpp:PluginFlags.RejectsLightFlagWhenMasterlessLocalIdIsBelowTheFullFloor`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.RejectsLightFlagBelowTheFloorWhenHedrIsBelowOne`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.AllowsLightFlagForLowLocalIdsWithHedrOneAndAMaster`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.AllowsLightFlagForMasterlessIdsAtTheFullFloor`, and `PluginEditTool/native/tests/unit/RadiclePluginEslTests.cpp:Native SetLight refuses a masterless plugin with owned IDs below 0x800`.

## Radicle/src/plugin/writer.cpp:remap_plugin_masters

`Radicle/src/plugin/writer.cpp:remap_plugin_masters` refuses dropping the last master of a light plugin that owns local IDs below 0x800 (GT-057). Light plugins with a master use the 0x001 floor from `Radicle/src/plugin/mutation.cpp:allows_hardcoded_range`; with no masters that floor reverts to 0x800, so those low own IDs would become invalid. The refusal throws `FormIdError` before any mutation. Full plugins, adds, reorders and light plugins with no low own IDs are unchanged. Tests: `Radicle/tests/test_master_metadata.cpp` `MasterMetadata.RemovingLastMasterOfLightPluginRefusesLowOwnIds`, `MasterMetadata.RemovingLastMasterOfLightPluginAllowsIdsAtTheFloor`, `MasterMetadata.RemovingLastMasterOfFullPluginAllowsLowOwnIds`; `PluginEditTool/native/tests/unit/RadiclePluginAddMastersTests.cpp` `Native CleanUnusedMasters refuses to drop the last master of a light plugin with low own IDs`.

## Radicle/src/c_api.cpp:radicle_plugin_remap_masters

`Radicle/src/c_api.cpp:radicle_plugin_remap_masters` remaps a copy of the plugin and assigns it back only when `fo4::remap_plugin_masters` succeeds, so a failed call (for example a dropped master that an own record or override still uses, which makes `encode_plugin_form_id` throw "is not in the destination master table") leaves the handle unchanged instead of empty (GT-090). Test: `Radicle/tests/test_c_api.cpp` `CApi.FailedRemapMastersLeavesThePluginUnchanged`.

## src/schema/scalar.cpp:scalar_descriptor

Record-agnostic: it returns the first whole-payload scalar field with the given subrecord signature, whatever record that signature sits in. Only tests call it (`tests/test_scalar.cpp`, `tests/test_scalar_embedded.cpp`, `tests/test_field_writer.cpp`).

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
- GT-138 compressed record size: `inflate_record_payload` checks the declared uncompressed size twice, both before the output buffer is allocated. First it refuses anything above `compressed.size() * 1032 + 64`. 1032 is zlib's maximum expansion ratio; the 64-byte slack covers the stream framing. Then it refuses anything above the absolute cap `k_max_record_uncompressed_bytes` (1 GiB, 2^30). The ratio bound keeps a small record from asking for much more output than its size allows, and the cap keeps any one record under 1 GiB. The output buffer is allocated once, for the zlib inflate attempt. Test: `tests/test_compression.cpp:Compression.OversizedDeclaredSizeIsRefusedBeforeAllocating` uses a 1,100,000-byte payload declaring 2^30 + 1 bytes; it passes the ratio bound and is refused by the cap.
- Tests: `tests/test_reader_limits.cpp` covers 16 levels (parses), 17 and 20 levels (refused), and a declared size of `0xFFFFFFFF` on a tiny payload (refused with the zlib expansion bound message). Ordinary compressed reads are still covered by `tests/test_compression.cpp`.

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
