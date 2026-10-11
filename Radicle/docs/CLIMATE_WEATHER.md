# Climate timing and weather parameters (#343)

Layout evidence is pinned to TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`. This is compatibility evidence;
it does not establish runtime transition algorithms.

CLMT in `Core/wbDefinitionsFO4.pas:6115–6126` uses the shared timing helper
`Core/wbDefinitionsCommon.pas:8435–8452`. TNAM is exactly six bytes:
sunrise begin/end, sunset begin/end, volatility, and packed moons/phase length.
The timing callback `Core/wbDefinitionsCommon.pas:wbClmtTime` displays ordinary time values in
ten-minute steps. Radicle preserves all six encoded bytes and exposes the
verified conversion below without clamping out-of-range clock values.

WTHR DATA is defined at FO4 lines 13057–13087. Lightning color is three bytes
by `Core/wbDefinitionsCommon.pas:9612–9621`.

| Offset | Stored value |
|---:|---|
| 0 | wind speed, u8 |
| 1–2 | unused bytes, kept in the original container |
| 3 | transition delta, u8 |
| 4 / 5 | sun glare / damage, u8 |
| 6 / 7 | precipitation begin fade-in / end fade-out, u8 |
| 8 / 9 / 10 | lightning begin fade-in / end fade-out / frequency, u8 |
| 11 | raw flags, u8 |
| 12–14 | lightning RGB, u8 each |
| 15 / 16 | visual-effect begin / end, u8 |
| 17 / 18 | wind direction / direction range, u8 |
| 19 | optional wind turbulence, u8; decoded by payload length, with the schema's version-119 annotation as compatibility evidence only |

The existing measured corpus in `WTHR_SEMANTIC_PROOF.md` has 19/20-byte DATA
variants and 107 WTHR records. Decode accepts those two lengths and preserves
every stored value. Other lengths and duplicate TNAM/DATA fail closed. No
weather scaled float values, flag enum names, transition duration formula or clock
wrap behavior are asserted from schema annotations alone.

The refreshed installed-data sweep covers 107 WTHR and 11 CLMT versions:
Fallout4.esm 71/7, DLCCoast.esm 16/3, DLCworkshop02.esm 3/0,
DLCNukaWorld.esm 17/1, and zero of either type in the other three masters.
Six DATA blocks are 19 bytes and 101 are 20 bytes. All seven unchanged
masters serialize byte-identically after both decoders are exercised.

The private [verified timing record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/weather-climate-397-343/README.md)
and local copy `weather-climate-397-343/README.md` (not committed, same note as the link above)
resolve all four time consumers and moon packing. Sections 3.2 and 3.4 stamp
the OG loader and consumers at 0x3989B0, 0x658090 and 0x648A50.
Each unsigned clock byte is multiplied by 1/6 to obtain game hours, including
128 through 255 without signed conversion or clamping. The low six moon bits
hold days per phase; bit 6 enables Secunda and bit 7 enables Masser.
Raw bytes remain available. Section 3.5 identifies the transition settings as
`fWeatherTransMin` and `fWeatherTransMax`, with compiled defaults 0.01 and 1.0
game hours, from stamped instruction and setting bytes. The GMST override
mechanism, exceptional timing-window behavior and live simulation remain
partial or unverified in sections 5–6. They are outside these stored timing
conversions; no transition-duration calculation is added.

`src/environment/base_records.cpp:decode_climate` and `decode_weather` expose
these optional fields through the existing environment decoders. C ABI 12
extends `radicle_climate` and `radicle_weather_info`; generated C# bindings and
the friendly `Climate.Timing` / `Weather.Parameters` properties follow those
declarations after integrating main `c50ad4d5405e1571113019654b10e3d4a05ca0f1`
(ABI 11). Existing exports are preserved; the climate/weather containers gain
their new fields. Re-bump from main if another ABI PR merges first.

`include/fo4/forms/weather.hpp:Weather` is the stable read-only name for the
existing lossless WTHR model. `weather_concept.hpp` remains an include shim.
LNAM, NAM4 and unproven DATA/clock behavior stay raw in the decoded views; the
typed `set_weather_mnam` (with master insertion) is unchanged, and per-field
editing of the stored bytes goes through the schema paths in Editing below.

GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` consumes climate
via `opencw/Services/CellService.cs`, currently choosing a weather by chance
and reading day colors/fog. A conversion probe must use the extended C ABI
and these raw timing/parameter values without Mutagen. Live simulation and
runtime transition behavior are separate acceptance work.

