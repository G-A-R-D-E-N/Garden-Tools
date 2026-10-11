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
resolves all four time consumers and moon packing (local copy not committed). Sections 3.2 and 3.4 stamp
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
LNAM, NAM4 and unproven DATA/clock behavior stay raw; existing MNAM mutation
remains restricted to its previously verified link slot.

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
