# Water writer

Source for every layout, order, default and required flag below: pinned TES5Edit `wbDefinitionsFO4.pas` at
`93cc0bc5a1251936c3c7859eee3150eda12a62d7` (local copy `build/xedit/wbDefinitionsFO4.pas` in the parity checkout, WATR
lines 12788-12875). Struct loading semantics come from xEdit `wbImplementation.pas:StructDoInit`. Official measurements
are over the seven shipped masters: 47 WATR (Fallout4.esm 42, DLCCoast.esm 2, DLCNukaWorld.esm 3).

## W0 merge (`Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION`)

- The W0 field writer and subrecord authoring branch (9cf04f75) was merged onto #396. Conflicts: `c_api.h` (ABI number),
  `test_c_api_quest.cpp` (ABI pin) and the generated `RadicleNative.g.cs`. Every generated file
  (`generated_scalar_fields.inc`, `generated_record_constraints.inc`, `generated_lstring_fields.inc`,
  `generated_formlink_lists.inc`, `generated_condition_scopes.inc`, `RadicleNative.g.cs`, `abi_layout_probe.cpp`) was
  regenerated with its tool. `Radicle/bindings/csharp/tools/generate_interop.py:HEADERS` is the W0 list, which already
  contains every header #396 exports from (`c_api_records.h` holds `radicle_water_resolve`).
