# Test strategy

## Principles

1. Tests prove stored plugin reads, edits and serialization; runtime behavior has separate acceptance.
2. Mutagen/FO4IDE results are oracles. When they disagree with Fallout 4 or CK, document the difference.
3. Unknown bytes must survive.
4. FormKey tests exist specifically to catch master-index corruption.

For editable surfaces, acceptance is parse -> expose -> edit -> serialize -> reparse. Check requested edited values, valid field addition/removal, required structure and cardinality, master/reference remapping, rejected malformed or unsafe writes, untouched fields/records/unknown bytes, and file save/reload. Native, C ABI and managed mutation tests must exercise each exposed path. A byte-identical unmodified round-trip establishes preservation alone.

Use known definitions and real plugins to prove decoded values. Runtime RE is required when the layout or safe mutation remains unresolved, or when reproducing runtime behavior. See [FO4_PARITY.md](FO4_PARITY.md) and #287 for capability-specific acceptance; #384 tracks required managed CI execution.

## Current coverage

| Area | How |
|------|-----|
| Signature 4CC | unit |
| Cursor truncation | unit |
| TES4 + GRUP walk | synthetic plugin |
| Unknown subrecords | synthetic `ZZZZ` |
| Nested groups | synthetic WRLD/CELL |
| `XXXX` | 70000-byte DATA |
| Compressed records | zlib payload + corrupt payload |
| Real ESP | `testdata/FW_HeatherSurvivalPatch.esp`, `SlowTime.esp` |
| FormKey on load | Heather local `000800:FW_HeatherSurvivalPatch.esp`, override `0EC5B7:llamaCompanionHeatherv2.esp` |
| Unmodified write | Heather and SlowTime byte-identical |
| Rebuild write | compressed + XXXX synthetic |
| Master removal | record headers re-encoded from FormKeys |
| KWDA / NAME FormLinks | remapped from FormKeys, not high-byte math |
| `FormLink.CtdaGetBaseValueActorValueUsesMastOrdinal` | FO4IDE custom ActorValue CTDA case |
| `FormLink.LightMasterCtdaUsesMastOrdinalNotFe` | ESL master listed in MAST, still `0x01xxxxxx` not `0xFExxxxxx` |
| TES4 ONAM | override FormIDs remapped |
| CTDA Use Global | comparison value remapped, floats still skipped |
| VMAD object format 1/2, arrays, structs | FormIDs remapped; ints left alone |
| VMAD QUST alias | alias object FormID remapped |
| Truncated VMAD | remap does not throw |
| CTDA GetLocked | comparison float is not treated as a FormID |
| On-disk FormKey resolve/encode | master removal |
| Light master on disk | `0x02000812` case from FO4IDE |
| Runtime `0x56xxxxxx` and `0xFExxxxxx` | unit |

## Real-data tests and game releases

Tests that read `FO4_DATA_DIR` call `detect_game_build` (`tests/real_data_build.hpp`) and record the `game_build` property. Release detection reads `Fallout4.exe` next to the Data folder (VS_FIXEDFILEINFO), and `FO4_GAME_BUILD` (for example `1.11.240.0`) overrides it. The release is OG for 1.10.163, NG for 1.10.980 or 1.10.984, AE for 1.11.x, and Unknown otherwise.

Exact-count assertions are pinned to AE 1.11.240.0 only (`exact_counts_known`):

- `Fallout4.esm` DIAL 35443, INFO 78087, SCEN 3568 (`test_dialogue_real.cpp`)
- per-master WTHR and CLMT counts (`kOfficialSkyCounts` in `test_sky_real.cpp`)
- per-master WATR counts (`test_water_real.cpp`)
- 107 WTHR when the corpus has 7 plugins (`test_weather_concept.cpp`)
- named BA2 archive versions and entry counts (`test_ba2_versions_real.cpp`)

On other builds these tests record the observed values with `RecordProperty` and skip only the exact equality. Decode, edit, round-trip and byte-identity checks run on every build.

## Planned (not this pass)

Named regression tests:

- `PerkAddActivateChoiceSerialization`
- `NullableEnumSerialization`
- `ShortSbyteEnumSerialization`
- `CustomActorValueCtdaFormLink`
- `MasterRemovalReindex`
- BA2 v7/v8 GNRL and DX10 are now covered by `Ba2NamedRealData` (installed data, `tests/test_ba2_versions_real.cpp`; skipped without `FO4_DATA_DIR`) and the synthetic `Ba2General.RoundTripsNextGenVersions7And8` (`tests/test_ba2.cpp`) and `Ba2Texture.RoundTripStoredAndCompressed` (`tests/test_ba2_texture.cpp`, versions 1, 7 and 8)

Reader/writer:

- read/write/read byte identity for unmodified plugins
- semantic identity where CK normalizes
- ESL, ESM, ESP-flagged-light
- truncated / fuzzed input (libFuzzer after the parser is stable)
- optional live test against `Fallout4.esm` when the game Data path is present (not vendored)

## Corpus / oracle

`tools/corpus` will later dump JSON from Mutagen and from `fo4inspect` and diff:

- header flags
- masters
- record count
- signatures
- FormKeys
- EditorIDs

`fo4inspect` JSON is the native inspection side of that diff. Writer acceptance additionally edits, saves and reparses the resulting plugin.

## Sanitizers

CMake options `FO4_ENABLE_ASAN` and `FO4_ENABLE_UBSAN`. Parsing untrusted plugins must fail with `parse_error`, not memory corruption.
