# Weather system (`Sky`/`TESWeather`/`TESClimate`) — decompile-verified

Formalizes and extends findings that previously existed only as prose in `Projects\Rad Mod\docs\
CHANGELOG.md` (a live debugging session's notes, not run through this workspace's formal
decompile-and-record pipeline) plus closes real gaps CommonLibF4 has never covered: `Clouds`,
`Precipitation`, `Atmosphere`, `Stars`, `SkySound`, and `TESRegionDataWeather` have RTTI/VTABLE IDs
reserved in CommonLibF4 but **no header at all** — every field on those classes is still raw-offset
territory. This is round 1 (20 functions); the raw 1.10.163 dump has ~95 more `Sky::`/`Clouds::`/
`Precipitation::`/`Atmosphere::`/`Stars::`/`Sun::`/`Moon::`/`SkySound::` functions never touched.

## Confirmed: `Sky::UpdateWeather` (RVA `0x64E530`) — the real weather-selection + transition-percent tick

The actual per-tick weather state machine. Key confirmed mechanics:

- **Weather source cascade**: `TESObjectCELL::GetSkyRegion()` → falls back to `PlayerRegionState`'s
  cached region → if a region resolves, `TESRegion::SelectWeather()`; if not, `TESWeatherList::
  GetRandomWeather()` on the climate's global weather list; if THAT returns null,
  `TESForm::GetFormByNumericID(0x15E)` — a hardcoded fallback weather FormID `0x15E`.
- **Mid-transition re-roll damping**: when the region/weather-list's chosen weather changes while
  already transitioning, the engine does NOT always jump immediately — it rolls
  `BSRandom::Float0To1()` against `elapsed/duration` and only switches the transition target if the
  roll favors it, otherwise keeps riding out the current transition. This is deliberate anti-churn
  smoothing, not a bug.
- **The exact `currentWeatherPct` formula**, previously only described qualitatively:
  ```
  currentWeatherPct = ((currentGameHour + adj) - lastWeatherUpdate)
                       / (weatherData[kTransDelta]/255.0 * (transMax - transMin) + transMin)
  ```
  `weatherData[kTransDelta]` is the CK-authored per-weather "Transition" byte (`TESWeather+0xA6F`,
  confirmed matches `WeatherData::kTransDelta = 0x3`, i.e. `weatherData[3]`). `transMin`/`transMax`
  are two unnamed engine float constants (not yet named) that bound how many in-game hours a
  transition takes at byte value 0 vs 255. **This is the formula `RadEngine_Weather.cpp`'s
  `BeginSmoothWeather()` approximates by hand** — worth revisiting to replicate exactly rather than
  approximately, using the real byte-to-duration scaling.
- A CK "always-transition-in" style flag exists — a `weatherData[kFlags]` bit `0x40` (i.e.
  `TESWeather+0xA77`, `weatherData[0xB]` — the same byte `WeatherDataFlags::kRainy=0x4` lives in,
  different bit) gates whether `Precipitation::ToggleRainOcclusion()` fires; not yet named precisely,
  candidate: a rain-occlusion-shadow-map opt-in flag.
- On a genuine weather change, `Precipitation::FlushAll()` and `Clouds::ClearTransTextures()` are
  called — confirms precipitation/cloud transition state is explicitly reset, not just re-derived.
- **Save-load interaction**: a `PlayerRegionState`-adjacent save flag (`pSaveLoadGame+0x360` bit 1)
  suppresses the normal `lastWeatherUpdate = currentGameHour` reset — weather transitions correctly
  resume their exact progress across a save/load rather than restarting.

## Confirmed: `Sky::ForceWeather` (RVA `0x651BE0`) — hard-snap confirmed, not a transition

Decompiled in full: calls `ResetWeather()`, sets `currentWeather` directly, sets EITHER
`overrideWeather` or `defaultWeather` depending on the bool param (never both), clears `lastWeather`
entirely, and **unconditionally hard-sets `currentWeatherPct = 1.0`** — both `currentGameHour` and
`lastWeatherUpdate` are set to the exact same value (so elapsed time = 0, nothing to transition
through even if something recomputed the percent later). Confirms the CHANGELOG.md claim exactly:
**there is no gradual-transition code path in `ForceWeather` at all, regardless of the boolean
argument** — any mod wanting a smooth transition must NOT call this function; use whatever sets
`overrideWeather`/`defaultWeather` without the hard `currentWeatherPct` reset (`Sky::UpdateWeather`'s
own natural cascade), matching `RadEngine_Weather.cpp`'s existing `BeginSmoothWeather()` approach of
setting the fields directly rather than calling `ForceWeather`.

## Confirmed: `Sky::UpdateFog` (RVA `0x64F950`) — exact `fogData[0..17]` offset map, and TWO other fog sources

The complete, byte-exact index map (supersedes the CHANGELOG.md's approximate description — this adds
the previously-missing Clamp indices):

| `fogData[]` index | `TESWeather` offset | Meaning |
|---|---|---|
| 0 / 2 | `+0xA80` / `+0xA88` | Near plane, Day / Night |
| 1 / 3 | `+0xA84` / `+0xA8C` | Far plane, Day / Night |
| 4 / 5 | `+0xA90` / `+0xA94` | Power, Day / Night |
| 6 / 7 | `+0xA98` / `+0xA9C` | Clamp, Day / Night *(not in the earlier informal notes)* |
| 8 / 10 | `+0xAA0` / `+0xAA8` | Height-Mid (near), Day / Night |
| 9 / 11 | `+0xAA4` / `+0xAAC` | Height-Range (near), Day / Night |
| 12 / 13 | `+0xAB0` / `+0xAB4` | High-Density-Scale, Day / Night |
| 14 / 16 | `+0xAB8` / `+0xAC0` | Height-Mid (far), Day / Night |
| 15 / 17 | `+0xABC` / `+0xAC4` | Height-Range (far), Day / Night |

**Fog does NOT always come from the weather record.** `Sky::UpdateFog` picks from three genuinely
different sources depending on context, in this priority order:
1. **Indoors with a room lighting template**: `BGSLightingTemplate::GetDataFogNearPlane/FarPlane/
   HeightMid/HeightRange/FarHeightMid/FarHeightRange/Power/Clamp/HighDensityScale` — a completely
   separate interior lighting-template fog block, not weather at all.
2. **Outdoors in a cell with its own fog override**: same field set, but via `TESObjectCELL::
   GetDataFog*` — a per-cell override that beats the weather's own values.
3. **Normal exterior, no override**: the weather-record path above, with a **smooth day/night blend**
   computed from `GetBeginSunriseColors`/`GetSunriseEnd`/`GetSunsetBegin`/`GetEndSunsetColors` (not a
   hard cutoff — a real ramped blend factor across dawn/dusk windows), and, if a transition is active
   (`lastWeather` non-null), a SECOND blend layer between old and new weather's fog values using
   `currentWeatherPct` as the interpolation factor.
4. **Terrain far-plane override**: if `BGSTerrainManager::bEnabled` is false, the far fog plane is
   further overridden by a horizon-distance query on the world root, blended against a constant
   (`DAT_14384e948`) and clamped — a fallback for when the terrain LOD system isn't managing draw
   distance itself.

`TESWeather::GetFogData` (RVA `0x658300`) is confirmed as the plain accessor into this same
`fogData[18]` array — no additional logic.

## Confirmed: `Sky::UpdateSound` (RVA `0x650510`) — the `ThunderFreq==0` div-by-zero (real CTD root cause)

