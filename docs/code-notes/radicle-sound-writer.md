# Sound writer (SNDR, SOUN, MUSC, FSTP, FSTS)

Source for every layout, order, default and required flag: pinned TES5Edit `wbDefinitionsFO4.pas` at
`93cc0bc5a1251936c3c7859eee3150eda12a62d7` (local copy `build/xedit/wbDefinitionsFO4.pas` in the parity checkout:
SNDR 9406-9450 with `wbSNDRDataDecider` 1556-1576, SOUN 11961-11969, MUSC 8665-8684, FSTP 8686-8690, FSTS 8692-8718,
`wbConditions` 5183) and `wbDefinitionsCommon.pas` (`wbSoundDescriptorSounds` 9200, `wbObjectBounds` 9074). Official
measurements are over the seven shipped masters: 7,427 SNDR (6,730 Standard, 673 Compound, 24 AutoWeapon), 339 SOUN,
134 MUSC, 175 FSTP, 42 FSTS.

## W0 merge (`Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION`)

- origin/nomad/radicle-subrecord-authoring-20261005 (9cf04f75) was merged onto #400 (bfe2e23a). Conflicts:
  `Radicle/CMakeLists.txt` (`radicle_c` sources: W0 list plus `src/c_api_sound.cpp`),
  `Radicle/bindings/csharp/tools/generate_interop.py:HEADERS` (W0 list plus `fo4/c_api_sound.h`), `c_api.h` (ABI),
  `Radicle/tests/test_c_api_quest.cpp` (ABI pin) and the generated `RadicleNative.g.cs`. Every generated file was
  regenerated with its tool, never hand-merged.
- ABI 12 is provisional against main `c50ad4d5405e1571113019654b10e3d4a05ca0f1`; `Radicle/tests/test_c_api_quest.cpp:AbiVersionIsTwelve` pins it. Main is merged without rewriting the published sound branch. The bump also covers the
  `radicle_sound_descriptor` layout change below. Reassign from current main before merging.

## Reader fixes (`Radicle/src/environment/sound.cpp:decode_sound_descriptor`)

- CNAM is optional. xEdit's `wbSNDRDataDecider` returns 0 (the `Values` BNAM layout) when CNAM is absent, so a SNDR
  without CNAM now decodes BNAM as the six-byte values struct and exposes the kind as absent:
  `Radicle/include/fo4/environment/sound.hpp:SoundDescriptorData::kind` is `std::optional`, and
  `Radicle/include/fo4/c_api_sound.h:radicle_sound_descriptor` gained `has_kind` (C# `SoundDescriptor.Kind` is
  `uint?`). No official SNDR lacks CNAM, so official decode results are unchanged. Unknown CNAM values still fail closed.
  Tests: `Radicle/tests/test_sound.cpp:SoundTest.MissingKindUsesTheValuesLayoutAndExposesNoKind`,
  `Radicle/bindings/csharp/Radicle.Interop.Tests/SoundRecordTests.cs:MissingKindUsesValuesLayoutAndStrayFilenameFailsClosed`.
