# WTHR FormID slot resolution evidence (Fallout4.esm)

Scope: WTHR records in Fallout4.esm resolved against the Fallout4.esm FormID index only.
A flat multi-DLC FormID index collides on overlapping master indexes; those polluted hits are not used as proof.

- Indexed forms: 1549276
- WTHR records: 71

| Slot | Values | Nulls | Unresolved | Target sigs |
|------|-------:|------:|-----------:|-------------|
| `GNAM` | 18 | 0 | 0 | `{'LENS': 18}` |
| `IMSP+00` | 71 | 2 | 0 | `{'IMGS': 69}` |
| `IMSP+04` | 71 | 2 | 0 | `{'IMGS': 69}` |
| `IMSP+08` | 71 | 2 | 0 | `{'IMGS': 69}` |
| `IMSP+0C` | 71 | 5 | 0 | `{'IMGS': 66}` |
| `IMSP+10` | 67 | 2 | 0 | `{'IMGS': 65}` |
| `IMSP+14` | 67 | 2 | 0 | `{'IMGS': 65}` |
| `IMSP+18` | 67 | 2 | 0 | `{'IMGS': 65}` |
| `IMSP+1C` | 67 | 3 | 0 | `{'IMGS': 64}` |
| `MNAM` | 71 | 67 | 0 | `{'SPGD': 4}` |
| `NNAM` | 71 | 27 | 0 | `{'RFCT': 44}` |
| `SNAM+00` | 52 | 1 | 0 | `{'SNDR': 51}` |
| `TNAM` | 1908 | 0 | 0 | `{'STAT': 1908}` |
| `UNAM+00` | 63 | 58 | 0 | `{'SPEL': 5}` |
| `UNAM+08` | 63 | 58 | 0 | `{'SPEL': 5}` |
| `WGDR+00` | 63 | 23 | 0 | `{'GDRY': 40}` |
| `WGDR+04` | 63 | 23 | 0 | `{'GDRY': 40}` |
| `WGDR+08` | 63 | 24 | 0 | `{'GDRY': 39}` |
| `WGDR+0C` | 63 | 23 | 0 | `{'GDRY': 40}` |
| `WGDR+10` | 63 | 23 | 0 | `{'GDRY': 40}` |
| `WGDR+14` | 63 | 23 | 0 | `{'GDRY': 40}` |
| `WGDR+18` | 63 | 24 | 0 | `{'GDRY': 39}` |
| `WGDR+1C` | 63 | 23 | 0 | `{'GDRY': 40}` |

## Runtime member citation (commonlib_NonVR `TESWeather`)

Reference-only: `commonlib_NonVR/include/RE/T/TESWeather.h`.

| On-disk slot | Runtime member | Pointer/element type |
|--------------|----------------|----------------------|
| `MNAM` | `precipitationData` | `BGSShaderParticleGeometryData*` (SPGD) |
| `NNAM` | `referenceEffect` | `BGSReferenceEffect*` (RFCT) |
| `IMSP` (4 or 8 x u32) | `imageSpace[8]` | `TESImageSpace*` (IMGS) |
| `WGDR` (8 x u32) | `godRays[8]` | `BGSGodRays*` (GDRY) |
| `GNAM` | `sunGlareLensFlare` | `BGSLensFlare*` (LENS) |
| `TNAM` repeated | `skyStatics` | `TESObjectSTAT*` (STAT) |
| `SNAM` `{u32,u32}` | `soundList` / `WeatherSound` | `BGSSoundDescriptorForm*` + `SoundType` |
| `UNAM` | `weatherSpellData[2]` | `MagicItem*` + float threshold (SPEL on disk) |

`DALC` is not a FormID container: each 32-byte `DALC` matches `sizeof(BGSDirectionalAmbientLightingColors)==0x20`.
`VNAM`/`WNAM` are floats (`volatilityMult`/`visibilityMult`), not FormIDs.
`NAM1` is `cloudLayerDisabledBits` (u32 bitfield), not a FormID.
`LNAM` remains opaque (always 4 bytes in corpus; no proven semantic mapping yet).

