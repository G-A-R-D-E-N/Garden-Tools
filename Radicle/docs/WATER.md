# Water field and selection evidence

`src/environment/base_records.cpp:decode_water` exposes WATR data through
`include/fo4/c_api_records.h:radicle_water_get_info` and the generated C#
`RadicleEnvironment.Water` getter. These landed in #381, ABI 4.

The field-layout evidence is TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`,
[`Core/wbDefinitionsFO4.pas`](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L12788).
This is a pinned compatibility oracle, not proof of runtime selection behavior.

| Subrecord | Layout evidence | Decoder surface |
|---|---|---|
| FNAM | lines 12792–12796, one byte | raw flags |
| DNAM | lines 12803–12868 | visual fields below |
| NAM0 / NAM1 | lines 12870–12871, three floats each | linear / angular velocity |
| NAM2 / NAM3 / NAM4 | lines 12872–12874, NUL-terminated paths | three noise textures |

DNAM stores Fog Depth Amount at byte 0; shallow/deep RGBA colors at 4/8;
six range/alpha floats at 12–32; underwater color at 36; underwater
amount/near/far at 40–48; normal, falloff, reflectivity, Fresnel, surface
effect and displacement floats at 52–92; reflection color at 96; sun
specular/sparkle and interior specular floats at 100–124. Colors expose RGB;
the original fourth byte remains in the lossless container.

The noise values are grouped by property, not stored as three structures:
three wind-direction floats at 128–136, speeds at 140–148, amplitude scales
at 152–160, UV scales at 164–172, and falloffs at 176–184. The optional tail
is silt amount at 188, light/dark RGBA colors at 192/196, and one reflection
byte at 200. The decoder requires 188 bytes and rejects data beyond 201;
unmodeled subrecords and untouched bytes stay in the container. Silt is
all-or-nothing as in xEdit (`StructDoInit` drops an optional member that does
not fit whole): it is decoded only from 200 bytes, the reflection byte only at
201, and a 189-199-byte payload decodes with neither.

`tests/test_water_real.cpp:WaterRealData` decodes every WATR version in each
of the seven official masters, records counts and DNAM sizes, exercises the
C ABI getter, and compares the complete untouched serialized master with
its original bytes. It uses installed `FO4_DATA_DIR`; absent data or a missing
named master skips cleanly. No game bytes are checked in.

The 2026-10-04 installed corpus has 47 WATR versions: Fallout4.esm 42,
DLCCoast.esm 2, DLCNukaWorld.esm 3, and zero in the other four masters.
Two DNAMs are 188 bytes and 45 are 201 bytes. That run passed 14 parameterized
native and C ABI checks, including untouched byte identity for all seven
masters. The file now defines 42 parameterized cases (6 tests x 7 masters),
not re-run at this head. The real-data cases run only when `FO4_DATA_DIR` is set,
and skip cleanly otherwise. In CI
the `fo4-data` runner supplies it to the `native-windows-real-data` job of `native-ci`; both `native-windows-tests` passes clear it. The Linux job passes
`FO4_DATA_DIR_LINUX`, which is currently unset, so they skip there.

Per-field population, measured on the shipped masters of OG 1.10.163.0, NG 1.10.984.0 and AE 1.11.240.0 (identical on all three). The counting rules match `populated_<field>` in `Radicle/tests/test_water_real.cpp:DecodesEveryWaterAndPreservesMasterBytes`: DNAM counts records that have one, NAM0 and NAM1 count velocity triples with a non-zero component, and NAM2-NAM4 count non-empty noise texture paths.

| Master | WATR | DNAM (188 / 201 bytes) | NAM0 | NAM1 | NAM2 | NAM3 | NAM4 |
|---|---|---|---|---|---|---|---|
| Fallout4.esm | 42 | 42 (2 / 40) | 36 | 0 | 42 | 42 | 42 |
| DLCCoast.esm | 2 | 2 (0 / 2) | 2 | 0 | 2 | 2 | 2 |
| DLCNukaWorld.esm | 3 | 3 (0 / 3) | 1 | 0 | 3 | 3 | 3 |
| Total | 47 | 47 (2 / 45) | 39 | 0 | 47 | 47 | 47 |

DLCRobot, DLCworkshop01, DLCworkshop02 and DLCworkshop03 have no WATR. No shipped record sets a non-zero angular velocity (NAM1).

## Editing

WATR is registered in `schema/fallout4/fields.json` and edited through the
generic W0 field writer and subrecord authoring (`PluginFile.SetScalar`,
`SetString`, `SetFormLink`, `FieldCount`, `AddSubrecord`, `RemoveSubrecord`,
`CreateRecord` and the matching `radicle_plugin_*` exports).

| Family | READ | WRITE | ADD | REMOVE | CREATE | REMAP |
| --- | --- | --- | --- | --- | --- | --- |
| DNAM floats (44) | `decode_water`, `radicle_water_get_info`, `Water` | `SetScalar` `WATR\DepthAmount` .. `WATR\SiltAmount`; prefix rule, silt absent on a 188-byte DNAM | refused (present and required) | refused (required) | 201-byte xEdit default | none |
| DNAM colors (6 x RGB) | same | `SetScalar` `WATR\<Color>Red/Green/Blue` (u8); pad bytes never written | same | same | zero | none |
| DNAM reflections | `screen_space_reflections` | `SetScalar` `WATR\ScreenSpaceReflections` (0..1, 201-byte DNAM only) | same | same | 1 (True) | none |
| FNAM flags | `flags` | `SetScalar` `WATR\Flags` | n/a (required) | refused (required) | `00` | none |
| ANAM | n/a | `SetScalar` `WATR\OpacityUnused` | `00` | yes | not created | none |
| NAM0 / NAM1 velocities | `linear_velocity`, `angular_velocity` | `SetScalar` `WATR\LinearVelocityX..Z`, `AngularVelocityX..Z` (12-byte payload only) | zero vector | yes | not created | none |
| NAM2-NAM4 noise textures | `noise_textures` | `SetString` `WATR\Layer1..3NoiseTexture` | empty string | yes | not created | none |
| FULL name | n/a | `SetString` `WATR\Name`, refused in localized plugins | empty string (non-localized plugins only) | yes | not created | none (string) |
| SNAM open sound | n/a | `SetFormLink` `WATR\OpenSound` (SNDR, nullable) | NULL link | yes | not created | yes |
| TNAM, XNAM, YNAM, INAM | n/a | `SetFormLink` `WATR\MaterialUnused` (MATT), `ConsumeSpell`, `ContactSpell` (SPEL), `ImageSpace` (IMGS); none nullable | refused (`no_default`) | yes | not created | yes |
| DATA, GNAM | n/a | n/a | refused (`no_default`) | yes | not created | none |
| Records | n/a | n/a | n/a | n/a | `CreateRecord("WATR", ...)`: EDID, FNAM, DNAM | n/a |

A 189–199-byte DNAM has no complete silt member (GT-115): silt needs a DNAM of at least 200 bytes. Native field reads and the
generic writer refuse all seven silt paths until 200 bytes; refused writes
leave the serialized plugin unchanged. No official record has a partial silt member.

Stays opaque: DATA and GNAM payloads (local measurement of the seven official
masters: every DATA is 0 bytes and every GNAM is 12 bytes of unknown data), the
color pad bytes (local measurement: byte 195 is 0xFF in four Fallout4.esm
records), and every byte of an official record that no edit
touched. The official order check finds no violators in the 47 WATR. Field
list, defaults provenance and refusal rules are in
`docs/code-notes/radicle-water-writer.md`.

## Cell/worldspace resolution

The same pinned FO4 source defines CELL XCLW as a float (line 6045)
and XCWT as a WATR link (6064). WRLD PNAM bit 3 is named Use Water Data
(13127–13136), and NAM2 is its WATR link (13141–13143). The pinned
[`Core/wbDefinitionsCommon.pas`](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsCommon.pas#L9718)
defines NAM3/NAM4 LOD water and DNAM default land/water heights at 9718–9728.
Radicle exposes these raw links/heights and load-order winners already.

The [selection record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md)
answers these questions for OG 1.10.163 (RE 6916afb), except the XCWT step, which it left unresolved; README Follow-up 2 (RE `4cdafd2`) resolved it; sections 3.1–3.7 stamp
addresses 0x3B53D0, 0x493910, 0x3B4D40, 0x493400, 0x3ADC29 and 0x491910.
`fo4::env::resolve_water`, C `radicle_water_resolve` and C# `EffectiveWater`
select a winning CELL XCWT, otherwise the exterior world's inherited NAM2,
otherwise Fallout4.esm form 0x18. The default is the winning `Fallout4.esm:0x18` WATR record, looked up by form ID, the same lookup the engine's global default makes (VERIFY section 1 row 28). Radicle also treats a deleted `Fallout4.esm:0x18` as missing (`src/environment/world.cpp:winning_type`); RE `4cdafd2` does not cover deletion. Three rules govern the failure cases. An
invalid or deleted WNAM parent ends inheritance at the current world (RE `6916afb`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md` §3.4 covers a missing or NULL parent; a deleted parent is treated the same way, and the RE does not address deletion specifically). An
invalid or non-WATR NAM2, read from the world where inheritance stops, selects
Fallout4.esm form 0x18 with `uses_default_water=1`. An XCWT whose target is
missing or not a WATR is dropped, as the engine drops it during the
cell's InitItem fix-up, so the cell falls back to the worldspace water or the
default (`src/environment/world.cpp:resolve_water`). RE `4cdafd2`
[README, Follow-up 2](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/4cdafd28b8b576749fea4ffdfde5f3c32fefdb7c/RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md)
and [`VERIFY-2026-10-10.md`](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/4cdafd28b8b576749fea4ffdfde5f3c32fefdb7c/RE-Work/subsystems/radicle-re-garden-tools/water-395-341/VERIFY-2026-10-10.md)
section 7 item 3 show this statically for OG. In `VERIFY-2026-10-10.md`, NG shows the same steps, including the unlink of an unresolved extra, in the decompile (section 3 row 9). For AE the decompile shows only the lookup and the TESWaterForm cast, with no visible remap step (section 4), and the AE unlink is COULD NOT CONFIRM (section 1 row 33). The RE does not address deleted XCWT targets, so Radicle treats them as missing, as for WNAM. A CELL or WRLD handle from an older override resolves like the winning version of that form (`src/environment/world.cpp:resolve_water` decodes `env.winner(form_key)`). In Radicle's model (not from the RE), the winning CELL version is decoded on its own: a winning override that omits XCWT or XCLW does not take them from the master. An XCWT of 0 is treated as absent (test CELL `0x921`); the engine's setter removes the extra for 0, confirmed for OG and NG and not read for AE (`VERIFY-2026-10-10.md` section 1 row 17). The engine's XCWT fix-up uses the last file that defined the cell (`FUN_140153690(cell, -1)`, [README, Follow-up 2](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/4cdafd28b8b576749fea4ffdfde5f3c32fefdb7c/RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md)). Still open in the RE: when the fix-up runs relative to loading a WATR from another plugin (F2-11), and runtime behaviour. Missing default WATR data stays unresolved
through a null record handle; no synthetic visual record is created. RE `6916afb`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md` §3.4 shows the engine synthesises a DefaultWater record when `Fallout4.esm` form ID 0x18 is absent. Radicle reports the default key with `uses_default_water=1` and leaves the record null by design; it does not synthesise one.

Heights: an absent XCLW selects the world default height for an exterior cell
with Has Water and a winning world (Radicle's inference: the RE shows only that the engine tests XCLW >= 2^31, VERIFY-2026-10-10 section 1 row 15, which calls the README's 'untouched' reading interpretation; the RE does not show what an absent XCLW starts as). A WRLD with no DNAM reports height 0.0 with
`has_height` set, and a WRLD input reports a height when its winning version is live. A WRLD input whose winning version is deleted resolves to the default water with no height, and so does a CELL whose winning worldspace is deleted: both go through `src/environment/world.cpp:winning_type`, which treats a deleted winner as missing (Radicle's policy; RE `4cdafd2` does not cover deleted records). Tests `DeletedWinningWorldspaceInputTakesNoWaterOrHeight` and `CellInDeletedWinningWorldspaceResolvesDefaultWater` in `Radicle/tests/test_environment_water_edges.cpp` cover them. Interior cells use
only their own XCWT, otherwise the default key, and report no XCLW height. The
bits come from xEdit CELL DATA bit 0 "Is Interior Cell" and bit 1 "Has Water"
([lines 5968-5969](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L5968-L5969)),
and WRLD PNAM bit 0 "Use Land Data" (height, [line 13128](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L13128))
and bit 3 "Use Water Data" (water, [line 13131](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L13131)).

`radicle_worldspace_get_info` reports the raw PNAM: `has_parent_flags` is 0 when
the record has no PNAM (`src/environment/world.cpp:decode_worldspace`,
`src/c_api_world.cpp:radicle_worldspace_get_info`). Water inheritance uses the
effective flags from `src/environment/world.cpp:effective_parent_flags` instead:
whichever of WNAM and PNAM comes last in file order wins, WNAM setting all bits
(0xFFFF), so a WNAM-only record inherits every bit. RE `6916afb`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md` §3.7 verifies (OG bytes, AE decompile) that WNAM sets all inherit bits, a later PNAM overwrites them, and WNAM alone inherits everything.

