# FACT (Faction) field coverage

The layouts follow xEdit 93cc0bc5 and `FactionReal` (tests/test_faction_real.cpp) checks them against the official masters.

Ground truth: xEdit at 93cc0bc5, `Core/wbDefinitionsFO4.pas` `wbRecord(FACT, ...)` at line 6785 to 6846, with helpers in `Core/wbDefinitionsCommon.pas` (`wbFactionRelations` at line 8540, `wbFactionRelationToStr` at line 4646) and `wbFactionRank` at lines 6771 to 6783.

Scalars live in `schema/fallout4/fields.json` (record `FACT`) and are generated into `include/fo4/schema/generated_scalar_fields.inc`, `generated_formlink_lists.inc`, `generated_lstring_fields.inc` and `generated_record_constraints.inc`. Relations and ranks are typed in `include/fo4/forms/faction.hpp` and `src/forms/faction.cpp`.

## Authoring constraints

Subrecord minimum and maximum, as recorded in `fields.json` (`authoring`):

| Subrecord | Min | Max | xEdit | Notes |
| --- | --- | --- | --- | --- |
| EDID | 0 | 1 | 3691 | |
| FULL | 0 | 1 | 3692 | Localized |
| XNAM | 0 | 65535 | 8543 to 8553 | Relations, repeated |
| DATA | 1 | 1 | 6789 to 6810 | Default `00000000` |
| JAIL, WAIT, STOL, PLCN, CRGR, JOUT | 0 | 1 | 6811 to 6816 | |
| CRVA | 1 | 1 | 6817 to 6829 | Required; default is Arrest and Attack On Sight set |
| RNAM, MNAM, FNAM, INAM | 0 | 65535 | 6771 to 6776 | Rank groups, repeated |
| VEND, VENC | 0 | 1 | 6831, 6832 | |
| VENV | 1 | 1 | 6833 to 6842 | Required; default End Hour 24, buys stolen and non-stolen items set |
| PLVD, CITC | 0 | 1 | 6843, 6844 | |
| CTDA | 0 | 65535 | 6845 | Conditions |

## Scalar fields

| Path | Subrecord | Layout | Gate | xEdit |
| --- | --- | --- | --- | --- |
| `FACT\Flags` | DATA | u32 at 0, whole payload | Exactly 4 bytes (`ScalarError` otherwise) | 6789 to 6810 |
| `FACT\CrimeArrest` | CRVA | u8 at 0 | Payload of 12 bytes or more | 6818 |
| `FACT\CrimeAttackOnSight` | CRVA | u8 at 1 | 12 bytes or more | 6819 |
| `FACT\CrimeMurder` | CRVA | u16 at 2 | 12 bytes or more | 6820 |
| `FACT\CrimeAssault` | CRVA | u16 at 4 | 12 bytes or more | 6821 |
| `FACT\CrimeTrespass` | CRVA | u16 at 6 | 12 bytes or more | 6822 |
| `FACT\CrimePickpocket` | CRVA | u16 at 8 | 12 bytes or more | 6823 |
| `FACT\CrimeUnknown` | CRVA | u16 at 10 | 12 bytes or more | 6824 |
| `FACT\CrimeStealMultiplier` | CRVA | f32 at 12 | 16 bytes or more | 6825, 6828 |
| `FACT\CrimeEscape` | CRVA | u16 at 16 | 18 bytes or more | 6826, 6828 |
| `FACT\CrimeWerewolf` | CRVA | u16 at 18 | 20 bytes or more | 6827, 6828 |
| `FACT\VendorStartHour` | VENV | u16 at 0 | Exactly 12 bytes | 6834 |
| `FACT\VendorEndHour` | VENV | u16 at 2 | Exactly 12 bytes | 6835 |
| `FACT\VendorRadius` | VENV | u16 at 4 | Exactly 12 bytes | 6836 |
| `FACT\VendorBuysStolenItems` | VENV | u8 at 8 | Exactly 12 bytes | 6838 |
| `FACT\VendorSellsEverythingNotInList` | VENV | u8 at 9 | Exactly 12 bytes | 6839 |
| `FACT\VendorBuysNonStolenItems` | VENV | u8 at 10 | Exactly 12 bytes | 6840 |
| `FACT\VendorUnknown` | VENV | u8 at 11 | Exactly 12 bytes | 6841 |

A payload shorter than a field's gate hides that field (the existing embedded-scalar rule). A payload whose size does not match the VENV gate hides every VENV field.

`CrimeArrest`, `CrimeAttackOnSight`, `VendorBuysStolenItems`, `VendorSellsEverythingNotInList` and `VendorBuysNonStolenItems` use the `enum` presentation with xEdit's `False` (0) and `True` (1) (`wbBoolEnum`, Common 7240 to 7247). Their bounds stay 0 to 255, so the names do not restrict what is written.

## Flags (DATA)

`FACT\Flags` stores DATA as a u32 with the `flags` presentation. The names below are the `names` table of that row in `fields.json`, so `ScalarDescriptor::names` carries one single-bit mask per name. Source: `wbDefinitionsFO4.pas` lines 6791 to 6807.

