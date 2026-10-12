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

## Real-asset corpus tests

Opt-in tests read an installed game and skip when their variables are unset. Each run reports `RecordProperty` counts, which must be recorded per release (OG, NG and AE) before a format gate is changed.

| Test | Variable | Covers |
|------|----------|--------|
| `NifCorpus.ReadsEveryVanillaNifAndHandlesGeometryTypes` | `FO4_DATA_DIR` | every NIF in the general archives of the present official masters and `cc*` plugins, per archive and per BS version |
| `Ba2RealData.ParsesEveryVanillaStringTableInMastersAndCcArchives` | `FO4_DATA_DIR` | every `Strings/*.STRINGS`, `*.DLSTRINGS`, `*.ILSTRINGS` entry, per extension and language |
| `PexCorpus.EveryLoosePexRoundTripsByteIdentically` | `FO4_PEX_CORPUS` | loose `.pex` directories |
| `PexCorpus.EveryArchivedPexRoundTripsByteIdentically` | `FO4_PEX_ARCHIVES` | `.ba2` files holding `Scripts/*.pex` |
| `PexCorpus.EveryDataDirArchivePexRoundTripsByteIdentically` | `FO4_DATA_DIR` (fallback when the two above are unset) | `.pex` entries in the general archives of the masters and `cc*` plugins |

`FO4_DATA_DIR` is the game's `Data` folder. The masters are the seven official plugins that are present (`Fallout4.esm`, `DLCRobot.esm`, `DLCworkshop01.esm`, `DLCCoast.esm`, `DLCworkshop02.esm`, `DLCworkshop03.esm`, `DLCNukaWorld.esm`), plus every `cc*.esl` and `cc*.esm` in the folder. Only archives paired with a plugin that is present are opened; a `cc` archive whose plugin is absent is not read.

Measured on AE 1.11.240 (BA2 name tables): Fallout4 strings are in `Fallout4 - Interface.ba2` (33 files under `STRINGS\`). Each DLC has 33 string files in its `<DLC> - Main.ba2`, with mixed `/` and `\` separators. Each cc Main archive holds 36 string files. PEX is in `Fallout4 - Misc.ba2` (7875 entries), plus the DLC and some cc Main archives.

Each release's results are recorded as OG, NG or AE in the issue or PR that changes a format gate.

## Sanitizers

CMake options `FO4_ENABLE_ASAN` and `FO4_ENABLE_UBSAN`. Parsing untrusted plugins must fail with `parse_error`, not memory corruption.