The supplied `probes/gardenengine/ClimateProbe.cs` and `climate_probe.tscn`
were copied into `opencw/tools/` in an isolated GardenEngine checkout at that SHA.
Build `opencw/OpenCommonwealth.csproj` with `RadicleSourceDir` set to this
Radicle checkout. Set `FO4_DATA_DIR` to installed data and `RADICLE_NATIVE_PATH`
to the built library of the matching ABI, then run the double-precision Mono editor
with `--headless --path opencw tools/climate_probe.tscn`.

On 2026-10-04 the actual GardenEngine build succeeded (82 existing warnings,
zero errors). Godot `4.8.dev.mono.double.custom_build.2e64cc835` printed
`climate_probe PASS` and exited 0 after converting Commonwealth's existing
sky payload plus its decoded timing/parameters. No Mutagen lookup is used
in that path. With game data unset it printed `SKIP` and exited 77.
These are field/consumer conversion checks, not live transition/rendering proof.

The ABI-15 follow-up probe was rerun on 2026-10-05 in an isolated GardenEngine
checkout at `676d2c1eabeceefedf53dc249cf63ee9a7f73740`. Its project built with
zero errors and 83 warnings. Godot
`4.8.dev-double.mono.double.custom_build.0b0af89c9` printed `climate_probe PASS`
and exited zero: Commonwealth timing converted to 5/9/17/21 hours, four days
per phase, Secunda enabled and Masser disabled. The existing sky conversion
still used the C ABI path. This remains conversion proof, not simulation.
These dated probes have not been rerun against this ABI-12 head; current-head
consumer acceptance is separate GardenEngine work.

## Editing

All edits go through the W0 schema surfaces: `radicle_plugin_set_record_scalar_at` / `PluginFile.SetScalar`,
`radicle_plugin_set_record_string` / `SetString`, `radicle_plugin_set_record_formlink` / `SetFormLink`,
`radicle_plugin_record_field_count` / `FieldCount`, `radicle_plugin_add_subrecord` / `AddSubrecord`,
`radicle_plugin_remove_subrecord` / `RemoveSubrecord` and `radicle_plugin_create_record` / `CreateRecord`.
VALIDATE means the value is range-checked on write and the subrecord is checked by `preflight_record`; links
are also type-checked on write (local targets) and by the error check after save.

