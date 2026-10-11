# AVIF and PERK evidence boundary

Anchors: `Radicle/src/c_api_records.cpp:radicle_record_copy_subrecord`, `Radicle/src/schema/form_link_mixed.cpp:perk_formid_offsets`, `Radicle/tests/test_actor_value_perk_real.cpp:NativeAndCAbiPayloadsAgreeAndReparseIdempotently`.

Issue #421 needs typed stored-field decoding, not a gameplay formula implementation. The existing generic payload API is the baseline: it exposes ordered subrecords without inventing their semantics. The corpus test checks native/C payload agreement, including every unknown byte, and re-parse idempotence. It does not check whole-plugin byte identity: `Radicle/src/plugin/writer.cpp:write_plugin` returns the source bytes for an unmodified plugin, so a byte comparison there is tautological. It does not constitute typed AVIF/PERK decoding or live runtime acceptance.

## Sources and confidence

The pinned format source is xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas` (SHA256 `f0bacc90a7dbd62bf90ff570d1974932bd4b99488bd0da9bbcf4e01abdfb9c5e`). Its local copy was compared byte-for-byte with the raw source at that commit. PERK is declared at lines 7582–7691; AVIF at 7826–7878. These are format definitions, not executable traces.

The existing private J13/J14 installed-record captures and the static OG report linked from [GardenEngine #194](https://git.nomadicinteractive.dev/garden-engine/GardenEngine/issues/194#issuecomment-5499) corroborate selected stored fields. J13 includes an individually fingerprinted PERK and 617 winning PERK payloads; its AVIF file contains 433 keys/editor IDs rather than complete payloads. J14 directly inspects Strength AVIF and a quest-training effect. The report carries the OG executable identity and verification stamps, but is remote static evidence, not a local decompile or AE comparison. Its referenced CK record document was not found in the inspected local evidence roots. No runtime offset or stub recommendation from that report is a shipping contract here.

`verified` below means directly corroborated stored bytes or a recorded static use, within the stated source scope. `partial` means a pinned format description or a single observed shape without a complete loader trace. `not found` identifies missing evidence, not proof that a feature cannot exist.

## AVIF field inventory

| Stored field | Evidence | Status and remaining boundary |
|---|---|---|
| EDID, FULL, DESC, ANAM | xEdit 7827–7830; J14 Strength record; OG report SPECIAL records | Verified presence on observed records; FULL/DESC/ANAM localized handling must reuse the existing localization APIs. General cardinality and malformed-shape policy remain partial. |
| NAM0 default-value payload | xEdit 7831 describes a float; OG report observes SPECIAL NAM0 = 0 | Partial general float interpretation; older form-version behavior is explicitly uncertain in xEdit. Do not synthesize a default from AVFL. |
| AVFL flags payload | xEdit 7832–7865; OG report observes the SPECIAL flags word | Verified observed word, partial general bit semantics. Preserve all bits; do not turn labels into runtime clamps or formulas. |
| NAM1 type payload | xEdit 7866–7877 describes an integer and enum labels | Partial; no AVIF loader trace located. Keep unknown values rather than guessing an enum ceiling. |
| Independent minimum/maximum fields | No independent fields in this pinned AVIF declaration | Not found. Flag labels and menu limits do not establish stored min/max fields. |
| Perk tree/rank links | J14 Strength record has no tree links; no such field in pinned AVIF declaration | Not found. Do not import another game's AVIF tree layout. |
| Runtime actor-value behavior | OG report covers selected SPECIAL uses; generic AVIF loader and AE comparison absent | Partial. Actor-value computation remains GardenEngine #194 scope. |

## PERK field inventory

| Stored field | Evidence | Status and remaining boundary |
|---|---|---|
| Record metadata, FULL, DESC | xEdit 7582–7589; J13 capture; OG report perk-grid consumers | Verified observed metadata and display fields; reuse generic/localized APIs. |
| ICON | xEdit 7590 describes an image string | Partial; retain the payload without assuming an asset path or default. |
| Top-level DATA | xEdit 7592–7602 describes trait, level, ranks and optional playable/hidden suffixes; J13 observes five bytes | Verified observed five-byte shape; shorter historical suffixes remain partial. xEdit declares unsigned bytes while the OG report names level/ranks signed bytes. High-bit interpretation needs a loader/consumer trace before choosing a public semantic type. |
| Acquisition conditions | xEdit 7591; J13/J14 and OG report distinguish conditions before the first PRKE | Verified observed scope; do not flatten effect conditions into acquisition conditions. Reuse CTDA decoding without evaluating eligibility. |
| SNAM, NNAM | xEdit 7603–7604; J13 resolved next links; OG report next-rank consumer | Verified observed link fields; chain ordering and cycle interpretation are consumer behavior, not decoder constants. |
| FNAM | xEdit 7605; OG report correlates the stored path with a grid display field | Partial beyond the observed path; no arbitrary asset/runtime behavior claim. |
| PRKE header | xEdit 7608–7612; J13 observed type/rank/priority bytes | Verified observed three-byte framing, partial type/rank semantics. The type byte selects the DATA shape below. Unknown variants must remain opaque. |
| Effect DATA union | xEdit 7613–7642 and `wbPerkDATADecider` (line 1411); J14 fingerprints quest/stage payload; J13 captures entry-point payload | Verified observed quest/stage shape; general union and unsupported variants remain partial. Quest and Ability FormIDs are remapped by PRKE type (see the remap rules below). |
| PRKC and effect CTDA | xEdit 7643–7647; J13 observed tab and condition scopes | Verified observed boundaries; tab semantics beyond the captures remain partial. |
| EPFT, EPFD | xEdit 7648–7687 and `wbEPFDDecider` (line 1430); existing qualified FormLink handling and tests | Partial general union. Four-byte EPFT 3/4/5 FormLink variants and the AVIF + float shapes (EPFT 8, and EPFT 2 with entry Function 5/12/13/14) are promoted; the two-float shape stays opaque. See the remap rules below. |
| EPFB fragment/entry identifier | xEdit 7661 calls it a unique perk entry ID; J13 observes the bytes | Partial. Do not equate this identifier with a VMAD fragment index without a loader trace. |
| EPF2, EPF3 | xEdit 7662–7670; existing FormLink regression asserts EPF3 preservation; the new generic-payload test covers every present EPF2/EPF3 byte | Partial label/flags interpretation. Preserve localized/unknown bytes and the exact size; older notes describing EPF3 as a generic 32-bit flags field do not override the pinned struct. |
| PRKF end marker | xEdit 7689; J13/J14 observed empty marker | Verified observed scope delimiter; malformed, missing and duplicate delimiters require a fail-closed typed decoder policy. |
| VMAD fragments | Existing `Radicle/src/schema/vmad.cpp` PERK routing | Partial for this issue; reuse the existing parser, then prove EPFB/fragment correspondence separately. |

## PERK FormID remap rules

Status: partial. Shapes are taken from the pinned xEdit source cited below; the corpus counts are reviewer-reported and TODO-verify against the official-master run.

Anchors: `Radicle/src/schema/form_link_mixed.cpp:perk_formid_offsets`, `Radicle/src/schema/form_link_mixed.cpp:mixed_formid_offsets`, `Radicle/tests/test_perk_entrypoints.cpp:PerkEntryPointRemapsOnlyFormIdEpfdVariants`, `Radicle/tests/test_perk_entrypoints.cpp:PerkEffectDataRemapsOnlyQuestAndAbilityFormIds`.

Each effect starts at PRKE, whose type byte at offset 0 selects its DATA shape. A DATA or EPFD payload is classified by the nearest preceding PRKE. The EPFT is the nearest preceding EPFT, and the EPFT state resets at each PRKE.

- PRKE type 0 (Quest + u16 stage, `wbDefinitionsFO4.pas:7614-7617`): the Quest FormID at offset 0 is remapped; the stage is not. TODO-verify: 101 six-byte DATA payloads in vanilla masters.
- PRKE type 1 (Ability, `wbDefinitionsFO4.pas:7618`): the four-byte SPEL FormID at offset 0 is remapped. TODO-verify: 64 four-byte DATA payloads.
- PRKE type 2 (Entry Point, `wbDefinitionsFO4.pas:7619-7640`): three bytes and no FormID. TODO-verify: 828 three-byte DATA payloads. Its Function byte (offset 1) feeds the EPFD rule.
- Top-level PERK DATA (five bytes, before the first PRKE) is not a FormID payload.
- EPFD (`wbDefinitionsFO4.pas:7671-7687`; decider `wbEPFDDecider`, line 1430): EPFT 3, 4 or 5 with a four-byte EPFD is a FormID at offset 0. EPFT 8 (AVIF + float) is a FormID at offset 0 when the EPFD is at least eight bytes. EPFT 2 has the same shape when its entry-point Function is 5, 12, 13 or 14, read from the type-2 effect DATA. Other EPFT 2 Functions and the two-float shape stay opaque. TODO-verify: 70 EPFT 8 records in vanilla masters, all with eight-byte EPFD.
- EPFD is optional. TODO-verify: EPFT 4 occurs 27 times with no EPFD; EPFT 3 never occurs; the only four-byte FormID EPFD observed is EPFT 5.
- Malformed sizes stay opaque without a strict-remap error, as the existing EPFD path already did. Known ceiling: a malformed PERK DATA or EPFD payload can keep a stale FormID under master remap.
- NO-EQUIVALENT: Num Ranks and Playable defaults. `wbDefinitionsFO4.pas:7595-7601` declares Num Ranks with `wbPERKNumRanksAfterLoad` (`wbDefinitionsCommon.pas:1033`), which rewrites 0 to 1 on load; Playable defaults to True. Radicle's generic payload API does not interpret DATA, so neither default is reproduced.

## Next implementation gate

Anchors: `Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION`, `Radicle/bindings/csharp/tools/generate_interop.py`, `Radicle/src/schema/vmad.cpp`.

First capture complete installed AVIF/PERK payloads through the existing C ABI and compare each ordered payload with the native reader. Record master hashes, form versions, subrecord sizes and separate top-level/effect DATA shapes. Validate preserved bytes without promoting runtime formulas. Then implement only the corroborated stored fields with explicit presence/opaque status, use scoped CTDA and existing VMAD handling, and regenerate the C ABI/C# layout checks when an API is actually added. ABI 11 is the main baseline for this evidence checkpoint; the next version must be reconciled against current main at integration time. OG 1.10.163 and AE 1.11.240 corpus identity and native loader evidence remain separate gates.

The outstanding loader questions are tracked in [RE request #443](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/443). The test reports all physical AVIF/PERK records in each master, including overrides, rather than only winning records. Its DATA size labels distinguish bytes before versus after the first PRKE; they do not assert that every later DATA belongs to a validated effect scope.

The first configured run passed all seven cases without skips: 433 physical AVIF and 656 physical PERK records. Every observed AVIF NAM0/AVFL payload was four bytes; NAM1 was absent on 44 base-master AVIF records. Every observed PERK DATA before the first PRKE was five bytes; later DATA payloads had sizes three, four or six. All 27 observed EPF3 payloads were two bytes. These are corpus observations, not exhaustive acceptance rules. Master hashes and pinned-source equality are retained in local `provenance.json` (not committed); field-size distributions and the seven per-master counts are in `official-payloads.log` and `.xml` (not committed). The installed executable reports 1.11.240.0; that alone does not prove separate OG/AE master identities.