| Bit | Name |
| --- | --- |
| 0x00000001 | Hidden From NPC |
| 0x00000002 | Special Combat |
| 0x00000004 | Unknown 3 |
| 0x00000008 | Unknown 4 |
| 0x00000010 | Unknown 5 |
| 0x00000020 | Unknown 6 |
| 0x00000040 | Track Crime |
| 0x00000080 | Ignore Crimes: Murder |
| 0x00000100 | Ignore Crimes: Assault |
| 0x00000200 | Ignore Crimes: Stealing |
| 0x00000400 | Ignore Crimes: Trespass |
| 0x00000800 | Do Not Report Crimes Against Members |
| 0x00001000 | Crime Gold - Use Defaults |
| 0x00002000 | Ignore Crimes: Pickpocket |
| 0x00004000 | Vendor |
| 0x00008000 | Can Be Owner |
| 0x00010000 | Ignore Crimes: Werewolf (unused) |

## Form ID fields

Schema formlink rows (`formlink_fields`), each a 4-byte payload unless noted. None is nullable, since xEdit lists no NULL target for them.

| Path | Subrecord | Targets | xEdit |
| --- | --- | --- | --- |
| `FACT\ExteriorJailMarker` | JAIL | REFR | 6811 |
| `FACT\FollowerWaitMarker` | WAIT | REFR | 6812 |
| `FACT\StolenGoodsContainer` | STOL | REFR | 6813 |
| `FACT\PlayerInventoryContainer` | PLCN | REFR | 6814 |
| `FACT\SharedCrimeFactionList` | CRGR | FLST | 6815 |
| `FACT\JailOutfit` | JOUT | OTFT | 6816 |
| `FACT\VendorBuySellList` | VEND | FLST | 6831 |
| `FACT\MerchantContainer` | VENC | REFR | 6832 |
| `FACT\RelationFaction` | XNAM (repeated) | FACT, RACE | 8545 |

## Relations (XNAM)

`forms/faction.hpp` `FactionRelation`: form ID (FACT or RACE), int32 modifier, and `FactionCombatReaction` (neutral 0, enemy 1, ally 2, friendly 3). xEdit: a 12-byte payload with the reaction member at offset 8 (lines 8544 to 8553). A payload that is not 12 bytes, a null form ID, or a reaction above 3 is malformed. Entries are read in file order. Add appends after the last XNAM, or before DATA when none exists. Set and remove take the zero-based relation index.

## Ranks (RNAM, MNAM, FNAM, INAM)

`forms/faction.hpp` `FactionRank`: u32 number (RNAM, exactly 4 bytes), optional male and female titles (MNAM and FNAM, localized strings read through `schema::read_localized_string`), and optional raw INAM bytes. xEdit: lines 6771 to 6776, a rank group starting with `RNAM` (line 6772). Titles are localized (`cpTranslate`). Each rank's members must follow the order RNAM, MNAM, FNAM, INAM, with at most one of each. Any other subrecord closes the group. Add appends after the last group, or before the first VEND, VENC, VENV, PLVD, CITC or CTDA when no rank exists. Set and remove take the zero-based rank index.

Titles in a localized plugin must be string IDs, and inline titles are refused there. Titles in a non-localized plugin must be inline text, and string IDs are refused there. Text containing a NUL is refused.

## Not covered

| xEdit element | Lines | Reason |
| --- | --- | --- |
| VENV unknown bytes 6 and 7 | 6837 | `wbUnknown(2)`, not a named field. Bytes are kept as they are on disk. |
| PLVD (Location) | 4200 to 4221, 6843 | A union decided by a Type value. No typed access. Remap of its form-ID members is `faction_plvd_formid_offsets` in `src/schema/form_link_mixed.cpp`. |
| CITC (Condition Count) | 6844 | Derived from the condition list. Not a value to edit. |
| CTDA conditions | 6845 | Shared condition machinery (`schema/ctda.hpp`). FACT is not wired into the condition scopes. |
| XNAM form ID remap | 8545 | The generic remap tables (`formid_subrecords.inc`, `structured_formids.inc`) do not list a 12-byte FACT XNAM. `src/schema/form_link_mixed.cpp` (`k_faction_layouts`, `faction_formid_offsets`) remaps it on the generic FormLink path in `src/schema/form_links.cpp`. `FactionXnamRelationFormIdRemapsAndKeepsModifierAndReaction` (`tests/test_packed_formlink_arrays.cpp`) covers it, and `WrongSizedFixedLayoutPayloadsFailClosedUnderStrictRemap` covers a wrong-size payload. Typed edits encode the form ID correctly. |
| FACT creation | n/a | `forms/faction.hpp` `create_faction` writes EDID, FULL (refused in a localized plugin), defined DATA flags and generated CRVA and VENV defaults. Relations and ranks are not set on create. Tests: `tests/test_faction.cpp`. |
| FULL | 3692 | Localized through the generic localized-field table. No typed FACT API. |
