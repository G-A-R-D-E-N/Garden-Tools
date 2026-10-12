# WTHR semantic proof notes (Issue #284)

Proof-first notes for native Radicle / PluginEditTool WTHR acceptance.
Corpus evidence: `WTHR_CORPUS.md` + `wthr_corpus_inventory.json`.
FormID evidence: `WTHR_FORMID_SLOTS.md` + `wthr_formid_slots_fo4.json`.

Runtime layout citation (reference-only, not modified):
`commonlib_NonVR/include/RE/T/TESWeather.h`
(`BGSDirectionalAmbientLightingColors` is 0x20 bytes).

Mutagen `Weather.xml` / `Weather.cs` is an oracle for on-disk packing shapes, not a license to guess unproven slots.

## Corpus summary

| Plugin | WTHR count |
|--------|------------|
| Fallout4.esm | 71 |
| DLCworkshop02.esm | 3 |
| DLCCoast.esm | 16 |
| DLCNukaWorld.esm | 17 |
| Other scanned DLC | 0 |
| **Total** | **107** |

Form versions observed: 40..131 (majority 131).
Distinct subrecord order patterns: 44 (optional cloud textures / SNAM / TNAM / GNAM / MODL / VNAM / WNAM / WGDR / UNAM drive the variance).

## Required vs optional (corpus-wide)

Required in all 107 records:

`EDID`, `LNAM`, `MNAM`, `NNAM`, `RNAM`, `QNAM`, `PNAM`, `JNAM`, `NAM0`, `FNAM`, `DATA`, `NAM1`, `IMSP`, `DALC` (repeated)

Optional:

- Cloud textures `00TX`..`L0TX` (layer index encoded in signature; sparse)
- `NAM4` (absent on 6 older records)
- `SNAM` (0..5 repeats)
- `TNAM` (0, 36, or 144 repeats; each 4 bytes)
- `WGDR`, `UNAM`, `VNAM`, `WNAM`, `GNAM`
- Aurora model `MODL`/`MODT` (8 records)

## Proven size families (cross-checked with runtime arrays)

| Subrecord | Observed sizes | Runtime / packing note |
|-----------|----------------|------------------------|
| `RNAM` / `QNAM` | always 32 | 32 x encoded u8 cloud layer speeds |
| `PNAM` / `JNAM` | 512 or 1024 | 32 layers x 4 or 8 time-of-day entries x 4 bytes |
| `NAM0` | 272 / 544 / 608 | `colorData` width variants; 608 = 19x8x4 matches `colorData[19][8]` |
| `FNAM` | 32 / 56 / 72 | 72 = 18x4 matches `fogData[18]` |
| `DATA` | 19 (form version below 119) / 20 (from 119) | 20 matches `weatherData[20]`; 19 is legacy without final byte |
| `NAM1` | 4 | `cloudLayerDisabledBits` |
| `DALC` | 24, 28 or 32 bytes x 4 (form version below 111) or 8 (from 111) | one `BGSDirectionalAmbientLightingColors` per time slot |
| `IMSP` | 16 (form version below 111) / 32 (from 111) | 4 or 8 ImageSpace form IDs |
| `WGDR` | 32 | 8 GodRays FormIDs |
| `SNAM` | 8 | `{FormID, SoundType u32}` |
| `UNAM` | 12 (form version below 130) / 24 (from 130) | two spell slots; short form omits trailing threshold/unknown |

Unsupported / divergent sizes must fail closed or stay opaque.

## Proven FormLinks (Fallout4.esm only)

| Slot | Nullable | Accepted target sigs | Notes |
|------|----------|----------------------|-------|
| `MNAM` | yes (0 common) | `SPGD` | precipitation |
| `NNAM` | yes | `RFCT` | reference / visual effect |
| `IMSP+0..+1C` | yes | `IMGS` | shorter IMSP only has +00..+0C |
| `WGDR+0..+1C` | yes | `GDRY` | absent WGDR on some records |
| `GNAM` | optional subrecord; non-null when present | `LENS` | sun glare lens flare |
| `TNAM` | list may be absent; NULL allowed (xEdit `[STAT,NULL]`), none NULL in FO4 corpus | `STAT` | sky statics |
| `SNAM+00` | rare null | `SNDR` | +04 is SoundType enum, not FormID |
| `UNAM+00` / `UNAM+08` | yes | `SPEL` | thresholds are floats beside the FormIDs |

Not FormIDs: `DALC`, `VNAM`, `WNAM`, `NAM1`, cloud speed/color/alpha blobs, `LNAM` (opaque).

## Still opaque / unproven for mutation

- Exact named color-channel mapping inside `NAM0` beyond array shape
- `LNAM` 4-byte payload meaning
- `NAM4` float[32] meaning (present on most records)
- Full DLC load-order remapped FormID proof (needs plugin reader remap, not a flat FormID map)
- `ONAM` (Mutagen lists it; **not observed** in this corpus)
- Cloud texture layers 16..29 rarity / engine `numCloudLayers` interaction beyond disabled-bits

