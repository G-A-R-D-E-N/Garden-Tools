# Stored dialogue and scene read surface

This slice addresses the stored-layout part of #344. It does not execute dialogue, evaluate conditions, or advance scenes. [RE #405](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/405) tracks those runtime questions. Unknown fields remain in the original record and action-specific payloads remain opaque.

## Evidence and validity

The compatibility source is [xEdit Core/wbDefinitionsFO4.pas](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas), commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`: DIAL lines 6309–6474, INFO 9672–9791, SCEN 8879–9165. This source describes storage; it is not a runtime specification. The newer [verified loader record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/dialogue-scenes-405-344/README.md) supersedes the older index's missing per-subrecord evidence. Its OG SCEN loader RVA 0x5975E0 establishes the two-byte action phase fields; the verified local copy is `../radicle-341-re-followup-20261005/build/RE-Work/6916afb/dialogue-scenes-405-344/README.md`. Runtime scene execution remains outside this reader.

Metadata-only measurement on the seven installed official masters on 2026-10-04:

| Master | DIAL | INFO | SCEN |
|---|---:|---:|---:|
| Fallout4.esm | 35,443 | 78,087 | 3,568 |
| DLCRobot.esm | 1,084 | 3,649 | 184 |
| DLCworkshop01.esm | 1 | 3 | 0 |
| DLCCoast.esm | 5,362 | 9,871 | 501 |
| DLCworkshop02.esm | 0 | 0 | 0 |
| DLCWorkshop03.esm | 541 | 1,008 | 64 |
| DLCNukaWorld.esm | 3,996 | 9,844 | 461 |
| Total | 46,427 | 102,462 | 4,778 |

There were no record/subrecord parse failures. Every DIAL DATA, PNAM, TIFC and SNAM was four bytes; every INFO ENAM was four bytes. All 96,768 TRDA blocks were 20 bytes and had a following four-byte NAM1. All 16,387 RNAM fields were four bytes. These measurements do not constitute execution of the new decoder or its corpus tests.

## DIAL

DATA stores byte flags, byte category and a little-endian u16 subtype. PNAM stores f32 priority. BNAM, QNAM and KNAM are branch, quest and keyword FormID links resolved in the owning plugin's master context. SNAM stores exactly four subtype-name bytes, including any padding; TIFC stores u32 info count. FULL/name handling uses the existing environment metadata and string-table API. Stored enum and flag values are returned without validation against guessed ranges.

## INFO

ENAM stores u16 flags and u16 encoded reset time. The reset value is returned unchanged; this slice does not adopt the schema's display conversion as a proven physical unit. TPIC, PNAM, DNAM and GNAM are previous-topic, previous-info, shared-info and info-group links. ANAM and TSCE are speaker and start-scene links.

Each TRDA starts a response. Its 20 bytes contain emotion KYWD (0–3), response number byte (4), sound SNDR (5–8), unknown byte (9), interrupt-percent u16 (10–11), and signed camera target/location aliases (12–15 and 16–19). NAM1 is response text; SNAM/LNAM are optional speaker/listener idle links within that response. RNAM is the record-level prompt. Notes, alternate lip text and the other response fields remain preserved and unexposed.

NAM1 and RNAM use the existing environment localization policy: the owning plugin's current localized flag selects a four-byte string ID, otherwise a strict NUL-terminated stored string. This is compatibility with existing Radicle behavior, not a resolution of TES4 bit 7 (#29). String IDs are resolved explicitly with the existing string tables: INFO NAM1 uses ILSTRINGS, INFO RNAM uses STRINGS. Missing IDs remain distinguishable from absent fields. C ABI inline text is UTF-8 using the existing Windows-1252 conversion policy; table lookups retain the existing raw-byte return contract.

Conditions reuse `read_ctda_conditions` and expose raw operands plus CIS1/CIS2 strings. No condition function is executed or given additional runtime meaning.

## SCEN

The measured corpus contains 21,306 phase groups. Every group has empty HNAM open/close markers and two empty NEXT separators in order, with no malformed groups. A phase's NAM0 name, WNAM u32 editor width, optional FNAM u16 flags, SCQS pair of i16 quest stages, and start/completion condition lists are scoped to that group.

The 6,270 actors are ordered ALID i32, LNAM u32 flags, DNAM u32 behavior-flags triplets outside actions. All measured triplets were well formed.

There are 24,983 actions, each opened by two-byte ANAM and closed by empty ANAM. The common header contains NAM0 name, ALID i32 actor alias, INAM u32 index, FNAM u32 flags, SNAM u16 start phase and ENAM u16 end phase. The newer [verified loader record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/dialogue-scenes-405-344/README.md), section 3, stamps the OG reader at RVA 0x5975E0 and confirms both unsigned phase widths. Only the header before the first ENAM is decoded. The following subrecords are exposed in order as opaque signature/payload views. This avoids conflating timer SNAM, action PNAM and record-level PNAM or inferring type-specific execution behavior. Unknown type discriminators keep the same opaque payload representation. SCQS names are compatibility labels; their runtime meaning remains unresolved.

Record FNAM is u32 flags in the prelude; PNAM after the groups is the parent quest link. The trailing record condition list is separate from phase and action conditions. Malformed known sizes, duplicate scoped scalar fields, unmatched markers and malformed actor triplets fail closed. Unmodified preservation writing retains all source bytes, including unknown data.

## Validation and consumer boundary

Native synthetic tests cover grouping, localization descriptors, raw conditions, opaque payloads and malformed input. Each new C ABI getter has a managed test. The seven-master corpus test sweeps native and C ABI getters and verifies unchanged master byte identity; it skips without `FO4_DATA_DIR`. Local and CI validation are recorded separately for each published head.

GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` has DIAL/INFO/SCEN names only in `Services/RecordVocabulary.cs`, and no dialogue/scene consumer. GardenEngine #150 needs that consumer before a meaningful probe. ABI 12 is reserved for this independent branch after checking main ABI 4 and PR #383's 5/6/7 batches; reconciliation with the other independent ABI branches is required before merge.
