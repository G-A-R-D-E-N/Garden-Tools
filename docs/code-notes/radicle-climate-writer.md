# Climate and weather writer

Source for every layout, order, default and required flag below: pinned TES5Edit `wbDefinitionsFO4.pas` at
`93cc0bc5a1251936c3c7859eee3150eda12a62d7` (local copy `build/xedit/wbDefinitionsFO4.pas` in the parity checkout:
CLMT lines 6115-6126, WTHR lines 13034-13108) and the shared helpers in `wbDefinitionsCommon.pas`
(`wbClimateTiming` 8435, `wbWeatherCloudAlphas` 9303, `wbWeatherCloudColors` 9346, `wbWeatherCloudSpeed` 9358,
`wbWeatherCloudTextures` 9379, `wbWeatherColors` 9433, `wbWeatherDirectionalLighting` 9485, `wbWeatherDisabledLayers`
9508, `wbWeatherFogDistance` 9521, `wbWeatherGodRays` 9574, `wbWeatherImageSpaces` 9589, `wbWeatherMagic` 9623,
`wbWeatherSounds` 9639, `wbAmbientColors` 6635, `wbWeatherTimeOfDay` 8335, `wbWeatherCloudColorsCounter` 1764).
Official measurements are over the seven shipped masters (11 CLMT, 107 WTHR).

## Climate timing mutation (PR #427)