## Partial

- `DATA` flag byte (offset 11): stored raw. Bit names are not asserted from schema annotations or commonlib `WeatherDataFlags` / Mutagen `Weather.Flag`; no enum is exposed until cited. Source: `Radicle/docs/CLIMATE_WEATHER.md` (offset table row 11; lines 38-39).
- `DATA` transition delta (offset 3): stored as u8. RE-010 on #520 comment 13142 (static analysis of OG 1.10.163) gives the compiled defaults fWeatherTransMax 1.0, fWeatherTransMin 0.01 and fWeatherTransAccel 4.0. The shipped Fallout4.esm on OG, NG and AE all override fWeatherTransMax to 3.0 and fWeatherTransAccel to 16.0. Every matching GMST overwrites the setting in load order, so the last one wins. With the Sky accelerate flag (bit 3) set, `pct = (pct - base) * (fWeatherTransAccel + 1.0) + base`, recomputed each frame: 5x with the compiled default, 17x with the shipped Fallout4.esm on every release. `transition_hours` computes the unaccelerated duration from caller-supplied bounds. Still open: live in-engine observation of the transition length at DATA[3] = 0 and 255 and of the effect of the game-setting values. Source: `Radicle/docs/CLIMATE_WEATHER.md` (offset table row 3; accelerated transition and GMST paragraphs).

## Radicle status before this issue

No WTHR semantic decoder/mutation surface in Radicle forms/.
Schema FormLink visitors know unrelated `MNAM`/`NNAM`/`SNAM`/`GNAM`/`TNAM` contexts (NAVM/QUST/etc.); WTHR links are registered in `Radicle/schema/fallout4/fields.json` (see `docs/code-notes/radicle-climate-writer.md`).
Mutagen Weather remains the in-tree C# oracle only.

## Safe first mutation surface

After read-only decode lands, the first transactional mutation should be **`MNAM` precipitation FormLink** (single nullable 4-byte slot, required subrecord, proven `SPGD|null`, no repeated-container rewrite):

1. before/after bytes on that subrecord only
2. save/reload
3. semantic round trip
4. untouched-byte preservation for all other WTHR bytes
5. external master insertion when linking a non-mastered `SPGD`
6. reject non-`SPGD` targets and malformed/missing `MNAM`

Cloud texture string edits are also low-risk but are not FormLink remap exercises.

## Proposed files to change (implementation phase)

- `Radicle/docs/WTHR_*.md` (proof; this file)
- `Radicle/tools/corpus/wthr_*.py` (reproducible corpus/FormID scans)
- `Radicle/include/fo4/forms/weather.hpp` + `Radicle/src/forms/weather.cpp` (read-only decode; later setters)
- `Radicle/tests/test_weather.cpp` (corpus-backed decode + mutation proofs)
- `Radicle/CMakeLists.txt` (test target)
- PluginEditTool native ABI only if a thin read API is required for #84 hand-off (defer until decode is green)
- Do **not** expand `fields.json` / schema codegen unless a missing capability is demonstrated
- Do **not** implement #84 visual editor or #285 CELL geometry

## Implementation status (this branch)

The read-only model is `forms::Weather` (struct `Weather` in `Radicle/include/fo4/forms/weather.hpp`), decoded by `decode_weather` in `Radicle/src/forms/weather.cpp`. `Radicle/include/fo4/forms/weather_concept.hpp` is an include shim for that header. The API status string is `k_weather_api_status` in `weather.hpp`. Tests: `Radicle/tests/test_weather_concept.cpp`.

Proven surfaces: read-only decode for corpus-proven slots; MNAM mutation acceptance including external-master remap via remap_plugin_masters; WTHR-aware FormLink visitation. Not in this PR: #84 UI, #285 CELL geometry, additional mutation families beyond MNAM.

## MNAM mutation acceptance

Status: **accepted in PR #286 pending audit** (external-master insertion uses remap_plugin_masters; set_weather_mnam is copy-first transactional so failed MNAM validation never leaves a remapped master table).

Proven and regression-covered:

1. NULL MNAM accepted
2. non-SPGD targets rejected
3. same-plugin SPGD works
4. existing-master SPGD works
5. previously unmastered external SPGD inserts the master and remaps FormLinks
6. existing WTHR FormLinks (SNAM/TNAM) remain semantically unchanged after remap
7. VNAM/WNAM floats are not treated as FormIDs during remap (WTHR-aware visit_link_offsets)
8. save/reload preserves the selected MNAM target
9. malformed/missing/duplicate MNAM fails closed
10. unchanged assignment is a no-op

## FormLink remap (WTHR)

visit_link_offsets classifies WTHR before the global FormID signature table. Proven slots: MNAM, NNAM, GNAM, TNAM, IMSP (16|32), WGDR (32), SNAM+0, UNAM+0/+8. Non-FormID collisions skipped: VNAM, WNAM, NAM1, LNAM, PNAM, JNAM, NAM0, NAM4, FNAM, RNAM, QNAM, DALC, DATA, EDID.

