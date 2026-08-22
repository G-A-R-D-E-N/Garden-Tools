# Test strategy

## Principles

1. Tests describe engine-facing behavior, not Mutagen internals.
2. Mutagen/FO4IDE results are oracles. When they disagree with Fallout 4 or CK, document the difference.
3. Unknown bytes must survive.
4. FormKey tests exist specifically to catch master-index corruption.

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
- `Ba2V7Gnrl` / `Ba2V7Dx10` / `Ba2V8Gnrl` / `Ba2V8Dx10`

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

Until a writer exists, `fo4inspect` JSON is the native side of that diff.

## Sanitizers

CMake options `FO4_ENABLE_ASAN` and `FO4_ENABLE_UBSAN`. Parsing untrusted plugins must fail with `parse_error`, not memory corruption.