Already root-caused informally in Rad Mod's CHANGELOG (a real "Dust Storm CTD/pink screen" bug hunt);
now formally recorded. The thunder branch computes `BSRandom() % (weatherData[kThunderFreq] *
SkySound::ucThunderCount)` — if a weather record's `ThunderFreq` field (`weatherData[0xA]`,
`WeatherData::kThunderFreq`) is `0`, this is an integer divide-by-zero, `EXCEPTION_INT_DIVIDE_BY_ZERO`
at `Sky::UpdateSound+0x407` (`div r8d`). **Any custom weather record with a Thunder sound assigned
MUST have a nonzero ThunderFreq**, full stop — this is a hard engine crash, not a soft failure.

## Confirmed: `TESClimate::GetRandomWeather` (RVA `0x658470`) — the weighted weather roll, with a dynamic-chance mechanic

Walks the climate's `WeatherEntry` linked list (`TESClimate+0x50`, matches `TESClimate.h`'s
`weatherList` field) doing a weighted random pick — sums each entry's effective chance, rolls
`BSRandom::UnsignedInt(0xFFFFFFFF) % totalChance`, walks the accumulated-range buckets to land on one
entry. **Previously undocumented mechanic**: each `WeatherEntry`'s effective chance is NOT always its
fixed CK-authored `chance` field — if `chanceVar` (a `TESGlobal*`) is set, the chance is instead read
LIVE from that global variable's float value (cast to int) every single roll. This means weather odds
for a specific climate entry can be dynamically adjusted at runtime via a global variable, without
touching the climate record itself — a real, verified mechanic worth knowing for any mod that wants
to bias weather odds (set the right global instead of trying to edit `chance` at runtime).

## Confirmed: `Clouds::SetBaseTexturesFromWeather` (RVA `0x646710`) — the real cloud-layer texture pipeline

Iterates up to `numCloudLayers` (the runtime `Clouds` object's own layer count, `Clouds+0x510`), and
for each layer checks the weather's `cloudLayerDisabledBits` (`TESWeather+0xA68`, one bit per layer —
matches the CommonLibF4 field exactly). If the layer is **enabled**, reads its texture path (a
`BSFixedString`, confirmed 16-byte stride starting at `TESWeather+0x28` = `cloudTexture[32]`, one
`BSFixedString` per slot) and loads it via `BSShaderManager::GetTexture(path, ...)`, ref-counted into
the runtime layer's texture slot. If **disabled**, clears the runtime slot instead. This is the real,
confirmed mechanism behind `cloudLayerDisabledBits` — a mod that wants to selectively hide cloud
layers on a weather record can flip this bitfield rather than editing texture paths.

## Confirmed: `Precipitation::CreatePrecip` (RVA `0x649B20`) — directly explains Rad Mod's failed dusty-precipitation attempt

**This is the real precipitation particle-geometry pipeline**, and it confirms Rad Mod's own
diagnosis in `docs\Rad Mod\PROGRESS.md` was correct: the failure was a **material/shader problem**,
not a fundamentally broken integration path.

```c
particleCount = precipData->sub->count;          // BGSShaderParticleGeometryData+0x20, +0x50
particleSize  = precipData->sub->size;            // +0x58, defaults to 1.0 if zero
BGSShaderParticleGeometryData::CreateShaderGeometry(
    precipData, windSpeedConst * sky->windSpeedField /*Sky+0x31c*/, sky->someWindField /*Sky+0x320*/,
    &outGeometry);
if (_spWeatherRoot) attach(outGeometry, _spWeatherRoot);   // vtable slot 0x1D0, likely AttachChild
```

Confirms: (1) precipitation genuinely IS driven through the standard `BGSShaderParticleGeometryData`
(SPGD) → `CreateShaderGeometry` pipeline — the same CK record type Rad Mod's attempt used; (2) wind
data from the live `Sky` object (two float fields near `Sky+0x31C`/`Sky+0x320`, not yet named — not
in the CommonLibF4-documented field list) feeds directly into particle creation, so precipitation
genuinely reacts to wind; (3) the resulting geometry attaches to a global `_spWeatherRoot` scene node.
**Since this pipeline is confirmed real and functional, the magenta/missing-texture/freeze Rad Mod hit
is very likely the specific `.BGEM` material assigned to the SPGD record** (their own notes already
suspected `CameraAttachWeatherParticlesDusty.BGEM` was the wrong asset — a camera-attach effect
material, not a real precipitation particle material) — **worth retrying with a correct
precipitation-class BGEM rather than avoiding SPGD.**

## Confirmed: `SkySound` — the thunder-sound scheduling that explains why the `ThunderFreq` crash was so reliable

- **`SkySound::SkySound`** (ctor, RVA `0x64BE90`): trivial field layout — `BSSoundHandle` (0x00),
  `TESWeather*` (0x08), `BGSSoundDescriptorForm*` (0x10), `SoundType` (0x18), a zeroed `uint` at
  `0x1C` (the next-recheck timestamp `HandleCheckTime` manages).
- **`SkySound::Play`** (RVA `0x64BF20`): skips entirely while a loading menu is open. Lazily acquires
  a sound handle via `BSAudioManager::GetSoundHandle` if not yet valid. **Critical finding**: normal
  ambient weather sounds only get re-evaluated once per second (`HandleCheckTime(this, 1000)` — a
  1000ms throttle), but **`SoundType == 3` sounds bypass the throttle and are evaluated on every
  single call**, unconditionally. Given the earlier-found `ThunderFreq` div-by-zero lives in the
  thunder-specific code path, this strongly suggests **`SoundType::kThunder` (or equivalent) is value
  `3`**, and explains why the `ThunderFreq==0` crash was so immediate and reliable in practice — it's
  not an occasional once-a-second roll, it's evaluated continuously.
- **`SkySound::HandleCheckTime`** (RVA `0x64BEC0`): the actual throttle — compares against
  `GetTickCount64()` with an XOR-masked timestamp (top bit reserved as a sign/valid flag), returns
  true and reschedules only when the interval has elapsed.

## Confirmed: `TESWeather::GetWeatherData` — the general decode rule for EVERY byte in `weatherData[20]`

```c
float TESWeather::GetWeatherData(WeatherData index, float rangeMax, float rangeMin) {
    return (weatherData[index] / 255.0f) * (rangeMax - rangeMin) + rangeMin;
}
```

This generalizes round 1's `kTransDelta` finding into the real, general rule: **every byte in
`weatherData[20]` is a normalized `0-255` value, not a physical unit** — the caller supplies the
real-world min/max range and this function does the LERP. `weatherData[kTransDelta]` (round 1's
transition-duration formula) is just one caller of this general accessor, not a special case. Any
future work reading `weatherData[]` fields directly (as raw bytes, the way `RadEngine_Internal.h`
already correctly does for `kFlags`/`kThunderFreq`) should keep in mind that CK-visible values (wind
speed, wind turbulence, etc.) are whatever range the CALLER chose when converting from the byte — the
byte itself carries no unit information on its own.

## Confirmed: `TESWeather::GetCloudColor` — real per-layer color indexing + fallback defaults

Confirms `cloudColorData[32][8]` (`TESWeather+0x268`, matches CommonLibF4) is indexed
`layer*8 + colorTime`, gated by `numCloudLayers` (`+0xDF0`). **If a weather has zero cloud layers
(or the requested layer index is out of range), it falls back to hardcoded default colors** rather
than returning black/garbage — real, previously-undocumented constants: Sunrise (`ColorTime==1`)
`0x9B9B9B`, Sunset (`==3`) `0x1C140F`, Day (`==0`) `0x5B4B46`, Night (else) `0x6C5953`.

