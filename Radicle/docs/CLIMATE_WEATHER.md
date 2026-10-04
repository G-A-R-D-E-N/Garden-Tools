# Climate timing and weather parameters (#343)

Layout evidence is pinned to TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`. This is compatibility evidence;
it does not establish runtime transition algorithms.

CLMT in `Core/wbDefinitionsFO4.pas:5855–5865` uses the shared timing helper
`Core/wbDefinitionsCommon.pas:7909–7925`. TNAM is exactly six bytes:
sunrise begin/end, sunset begin/end, volatility, and packed moons/phase length.
The timing callback at FO4 lines 3117–3126 displays ordinary time values in
ten-minute steps. Radicle exposes the encoded bytes without normalizing
out-of-range clock values or interpreting the packed moon byte.

WTHR DATA is defined at FO4 lines 12609–12639. Lightning color is three bytes
by `Core/wbDefinitionsCommon.pas:9021–9029`.

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
| 19 | optional wind turbulence, u8; schema adds this at form version 119 |

The existing measured corpus in `WTHR_SEMANTIC_PROOF.md` has 19/20-byte DATA
variants and 107 WTHR records. Decode accepts those two lengths and preserves
every stored value. Other lengths and duplicate TNAM/DATA fail closed. No
scaled float values, flag enum names, transition duration formula or clock
wrap behavior are asserted from schema annotations alone.

The refreshed installed-data sweep covers 107 WTHR and 11 CLMT versions:
Fallout4.esm 71/7, DLCCoast.esm 16/3, DLCworkshop02.esm 3/0,
DLCNukaWorld.esm 17/1, and zero of either type in the other three masters.
Six DATA blocks are 19 bytes and 101 are 20 bytes. All seven unchanged
masters serialize byte-identically after both decoders are exercised.

The existing private, decompile-verified record
`E:/GardenEngine/git/garden-tools/git/gt-stack/git/F4RR/RE-Work/Runtime/Organized/1.10.163/README-Weather.md`
records `Sky::UpdateWeather` at RVA `0x64E530`. It confirms transition delta
at DATA byte 3 and interpolation between two engine duration constants using
the stored byte divided by 255; the two duration bounds remain unnamed there.
This supports exposing the transition byte, not inventing those bounds.
The [runtime evidence index, section a-343](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a/RE-Work/subsystems/plugin-io-garden-tools/evidence-index-garden-tools/README.md#a-343)
was read through the authenticated browser on 2026-10-04. Its stamped local copy
is `E:/GardenEngine/git/garden-tools/git/radicle-343-20261004/build/RE-Work/evidence-index-garden-tools.md`.
For OG 1.10.163 it records the TNAM six-byte read in `TESClimate::Load`
(RVA `0x3989B0`) and sunrise-begin consumers at RVAs `0x658090` / `0x648A50`:
the first byte is divided by six to obtain game hours. This proves ten-minute
units for sunrise begin only. The other three timing consumers remain partial
and moon packing is not found. Radicle exposes all six stored bytes and does
not generalize the first consumer's proof into unverified runtime behavior.
The remaining runtime interpretation is tracked in
[#397](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/397).

`src/environment/base_records.cpp:decode_climate` and `decode_weather` expose
these optional fields through the existing environment decoders. C ABI 8
extends `radicle_climate` and `radicle_weather_info`; generated C# bindings and
the friendly `Climate.Timing` / `Weather.Parameters` properties follow those
declarations. Main was checked at ABI 4 and PR #383 reserves 5/6/7 immediately
before this extension. Recheck/rebase before merging either ABI series.

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
were copied into `opencw/tools/` in the isolated GardenEngine checkout
`E:/GardenEngine/GardenEngine/git/radicle-343-probe-20261004` at that SHA.
Build `opencw/OpenCommonwealth.csproj` with `RadicleSourceDir` set to this
Radicle checkout. Set `FO4_DATA_DIR` to installed data and `RADICLE_NATIVE_PATH`
to the built ABI-8 library, then run the matching double-precision Mono editor
with `--headless --path opencw tools/climate_probe.tscn`.

On 2026-10-04 the actual GardenEngine build succeeded (82 existing warnings,
zero errors). Godot `4.8.dev.mono.double.custom_build.2e64cc835` printed
`climate_probe PASS` and exited 0 after converting Commonwealth's existing
sky payload plus its decoded timing/parameters. No Mutagen lookup is used
in that path. With game data unset it printed `SKIP` and exited 77.
These are field/consumer conversion checks, not live transition/rendering proof.
