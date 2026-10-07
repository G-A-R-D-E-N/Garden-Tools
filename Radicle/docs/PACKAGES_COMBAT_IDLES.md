# Stored package, combat-style and idle editing

This #345 slice reads and edits stored PKDT, PSDT, PKCU version/template, scoped package links, location/target fields, combat-style values, and idle selection data and strings. It does not schedule or execute NPC behavior. [RE #407](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/407) contains static OG findings and the remaining questions. Procedure trees, data-input ownership, leaf payloads and VMAD fragments remain opaque.

## Sources and scope

The pinned compatibility source is [xEdit Core/wbDefinitionsFO4.pas](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas), commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`: CSTY 6233–6307, IDLE 9646–9671 and PACK 10526–10667. These are disk schema descriptions, not runtime execution proof. The private index at `ReverseEng/Fallout4_Runtime_Research`, commit `f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a`, `RE-Work/subsystems/plugin-io-garden-tools/evidence-index-garden-tools/README.md`, verifies CK tag sets and CSTY sizes, but reports PACK/IDLE payload layouts NOT FOUND. Its unresolved printed combat tag typo is not used as a tag name.

## PACK

The newer [verified runtime record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/packages-combat-idle-407-345/README.md) supersedes the older missing-layout index. Its local copy is `../radicle-341-re-followup-20261005/build/RE-Work/6916afb/packages-combat-idle-407-345/README.md`; Q1 stamps the OG loader at RVA 0x719180 and schedule consumer at 0x71EC30.

PKDT is 12 bytes: u32 general flags, u8 type, interrupt override, preferred speed and unknown byte, u16 interrupt flags and an unnamed trailing u16. These are raw stored compatibility fields. The runtime uses that interpretation from form version 0x15; older forms follow a legacy path that forces effective preferred speed to 2. This reader still returns the stored byte, without claiming that effective value. PSDT is 12 bytes: signed bytes month, weekday, date, hour and minute, three unused bytes, then unsigned u32 duration in stored minutes. These stored values are returned unchanged. The reader does not apply the runtime's pre-version-122 month adjustment, defaults, or schedule evaluation.

PKCU is 12 bytes: u32 data-input count, package-template FormID and u32 version counter. CNAM combat style and QNAM owner quest belong to the prelude before PKCU; later CNAM values are not mistaken for a record-level link. Only prelude CTDA/CIS lists are decoded with the existing condition model. POBA, POEA and POCA are empty on-begin/end/change markers, each with an optional four-byte INAM idle link in its own scope. All remaining post-PKCU subrecords are exposed as ordered opaque signature/payload views, including procedure and data-input condition lists, instead of flattening them into the top-level conditions. Duplicated known markers and malformed known sizes fail closed.

Q4/Q5 verify PLDT/PTDA constructors at OG RVAs 0x726520/0x729300.
The disk compatibility reader accepts PLDT 8/12/16 stored bytes: signed i32 type at 0, raw u32 value at 4,
optional signed i32 radius at 8 and optional collection-item index at 12. PTDA accepts 12 bytes:
signed i32 type, raw u32 value and i32 count-or-distance. Each view carries its original
record subrecord index. PLDT types 0/1/4/6 and PTDA 0/1/3/7 resolve the value
as a FormID; other supported variants keep it numeric. All stored values,
including unused nonzero fields, stay unchanged. Unknown types/shapes receive
no typed view and retain their raw bytes. These views do not claim ownership
by a particular data input or procedure. The dispatch/container grammar is
known, but arbitrary leaf payload boundaries are not, so no complete tree
or data-input structure is invented.

## CSTY

CSGD is 12 f32 values in the pinned schema's order: offensive, defensive and group-offensive multipliers; melee, magic, ranged, shout, unarmed and staff equipment-score multipliers; avoid, dodge and evade threat chances. CSME has nine or ten f32 values: attack-staggered, power-attack-staggered, power-attack-blocking, bash, bash-recoil, bash-attack, bash-power-attack, special-attack, block-when-staggered, and optional attack-when-staggered multipliers.

CSRA and CSCV are individual f32 ranged-accuracy and cover-search values. CSCR is 44 bytes: nine f32 values (circle, fallback, flank distance, stalk time, charge distance, throw probability, sprint-fast probability, sideswipe probability and disengage probability), u32 throw-max-targets and f32 flank variance. CSLR accepts three, four or five f32 values: strafe, adjust-range, crouch, optional wait and optional range multipliers. CSFL has eight f32 values: hover chance, dive-bomb chance, ground-attack chance, hover time, ground-attack time, perch-attack chance, perch-attack time and flying-attack chance. DATA is raw u32 flags. Omitted optional values remain absent; arrays keep their actual stored count. No runtime numeric bounds or selection effects are asserted.

## IDLE

ANAM is eight bytes: parent and previous FormID links. DATA is six bytes: loop minimum and maximum bytes, raw flag byte, raw padding byte and u16 replay delay. The compatibility API's `GroupSection` name does not establish a runtime meaning for the padding byte. DNAM behavior graph, ENAM animation event and GNAM animation file are strict NUL-terminated stored strings. The existing CTDA model supplies raw conditions and CIS operands. Loop/replay consumers remain unresolved in Q8; their units and selection effects are not invented. Strings follow the existing C ABI Windows-1252 to UTF-8 policy.

## Editing

| Surface | WRITE | ADD / REMOVE | CREATE | REMAP / OPAQUE |
|---|---|---|---|---|
| PACK | Known PKDT bytes, signed schedule bytes, unsigned duration, version/template, scoped combat style/quest/idles, discriminated PLDT/PTDA links/numeric fields and present tails | Stable known fields only; coupled counters/markers refused; unlisted sections refuse additions | Refused | Known link variants remap; numeric lookalikes and unknown bytes preserved; unknown PLDT/PTDA variants refuse strict remap |
| CSTY | All known stored floats and integer slots, including existing optional tails | Required fields cannot be removed; unknown CSMD cannot be added/removed | xEdit required defaults | Unknown CSMD preserved |
| IDLE | ANAM parent/previous, DATA selection bytes/replay delay, stored graph/event/file strings | Optional DNAM/ENAM/GNAM on fully known layouts; condition groups refuse structural edits | Zero ANAM/DATA defaults | Both ANAM links remap; unknown and condition payloads preserved |

Setters use `PluginFile.SetScalar`, `SetString`, `SetFormLink` and `FieldCount`, or their native/C ABI equivalents. Repeated location/target indices retain physical occurrence order, including inactive union variants. `PACK\Idles` indices 0/1/2 are begin/end/change and require all three valid marker scopes. The eight-byte PLDT has no radius and cannot acquire one through a scalar edit. Unknown/discriminator/count fields have no writable schema entry. See `docs/code-notes/radicle-package-writer.md` for exact mutation and refusal boundaries.

## Validation

Metadata-only measurement on 2026-10-04:

| Official master | PACK | CSTY | IDLE |
|---|---:|---:|---:|
| Fallout4.esm | 3,828 | 112 | 3,025 |
| DLCRobot.esm | 99 | 14 | 224 |
| DLCworkshop01.esm | 5 | 0 | 7 |
| DLCCoast.esm | 492 | 6 | 298 |
| DLCworkshop02.esm | 0 | 0 | 0 |
| DLCWorkshop03.esm | 41 | 0 | 10 |
| DLCNukaWorld.esm | 425 | 11 | 141 |
| Total | 4,890 | 143 | 3,705 |

All packages have one PKDT/PSDT/PKCU of 12 bytes and one each of POBA/POEA/POCA, empty and in that order. All 143 combat styles have CSGD48, CSRA4, CSCR44, CSCV4, CSFL32 and DATA4; CSME is 36 bytes in 46 records and 40 in 97, while CSLR is 12 bytes in 33, 16 in 10 and 20 in 100. CSMD is absent. All idles have ANAM8 and DATA6. DNAM/ENAM/GNAM occur 3,704/2,904/841 times; all present strings have exactly one trailing NUL. Parent links are 333 null, 1,115 AACT and 2,257 IDLE; previous links are 2,232 null and 1,473 IDLE. There are no malformed measured shapes, unresolved links, wrong-type links or record/subrecord parse failures.

New synthetic tests cover raw values, scoped package fields, location/target variants, combat size variants, conditions, malformed input and unchanged byte preservation. A seven-master corpus sweep invokes all native/C ABI getters and compares unchanged master bytes; it skips cleanly without `FO4_DATA_DIR`. Every new getter has a managed binding test. Local and CI validation are recorded separately for each published head; metadata measurements are not decoder acceptance.

Writer validation: both full suites have zero failures across 874 registered cases. The official run has 863 passes and 11 unrelated skips, including all 14 new official order/mutation cases passing; managed bindings have 86 passes. The focused seven-master writer run took 89.66 seconds. Reader regressions and the duplicate-PKCU strict-remap refusal were proven red before their fixes and green afterward. Independent read-only review and its follow-up accepted the changes. Evidence is in `build/evidence/packages-final-full.log`, `packages-final-official-full.log`, `packages-official-focused.xml`, `packages-reader-red.log`, and `packages-review-red.log` / `packages-review-green.log`.

ABI 22 is provisional. Renumber independent branches in merge order before integration. Local official-data acceptance and current-head CI are separate results.