- The top-level "legacy flags" FNAM is gone (`Radicle/include/fo4/environment/sound.hpp:SoundDescriptorData` has no
  legacy-flags member; the C struct field and the C# LegacyFlags property were removed). xEdit defines FNAM only inside the rate-of-fire group
  {ITMS, INTV, FNAM, ITME}. The old decoder tested the Standard-kind FNAM branch before the rate-group branch, so a
  Standard descriptor carrying a rate group had its file name misread as flags (or failed on size). The official census
  has 120 FNAM, all inside the 24 AutoWeapon rate groups, and no top-level FNAM. FNAM is now only read as a rate file;
  one outside a group fails closed ("rate filename is out of order"). The RE record
  (`build/RE-Work/6916afb/sound-399-340/README.md`, Standard loader 0x4C1C10) shows the
  OG engine would accept a 4-byte top-level FNAM as legacy flags (remapped onto the LNAM looping bits); because nothing
  ships one and xEdit does not define it, Radicle does not expose it. Test:
  `Radicle/tests/test_sound.cpp:SoundTest.RateGroupFilenameIsNeverReadAsTopLevelFlags`.
- BNAM signedness was already right: FrequencyShift and FrequencyVariance are `itS8` (221 official Standard
  descriptors have a negative frequency shift), Priority and DbVariance `itU8`, StaticAttenuation `itU16` hundredths.
- `Radicle/src/environment/sound.cpp:decode_sound_marker` exposes REPT Stackable whenever REPT is nine bytes. xEdit has no
  form-version gate (`SetOptionalFrom(2)`); the old `form_version > 0x70` gate is removed. Officially the 66 nine-byte REPT
  are all above 0x70 and the three eight-byte ones at or below, so official results are unchanged. Test:
  `Radicle/tests/test_sound.cpp:SoundTest.LastControlsAndRepeatStackableFollowPayloadSizeNotFormVersion`.
- MUSC, FSTP and FSTS readers match xEdit. FSTS DATA reverse category order (Swimming, Sneaking, Sprinting, Running,
  Walking) was verified on official data: `DefaultFootstepSetXXX` resolves to Sneak/Sprint/Run/Walk footsteps in the
  expected categories and `FSTArmorPowerFootstepSet`'s first block is its four WaterDeep (swimming) footsteps; every
  official DATA is exactly sum(XCNT)*4 bytes.

## `when` with a missing discriminator (`Radicle/src/schema/field_address.cpp:record_discriminator_matches`)

- `FieldDiscriminator::match_absent` (`Radicle/include/fo4/schema/field_address.hpp`, JSON `"match_absent": true`, only
  valid with `when.subrecord`) makes a missing discriminator subrecord count as a match. A duplicated or short
  discriminator is still a mismatch. It exists for SNDR BNAM, whose values layout applies when CNAM is absent.
  `Radicle/tools/field_address_codegen.py:parse_when` validates it; `render_address` now always emits the trailing bool
  so GCC's `-Wmissing-field-initializers` stays quiet. Codegen tests:
  `Radicle/tools/test_field_address_codegen.py:test_when_rules`, `test_rendering_includes_the_address`.

## Registered fields (`Radicle/schema/fallout4/fields.json`, records SNDR, SOUN, MUSC, FSTP, FSTS)

All `existing_only`, `presentation: identity`, f32 bounds +-1e9.

- SNDR strings `Notes` (NNAM), `Sounds` (ANAM, `repeated`), `RateOfFireFile` (FNAM, `repeated`); LNAM u8 x4
  `ValuesUnknown`, `Looping`, `Sidechain`, `RumbleSendValue` (`payload_size` 4; Looping is an xEdit enum 0/8/16/32 that
  W0 bounds cannot restrict, so 0..255); BNAM values `FrequencyShift` i8, `FrequencyVariance` i8, `Priority` u8,
  `DbVariance` u8, `StaticAttenuation` u16 (raw hundredths), `payload_size` 6, `when` CNAM u32 in {Standard, Compound}
  with `match_absent`; `RateOfFireRpm` (INTV u32, `repeated`, `payload_size` 4). Links `Category` (GNAM, SNCT),
  `AlternateSoundFor` (SNAM, SNDR), `OutputModel` (ONAM, SOPM), `BaseDescriptor` (BNAM, SNDR, `when` CNAM == AutoWeapon),
  `Descriptors` (DNAM, SNDR, `repeated`); none nullable, as in xEdit.
- INTV and FNAM occur only inside rate groups and each complete group has exactly one of each (the reader enforces it),
  so the occurrence index is the group index and an in-place edit never changes the group structure. xEdit sorts the
  groups on RPM (`wbRArrayS`, all 24 official records are sorted) and the RE record says selection assumes ascending
  INTV; Radicle writes in place and never re-sorts, so keeping the order is the caller's job.
- SOUN `RepeatMinTime`, `RepeatMaxTime` (f32 at 0/4) and `RepeatStackable` (u8 at 8, bounds 0..1 for `wbBoolEnum`; all
  66 official values are 0 or 1) without `payload_size`: the prefix rule makes Stackable absent on an eight-byte REPT.
  Link `SoundDescriptor` (SDSC, SNDR).
- MUSC `Flags` (FNAM u32), `Priority` and `Ducking` (PNAM u16 at 0/2, raw hundredths), `FadeDuration` (WNAM f32), all
  `payload_size` 4. Link `Tracks` (TNAM, MUST, nullable, `stride` 4).
- FSTP string `Tag` (ANAM), link `ImpactDataSet` (DATA, IPDS, nullable).
- FSTS link `Footsteps` (DATA, FSTP, `stride` 4). The index is the physical element in stored (reverse category) order.

## Not registered (read-only through the decoders, OPAQUE for writing)

- SNDR CNAM: W0 bounds cannot restrict a u32 to the three enum values and changing CNAM switches the BNAM layout.
- SNDR ITMC: the rate-group count; editing it desynchronises the groups.
- FSTS XCNT: the five counts define how DATA splits into categories. Element edits of DATA are allowed; inserting or
  removing elements (which needs XCNT changed in step) is not.
- MUSC TNAM single-track insert/remove: JSON FormLink lists are per-subrecord only and the packed-array storage requires
  a counter subrecord, so a counterless packed list is not expressible. Tracks can be edited and counted; TNAM as a whole
  can be added (one NULL track) or removed.
- SNDR CTDA: handled by the existing condition code; listed in authoring only for order.

## Authoring (`fields.json` `authoring` blocks)

- SNDR order EDID, NNAM, CNAM, GNAM, SNAM, ANAM*, ONAM, CTDA*, LNAM, BNAM, DNAM*, ITMC; no entry is required in xEdit.
  Defaults: NNAM and ANAM a single NUL (the W0 empty-string convention), LNAM `00000000` (zeroed struct; Looping 0 is the
  enum's `None`). `coupled`: CNAM (removing an AutoWeapon CNAM would turn the BNAM link into values, adding one needs a
  value xEdit gives no default for), BNAM (its layout depends on CNAM, so one fixed default is wrong for AutoWeapon and
  W0 FormLink fields cannot pin a payload size), ITMC (counts the rate groups). ANAM and DNAM are `uncounted` repeats
  (`wbRArray` with no count). GNAM, SNAM, ONAM and DNAM have no default (their targets exclude NULL). CTDA is a counted
  repeat and so not authorable.
- The rate-group members ITMS/INTV/FNAM/ITME are deliberately not listed: W0 orders signatures linearly and five
  interleaved groups would violate any linear order. The 24 official AutoWeapon records therefore carry unlisted
  subrecords, so add on them is `unknown_order`; field edits and removal of listed entries still work.
- SOUN order EDID, OBND (required, default twelve zero bytes, the zeroed `wbObjectBounds` struct), SDSC (required,
  `[SNDR]` without NULL, no default), REPT (default nine zero bytes: 0.0, 0.0, false).
- MUSC order EDID, FNAM, PNAM, WNAM (all required), TNAM. Defaults: FNAM `00000000`, PNAM `32000000` (Priority
  `SetDefaultEditValue('50')`, ducking 0), WNAM `00000000` (0.0), TNAM `00000000` (one NULL track; NULL is an allowed
  target).
- FSTP order EDID, DATA (required, default NULL; `[IPDS, NULL]`), ANAM (required, default empty string).
- FSTS order EDID, XCNT, DATA, both required and `coupled`.
- CREATE: SNDR creates EDID only (xEdit requires nothing); MUSC creates EDID + FNAM + PNAM + WNAM; FSTP creates EDID +
  DATA + ANAM. SOUN is refused with `no_default` (SDSC has no NULL); FSTS is refused with `not_authorable` (XCNT and DATA
  are coupled, and W0 defaults cannot be zero-length, which an empty DATA would need).
- `Radicle/tests/test_sound_writer_real.cpp:SoundWriterReal.EveryOfficialRecordIsListedOrderedAndPreflightClean`: every
  official sound record passes `preflight_authoring_record` (0 order violators); every subrecord is listed except the
  rate-group members of the 24 AutoWeapon SNDR, which the test counts (`rate_group_records`).

## Link remapping (`Radicle/src/schema/form_link_mixed.cpp:packed_formid_array_offsets`)

- FSTP DATA and FSTS DATA are now scanned as packed FormID arrays (beside MUSC TNAM; a DATA that is not a multiple of four bytes throws on strict remap). Before, the generic signature table did not treat
  DATA as a link in these records, so master remapping and rekeying skipped every footstep link and impact data set.
  The rest was already covered: SNDR GNAM/SNAM/ONAM/DNAM and the 4-byte AutoWeapon BNAM by the generic table (the
  6-byte values BNAM never matches its size-4 rule; SNDR CNAM and LNAM are on the corpus deny list), SOUN SDSC, and
  MUSC TNAM by the same packed-array rule. Test:
  `Radicle/tests/test_sound_subrecords.cpp:SoundSubrecords.FormLinkScanVisitsExactlyTheLinkSlots`.

## Tests

- Native: `Radicle/tests/test_sound_authoring.cpp` (fields by kind including missing CNAM, signedness and range refusal,
  links null/wrong type/unknown master, repeated ANAM/DNAM/INTV/FNAM, MUSC TNAM and FSTS element edits),
  `Radicle/tests/test_sound_subrecords.cpp` (add/remove positions and refusals, unknown order, create and refusal, link
  scan), fixture `Radicle/tests/sound_writer_fixture.hpp`; reader fixes in `Radicle/tests/test_sound.cpp`.
- C ABI: `Radicle/tests/test_sound_writer_c_api.cpp:SoundWriterCApi`. C#:
  `Radicle/bindings/csharp/Radicle.Interop.Tests/SoundAuthoringTests.cs`.
- Official: `Radicle/tests/test_sound_writer_real.cpp` (order check on a sound-only sample of each master, first and
  last slot of every scalar/string path plus the last slot of every link path compared against an independently
  patched copy, and a full-master link/add/remove sweep over the first 60 records of each type plus every
  AutoWeapon SNDR, where every other record of the master stays byte-identical and every sound record still decodes;
  the cap exists because each local-target link check walks the whole master, which made all 5,475 Fallout4.esm SNDR
  take many minutes).

## Radicle/src/plugin/field_writer.cpp:scalar_slots — rate-group ownership
The rate fields must use the same complete ITMS/INTV/FNAM/ITME blocks and ITMC count as `Radicle/src/environment/sound.cpp:decode_sound_descriptor` (pinned FO4 lines 9438–9449). Raw occurrence indexing previously exposed a stray top-level FNAM or incomplete blocks. `Radicle/tests/test_sound_authoring.cpp:MalformedRateGroupsRefuseCountsAndEditsWithoutChangingBytes` covers stray filenames, wrong counts, incomplete groups, nonempty markers, malformed RPM/string payloads and duplicate count/RPM fields. Both count and edit paths must refuse these records before writing bytes. Valid blocks keep their existing occurrence order; callers remain responsible for RPM ordering.

## Radicle/schema/fallout4/fields.json:SNDR, SOUN, FSTP — fixed link widths
SNDR GNAM/SNAM/ONAM/DNAM and AutoWeapon BNAM, SOUN SDSC, and FSTP DATA are exactly four-byte links in pinned FO4 lines 9406–9450, 11961–11969 and 8686–8690. A longer payload has no verified link layout. `Radicle/tests/test_sound_authoring.cpp:UnknownFixedLinkWidthsRefuseWithoutChangingBytes` checks count and mutation refusal on six/eight-byte payloads. The shared `Radicle/src/plugin/field_writer.cpp:formlink_slots` size constraint must apply before slot exposure or writes, rather than treating the first four bytes as an editable link.

## Radicle/src/environment/sound.cpp:decode_sound_marker, decode_footstep_set — writer shape checks
Pinned FO4 lines 11964–11969 establish the eight/nine-byte SOUN REPT forms; lines 8692–8718 define FSTS XCNT and the five DATA category arrays. The shared field writer must use the same REPT width and FSTS count validation as the readers before counting or editing. `Radicle/tests/test_sound_authoring.cpp:UnknownRepeatWidthsRefuseWithoutChangingBytes` and `MalformedFootstepCountsRefuseWithoutChangingBytes` cover unsupported repeat widths, missing/short/duplicate counts, duplicate DATA and mismatched/overflow-scale totals. These checks prevent editing an unverified prefix or a list whose category boundaries are unavailable.

## Radicle/tests/test_sound_real.cpp:SoundRealData.DecodesEverySoundRecordThroughNativeAndAbiAndPreservesBytes
Every official sound record compares native and C presence flags, numeric values, file strings (with the documented Windows-1252 to UTF-8 conversion), resolved link keys, arrays and category order. Zero-family masters report zero totals and still check preservation. `Radicle/tests/test_sound_writer_real.cpp:LinksAndOptionalSubrecordsEditOfficialRecordsAndKeepEveryOtherRecordByteIdentical` also validates byte identity when its family is absent instead of skipping. Missing installed data remains the only family-fixture skip condition. The new stateless parsing functions `Radicle/src/environment/sound.cpp:decode_sound_rate_files`, `decode_sound_repeat` and `decode_footstep_counts` share the existing readers' shape checks with writer count/edit paths; the C ABI is unchanged. The fixed-link payload-size metadata is shared with #427 and only applies after the kind discriminator selects the applicable layout.