## Confirmed: `TESWeather::QPrecipitationData` — trivial, but closes the precipitation-field loop

`TESWeather+0xFB8` → `BGSShaderParticleGeometryData*`, exactly `TESWeather.h`'s already-exposed
`precipitationData` field. Combined with round 2's `Precipitation::CreatePrecip` finding, this
confirms the full, already-CommonLibF4-accessible chain: `weather->precipitationData` (real field,
no raw offset needed) → `Precipitation::CreatePrecip` → `BGSShaderParticleGeometryData::
CreateShaderGeometry`. Rad Mod's own approach (reading `TESWeather::Precipitation` directly) was
structurally correct from the start — reinforces that their failure was the material asset, not the
data path.

## Confirmed: `Sky::Update` (RVA `0x64DA60`) — the real per-frame call order, and a second responsibility

Never writes `currentWeatherPct` (`this+0x360`) directly anywhere in its own 449-line body — it only
calls `UpdateWeather` (which does), confirming the CHANGELOG.md claim exactly. **The real per-frame
sub-update call order**, confirmed by grepping the decompiled body in sequence:
```
UpdateWeather -> UpdateFog -> UpdateWind -> UpdateSound -> UpdateAurora -> ... -> UpdateEffects
```
Also confirmed: `Sky::Update` owns a SECOND, largely unrelated responsibility beyond weather — a
sizable chunk of the function (before the weather sub-calls) manages **interior room imagespace-LUT
crossfading** (`BSMultiBoundRoom`/`TESObjectCELL::GetRefMultiboundMap`/`ImageSpaceLUTData` — blending
the color-grading LUT as the camera crosses between interior multibound rooms). Not itself examined
in depth this pass, but worth knowing `Sky` isn't purely a weather class — it's also the interior
lighting-transition owner.

## Confirmed: the full region→climate weather-selection chain, and how overlapping regions blend

