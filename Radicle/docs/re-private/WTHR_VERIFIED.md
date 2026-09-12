# Verified WTHR sources (Issue #284)

## f4-re-mcp (this session)

Queried via `ReverseEngineering/Projects/re-mcp` against `re-index.sqlite` (1.10.163).
Artifacts: `wthr_re_mcp_results.json`, `wthr_re_mcp_load.json`.

| Symbol | RVA | Address Library ID | Notes |
|--------|-----|--------------------|-------|
| `TESWeather::Load` | `0x48c2f0` | 731579 | `bool Load(TESWeather*, TESFile*)` sizeof 4054 — on-disk chunk parse |
| `TESWeather::WeatherSound::LoadFrom` | `0x48bde0` | — | SNAM sound+type stream |
| `TESWeather::SpellData::LoadFrom` | `0x48bf70` | — | UNAM spell+threshold stream |
| `TESWeather::GetCloudColor` | `0x48c220` | — | cloudColorData[layer][ColorTime] |
| `TESWeather::GetCloudAlpha` | `0x48c2b0` | — | cloudAlpha[layer][ColorTime] |
| `TESWeather::GetFogData` | `0x658300` | — | fogData[] accessor |
| `TESWeather::QPrecipitationData` | `0x64b520` | — | returns `BGSShaderParticleGeometryData*` |
| `ConcreteFormFactory<TESWeather,57>` | — | — | form type enum 57 = WTHR |

`check_name_confidence(TESWeather)`: PDB corroboration **confirmed**.

## commonlib_NonVR (reference-only)

`include/RE/T/TESWeather.h` — runtime member layout (cloudTexture[32], speeds, colors, alphas, weatherData[20], fogData[18], colorData[19][8], imageSpace[8], godRays[8], DALC[8], precipitationData, referenceEffect, sunGlareLensFlare, skyStatics, weatherSpellData[2], soundList, volatility/visibility).

`BGSDirectionalAmbientLightingColors` sizeof `0x20`.

## On-disk corpus

`Radicle/docs/WTHR_CORPUS.md` + FO4-only FormID resolution in `WTHR_FORMID_SLOTS.md`.