- ABI 19 is provisional (#396 had 14, W0 had 10, vegetation 17 and climate 18 are taken by sibling tranches);
  `Radicle/tests/test_c_api_quest.cpp:AbiVersionIsNineteen` pins it. Reassign from current main before merging.

## Reader fix (`Radicle/src/environment/base_records.cpp:decode_water`)

- xEdit `wbDefinitionsFO4.pas` declares DNAM `SetOptionalFrom(4)`: the fifth top-level member, the 12-byte
  `Silt Properties` struct, and the `Screen Space Reflections` byte after it are optional. `StructDoInit` treats an
  optional member whose full size does not fit as missing and ends the struct there, so silt is all-or-nothing: it is
  present only from 200 bytes, and the reflection byte only at 201. The decoder used to accept a partial silt at 192 and
  196 bytes (silt amount, then light colour). It now reads all three silt members only when the payload is at least
  `k_water_silt_end` (200). Payloads of 189-199 bytes decode the required 188 bytes with no silt and no reflection flag,
  like xEdit; the extra bytes stay in the record. Below 188 and above 201 still throw `WorldDecodeError`.
  Test: `Radicle/tests/test_water_authoring.cpp:WaterReader.SiltIsAllOrNothingLikeXEdit` (188, 192, 196, 199, 200, 201
  decode; 187 and 202 throw). Official decode is unchanged: every official DNAM is 188 or 201 bytes.
- Everything else matches xEdit. FNAM must be exactly one byte, NAM0/NAM1 exactly 12, NAM2-NAM4 single strings; colours
  expose RGB and leave the fourth byte (xEdit `wbByteColors` unused byte) alone. `screen_space_reflections` is
  `byte != 0`; xEdit's `wbBoolEnum` names only 0 and 1, and the raw byte is reachable as `WATR\ScreenSpaceReflections`.
  The decoder does not read FULL, ANAM, TNAM, SNAM, XNAM, YNAM, INAM, DATA or GNAM; those are reachable only through the
  field writer and the generic record surfaces.

## Registered fields (`Radicle/schema/fallout4/fields.json`, record WATR)

All paths are `existing_only`, `presentation: identity`; f32 bounds are +-1e9 like the other registered records.

- Scalars outside DNAM: `WATR\OpacityUnused` (ANAM u8, whole payload), `WATR\Flags` (FNAM u8, whole payload, 0..255 so
  unnamed bits survive; xEdit names 0x01 Dangerous, 0x02 Unknown 1, 0x04 Directional Sound),
  `WATR\LinearVelocityX/Y/Z` (NAM0 f32 at 0/4/8, `payload_size` 12) and `WATR\AngularVelocityX/Y/Z` (NAM1, same).
- DNAM (63 fields, no `payload_size`, prefix rule): 44 f32 in xEdit order (`DepthAmount` 0, `ColorShallowRange` 12,
  `ColorDeepRange` 16, `ShallowAlpha` 20, `DeepAlpha` 24, `AlphaShallowRange` 28, `AlphaDeepRange` 32,
  `UnderwaterFogAmount` 40, `UnderwaterNearFog` 44, `UnderwaterFarFog` 48, `NormalMagnitude` 52, `ShallowNormalFalloff`
  56, `DeepNormalFalloff` 60, `ReflectivityAmount` 64, `FresnelAmount` 68, `SurfaceEffectFalloff` 72, the
  `Displacement Simulator` floats as `DisplacementForce` 76, `DisplacementVelocity` 80, `DisplacementFalloff` 84,
  `DisplacementDampener` 88, `DisplacementStartingSize` 92, `SunSpecularPower` 100, `SunSpecularMagnitude` 104,
  `SunSparklePower` 108, `SunSparkleMagnitude` 112, `InteriorSpecularRadius` 116, `InteriorSpecularBrightness` 120,
  `InteriorSpecularPower` 124, `Layer1..3WindDirection` 128-136, `Layer1..3WindSpeed` 140-148, `Layer1..3AmplitudeScale`
  152-160, `Layer1..3UVScale` 164-172, `Layer1..3NoiseFalloff` 176-184, `SiltAmount` 188); 18 colour bytes as
  `<Name>Red/Green/Blue` u8 for `ShallowColor` 4, `DeepColor` 8, `UnderwaterColor` 36, `ReflectionColor` 96, `LightColor`
  192 and `DarkColor` 196; `ScreenSpaceReflections` u8 0..1 at 200 (`wbBoolEnum`). Displacement fields carry the
  `Displacement` prefix so `WATR\Velocity` cannot be confused with the NAM0/NAM1 velocities.
- Size rule. A field that does not fit in the stored DNAM is absent (NOT_FOUND, no byte change): a 188-byte DNAM (the
  two official records at form version 122 and 123) has no silt or reflection fields, a 200-byte one has no reflection
  byte. W0 has one `payload_size` per path, so the prefix rule cannot express silt all-or-nothing: on a non-xEdit
  189-199-byte DNAM the silt bytes that fit are writable in place (no resize) although the decoder and xEdit show no silt.
  No official record has such a size.
- Strings: `WATR\Name` (FULL, registered in `localized_subrecords`, so a localized plugin refuses it, see
  `Radicle/src/plugin/field_writer.cpp:write_scalar`), `WATR\Layer1NoiseTexture`, `Layer2NoiseTexture`,
  `Layer3NoiseTexture` (NAM2, NAM3, NAM4).
- FormLinks (all offset 0, whole 4-byte subrecord): `WATR\MaterialUnused` (TNAM, MATT), `WATR\OpenSound` (SNAM, SNDR,
  nullable: `[SNDR, NULL]`), `WATR\ConsumeSpell` (XNAM, SPEL), `WATR\ContactSpell` (YNAM, SPEL), `WATR\ImageSpace` (INAM,
  IMGS). Only SNAM's target list contains NULL in xEdit, so it is the only nullable link. All five signatures are in
  `Radicle/include/fo4/schema/formid_subrecords.inc`, so `Radicle/src/schema/form_links.cpp:visit_link_offsets` remaps
  them when masters change; DNAM, GNAM, NAM0 and NAM1 are in that table too but are never 4 bytes in WATR, so they are not
  remapped.

## Authoring (`fields.json` WATR `authoring`)

- Order (xEdit): EDID, FULL, ANAM, FNAM, TNAM, SNAM, XNAM, YNAM, INAM, DATA, DNAM, GNAM, NAM0, NAM1, NAM2, NAM3, NAM4, all
  maximum 1. Required (minimum 1): FNAM (`wbInteger(FNAM, ..., cpNormal, True)`) and DNAM (`.SetRequired`).
- Defaults: FNAM `00` and ANAM `00` (u8 with no declared default; xEdit initialises integers to zero); SNAM `00000000`
  (NULL, allowed by `[SNDR, NULL]`); NAM0 and NAM1 twelve zero bytes (`wbVec3`, no declared default); NAM2-NAM4 and FULL a
  single NUL (empty string, the W0 convention; FULL's default is never used in a localized plugin,
  `Radicle/src/plugin/subrecord_authoring.cpp:default_usable`).
- DNAM default is the full 201-byte struct: every `SetDefaultEditValue` from the plan list (DepthAmount 8192,
  ColorShallowRange 0.6, ColorDeepRange 0.3, DeepAlpha 1, AlphaShallowRange 1, AlphaDeepRange 0.5, UnderwaterFogAmount 1,
  UnderwaterFarFog 1000, NormalMagnitude 0.5, ShallowNormalFalloff 1, DeepNormalFalloff 0.75, ReflectivityAmount 0.5,
  FresnelAmount 0.025, SurfaceEffectFalloff 0.9, DisplacementForce 0.4, DisplacementVelocity 0.6, DisplacementFalloff
  0.985, DisplacementDampener 10, DisplacementStartingSize 0.05, SunSpecularPower 50, SunSpecularMagnitude 1,
  SunSparklePower 1, SunSparkleMagnitude 1, InteriorSpecularRadius 100000, InteriorSpecularBrightness 1,
  InteriorSpecularPower 100, Layer1..3UVScale 100, Layer1..3NoiseFalloff 300, ScreenSpaceReflections 1 for `True`) and
  zero for every other float, every colour byte and every colour pad byte. 201 rather than 188: `StructDoInit` applies
  the optional-member cut only when it loads existing data (`Assigned(aBasePtr)`), so a newly created xEdit DNAM has every
  member including silt and the reflection flag; and every official record at the current form version 131 stores 201
  bytes (the two 188-byte records are versions 122/123). The bytes are the little-endian f32 encodings of the values
  above at their offsets and are checked field by field after decode in `Radicle/tests/test_water_authoring.cpp:WaterAuthoring.CreatesFromTheXEditDefaults`.
- No default for TNAM, XNAM, YNAM, INAM (target lists without NULL, so a zero link is invalid), DATA and GNAM
  (`wbByteArray(..., 'Unused', 0)`: size 0 is xEdit's variable-length byte array, not a 0-byte payload; every official DATA
  is 0 bytes but every official GNAM is 12 bytes of unknown data, and a zero-length default is not expressible). Add is
  `no_default` for these; removal is allowed because xEdit marks none of them required.
- `coupled`: none. No WATR subrecord owns siblings (there is no model group, destruction group or count field), so
  every optional entry is freely removable.
- CREATE is enabled: both required entries have proven defaults. `create_record(WATR)` writes EDID, FNAM `00` and the
  201-byte DNAM, in localized and non-localized plugins alike (FULL is optional, so localization does not block it).

## Official order check

`Radicle/tests/test_water_real.cpp:WaterRealData.EveryOfficialWaterIsListedInXEditOrderAndPreflightClean` checks every
subrecord of all 47 official WATR is listed, appears in non-decreasing authoring position, passes
`preflight_authoring_record` and has no `preflight_record` error. Violators: none. Observed shapes: 20 records with SNAM,
20 without, 2 with FULL and TNAM, 2 with FULL only, 2 without YNAM, 1 without XNAM and YNAM; every record has ANAM,
FNAM, INAM, DATA, DNAM, GNAM, NAM0-NAM4.

## Stays opaque

- DATA and GNAM payloads (unused/unknown bytes, preserved; listed in the order, removable, not addable).
- Colour pad bytes at DNAM 7, 11, 39, 99, 195, 199. Not always zero in shipped data: byte 195 (silt light colour pad) is
  0xFF in four Fallout4.esm records (0x1BDDB1, 0x1450A3, 0x16E900, 0x16E8FF), so they are never rewritten.
- DNAM bytes 188-199 of a 189-199-byte payload as far as the decoder is concerned (see the size rule above).
- FULL in localized plugins (string ID, W0 refusal).

## Tests

- `Radicle/tests/test_water_authoring.cpp` (fixture `Radicle/tests/water_fixture.hpp`): field and colour edits with exact
  DNAM bytes, bounds, prefix-rule absence at 188 bytes, strings and the localized refusal, links (null, wrong type,
  unknown master), add/remove order, unknown-order refusal, create and decode, reader fix.
- `Radicle/tests/test_water_c_api.cpp:WaterCApi.FieldLinkAuthoringAndCreateStatusesMapAndSurviveReload`: status mapping,
  save, `radicle_water_get_info` decode.
- `Radicle/bindings/csharp/Radicle.Interop.Tests/WaterAuthoringTests.cs`: the same through `PluginFile` and
  `RadicleEnvironment.Water`.
- `Radicle/tests/test_water_real.cpp:WaterRealData.EveryScalarAndStringPathEditsWithoutTouchingOtherBytes` edits every
  registered scalar and string path on every official WATR, compares every subrecord after reparse against an
  independently patched copy and every other record byte for byte; localized FULL edits must throw.
  `LinksAndOptionalSubrecordsEditAndKeepEveryOtherRecordByteIdentical` sets every present link to an official target,
  replaces or adds SNAM with an official SNDR, checks the required and `no_default` refusals and keeps every other record
  byte-identical after save and reparse.