Closes the loop on the cascade `Sky::UpdateWeather` walks (round 1's "weather source cascade"):

- **`TESRegion::SelectWeather`** — trivial dispatcher: looks up the region's `TESRegionData` entry of
  type `3` via `TESRegionDataList::Find(list, 3)` (confirms `3` is the weather region-data-type ID),
  then forwards straight to `TESWeatherList::GetRandomWeather` on that entry's embedded weather list.
- **`TESWeatherList::GetRandomWeather`** — the SAME weighted-random-with-live-global-override
  algorithm as round 1's `TESClimate::GetRandomWeather` (identical structure: sum effective chances,
  where each entry's chance is its fixed value UNLESS a `TESGlobal*` override is set, then roll and
  walk accumulated buckets). Confirms this is a genuinely shared, reused selection algorithm applied
  at both the region level (`TESWeatherList`) and the climate level (`TESClimate`'s own list) — region
  overrides climate, both use identical weighted-roll logic including the dynamic-chance-global
  mechanic.
- **`TESRegionDataWeather::BlendInto`** (RVA `0x290B10`) — **previously totally undocumented**: how
  two overlapping regions' weather data actually combine. Guards on the other region-data's type being
  exactly `3` and a nonzero blend-strength argument. If the two regions have sufficiently different
  priority bytes, the higher-priority one's weather list simply wins outright
  (`TESWeatherList::CopyComponent`, full replace). If priorities are close, it computes a genuine
  **blended priority value** via a smoothstep-like quadratic formula —
  `((100-a)*b + a² + (100-b)*a + b²) * 0.005` — and calls `TESRegionData::SetPriority` with the
  result, meaning overlapping regions don't just pick a winner, they can produce an intermediate
  priority that affects how future blends resolve. This is real, substantive region-overlap behavior
  that was completely unknown before this pass.

## Confirmed: `Sky::UpdateWind` (RVA `0x650190`) — the full wind pipeline, and the `weatherData[]` wind indices

Closes the loop on round 2's unnamed `Sky+0x31C`/`Sky+0x320` wind fields from `Precipitation::
CreatePrecip`. Confirmed index map (all decoded via the general `GetWeatherData` 0-255 rule):

| `weatherData[]` index | Feeds | Drives |
|---|---|---|
| `[0]` (`kWindSpeed`) | `Sky+0x31C`, weather-transition-blended like fog | `BSShaderManager::fWindMagnitude` — a real, global shader-visible wind magnitude |
| `[0x13]` (`kWindTurbulence`) | `Sky+0x324`, same blend pattern | `BSShaderManager::fWindFrequency`/`fWindMinSpeed`/`fWindMaxSpeed` — turbulence controls gust frequency AND the speed variance range, not just magnitude |
| `[0x11]` | wind-direction base angle | `BSShaderManager::fWindAngle` |
| `[0x12]` | wind-direction variance | same, via a jittered-direction algorithm: samples 4 random offsets within the variance range and keeps whichever lands closest to the base angle |

All four use the exact same weather-transition blend pattern already confirmed for fog
(`Sky::UpdateFog`): current-weather value and target-weather value interpolated by `currentWeatherPct`
when a transition is active. Wind is a first-class weather-transition property, not a snap-to-new-value
on weather change.

## Correction: `UpdateColorsFromTemplate`/`UpdateFogFromTemplate` are interior ROOM crossfade, not weather-transition blending

These were queued assuming they were the "purple sky" weather-template-inheritance bug class from
Rad Mod's CHANGELOG.md history. **Decompiled and that assumption was wrong** — both gate on
`Sky+0x38 > 0.0`, the SAME interior-room-crossfade timer `Sky::Update` manages (the
`BSMultiBoundRoom`/`ImageSpaceLUTData` logic noted above), and both call `GetCurrentRoomLightingTemplate`/
`GetPreviousRoomLightingTemplate` — `BGSLightingTemplate` (interior room lighting), not `TESWeather`.
**These are the interior half of the same blend pattern**: `Sky::UpdateFog`'s indoor branch (round 1)
already showed `GetCurrentRoomLightingTemplate`/`GetPreviousRoomLightingTemplate` calls feeding
directly into fog values when indoors; `UpdateColorsFromTemplate`/`UpdateFogFromTemplate` are what
computes the CROSSFADE between the previous and current room's template when moving between rooms
with different lighting templates, using the same `(this+0x38)` blend factor and a room-transition
timer that eases in via a smoothstep-style curve (`Sky::Update`'s `(4 - 2x)*x²` shape). **The real
"purple sky" weather-to-weather bug (per the existing CHANGELOG history) is a different mechanism
entirely** — `TESWeather::Aurora`/imagespace-slot inheritance, not this room-lighting-template system.
Filing this correction so a future pass doesn't repeat the same wrong assumption.

## Confirmed: `Precipitation::QRainDirection`/`QRainDistance`/`QRainOcclusion` — global static state, setters not yet traced

All three are trivial static accessors returning module-global state, not per-instance data:
`QRainDirection()` returns `&kRainDirection` (a static `NiPoint3`), `QRainDistance()` returns
`fCurrentCubeSize * 0.5`, `QRainOcclusion()` returns the `bRainOcclusion` flag (already known from
round 1's toggle in `Sky::UpdateWeather`). **Confirms rain direction/distance are genuinely global,
not per-weather** — but this pass didn't trace WHERE `kRainDirection`/`fCurrentCubeSize` get written
(almost certainly inside `Precipitation::Update`/`ComputeProjection`, both recorded but not yet
examined in depth) — so whether rain direction is actually driven by `Sky::UpdateWind`'s
`fWindAngle` remains an open question, not confirmed either way.

## Confirmed: `TESRegionDataWeather::LoadRegionDataWeather` — thin thunk

Just forwards to `TESWeatherList::LoadWeatherListChunk(list, file, region)` — the real ESP chunk
(RDWT) parsing lives on `TESWeatherList`, not `TESRegionDataWeather` itself. Not itself examined.

## Confirmed: `Sky::UpdateEffects` (RVA `0x655280`) — weather camera VE spawn/despawn thresholds and magic-spell cleanup

Sharpens the CHANGELOG.md claim ("VE instantiated once, only re-created when its slot is null") with
the actual precise mechanics:

- **Two effect slots, not one**: `Sky+0x420` (current/incoming `ReferenceEffect*`) and `Sky+0x428`
  (outgoing, fading-out effect). On a weather-just-changed flag, the current effect is DEMOTED to
  outgoing (not destroyed) so it can fade rather than cut.
- **Weather-attached magic effects get cleaned up explicitly**: `TESWeather+0xDB8` (matches
  `weatherSpellData[2]`) is a `MagicItem*` — if the player is a spell-target of the OLD weather's
  spell but not the new one, `TESObjectREFR::QuickRelease`; if newly a target of the outgoing
  weather's spell, `MagicTarget::Dispel`. Confirms radiation-storm-style weather DoT spells are
  swapped cleanly during transitions, not left dangling.
- **New camera effects don't spawn instantly at transition start** — they wait until
  `currentWeatherPct >= weatherData[0xF]/255` (a dedicated CK-authored threshold byte, distinct from
  `kTransDelta`), then call `ReferenceEffectController::InstantiateHitEffects`. The OUTGOING effect
  is torn down once `currentWeatherPct >= weatherData[0x10]/255` **of the previous weather's own
  threshold byte**. Two independent, per-weather-record thresholds control exactly when during a
  transition the visual effect appears/disappears — not a hardcoded fraction.
- Both effect slots respond to a global `EffectsEnabled` toggle (passed inverted into a vtable call
  on each) — confirms a real, callable kill-switch for weather camera effects exists.

## Confirmed: `Sky::UpdateAurora` (RVA `0x650BF0`) — identity-checked crossfade + a new `weatherData[kFlags]` bit

Aurora (`TESWeather.aurora`, `+0xF78`) cross-fades between the current and last weather using
`currentWeatherPct` — UNLESS both weathers reference the **same** aurora object (checked via a
vtable identity call), in which case there's no fade at all (blend factor pinned to 1.0) — switching
between two weathers that share an aurora doesn't interrupt it. Also gated by a **previously
unrecorded `weatherData[kFlags]` bit, `0x10`** (`TESWeather+0xA77`, same byte `kRainy=0x4` and the
round-1 rain-occlusion `0x40` bit live in) — when clear, a separate time-of-day-windowed opacity ramp
applies using four Sky-side runtime bounds (`Sky+0x410/0x414/0x418/0x41C`, not yet traced to their
source). `weatherData[kFlags]` now has three confirmed bits: `0x4`=`kRainy`, `0x10`=aurora
time-window gate, `0x40`=rain-occlusion.

## Confirmed: `Sky::SkipWeatherTransition` (RVA `0xEF5C80`) — trivial flag setter

Sets bit `0x1000000` on `Sky+0x394`. A real, callable "snap the current transition to completion"
primitive — not itself examined for where that flag gets consumed (almost certainly read inside
`Sky::UpdateWeather`'s percent computation, forcing an early pct=1), but confirmed real and simple
enough to call directly if a mod wants to skip an in-progress transition without `ForceWeather`'s
full state reset.

## Confirmed: `Sky::SetWeatherExternal` (RVA `0x651A60`) — the real public smooth-transition API

This is the actual "set weather without hard-snapping" entry point — the counterpart to
`ForceWeather`'s instant snap, and a strong candidate for Rad Mod's `BeginSmoothWeather()` to call
directly instead of hand-replicating field writes:

```c
void Sky::SetWeatherExternal(Sky*, TESWeather* weather, bool isOverride, bool allowSmoothRestart)
```

- **Bootstrap case**: if `Sky+0x48` (`currentWeather`) is null (no weather set at all yet), falls
  straight through to `ForceWeather` — there's nothing to transition *from*.
- **Otherwise**: writes `weather` into `Sky+0x60` (`overrideWeather`) if `isOverride`, else into
  `Sky+0x58` (`defaultWeather`) — same override/default distinction `ForceWeather` uses. The
  `defaultWeather` path additionally clears `PlayerRegionState`'s cached region and `Sky+0x68` (the
  cached region pointer `UpdateWeather`'s cascade reads) — forces a fresh region→climate→weather
  resolve on the next tick rather than reusing a stale cached region.
- **If `allowSmoothRestart` is false**: returns immediately after the field write. Nothing else
  happens — the *next* `Sky::UpdateWeather` tick picks up the new `currentWeather`/`lastWeather`
  cascade naturally and starts an ordinary transition.
- **If `allowSmoothRestart` is true and a `lastWeather` is already set** (i.e. a transition is
  already in progress): sets bit `0x8` on `Sky+0x394` and stashes the *current* `currentWeatherPct`
  into `Sky+0x390`. **This closes the loop on a mechanic round 1 found but couldn't fully explain**:
  `Sky::UpdateWeather` was seen reading `(Sky+0x394 & 8)` to gate a branch that recomputes
  `currentWeatherPct = (currentWeatherPct - Sky+0x390) * factor + Sky+0x390`. `Sky+0x390` is
  therefore a **transition-restart anchor point** — when a new weather change interrupts an
  already-in-progress transition, this lets the new transition resume smoothly from wherever the
  interrupted one was, instead of jarring back to 0%. If no `lastWeather` exists (transition already
  finished), the bit and anchor are cleared instead (`& ~8`, anchor zeroed).

**Practical takeaway**: `Sky::SetWeatherExternal(weather, isOverride, true)` at RVA `0x651A60` is very
likely the correct, engine-native way to trigger a smooth weather change externally — a strong
candidate to call directly (via a `REL::Relocation<>` thunk) rather than replicating the
override/default-field + region-cache-clear + restart-anchor logic by hand.

## Confirmed: `Sky::UpdateHDRValues` (RVA `0x651D00`) — per-frame HDR/imagespace-LUT blend

Builds `TESWeather::kImageSpaceBaseData` (the global HDR/cinematic/tint/DOF accumulator consumed by
the post-process pipeline) and up to 4 weighted `TESWeather::kImageSpaceLUTData` slots each frame:

- **Frozen-override fast path**: if `Sky+0x36C` (the same "override state" field `GetFogValuesFromTemplate`
  and `UpdateHDRValues` both gate on) is `0`, it instead copies HDR/cinematic/tint/DOF data directly
  from `TESForm::GetFormByNumericID(0x167)` (a fixed default `TESImageSpace` form) into
  `kImageSpaceBaseData` and returns — bypassing all weather-driven blending. `0x36C` values `1`/`2`
  skip the function entirely (early-out at the top). This is a real "force default imagespace,
  ignore weather" override state — not yet traced to what sets it (console command / debug menu is
  the likely source given the pattern).
- **Normal path**: computes a day/night blend factor from the same
  `GetBeginSunriseColors`/`GetSunriseEnd`/`GetSunsetBegin`/`GetEndSunsetColors` sunrise/sunset window
  functions used elsewhere (fog, stars), then calls a shared time-window helper
  (`_GetTimes___...ColorTime_TESWeather...`) to get up to 2 `TESImageSpace` indices + weights for the
  *current* weather (day and/or night, whichever the current game hour falls into/near). Each is
  weighted by `currentWeatherPct` (`Sky+0x360`) and accumulated into `kImageSpaceBaseData` via
  `ImageSpaceBaseData::WeightedAdd`; the resolved LUT texture (`TESImageSpace::GetImagespaceLUTTexture`)
  is stored into the corresponding `kImageSpaceLUTData` slot.
- If `lastWeather` exists and a transition is in progress (`currentWeatherPct < 1.0`), repeats the
  same day/night lookup against `lastWeather` and blends in the *remaining* `(1 - currentWeatherPct)`
  weight — so up to 4 total weighted imagespace sources (current-day, current-night, last-day,
  last-night) can be active mid-transition. Any unused slots (fewer than 4 sources resolved) are
  cleared to `BSFixedString::QEmptyString()`.
- **Solar lens flare swap**: if `currentWeather` has non-null data at `+0xFA8` (a `BSLensFlareRenderData*`
  offset, `+0x20` from that pointer), and it differs from the currently-active
  `BSImagespaceShaderLensFlare::pSolarLensFlare_DO`, the old one's textures are cleared and the new
  one's loaded — confirms weather records can carry per-weather solar lens flare data, swapped
  cleanly on weather change exactly like the aurora/spell cleanup patterns already documented.

## Confirmed: `Sky::GetFogValuesFromTemplate` (RVA `0x653A20`) — the exact fog-source fallback chain

This is the shared helper both `UpdateColorsFromTemplate`/`UpdateFogFromTemplate` (interior room
crossfade, see the Correction section above) call into, and it nails down the **exact priority order**
for the "three fog sources" round 1 identified only loosely:

1. Explicit `BGSLightingTemplate*` argument, if passed non-null → read its 9
   `BGSLightingTemplate::GetDataFog*` accessors directly (Near/Far Plane, HeightMid/Range near+far,
   Power, Clamp, HighDensityScale).
2. Else, if `TES+0x58` (current cell) is set → same 9 values from `TESObjectCELL::GetDataFog*`
   (per-cell fog override).
3. Else, if `Sky+0x28` (Sky's own cached room lighting template) is set → same 9 `BGSLightingTemplate`
   accessors.
4. Else (no template anywhere) → hardcoded fallback pulled straight off the live `Sky` instance's own
   `fogData[]` fields (`Sky+0x328` Near, `+0x32C` Far, `+0x338` HeightMid, `+0x33C` HeightRange,
   `+0x340` HeightMid-far, `+0x344` HeightRange-far, `+0x34C` Power, `+0x350` Clamp, `+0x354`
   HighDensityScale) — i.e. "use whatever the weather-driven fog cascade already computed."
5. Regardless of source, a sanity clamp runs after: Far Plane is reset to `163840.0` if `<= 0` or
   `> 163840.0`; Near Plane is reset to `FarPlane * 0.17` if invalid (`<=0`) or `>= FarPlane`. Confirms
   `163840.0` (`10 * 16384`, a round power-of-two-ish worldspace-unit bound) is the engine's hardcoded
   absolute fog-far ceiling, independent of any authored record data.

## Confirmed: `TESClimate::GetWeatherOfType` (RVA `0x658560`) — classification-bit lookup, same dynamic-chance pattern

`GetWeatherOfType(uint bitIndex)` walks the climate's weather list and returns the single weather
whose `weatherData[kFlags]` byte (`TESWeather+0xA77`) has bit `bitIndex` set AND has the **lowest**
priority value among matches — where priority is either a fixed int field or, if a `TESGlobal*`
override is present on that list entry, the **live global's current value** (identical
dynamic-chance-via-global mechanic already found in `GetRandomWeather`/`TESRegionDataWeather`'s
weighted-random pickers). This is very likely the function behind script-level "force rainy/snowy
weather of this climate" calls — `bitIndex` selects by classification bit (e.g. `kRainy=0x2` matches
the `weatherData[kFlags]` bit layout — 3 bits confirmed so far: `0x4 kRainy`, `0x10` aurora gate,
`0x40` rain-occlusion; `GetWeatherOfType`'s bit space may be a different/wider enum than the raw
`kFlags` byte, not yet cross-checked bit-for-bit).

## Confirmed: `Clouds::Update` (RVA `0x645E20`) — per-layer cloud-scroll accumulation, closes the gap CommonLibF4 has zero coverage of

The real per-frame cloud animation driver, for up to 32 active cloud layers (`Clouds+0x510` = active
layer count):

- **Correction from the initial pass**: the two 32-byte `TESWeather` arrays this reads
  (`+0x228+layer` and `+0x248+layer`) are NOT a day/night pair — re-reading the exact pointer
  arithmetic on the final write (`pCloudTexCoordOffset[layer].x` vs `.y`, stride-8 vec2, `.x` at
  `+0` / `.y` at `+4`) shows both bytes are read from the SAME weather (current, then separately from
  `lastWeather`) every tick, with **no time-of-day input at all** in this function. They are two
  **independent per-layer scroll-speed axes**: `+0x248+layer` feeds the `.x` texcoord-offset
  component, `+0x228+layer` feeds `.y` (this direction is fairly confident from the write order, but
  the two arrays' exact CK-authored field names — `cloudLayerSpeedX`/`cloudLayerSpeedY` are the
  natural guess — are not independently confirmed). Each byte is normalized via the general
  `weatherData`-style 0-255 formula into a **symmetric** range `[-DAT_143718b08, +DAT_143718b08]`
  (a single global max-scroll-speed constant, not a per-weather field) — so a byte value of `0`
  scrolls at the full negative rate, `255` at the full positive rate, `~128` near zero.
- Blends the current-weather axis value against `lastWeather`'s equivalent axis value using
  `currentWeatherPct` (linear interpolation) — a single-tier blend (weather-transition only), simpler
  than fog/wind/imagespace's day-then-night-then-transition three-tier blend.
- The resulting per-layer speed is scaled by delta-time and **accumulated** (added, not set) into
  `BSSkyShader::pCloudTexCoordOffset[layer]` — literally the UV-scroll offset consumed by the cloud
  shader each frame. This is the whole "clouds drift across the sky" mechanism.
- Also re-triggers `Clouds::SetBaseTexturesFromWeather` (full texture reload, already documented above)
  whenever `Sky+0x394` bits `0`/`1` are set (a "weather just changed" flag pair, distinct from the
  `0x8` restart-anchor bit found in `SetWeatherExternal`) or an internal per-Clouds dirty flag
  (`Clouds+0x512`) is set — confirming cloud layer textures are lazily reloaded only on actual weather
  change, not every frame.

## Confirmed: `Atmosphere::Update` (RVA `0x644BD0`) — the shader-constant pushdown, cross-checks the fogData map

Pushes already-blended `Sky` state into the render pipeline each frame — genuinely closes CommonLibF4's
zero coverage of `Atmosphere`, and **independently cross-confirms** several previously-documented field
offsets by using them the same way:

- Sets the active `BSFogProperty` on `BSShaderManager` slot 0 from `Atmosphere+0x18` if it differs from
  what's currently bound (lazy rebind, not per-frame).
- Pushes `Sky+0x108/0x10C/0x110`, `Sky+0xFC/0x100/0x104`, and `Sky+0xA8/0xAC/0xB0` (three RGB color
  triples — sky color, and two others not yet individually named) into `BSSkyShader` shader-constant
  globals, gated on the same `Sky+0x36C` override-state check (`0x36C - 2 < 2`, i.e. values 2 or 3)
  seen in `UpdateHDRValues`/`GetFogValuesFromTemplate`.
- Sets a fog-enable bit on the `BSFogProperty` object based on `Sky+0x32C` (fogData Far) vs `Sky+0x328`
  (fogData Near) — **independently confirms the `fogData[0]`=Near/`fogData[1]`=Far index mapping**
  documented earlier from `Sky::UpdateFog` alone.
- Pushes a long list of further `Sky` fields (`+0x330/0x334` wind-adjacent, `+0x33C/0x340` fog
  height-mid/range, `+0x344/0x348/0x34C/0x350/0x354` fog power/clamp/high-density/far-height, `+0x138/
  0x13C/0x140`, `+0x174/0x178/0x17C/0x180/0x184/0x188`, `+0xB4/0xB8/0xBC`, `+0x304/0x308/0x30C`) into
  a block of module-level shader-constant globals (`DAT_146541f04..f5c`) — a "current frame's fully
  resolved atmosphere state" staging buffer read by the render thread. A `bOverrideFogDistance` global
  toggle can substitute the Near/Far pair with values cached on the `Atmosphere` object itself instead
  of the live `Sky` fog fields — a debug/console fog-distance-lock switch.
- Adjusts the sky-dome camera's far clip plane (`NiCamera::SetViewFrustum`) to match the current world
  root's far plane, when the world scene isn't flagged "complex" (`Main::iSceneIsComplex`) — keeps the
  sky dome from clipping against dynamic far-plane changes.

## Confirmed: `Stars::Update` (RVA `0x658FD0`) — star-field opacity fade + streamed-resource lifecycle

- **Resource streaming**: manages a ref-counted `hStars` star-mesh resource handle with an async-load
  check (`(handle_flags & 0x70000000) + 0xD0000000) & 0xEFFFFFFF) == 0`, a "fully loaded" bit-pattern
  test) — swaps the pending handle into the live one once ready, decrementing/releasing the old one,
  and toggles a visibility bit (`+0x108` on the resolved node, bit `0x20`) based on whether a resource
  is bound at all. Confirms star rendering is backed by a real streamed model resource, not a static
  scene node.
- **Opacity fade**: once loaded, computes star-field alpha (`Stars+0x18`) from the *midpoints* of the
  sunset/sunrise color-transition windows (`(sunsetEndColors - (sunsetEndColors-sunsetBegin)*0.5)` and
  the sunrise equivalent) — a narrower, independently-computed fade window than the day/night boundary
  logic `TESWeather::GetImageSpace` uses elsewhere. Alpha is `0` in full daylight, ramps to `1.0`
  through the sunset/sunrise midpoint windows, full `1.0` at night. Additionally forced to `0` if the
  current weather's color (`Sky+0xF0/0xF4/0xF8`, an RGB triple — likely the "sky color at zenith" or
  overcast-tint field) is pure black — an overcast/heavily-clouded weather can suppress stars entirely
  regardless of time of day.

## Confirmed: `Precipitation::Update` (RVA `0x649480`) — precipitation intensity fade IS wired directly to water ripples

A genuinely new cross-system connection, not previously documented anywhere in Rad Mod's own history:

- Reads **two previously-unlabeled `weatherData[]` bytes**, index `6` (`TESWeather+0xA72`, current
  weather) and index `7` (`+0xA73`, last weather) — tentatively "precipitation density/intensity"
  fields (distinct from the already-named `kWindSpeed=0`/`kTransDelta=3`/`kThunderFreq=0xA`/
  `kFlags=0xB`/`kWindTurbulence=0x13`; indices 6/7 are new and not yet cross-checked against a CK
  field-name label). Their exact CK-authored name is unrecovered, but their role here (driving both
  particle-system precipitation intensity AND water ripple strength) is unambiguous from the call
  pattern.
- Determines whether current/last weather each have "real" precipitation by checking their
  `precipitationData` (`+0xFB8`, the `BGSShaderParticleGeometryData*` already confirmed in
  `TESWeather::QPrecipitationData`) resolves to a loaded shader-geometry object.
  When either does, it calls **`TESWaterSystem::ToggleWaterRipples(bool enable, float intensity)`** —
  **confirms rain-driven water ripples are a real, direct engine feature gated by the same
  weather-transition percentage** (`Sky+0x360`) as every other blended weather field, not a separate
  hardcoded rain check. This is the mechanism behind vanilla rain visibly rippling puddles/water
  bodies, and confirms `TESWaterSystem::ToggleWaterRipples` as a callable, verified API surface if a
  mod wanted to drive water ripples independently of weather (e.g. a custom storm system).
- Also manages `BGSShaderParticleGeometryData` resource ref-counting and root-attach/detach against
  `_spWeatherRoot` — the same particle root `Precipitation::CreatePrecip` (already documented) creates
  geometry under — confirming `Update` and `CreatePrecip` share the same weather-particle-root
  lifecycle, useful context for the Rad Mod dusty-precipitation retry.

## Confirmed: `TESRegionDataWeather::GetRandomWeather` / `UsesWeather` (RVA `0x28C1B0` / `0x28C470`)

Both close out `TESRegionDataWeather`'s core selection surface (alongside the already-documented
`BlendInto`/`LoadRegionDataWeather`):

- **`GetRandomWeather`**: identical weighted-random-with-live-global-override algorithm as
  `TESClimate::GetRandomWeather`/`TESWeatherList::GetRandomWeather`, just operating directly on the
  region data's own `{TESWeather*, chance, chanceVar}` entry list rather than wrapping a separate
  `WeatherEntry` linked list — confirms **every level of the weather-selection hierarchy** (Climate,
  WeatherList, RegionDataWeather) shares one identical selection algorithm, not three independent
  implementations.
- **`UsesWeather(TESWeather*)`**: linear scan for a matching weather pointer whose effective chance
  (fixed, or live global if `chanceVar` is set) is non-zero — a chance of `0` (fixed or live) means
  "not used" even if the weather is technically present in the list. Confirms the same
  dynamic-chance-via-global mechanic can silently zero-out a region's use of a weather at runtime, not
  just at the initial pick.

## Gap-closing round: real vtables dumped, three previously-guessed function names retracted

The prior "genuine gaps" list named `Precipitation::RenderOcclusionMap`/`ComputeProjection`/
`UpdateRainOcclusionData` and `Clouds::RemoveTextures` as untouched targets. Those names were **never
verified** — plausible-sounding guesses at what unexplored methods might be called, written down as
gap markers rather than confirmed facts. Per this workspace's "never guess a verifiable fact" rule,
they've now been checked against the real vtables (`FindDumpVtable.java` on `Precipitation`, `Clouds`,
`TESRegionDataWeather`) instead of left standing:

- **`Precipitation` has exactly ONE virtual method total** beyond what's already documented: vtable
  slot `[0]`, decompiled below. It is the scalar-deleting destructor. **There is no
  `RenderOcclusionMap`/`ComputeProjection`/`UpdateRainOcclusionData` virtual method on `Precipitation`
  at all** — those names don't exist in this class. If a rain-occlusion shadow-map path exists in the
  engine, it lives elsewhere (most likely `BSShaderManager`/`ShadowSceneNode`/a dedicated occlusion
  class, not `Precipitation` itself) — retracted rather than left as an open lead that doesn't exist.
- **`Clouds` has exactly THREE virtual methods total**: `[0]` destructor, `[1]` inherited
  `SkyObject::GetRoot` (not overridden), `[2]` `Clouds::Update` — already fully covered. **There is no
  `Clouds::RemoveTextures` virtual method** — same retraction as above.
- **`TESRegionDataWeather` has 9 virtual slots**, of which 5 were already covered
  (`LoadRegionData`/`Initialize` inherited from `TESRegionData`, `BlendInto`,
  `LoadRegionDataWeather`) and this round closes the remaining 4: `GetID`, `Copy`, `Blend`,
  `Validate` — the real member set, not "two other constructors" (constructors aren't virtual and
  wouldn't appear in a vtable at all — that phrasing in the prior gaps note was imprecise).

## Confirmed: `Precipitation::Func1` (RVA `0x64B040`) — the destructor; confirms no other virtual surface exists

Straightforward scalar-deleting destructor: calls `Precipitation::~Precipitation`, then conditionally
frees via `MemoryManager::Deallocate` if the low bit of the `_flags` argument is set (standard MSVC
`operator delete`-folded-destructor pattern). No occlusion/shadow logic anywhere in it — confirms the
class's entire public virtual surface is `Update` + this destructor.

## Confirmed: `TESRegionDataWeather::GetID`/`Copy`/`Blend`/`Validate` (RVA `0x290A20`/`0x290A30`/`0x290AD0`/`0x290C70`)

- **`GetID`** — trivial, returns the constant `3`. Directly confirms `REGION_DATA_ID::kWeather == 3`
  (previously only inferred indirectly from `TESRegionDataList::Find(list, 3)` in `TESRegion::
  SelectWeather`).
- **`Copy`** — allocates a new `TESRegionData` (`0x20` bytes), copy-constructs the `TESRegionData`
  base, re-stamps the vtable pointer to `VTABLE_TESRegionDataWeather`, default-constructs a
  `TESWeatherList` at `this+0x10`, then deep-copies the source's weather-entry list via
  `TESWeatherList::CopyComponent`. Confirms `TESRegionDataWeather`'s only payload beyond the
  `TESRegionData` base is a single `TESWeatherList` at offset `+0x10` — consistent with
  `GetRandomWeather`'s entry-list walk already starting at `this+0x10`.
- **`Blend`** — calls its own vtable slot `[4]` (`Copy`) to clone itself, then calls the clone's
  vtable slot `[6]` (`BlendInto`) against `param_1` (the other `TESRegionData`) with a literal mode
  argument of `2`, returning the blended clone. So `Blend` = "copy self, then call `BlendInto(other,
  mode=2)` on the copy" — a clean two-step composition of the already-documented `Copy` and
  `BlendInto`. The `2` mode constant is a confirmed real call-site value; its exact enum meaning
  wasn't independently traced (`BlendInto`'s own mode-handling wasn't re-examined this round).
- **`Validate`** — returns `true` only when the region data has at least one weather entry (either
  `this+0x10` or `this+0x18` non-null); returns `false` for a genuinely empty weather list. The
  straightforward "is this region-data-weather populated" check.

## Confirmed: `Atmosphere`/`Stars` vtables dumped — both fully closed, zero remaining virtual surface

Same `FindDumpVtable` treatment as `Precipitation`/`Clouds`/`TESRegionDataWeather`, same result:

- **`Atmosphere`** has exactly 3 virtual slots: `[0]` `Func1` (scalar-deleting destructor, decompiled —
  a trivial `~Atmosphere` + conditional `MemoryManager::Deallocate`, identical shape to
  `Precipitation::Func1`), `[1]` inherited `SkyObject::GetRoot` (not overridden), `[2]` `Update`
  (already fully documented above). **No `Atmosphere::Init` or any other virtual method exists** — the
  earlier gaps note's mention of `Atmosphere::Init` as an open item was another unverified guess;
  retracted.
- **`Stars`** has exactly 3 virtual slots, identical pattern: `[0]` `Func1` (destructor, decompiled,
  same shape), `[1]` inherited `SkyObject::GetRoot`, `[2]` `Update` (already documented). Zero
  remaining virtual surface.

Every `SkyObject` subclass this sweep touched (`Clouds`, `Precipitation`, `Atmosphere`, `Stars`) turns
out to follow the **exact same 3-slot vtable shape**: destructor, inherited `GetRoot`, one real
`Update` override. This is now a confirmed, consistent pattern across the whole `SkyObject` hierarchy,
not a coincidence — `SkyObject` itself is the real polymorphic surface (`Update`/`GetRoot`/dtor), and
each subclass only ever overrides `Update`.

## Confirmed: `SkySound` is NOT a `SkyObject` — no vtable exists, and now the full list mechanism is known

`FindDumpVtable.java` found **no vtable symbol for `SkySound` at all** (unlike every other class this
sweep checked). A broader symbol search confirms why: `SkySound`'s own constructor (`0x64BE90`,
already documented above) writes no vtable pointer into `this+0`, and `SkySound`s are stored in a
plain `BSSimpleList<SkySound*>` — a raw linked list, not a `SkyObject`-style polymorphic collection.
**`SkySound` is a lightweight non-polymorphic value struct managed directly by `Sky`, not a
`SkyObject` subclass** — despite CommonLibF4's `IDs_RTTI.h`/`IDs_VTABLE.h` reserving a `REL::ID` slot
for it (that ID is very likely an RTTI *type descriptor* reservation used for something else, e.g. a
`dynamic_cast` guard elsewhere, not evidence of an actual compiler-generated vtable — the two are
easy to conflate but aren't the same thing).

**`Sky::FindSkySound(BGSSoundDescriptorForm*, TESWeather*)`** (RVA `0x654A50`) walks the linked list at
**`Sky+0x370`** (previously unknown field, now confirmed as the `BSSimpleList<SkySound*>` head),
comparing each entry's `BGSSoundDescriptorForm*` (`+0x10`) and `TESWeather*` (`+8`) fields against the
requested pair, returning `true` on a match — the shape of a dedup/presence check. **Correction**:
`FindCallers` against its RVA found zero direct callers. The real construction site
(`Sky::ActivateWeatherSounds`, below) implements the identical comparison **inlined**, not via a call
to this function — `FindSkySound` is real, correctly-shaped, but appears to be dead/orphaned code in
this build, not the actual mechanism in use.

## Confirmed: `Sky::ActivateWeatherSounds` (RVA `0x654800`) — where `SkySound` is actually constructed, and a brand new `TESWeather` field

The real construction site, closing the loop `FindSkySound` alone couldn't:

- Iterates a linked list starting at **`TESWeather+0xDC8`** — a completely new, previously-unknown
  field: **the weather record's own per-weather sound-descriptor list**, i.e. the runtime form of
  whatever CK's weather-record "Sounds" list authors (each entry pairs a `BGSSoundDescriptorForm*`
  with a `TESWeather::SoundType`).
- For each entry, skips ambient-type (`soundType==1`) entries if a global ambient-sound kill-switch
  (`DAT_143718ee8`) is off — a second, independently-discovered global toggle alongside the camera-
  effects `EffectsEnabled` switch found in round 5.
- Walks `Sky+0x370` doing the **exact same** descriptor-priority/weather/soundType match `FindSkySound`
  implements (inlined here, confirming the correction above) to check whether this weather+sound
  combo is already active.
- If not found: acquires a `BSSoundHandle` via `BSAudioManager::GetSoundHandle`, allocates and
  populates a new 0x20-byte `SkySound` node (field layout matches the constructor exactly), and
  prepends it to the `Sky+0x370` list. **If the new sound's type is `3` (Thunder), increments
  `SkySound::ucThunderCount` by 1.**
- This closes the `ucThunderCount` lifecycle completely: it's **not** a per-frame jitter value, it's a
  **live count of currently-active Thunder-type `SkySound` instances** — incremented here on
  activation, decremented (`+0xFF` byte-wraparound `== -1`) in `Sky::UpdateSound` when an invalidated
  Thunder sound handle gets cleaned up (see below). The round-1 `ThunderFreq==0` div-by-zero crash is
  actually `ThunderFreq * ucThunderCount == 0` — since `ucThunderCount` should organically be ≥1
  whenever that code path (which only runs for an already-active Thunder sound handle) executes, the
  dominant real-world trigger remains an authored `ThunderFreq==0`, but the multiplication means a
  transition-window race (one weather's Thunder sound getting cleaned up while another's stale handle
  still executes this branch) is a second, narrower theoretical path to the same crash.

## Confirmed: `Sky::UpdateSound`'s full sound-scheduling body — resolves the `weatherData[6]`/`[7]`/`[8]`/`[9]` bytes

Reading past the list-cleanup preamble (already covered) into the actual per-`SkySound` scheduling
loop resolves several previously-unnamed fields:

- **Thunder branch (`soundID==3`)**: when a Thunder sound handle is valid but not currently playing,
  and a randomized retry timer has elapsed, rolls `random % (ThunderFreq * ucThunderCount) == 0`
  (`ThunderFreq` = `weatherData[0xA]`, `TESWeather+0xA76` — matches the already-known offset). On a
  hit, computes a fade intensity from **`weatherData[8]`** (`+0xA74`, current weather) or
  **`weatherData[9]`** (`+0xA75`, last weather) — **now confirmed as the Thunder onset-threshold
  percentage pair**, current/last weather respectively, analogous to the camera-effect spawn/despawn
  thresholds (`weatherData[0xF]`/`[0x10]`) found in round 5. On success, `SkySound::Play(intensity)`
  runs, `Sky+0x378`/`Sky+0x380` cache the intensity and timestamp, and `TESWeather::CheckApplySpell`
  is called — confirms Thunder can trigger a weather-attached spell hit on the player (e.g. a
  lightning-strike damage spell), previously undocumented.
- **Ambient branch (`soundID==1`)**: reads **`weatherData[6]`** (`+0xA72`, current weather) and
  **`weatherData[7]`** (`+0xA73`, last weather) to compute fade thresholds for the ambient
  weather-sound loop — **these are the exact same two bytes `Precipitation::Update` reads** for its
  own intensity fade (round 6). **Correction**: round 6 tentatively guessed these were "precipitation
  density/intensity" fields; this confirms a more precise and better-supported reading — they're a
  general **ambient/precipitation fade-threshold pair**, shared by design between the audio ambient
  loop and the visual precipitation system (a single CK-authored "how far into the transition does
  rain sound+look start/stop" value driving both).
- Both branches, and a third default branch (any other `soundID`), share a common weather-vs-lastWeather
  match/fade-percent structure, then either call `SkySound::Play` or `BSSoundHandle::Stop` and mark the
  handle invalid (`bit 0x80000000` on the packed status field) for cleanup on the next `UpdateSound`
  pass — confirms `soundID` bit `0x80000000` (distinct from the `1`/`3` type values) is the "pending
  removal" flag consumed by the list-cleanup preamble.

## Confirmed: the rain-occlusion shadow-map system is real — retraction of the round-8 retraction

Round 8 concluded `Precipitation`'s occlusion functions didn't exist because they weren't in its
vtable. That conclusion was too broad: they exist as **real, non-virtual** member functions (found via
a plain symbol-name search, `RenderOcclusionMap`/`RenderOcclusionMapImpl`/`UpdateRainOcclusionData`/
`ToggleRainOcclusion`, plus a whole separate `OcclusionMapData` class) — not virtual, but real:

- **`Precipitation::RenderOcclusionMap`** (RVA `0x648DF0`, static): gets the `Precipitation` singleton
  from **`Sky+0xA0`** (a newly-confirmed field). Branches on global `bRainOcclusion`:
  - **Off**: performs a real D3D render-target clear sequence (lock renderer, acquire render-target
    slot `0x56`, set it + depth-stencil slot `8`, clear, flush, restore, release, unlock) exactly once
    per toggle-off (guarded by `bClearOcclusionMap`), then still calls `ComputeProjection` every frame
    regardless of the toggle state.
  - **On**: checks for live/loaded precipitation particle geometry (or a persistent "camera is under
    rain cover" indicator at `_St+0x2F8`, the same field `Precipitation::Update` sets), resolves a
    `BSParticleShaderCubeEmitter*` from the geometry, and calls `RenderOcclusionMapImpl` — the actual
    render call (not itself decompiled this round).
  - Confirms render-target slot `0x56` / depth-stencil slot `8` as the real occlusion-map render
    targets, and that `ComputeProjection` (referenced but not yet decompiled) runs unconditionally.
- **`Precipitation::UpdateRainOcclusionData`** — closes the provenance of the `kRainDirection` global
  (previously only known via the `QRainDirection` read-only accessor). Computes a wind vector via
  `BGSShaderParticleGeometryData::GetWindVector`, scaled by `Sky+0x31C` (the wind-magnitude field
  `Sky::UpdateWind` already confirmed) and a fixed multiplier, adds a Z component derived from the
  geometry's own gravity/fall-direction flag, normalizes the result (with a near-zero-magnitude guard
  falling back to a zero vector), and writes it to `kRainDirection`. **Confirms `kRainDirection` is
  "which way rain visually streaks/leans," driven by wind plus the particle system's own fall
  direction** — not an independent input.
- **`Precipitation::ToggleRainOcclusion`** — trivial: flips `bRainOcclusion`; if turning it off, sets
  `bClearOcclusionMap = true` so the stale occlusion buffer gets cleared on the next
  `RenderOcclusionMap` call. Matches both globals `RenderOcclusionMap` reads.

**Lesson carried forward**: a vtable dump proves a class's *virtual* surface, not its *entire* surface
— retracting a guessed name because it isn't virtual is a different claim than retracting it because
it doesn't exist, and round 8's writeup conflated the two. Corrected here rather than left standing.

## Genuine gaps remaining after this pass

`Precipitation::RenderOcclusionMapImpl` and `ComputeProjection` (the actual D3D draw calls) remain
undecompiled — `RenderOcclusionMap` calls both but their own bodies weren't pulled this round. The
`OcclusionMapData` class (constructor/destructor symbols confirmed to exist) is entirely unexamined.
`TESWeather+0xDC8`'s exact node layout beyond `{BGSSoundDescriptorForm*, SoundType}` (i.e. whether
there's a third field, and the list's node-count/size) wasn't fully walked. `Sky+0xA0`
(`Precipitation*` singleton) and `Sky+0x378`/`Sky+0x380` (cached thunder intensity + timestamp) are
newly confirmed fields with no further consumers traced yet. This is the real next round of work if
the CTD-class of bugs (the `ThunderFreq` one above) or precipitation-specific work (Rad Mod's failed
dusty-precipitation attempt, see `docs\Rad Mod\PROGRESS.md`) needs deeper engine grounding.