Water type follows PNAM bit 3; height follows bit 0 independently. Ordered
WNAM/PNAM fields affect inheritance, and active cycles fail closed. RE `6916afb`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md` §3.3 shows the engine has no cycle guard, so the RE expects a hang on such a cycle (static only, not executed; §5 fixture 7, VERIFY section 1 row 22). This throw is a deliberate divergence. A WRLD input always resolves its height through PNAM bit 0 (`src/environment/world.cpp:resolve_water`), so a parent cycle whose worlds set bit 0 makes the whole selection throw, even when the water-type walk (bit 3) would not cycle. Exterior
CELL heights require Has Water without Interior. Positive fractions round at
one half, negative fractions truncate, and NaN/below-range values become
-2^31. Values at least +2^31 select the world's default height: **-2^31 is
a real height**, not a default sentinel. The original XCLW bytes remain intact.
Interior height volumes and live rendering are outside this selection query.
The selection result was added as ABI 28 after integrating main ABI 27. The
current number is `RADICLE_ABI_VERSION` in `Radicle/include/fo4/c_api.h`, pinned
by `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsSeventyEight`; if
another ABI PR merges first, re-bump against that pin. See
`docs/code-notes/radicle-water-writer.md`.

GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` has the consumer
`opencw/Services/WaterBuild/WaterBuilder.cs:Build`, `ReadWorld`, and `Material`,
and runtime probe `opencw/tools/probes/WaterProbe.cs` with scene
`opencw/tools/water/water_probe.tscn`. It is pinned to Radicle
`2360e88138aa75c60db78ab579a3d2d7fb81d142`. Its
`WaterRules.IsDefaultHeight` recognizes missing/nonfinite or magnitude at
least `1e30`; it misses the verified positive 2^31 default-height threshold.
That historical consumer policy is not runtime evidence.

**historical: run 2026-10-05 against an ABI-14 build, not re-run at this head.** The supplied `probes/gardenengine/WaterSelectionProbe.cs` and scene were run
in an isolated checkout of GardenEngine `676d2c1eabeceefedf53dc249cf63ee9a7f73740`.
Build `opencw/OpenCommonwealth.csproj` with `RadicleSourceDir` set to this
checkout, set `FO4_DATA_DIR` and `RADICLE_NATIVE_PATH` to its ABI-14 library,
then run the matching double-precision Mono editor with
`--headless --path opencw tools/water_selection_probe.tscn`.
The 2026-10-05 build had zero errors (82 existing warnings); Godot
`4.8.dev-double.mono.double.custom_build.0b0af89c9` printed
`water_selection_probe PASS cells=36864 heights=36864` and exited zero.
Each selected WATR key passed through the existing `WaterBuilder.Material`
converter and retained its key/fog-depth value. With game data unset it
printed SKIP and exited 77. This verifies consumer conversion, not live
rendering or adoption by `WaterBuilder.Build`.