| Family | READ | WRITE | ADD | REMOVE | CREATE | REMAP |
| --- | --- | --- | --- | --- | --- | --- |
| CLMT TNAM timing (6 u8) | `decode_climate`, `radicle_climate`, `Climate.Timing` | `CLMT\SunriseBegin` .. `MoonsPhaseLength` (6-byte TNAM only), or `SetClimateTiming` | required, always present | refused (required) | default `00 00 00 00 00 03` | none |
| CLMT WLST entries (stride 12) | `Climate.Weathers` | `CLMT\Weather` (WTHR, nullable), `CLMT\WeatherChance` (i32), `CLMT\WeatherGlobal` (GLOB, nullable) by entry index; `FieldCount` | WLST with one zeroed entry; entries cannot be inserted | WLST as a whole; single entries cannot be removed | optional | weather and global links |
| CLMT FNAM, GNAM, MODL strings | sun textures; MODL not decoded | `CLMT\SunTexture`, `SunGlareTexture`, `Model` | FNAM, GNAM empty string; MODL refused (coupled) | FNAM, GNAM yes; MODL refused | optional | none |
| CLMT records | n/a | n/a | n/a | n/a | `CreateRecord("CLMT")`: EDID + TNAM | n/a |
| WTHR LNAM, NAM1, VNAM, WNAM | `forms::Weather` raw | `MaxCloudLayers`, `DisabledCloudLayers`, `VolatilityMult`, `VisibilityMult` (4-byte payloads) | VNAM, WNAM refused (version-gated, coupled) | LNAM, NAM1 refused (required); VNAM, WNAM refused | refused | none |
| WTHR RNAM, QNAM cloud speeds | `Weather::cloud_speed_y/x` (stored u8) | `CloudSpeedY`, `CloudSpeedX` index 0..31 (32-byte payload) | required | refused | refused | none |
| WTHR PNAM, JNAM cloud colours and alphas | `Weather::cloud_colors`, `cloud_alphas` | `CloudColor<Time><RGB>`, `CloudAlpha<Time>` by layer, 1024-byte payloads only | required | refused | refused | none |
| WTHR NAM0 weather colours | `decode_weather` colours, `Weather.Color` | `WeatherColor<Time><RGB>` by colour group, 608-byte payload only | required | refused | refused | none |
| WTHR NAM4 | raw | `UnknownNAM4` index 0..31 (128 bytes) | refused (coupled) | refused (coupled) | refused | none |
| WTHR FNAM fog | `fog_distances` | 18 `Fog*` floats, prefix semantics (32/56/72) | required | refused | refused | none |
| WTHR DATA parameters | `Weather.Parameters` | 18 u8 fields, `WindTurbulence` only on 20-byte DATA | required | refused | refused | none |
| WTHR MNAM, NNAM, GNAM | `forms::Weather` | `Precipitation` (SPGD), `VisualEffect` (RFCT) nullable; `SunGlareLensFlare` (LENS, not nullable) | MNAM/NNAM required; GNAM `no_default` | GNAM yes | refused | yes |
| WTHR SNAM sounds | `Weather::snam` | `Sound` (SNDR or SOUN, nullable), `SoundType` (0..3) by occurrence | yes, zeroed (NULL, Default) | yes | refused | yes |
| WTHR TNAM sky statics | `Weather::tnam` (NULL allowed) | `SkyStatic` (STAT, nullable) by occurrence | yes, NULL | yes | refused | yes |
| WTHR IMSP, WGDR | `Weather::imsp`, `wgdr` | `ImageSpace` (IMGS), `GodRays` (GDRY) by time of day, nullable; 16-byte IMSP has 4 elements | IMSP required; WGDR refused (coupled) | refused | refused | yes |
| WTHR DALC ambient colours | `Weather::dalc` (24/28/32 bytes) | 22 `Ambient*` fields by occurrence; specular/fresnel only when stored | refused (count 4 or 8 by version) | refused | refused | none |
| WTHR UNAM magic | `Weather::unam` | `LightningSpell`, `ActivateSpell` (SPEL, nullable), `LightningThreshold`; `ActivateThreshold` on 24-byte UNAM only | refused (coupled) | refused (coupled) | refused | yes |
| WTHR cloud textures 00TX .. O0TX | `Weather::cloud_textures` | `CloudTexture0` .. `CloudTexture31` | yes, empty string | yes | refused | none |
| WTHR aurora model MODL | `Weather::modl` | `AuroraModel` | refused (coupled) | refused (coupled) | refused | none |
| WTHR records | n/a | n/a | n/a | n/a | refused (`no_default`; DALC count and version-sized tables) | n/a |

Stays opaque: PNAM/JNAM at 512 bytes, PNAM at 64 bytes and NAM0 at 272 or 544 bytes (readable, not field-writable),
DATA bytes 1-2, UNAM bytes 16-23, colour pad bytes, MODT/MODC/MODS/MODF, insertion or removal of single
WLST entries, and DALC count changes. All official CLMT and WTHR records are listed and ordered in the
authoring schema (0 violators). Field list, size rules, defaults provenance and refusal reasons are in
`docs/code-notes/radicle-climate-writer.md`.
