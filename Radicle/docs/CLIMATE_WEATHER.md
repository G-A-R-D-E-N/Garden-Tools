# Climate timing and weather parameters (#343)

Layout evidence is pinned to TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`. This is compatibility evidence;
it does not establish runtime transition algorithms.

CLMT in `Core/wbDefinitionsFO4.pas:6115–6126` uses the shared timing helper
`Core/wbDefinitionsCommon.pas:8435–8452`. TNAM is exactly six bytes:
sunrise begin/end, sunset begin/end, volatility, and packed moons/phase length.
The timing callback `Core/wbDefinitionsCommon.pas:wbClmtTime` displays ordinary time values in
ten-minute steps. Radicle preserves all six encoded bytes and exposes the
verified conversion below without clamping out-of-range clock values. This differs from xEdit's
display, which subtracts 143 while a value is above 143 (`Core/wbDefinitionsCommon.pas:3349-3359`,
`wbClmtTime`, at `93cc0bc5`). The 2026-10-11 scan of the seven official masters found no CLMT TNAM
with a sunrise or sunset byte above 143 (11 records), so the two agree on all official data.

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
| 19 | optional wind turbulence, u8; present only when the form version is 119 or later (xEdit `wbFromVersion(119, 'Wind Turbulance')`, `Core/wbDefinitionsFO4.pas:13086` @93cc0bc5) |

The existing measured corpus in `WTHR_SEMANTIC_PROOF.md` has 19/20-byte DATA
variants and 107 WTHR records. Decode accepts the length its form version selects
(19 before 119, 20 from 119) and preserves every stored value. Other lengths and
duplicate TNAM/DATA fail closed. No
weather scaled float values, flag enum names, transition duration formula or clock
wrap behavior are asserted from schema annotations alone.

The refreshed installed-data sweep covers 107 WTHR and 11 CLMT versions:
Fallout4.esm 71/7, DLCCoast.esm 16/3, DLCworkshop02.esm 3/0,
DLCNukaWorld.esm 17/1, and zero of either type in the other three masters.
Six DATA blocks are 19 bytes and 101 are 20 bytes. All seven unchanged
masters serialize byte-identically after both decoders are exercised.

The private [verified timing record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/weather-climate-397-343/README.md)
resolves all four time consumers and moon packing (local copy not committed). Sections 3.2 and 3.4 stamp
the OG loader and consumers at 0x3989B0, 0x658090 and 0x648A50.
Each unsigned clock byte is multiplied by 1/6 to obtain game hours, including
128 through 255 without signed conversion or clamping. The low six moon bits
hold days per phase; bit 6 enables Secunda and bit 7 enables Masser.
`ClimateTiming::moon_phase` returns `(days passed % (days per phase * 8)) / days per phase`
(0 to 7), or no phase when days per phase is 0, so the phase is left unchanged.
Raw bytes remain available.

Byte 4 (volatility) is the weather hold time, verified from `Sky::UpdateWeather` (0x64E530) in OG 1.10.163.
The override is released when elapsed time is greater than the hold time, a re-roll happens at the same
point, and a switch happens when the random value is greater than elapsed / hold time. The hold time is
`max(0.1, (max - min) * (255 - x) / 255 + min)` with `x = clamp(volatility * multiplier, 0, 255)`; the
compiled defaults for min and max are 1.0 and 23.0 game hours (GMSTs fWeatherDurationMin and
fWeatherDurationMax; RE-010 on #520 comment 13142, static analysis of OG 1.10.163). The AE 1.11.240
Fallout4.esm sets neither. `ClimateTiming::volatility_hold_hours`
takes the multiplier and bounds as caller-supplied inputs. The field names of the Sky fields are inferred
from their use. The CLMT TNAM byte 4 reader at 0x656270 is the only consumer of that byte in the Sky and
TESClimate code (RE-010 on #520 comment 13142, OG 1.10.163); it is not proven image-wide, and three loads
are unclassified.

The WTHR DATA transition byte (byte 3, read unsigned) gives `transition_hours` = byte / 255 * (max - min) + min
game hours. The bounds are the game settings `fWeatherTransMin` and `fWeatherTransMax`. Compiled OG defaults
(RE-010 on #520 comment 13142, release OG 1.10.163) are fWeatherTransMax 1.0, fWeatherTransMin 0.01 and
fWeatherTransAccel 4.0. The AE 1.11.240 Fallout4.esm overrides fWeatherTransMax to 3.0 and fWeatherTransAccel
to 16.0; it does not set fWeatherTransMin. Every matching GMST overwrites the setting in load order, so the
last one wins. `transition_hours` takes both bounds as inputs and reads no GMST. The routing from a plugin
GMST value to the transition code is verified: the tag table gives
GMST type 3, the DATA reader writes the value the transition code reads, and the transition code reads
fWeatherTransMax, Min and Accel at 0x64E964, 0x64E970/87 and 0x64E9C3. The shipped OG Fallout4.esm values
are not verified, because no 1.10.163 Fallout4.esm is on disk.

Accelerated transition: when the Sky accelerate flag (Sky+0x394 bit 3, 0x8) is set, the transition percentage
is `pct = (pct - base) * (fWeatherTransAccel + 1.0) + base`, recomputed each frame and not compounding. That is
5x with the compiled defaults and 17x with the AE override. The Papyrus `Weather.SetActive(abOverride, abAccelerate)`
second argument sets the flag, and the console `SetWeather` can also. `transition_hours` does not model this
multiplier (RE-010 on #520 comment 13142).

Sunrise and sunset windows are stored as-is. The timing classifier (RE-010 on #520 comment 13142, OG 1.10.163)
opens the sunrise window only if sunrise begin < t < sunrise end, day covers sunrise end <= t <= sunset begin,
the sunset window opens only if sunset begin < t < sunset end, and anything else is night. None of the timing
helpers checks ordering, so a reversed or zero-width window never opens: its time is classified as plain day or
night with fraction 1.0. Sunrise begin is floored at 0 and sunset end is capped at 23.99 using
fDaytimeColorExtension (compiled 0.5); raw sunrise end and sunset begin are not clamped, and a sunset begin of
24 hours or more means sunset never opens. Radicle does not reorder or clamp the stored window. A window whose
end minus begin is 24 hours divides by zero in 0x647CE0 and produces a NaN vector; `preflight_record` reports it
as `climate_sun_window_full_day`. Reversed and zero-width windows (end at or before begin) are reported as `climate_sun_window_never_opens`.

NG 1.10.984 was byte-checked on the Steamless-unpacked image for the transition formula (0x668990), the
CLMT timing-byte readers (0x662DB0, 0x671A40, 0x673280), and the phase routine; they are byte-identical to
the AE functions and the OG defaults match. The NG volatility consumer is FUN_1406704e0 with the same
constants. The engine reads weatherData unsigned (movzx); CommonLib declares it as `int8_t`. The RE-010 facts
above are OG 1.10.163 static analysis; NG was not checked for them. Evidence: `RE-Work/subsystems/radicle-re-garden-tools/weather-climate-397-343/` (`findings.jsonl`), Fallout4_Runtime_Research branch `re/garden-tools-issues-2026-10-04`, summarized as RE-010 on #520.

Still open (#397 follow-ups and #343 comment 12950): the WTHR DATA flag byte (byte 11), including which bits
the runtime tests; whether WTHR DATA bytes 1-2 are read anywhere; whether 0x656270 is the only consumer of
CLMT byte 4 across the image (three loads unclassified); the shipped OG Fallout4.esm GMST values;
the low-priority LNAM 4-byte payload, NAM4 float[32] indices and NAM0 color-group mapping. Live in-engine
observation of the override-release time, the transition length at DATA[3] = 0 and 255, and the effect of the
game-setting values is not done. The live simulation is separate acceptance work.

`src/environment/base_records.cpp:decode_climate` and `decode_weather` expose
these optional fields through the existing environment decoders. C ABI 25
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
These dated probes have not been rerun against this ABI-25 head; current-head
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
| WTHR PNAM, JNAM cloud colors and alphas | `Weather::cloud_colors`, `cloud_alphas` | `CloudColor<Time><RGB>`, `CloudAlpha<Time>` by layer, 1024-byte payloads only | required | refused | refused | none |
| WTHR NAM0 weather colors | `decode_weather` colors, `Weather.Color` | `WeatherColor<Time><RGB>` by color group, 608-byte payload only | required | refused | refused | none |
| WTHR NAM4 | raw | `UnknownNAM4` index 0..31 (128 bytes) | refused (coupled) | refused (coupled) | refused | none |
| WTHR FNAM fog | `fog_distances` | 18 `Fog*` floats, prefix semantics (32/56/72) | required | refused | refused | none |
| WTHR DATA parameters | `Weather.Parameters` | 18 u8 fields, `WindTurbulence` only on 20-byte DATA | required | refused | refused | none |
| WTHR MNAM, NNAM, GNAM | `forms::Weather` | `Precipitation` (SPGD), `VisualEffect` (RFCT) nullable; `SunGlareLensFlare` (LENS, not nullable) | MNAM/NNAM required; GNAM `no_default` | GNAM yes | refused | yes |
| WTHR SNAM sounds | `Weather::snam` | `Sound` (SNDR or SOUN, nullable), `SoundType` (0..3) by occurrence | yes, zeroed (NULL, Default) | yes | refused | yes |
| WTHR TNAM sky statics | `Weather::tnam` (NULL allowed) | `SkyStatic` (STAT, nullable) by occurrence | yes, NULL | yes | refused | yes |
| WTHR IMSP, WGDR | `Weather::imsp`, `wgdr` | `ImageSpace` (IMGS), `GodRays` (GDRY) by time of day, nullable; 16-byte IMSP has 4 elements | IMSP required; WGDR refused (coupled) | refused | refused | yes |
| WTHR DALC ambient colors | `Weather::dalc` (24/28/32 bytes) | 22 `Ambient*` fields by occurrence; specular/fresnel only when stored | refused (count 4 or 8 by version) | refused | refused | none |
| WTHR UNAM magic | `Weather::unam` | `LightningSpell`, `ActivateSpell` (SPEL, nullable), `LightningThreshold`; `ActivateThreshold` on 24-byte UNAM only | refused (coupled) | refused (coupled) | refused | yes |
| WTHR cloud textures 00TX .. O0TX | `Weather::cloud_textures` | `CloudTexture0` .. `CloudTexture31` | yes, empty string | yes | refused | none |
| WTHR aurora model MODL | `Weather::modl` | `AuroraModel` | refused (coupled) | refused (coupled) | refused | none |
| WTHR records | n/a | n/a | n/a | n/a | refused (`no_default`; DALC count and version-sized tables) | n/a |

Stays opaque: PNAM/JNAM at 512 bytes, PNAM at 64 bytes and NAM0 at 272 or 544 bytes (readable, not field-writable),
DATA bytes 1-2, UNAM bytes 16-23, color pad bytes, MODT/MODC/MODS/MODF, insertion or removal of single
WLST entries, and DALC count changes. All official CLMT and WTHR records are listed and ordered in the
authoring schema (0 violators). Field list, size rules, defaults provenance and refusal reasons are in
`docs/code-notes/radicle-climate-writer.md`.
