# Stored package, combat-style and idle read slice

This partial #345 slice exposes stored PKDT, PSDT, PKCU, package links and top-level conditions, combat-style values, and idle selection data. It does not schedule or execute NPC behavior. [RE #407](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/407) tracks procedure/input discrimination and runtime selection semantics. Procedure trees, location/target variants, data-input streams and VMAD fragments remain opaque.

## Sources and scope

The pinned compatibility source is [xEdit Core/wbDefinitionsFO4.pas](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas), commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`: CSTY 6233–6307, IDLE 9646–9671 and PACK 10526–10667. These are disk schema descriptions, not runtime execution proof. The private index at `ReverseEng/Fallout4_Runtime_Research`, commit `f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a`, `RE-Work/subsystems/plugin-io-garden-tools/evidence-index-garden-tools/README.md`, verifies CK tag sets and CSTY sizes, but reports PACK/IDLE payload layouts NOT FOUND. Its unresolved printed combat tag typo is not used as a tag name.

## PACK

PKDT is 12 bytes: u32 general flags, u8 type, interrupt override, preferred speed and unknown byte, u16 interrupt flags and two unknown bytes. PSDT is 12 bytes: signed bytes month, weekday, date, hour and minute, three unused bytes, then u32 encoded duration. These signed values and duration are returned unchanged; no defaults, enum ranges or unit conversion are invented.

PKCU is 12 bytes: u32 data-input count, package-template FormID and u32 version counter. CNAM combat style and QNAM owner quest belong to the prelude before PKCU; later CNAM values are not mistaken for a record-level link. Only prelude CTDA/CIS lists are decoded with the existing condition model. POBA, POEA and POCA are empty on-begin/end/change markers, each with an optional four-byte INAM idle link in its own scope. All remaining post-PKCU subrecords are exposed as ordered opaque signature/payload views, including procedure and data-input condition lists, instead of flattening them into the top-level conditions. Duplicated known markers and malformed known sizes fail closed.

## CSTY

CSGD is 12 f32 values in the pinned schema's order: offensive, defensive and group-offensive multipliers; melee, magic, ranged, shout, unarmed and staff equipment-score multipliers; avoid, dodge and evade threat chances. CSME has nine or ten f32 values: attack-staggered, power-attack-staggered, power-attack-blocking, bash, bash-recoil, bash-attack, bash-power-attack, special-attack, block-when-staggered, and optional attack-when-staggered multipliers.

CSRA and CSCV are individual f32 ranged-accuracy and cover-search values. CSCR is 44 bytes: nine f32 values (circle, fallback, flank distance, stalk time, charge distance, throw probability, sprint-fast probability, sideswipe probability and disengage probability), u32 throw-max-targets and f32 flank variance. CSLR accepts three, four or five f32 values: strafe, adjust-range, crouch, optional wait and optional range multipliers. CSFL has eight f32 values: hover chance, dive-bomb chance, ground-attack chance, hover time, ground-attack time, perch-attack chance, perch-attack time and flying-attack chance. DATA is raw u32 flags. Omitted optional values remain absent; arrays keep their actual stored count. No runtime numeric bounds or selection effects are asserted.

## IDLE

ANAM is eight bytes: parent and previous FormID links. DATA is six bytes: loop minimum and maximum bytes, raw flag byte, animation-group section byte and u16 replay delay. DNAM behavior graph, ENAM animation event and GNAM animation file are strict NUL-terminated stored strings. The existing CTDA model supplies raw conditions and CIS operands. Loop/flag semantics are not interpreted, and strings follow the existing C ABI Windows-1252 to UTF-8 policy.

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

New synthetic tests cover raw values, scoped package fields, combat size variants, conditions, malformed input and unchanged byte preservation. A seven-master corpus sweep invokes all native/C ABI getters and compares unchanged master bytes; it skips cleanly without `FO4_DATA_DIR`. Every new getter has a managed binding test. Build and test execution is through CI only, as requested; metadata measurements do not establish that the new decoder passed the real-data tests.

ABI 13 is reserved after a fresh main ABI 4 / PR #383 ABI 7 check, following this campaign's independent 8–12 branches. All branches need ABI reconciliation before merge. GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` has these record names only in `Services/RecordVocabulary.cs`. GardenEngine #151 needs an actual package/combat/idle consumer before a meaningful probe.