- `Radicle/src/environment/base_records.cpp:set_climate_timing` replaces only an existing unique six-byte CLMT/TNAM. The stored field order follows the documentation in TES5Edit `wbDefinitionsFO4.pas` at commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7` and the existing `decode_climate` contract. Byte values are retained as stored; this API makes no runtime timing or range-normalization claim.
- `Radicle/src/environment/base_records.cpp:set_climate_timing` validates the record signature, uniqueness and exact size before touching payloads or original-byte caches. Unchanged input is a successful no-op. Missing records return false; missing, duplicate or unknown-length TNAM throws. It does not add/remove TNAM, create CLMT, edit WLST or change masters.
- `Radicle/src/c_api_base_records.cpp:radicle_plugin_set_climate_timing` takes six raw bytes through a dedicated input structure. `Radicle/bindings/csharp/Radicle.Interop/PluginFile.cs:SetClimateTiming` uses the existing editable plugin handle; callers save it and reload the environment for updated read views.
- `Radicle/tests/test_sky_parameters.cpp:SkyParametersTest` and `Radicle/bindings/csharp/Radicle.Interop.Tests/SkyParameterTests.cs` cover edit/save/reload, no-op byte preservation, malformed-input refusal and neighboring data.
- `set_climate_timing` was kept as is when W0 was merged rather than re-implemented on the generic path: its status contract differs (it throws for a missing, duplicate or wrong-size TNAM where `Radicle/src/plugin/field_writer.cpp:write_scalar` reports `subrecord_missing`), and `radicle_plugin_set_climate_timing` maps that throw to PARSE_ERROR, which `SkyParameterTests.TimingEditsRefuseMissingDuplicateAndUnknownLayouts` pins. The generic `CLMT\SunriseBegin` .. `CLMT\MoonsPhaseLength` paths write the same six bytes (`payload_size` 6) and are the per-byte alternative.

## W0 merge (`Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION`)

- The W0 field writer and subrecord authoring branch (f7b56842) was merged onto #427. Conflicts: `c_api.h` (ABI number), `PluginFile.cs` (both sides added methods; kept `SetClimateTiming` and every W0 method), `test_c_api_quest.cpp` (ABI pin) and the generated `RadicleNative.g.cs`. Every generated file (`generated_scalar_fields.inc`, `generated_record_constraints.inc`, `generated_lstring_fields.inc`, `generated_formlink_lists.inc`, `generated_condition_scopes.inc`, `RadicleNative.g.cs`, `abi_layout_probe.cpp`) was regenerated with its tool; `Radicle/bindings/csharp/tools/generate_interop.py:HEADERS` is the W0 list (it already contains every header #427 exports from). The updated reader branch at `429a1195b45ff7448c184891c2c2326767b671be` imports main `c50ad4d5405e1571113019654b10e3d4a05ca0f1` (ABI 11). This writer uses ABI 12 and `Radicle/tests/test_c_api_quest.cpp:AbiVersionIsTwelve` pins it; every existing export is retained. Re-bump from then-current main after the reader or another ABI PR merges first.

## Reader fixes (`Radicle/src/forms/weather.cpp:decode_weather`)

`Radicle/src/forms/weather.cpp:decode_cloud_colors` reports cloud layers independently
of time-slot count. At the pinned xEdit commit above,
`Core/wbDefinitionsCommon.pas:1764–1781` gives four layers before form version 35
and 32 from version 35; lines 8335–8370 give four color times before version 111
and eight thereafter. The previous fixed eight-time divisor reported two layers
for 64-byte PNAM and 16 for 512 bytes. The layer count now follows those proven
layouts, and `decode_cloud_alphas` reuses it for the per-layer comparison.
`Radicle/tests/test_climate_weather_authoring.cpp:CloudColorLayerCountsMatchTheStoredLayouts`
records the failing 2/16 values and covers the corrected 4/32/32 counts.

## Partial optional color preservation

`Radicle/schema/fallout4/fields.json:WTHR\\AmbientSpecularRed`, Green and Blue
require the complete optional Specular color member. At the pinned xEdit commit,
`Core/wbDefinitionsCommon.pas:6635–6655` makes Specular optional after the
24-byte Directional group; `wbByteColors` at lines 6680–6710 stores RGB plus
one padding byte. `Core/wbImplementation.pas:22589–22604` replaces an optional
member with an empty value when its complete size does not fit. DALC sizes
25–27 must therefore leave all three Specular channels absent, rather than
expose whichever individual byte happens to fit.

`Radicle/include/fo4/schema/scalar.hpp:ScalarDescriptor.minimum_payload_size`
and `Radicle/include/fo4/schema/field_address.hpp:FieldShape.minimum_payload_size`
carry the 28-byte group boundary. `Radicle/src/schema/scalar.cpp:scalar_field_bytes`,
`patch_scalar_field` and `scalar_field_shape`, plus
`Radicle/src/schema/field_address.cpp:resolve_field_slots`, enforce it on the
shared read/write paths. This reuses the same default-zero metadata and guard
reviewed for optional water colors; unrelated descriptors remain unchanged.
`Radicle/tools/schema_codegen.py:load_fields` validates the bound and `render`
emits it. `Radicle/tests/test_climate_weather_authoring.cpp:PartialOptionalSpecularColorsStayAbsentAndUnchanged`
proves high-level and direct-payload refusal without changed bytes; complete
28/32-byte members retain the existing edit coverage.

- `Radicle/include/fo4/forms/weather.hpp:Weather::cloud_speed_y` and `cloud_speed_x` are now `uint8_t`. xEdit stores RNAM/QNAM as `itU8` with `wbWeatherCloudSpeedToStr` (bias 127, display `(v-127)/1270`); the official corpus holds 62..216 with 127 dominant, so the old `int8_t` view turned every value above 127 negative. The raw byte is exposed; no bias is applied. Test: `Radicle/tests/test_climate_weather_authoring.cpp:ClimateWeatherReader.CloudSpeedsAreUnsignedBytesWithTheirStoredBias`.
- `Weather::tnam` is now `std::vector<std::optional<FormKey>>`; a NULL TNAM decodes as `nullopt` (xEdit `wbFormIDCk(TNAM, 'Static', [STAT, NULL])`). No official TNAM is NULL, so official decode results are unchanged.
- `decode_weather` accepts DALC of 24, 28 or 32 bytes (`wbAmbientColors` is `SetOptionalFrom(1)`: Specular and Fresnel Power are optional). The raw payload is kept; all 840 official DALC are 32 bytes. 20, 30 and 36 bytes are still refused.
- `decode_cloud_colors` accepts a 64-byte PNAM when the record form version is below 35 (`wbWeatherCloudColorsCounter` gives 4 layers before version 35, each layer is 4 times x 4 bytes before version 111). `decode_cloud_alphas` now compares bytes per layer (PNAM size / layer count against JNAM size / 32) instead of total size, which is identical for 512/512 and 1024/1024 and lets a 64-byte PNAM pair with a 512-byte JNAM (JNAM always has 32 layers). The lowest official form version is 40, so this path is unreachable in shipped data and covered only by `ClimateWeatherReader.FourLayerCloudColorsDecodeOnlyBeforeFormVersion35`.
- CLMT (`Radicle/src/environment/base_records.cpp:decode_climate`) already matches xEdit: WLST must be a multiple of 12, TNAM exactly 6, FNAM/GNAM single strings. MODL is not read by the decoder; it is reachable as `CLMT\Model`.
- `Radicle/src/environment/base_records.cpp:decode_weather` (the environment and C ABI view) is unchanged: it does not decode RNAM, QNAM, TNAM or DALC.

## Registered fields (`Radicle/schema/fallout4/fields.json`, records CLMT and WTHR)

All paths are `existing_only`, `presentation: identity`; f32 bounds are +-1e9 like the other registered records.

- CLMT: `CLMT\SunriseBegin`, `SunriseEnd`, `SunsetBegin`, `SunsetEnd`, `Volatility`, `MoonsPhaseLength` (u8 0..5 of TNAM, `payload_size` 6); `CLMT\WeatherChance` (WLST, `stride` 12, offset 4, i32); `CLMT\SunTexture` (FNAM), `CLMT\SunGlareTexture` (GNAM), `CLMT\Model` (MODL) strings; FormLinks `CLMT\Weather` (WLST stride 12 offset 0, WTHR, nullable) and `CLMT\WeatherGlobal` (offset 8, GLOB, nullable). xEdit declares WLST `wbArrayS` (sorted on Weather); the official Fallout4.esm `DefaultClimate` (0000015F) stores its eight entries unsorted, so Radicle writes in place and never re-sorts.
- WTHR scalars: `MaxCloudLayers` (LNAM u32, `payload_size` 4); `CloudSpeedY`/`CloudSpeedX` (RNAM/QNAM u8, `stride` 1, `payload_size` 32, element = cloud layer); `CloudColor<Time><Red|Green|Blue>` (PNAM u8, `stride` 32, `payload_size` 1024, element = layer, time t at t*4); `CloudAlpha<Time>` (JNAM f32, `stride` 32, `payload_size` 1024); `WeatherColor<Time><Red|Green|Blue>` (NAM0 u8, `stride` 32, `payload_size` 608, element = colour group in `wbWeatherColors` order: 0 Sky-Upper, 1 Fog Near, 2 Unused, 3 Ambient, 4 Sunlight, 5 Sun, 6 Stars, 7 Sky-Lower, 8 Horizon, 9 Effect Lighting, 10 Cloud LOD Diffuse, 11 Cloud LOD Ambient, 12 Fog Far, 13 Sky Statics, 14 Water Multiplier, 15 Sun Glare, 16 Moon Glare, 17 Fog Near High, 18 Fog Far High); `UnknownNAM4` (NAM4 f32, `stride` 4, `payload_size` 128); 18 `Fog*` f32 at FNAM 0..68; 18 DATA u8 (`WindSpeed` 0, `TransitionDelta` 3, `SunGlare` 4, `SunDamage` 5, `PrecipitationBeginFadeIn` 6, `PrecipitationEndFadeOut` 7, `LightningBeginFadeIn` 8, `LightningEndFadeOut` 9, `LightningFrequency` 10, `Flags` 11, `LightningColorRed/Green/Blue` 12-14, `VisualEffectBegin` 15, `VisualEffectEnd` 16, `WindDirection` 17, `WindDirectionRange` 18, `WindTurbulence` 19); `DisabledCloudLayers` (NAM1 u32, `payload_size` 4); `SoundType` (SNAM u32 at 4, `repeated`, `payload_size` 8, 0..3 from the xEdit enum); 22 DALC fields (`repeated`, occurrence = time of day: `Ambient<XPlus..ZMinus><Red|Green|Blue>` at d*4+c, `AmbientSpecular<Red|Green|Blue>` at 24-26, `AmbientFresnelPower` f32 at 28); `LightningThreshold` (UNAM f32 at 4), `ActivateThreshold` (UNAM f32 at 12); `VolatilityMult` (VNAM) and `VisibilityMult` (WNAM) f32 `payload_size` 4. Time names follow `wbWeatherTimeOfDay`: Sunrise, Day, Sunset, Night, EarlySunrise, LateSunrise, EarlySunset, LateSunset.
- WTHR strings: `CloudTexture0` .. `CloudTexture31` (00TX .. O0TX, the 32 `wbWeatherCloudTextures` signatures) and `AuroraModel` (MODL).
- WTHR FormLinks: `Precipitation` (MNAM, SPGD), `VisualEffect` (NNAM, RFCT), `Sound` (SNAM offset 0, `repeated`, SNDR or SOUN), `SkyStatic` (TNAM, `repeated`, STAT), `ImageSpace` (IMSP, `stride` 4, IMGS), `GodRays` (WGDR, `stride` 4, GDRY), `LightningSpell` (UNAM 0, SPEL), `ActivateSpell` (UNAM 8, SPEL), all nullable as in xEdit; `SunGlareLensFlare` (GNAM, LENS) is not nullable (`[LENS]`). IMSP and WGDR use `stride` 4 so the element index is the time of day and a 16-byte IMSP simply has 4 elements: index 4..7 is NOT_FOUND, never a misfit throw. Edits through these links (and the existing `Radicle/src/forms/weather.cpp:set_weather_mnam`, which also inserts masters) are remapped by `Radicle/src/schema/form_links.cpp:visit_link_offsets`, and `Radicle/src/graph/check_errors.cpp:slot_targets` now validates their target types after save.
- Size rules. `payload_size` gates the fixed arrays whose layout depends on form version: JNAM/PNAM at 1024 (version 111+), NAM0 at 608 (version 119+), NAM4 128, RNAM/QNAM 32, LNAM, NAM1, VNAM, WNAM 4, SNAM 8, TNAM 6. A 512-byte JNAM/PNAM, a 272- or 544-byte NAM0 or any other size makes those fields absent (NOT_FOUND, no byte change). FNAM (32/56/72), DATA (19/20), DALC (24/28/32) and UNAM (12/24) keep xEdit's prefix semantics instead: no `payload_size`, a field that does not fit in the stored payload is absent (`DATA` 19 has no `WindTurbulence`, FNAM 32 has only the first eight floats, a 12-byte UNAM has no `ActivateThreshold`). The official corpus shows every one of these sizes coupled to its form version.

## WTHR DATA, FNAM and DALC minimum sizes

`Radicle/schema/fallout4/fields.json:WTHR\\WindSpeed` (every WTHR DATA row), `Radicle/schema/fallout4/fields.json:WTHR\\FogDayNear` (every WTHR FNAM row) and `Radicle/schema/fallout4/fields.json:WTHR\\AmbientXPlusRed` (every WTHR DALC row) carry `minimum_payload_size`, which the writer checks through `Radicle/src/schema/field_address.cpp:resolve_field_slots` before any byte changes. The generated copies in `Radicle/include/fo4/schema/generated_scalar_fields.inc` carry the same values; `Radicle/tools/schema_codegen.py` emits them from the JSON.
The gate is a minimum only (no upper bound exists), so the rows reject short subrecords and sizes between the xEdit sizes are still accepted.
- DATA: `source:xEdit Core/wbDefinitionsFO4.pas:13057 @93cc0bc5` (`wbStruct(DATA)`) lists 19 required bytes (offsets 0..18). Turbulence (offset 19) is `wbFromVersion(119)`, so the full size is 20. Rows on offsets 0..18 use minimum 19; the Turbulence row keeps its offset bound of 20.
- FNAM: `source:xEdit Core/wbDefinitionsCommon.pas:9521 @93cc0bc5` (`wbWeatherFogDistance`) lists 8 required floats (32 bytes). The 119 group (offsets 32..55) and the 120 group (56..71) are version-gated, so the full size is 72. Rows on offsets 0..28 use minimum 32; tail rows use their own offset plus 4.
- DALC: `source:xEdit Core/wbDefinitionsCommon.pas:6635 @93cc0bc5` (`wbAmbientColors`, `SetOptionalFrom(1)`) lists the six 4-byte directional colors (24 bytes) as required. Specular (24..27) and Fresnel Power (28..31) are optional, so sizes 24, 28 and 32 are defined. Directional rows use minimum 24; Specular keeps 28; Fresnel uses 32.
- A 21-byte DATA is not an xEdit size, but a minimum cannot refuse it; `Radicle/tests/test_climate_weather_authoring.cpp:WeatherSubrecordsBelowTheirXEditSizeRefuseScalarWrites` therefore uses an 18-byte DATA and a 31-byte FNAM.

## Authoring (`fields.json` `authoring` blocks)

- CLMT order: EDID, WLST, FNAM, GNAM, MODL, MODT, MODC, MODS, MODF, TNAM (xEdit; `wbGenericModel` for the model group). TNAM is required (`wbClimateTiming(...).SetRequired`). Defaults: TNAM `000000000003` (five u8 without a declared default, `Moons / Phase Length` `SetDefaultNativeValue(3)`); WLST twelve zero bytes (one zeroed `Weather Type` struct: NULL weather, chance 0, NULL global, both links allow NULL), FNAM and GNAM a single NUL (empty string, the W0 convention for optional strings). MODL is `coupled` (it owns MODT/MODC/MODS/MODF); MODT, MODC, MODS, MODF have no default.
- WTHR order: EDID, 00TX .. O0TX, LNAM, MNAM, NNAM, RNAM, QNAM, PNAM, JNAM, NAM0, NAM4, FNAM, DATA, NAM1, SNAM, TNAM, IMSP, WGDR, DALC, MODL, MODT, MODC, MODS, MODF, GNAM, UNAM, VNAM, WNAM. Required (minimum 1): LNAM, MNAM, NNAM, RNAM, QNAM, PNAM, JNAM, NAM0, FNAM, DATA, NAM1, IMSP, and DALC with minimum 4 and maximum 8. RNAM is not individually `SetRequired` in xEdit but sits in the required `Cloud Speeds` RStruct, every official record has it and the reader requires it.
- `coupled` on NAM4, WGDR, UNAM, VNAM, WNAM (and MODL): xEdit marks them required, but their presence follows the form version (official: NAM4 from 119, WGDR and UNAM from 120, VNAM/WNAM from 126, the VNAM/WNAM comment says "Form Version 126+") and the W0 rules cannot express a version gate. Minimum 0 keeps the six older official records valid, and `coupled` stops both removing them from a newer record and adding them to an older one.
- DALC is `maximum 8` without `uncounted`, so it is `not_authorable` for add and remove: the count must be 4 (before version 111) or 8 (from 111), one per time of day, and an added or removed DALC would shift every later time slot.
- SNAM and TNAM are `maximum 65535, uncounted`: xEdit declares `wbRArray('Sounds', wbStruct(SNAM ...))` and `wbRArrayS('Sky Statics', wbFormIDCk(TNAM ...))` with no count subrecord. Defaults are the zeroed struct (`0000000000000000`: NULL sound, type 0 `Default`) and `00000000` (NULL static); both link lists contain NULL. TNAM is `wbRArrayS` (sorted) but no official TNAM run is sorted (85 of 85 records), so append order is accepted; the official counts are 36 or 144 per record, which nothing in xEdit ties to a count.
- Cloud textures default to a single NUL. GNAM (`[LENS]`, no NULL) and MODT/MODC/MODS/MODF have no default (add is `no_default`); GNAM can be removed.
- No defaults are registered for the required WTHR entries. `create_record` for WTHR is refused with `no_default` (LNAM is the first required entry without one) and changes nothing. It could not succeed anyway: DALC is a required non-editable entry (`not_authorable`), PNAM/JNAM/NAM0/IMSP/DALC sizes depend on the new record's form version, and the IMSP default 359 (`SetDefaultNativeValue(359)`) is a raw Fallout4.esm form ID whose meaning depends on the plugin's master list. `create_record` for CLMT succeeds: EDID + TNAM default, then WLST and the strings can be added.
- `Radicle/tests/test_climate_weather_real.cpp:ClimateWeatherReal.EveryOfficialRecordIsListedOrderedAndPreflightClean`: every subrecord of all 11 official CLMT and 107 WTHR is listed, every record passes `preflight_authoring_record` (0 violators) and `preflight_record` reports no error for the new fields.

## Stays opaque

- PNAM/JNAM at 512 bytes (versions 35-110) and PNAM at 64 bytes (before 35), NAM0 at 272 and 544 bytes: no field is registered for those sizes because one path can carry only one `payload_size`. They are preserved and readable through `forms::Weather`, but not writable per field.
- DATA bytes 1-2, UNAM bytes 16-23, the colour pad bytes in PNAM/NAM0/DALC, MODT/MODC/MODS/MODF payloads, WLST add/remove of individual entries (W0 FormLink lists are u32-packed or per-subrecord; a 12-byte packed struct list is not supported, so entries can be edited and counted but not inserted or removed), and DALC count changes.

## Tests

- `Radicle/tests/test_climate_weather_authoring.cpp` (native fields, links, add/remove, create and refusal, reader fixes) with the fixture in `Radicle/tests/climate_weather_fixture.hpp`; `Radicle/tests/test_climate_weather_c_api.cpp:ClimateWeatherCApi` (status mapping, save, environment decode); `Radicle/bindings/csharp/Radicle.Interop.Tests/ClimateWeatherAuthoringTests.cs`.
- `Radicle/tests/test_climate_weather_real.cpp:ClimateWeatherReal.EveryScalarAndStringPathEditsFirstAndLastSlotsWithoutTouchingOtherBytes` edits the first and last slot of every registered scalar and string path on every official CLMT/WTHR and compares every subrecord after reparse against an independently patched copy (Fallout4.esm 78 records, 160 paths, 14440 edits; DLCCoast 19/151/3507; DLCworkshop02 3/141/655; DLCNukaWorld 18/150/3695). `LinksAndOptionalSubrecordsEditOfficialRecordsAndKeepEveryOtherRecordByteIdentical` sets links, adds and removes TNAM, adds SNAM with a sound and type, adds FNAM to the one CLMT without it and checks refusals; after save and reparse every other record is byte-identical (Fallout4.esm 78 records, 206 link edits, 71 subrecords added; DLCCoast 19/53/17; DLCworkshop02 3/9/3; DLCNukaWorld 18/53/17).

## Weather FormLink payload boundaries

`Radicle/src/plugin/field_writer.cpp:formlink_slots` and `Radicle/schema/fallout4/fields.json:WTHR.formlink_fields` must restrict edits to proven payload shapes. TES5Edit pin `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `wbDefinitionsCommon.pas:wbWeatherGodRays` (9574–9586), `wbWeatherImageSpaces` (9589–9609), `wbWeatherMagic` (9623–9636), and `wbWeatherSounds` (9639–9654) define WGDR as eight links, IMSP as four or eight links, UNAM as 12 or 24 bytes, and SNAM as eight bytes. `wbDefinitionsFO4.pas:WTHR` (13034–13108) defines MNAM/NNAM/GNAM and each TNAM as one four-byte link. A generic stride must not make a ninth weather-time slot editable in an unknown 36-byte IMSP/WGDR payload. `Radicle/tests/test_climate_weather_authoring.cpp:UnknownWeatherLinkShapesRefuseWithoutChangingBytes` exercises these refusals and preserves the original serialized bytes; existing link tests cover the accepted shapes.
