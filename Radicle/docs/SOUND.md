# Sound record read surface (#340)

Compatibility layout evidence: TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`.
These declarations establish stored-field compatibility, not runtime playback.
The authenticated private runtime evidence index at commit
`f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a`, section a-340, confirms CK tag
inventories. The newer [verified runtime record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/sound-399-340/README.md)
supplies the OG component-hook grammar: Standard loader address 0x4C1C10,
AutoWeapon loader 0x2CB540 and SOUN loader 0x55A1E0.

- SNDR lines 9406–9450: CNAM selects Standard `1EEF540A`, Compound
  `54651A43`, or AutoWeapon `ED157AE3`. GNAM category, SNAM alternate,
  ONAM output model and repeated DNAM descriptor links are four bytes.
  Repeated ANAM file paths use the shared helper at Common lines 9200–9203.
  Standard BNAM is six bytes: signed frequency shift/variance, unsigned
  priority/dB variance, and attenuation hundredths as u16. AutoWeapon BNAM
  is a four-byte base-descriptor link; the decider is FO4 lines 1556–1576.
  LNAM has four stored bytes; the loop byte is exposed raw, as are the
  remaining control bytes. Repeated LNAM uses the last stored value. FNAM
  exists only inside the ITMS/INTV/FNAM/ITME rate blocks (counted by ITMC),
  which describe auto-weapon file alternatives; a top-level FNAM is not an
  xEdit field, never occurs in official data and fails closed. A missing CNAM
  selects the values BNAM layout, as xEdit's decider does, and the kind is
  reported as absent.
- SOUN lines 11961–11970: SDSC is its SNDR link; REPT stores two floats
  and an optional stackable byte (`SetOptionalFrom(2)`): accept eight or nine
  bytes without inventing the missing byte's default. The optional byte is
  exposed whenever REPT is nine bytes (xEdit has no form-version gate).
- MUSC lines 8665–8684: FNAM u32 flags, PNAM priority/ducking as two u16,
  WNAM float fade duration, TNAM packed four-byte MUST links.
- FSTP lines 8686–8690: DATA is an IPDS link, ANAM a string tag.
- FSTS lines 8692–8718: XCNT has five u32 counts (walking through swimming),
  while DATA stores matching packed FSTP links in reverse category order.
- ACTI lines 5648–5649: SNAM/VNAM are looping/activation SNDR links.

The 2026-10-04 independent installed-data survey measured 7,427 SNDR,
339 SOUN, 134 MUSC, 175 FSTP and 42 FSTS versions across all seven masters.
SNDR kinds: 6,730 Standard, 673 Compound, 24 AutoWeapon. Every Standard
BNAM is six bytes, every AutoWeapon BNAM four, and Compound has none.
LNAM is four bytes on 6,730 records. All 1,742 DNAM occurrences are four
bytes. The 24 auto-weapon records have 120 empty ITMS/ITME markers and
120 four-byte INTV values. SOUN REPT has 66 nine-byte and three eight-byte
variants. No Bethesda payloads are retained in the survey output.

The linked record verifies descriptor dispatch and several selection/control
paths statically in OG. Category mixing effects, alternate substitution,
output-model attenuation curves and live playback remain unresolved. Readers
expose the stored fields and links; they do not choose a playback file or invent
an audio engine. Unknown descriptor kinds/sizes fail closed and all untouched
bytes remain in the original container.
The remaining runtime questions are tracked in
[#399](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/399).

The native environment read functions are in `environment/sound.hpp`; the
separate `c_api_sound.h` exposes owned descriptor/music/footstep-set arrays and
fixed marker/footstep views, and `radicle_sound_resolve_files` returns an owned file list released by `radicle_sound_file_list_free`. The generator reads that header and C# copies the
arrays before releasing their holders. The ABI is `RADICLE_ABI_VERSION` in `Radicle/include/fo4/c_api.h`.

`test_sound_real.cpp` visits every sound-record version through both native
and C ABI getters, checks native/C value agreement for all exposed fields, then compares untouched serialization with the original
master bytes, and each SNDR record slice after a write and reread. It counts SOUN markers whose SDSC resolves in the loaded master but does not give a non-empty path (`SoundMarkerUnresolved`); markers whose SDSC is not in that master are counted separately (`SoundMarkerCrossMaster`). It skips cleanly with game data unset. Native synthetic cases
exercise union selection, malformed groups, optional tails and reversed
category ordering; managed getter tests cover all five record families.
Local and CI results are recorded separately for each published head.

The 2026-10-04 GardenEngine audit at main `10ea81f1590b06dad962ff86721c0a55f00ef173` found no SOUN/SNDR
consumer or marker-to-descriptor conversion path. Record vocabulary and generic
reference discovery are present; generic Godot audio commands do not consume
these forms. Its #144 audio runtime must supply that path before the requested
GardenEngine probe can exercise it.

## Marker-to-file resolution

`resolve_sound_files` (`src/environment/sound.cpp:resolve_sound_files`, C ABI `radicle_sound_resolve_files`) returns the sound file paths that a SOUN marker or SNDR descriptor plays:

- SOUN: follows its SDSC link to the SNDR.
- Standard (CNAM `1EEF540A`, or no CNAM): the ANAM files in stored order. A descriptor with no ANAM files resolves to an empty list.
- Compound (`54651A43`): each DNAM child resolved recursively, concatenated in stored order. The Compound's own ANAM and rate blocks are not read.
- AutoWeapon (`ED157AE3`): the BNAM base descriptor resolved recursively, then its rate files in stored order.

Chains are capped at eight descriptor levels, and a descriptor that reaches one of its own ancestors is a cycle; a child shared by two branches is allowed. A resolve also visits at most 4096 descriptors (`k_max_sound_visits`) and emits at most 4096 files (`k_max_sound_files`), so a wide fan-out cannot blow up. Shipped data peaks at depth 2 and fan-out 10. A missing or unresolved link, a marker without SDSC, a non-SNDR target, an unknown kind, a depth overflow, a visit or file limit overflow or a cycle fails closed: the native call throws `WorldDecodeError`, the C ABI returns a non-OK status with a null list, and no partial list is returned.

Divergences from the runtime, recorded here and not changed in code:

- Compound duplicates: `resolve_sound_files` keeps stored DNAM order and duplicates. The engine collects DNAM children in a set, so duplicates collapse and record order is lost. A caller that needs engine playback must collapse duplicates itself.
- AutoWeapon zero-rate entries: `resolve_sound_files` still lists the files of INTV 0 entries. The engine drops them at load; `select_rate_file_index` applies that runtime view.
- Missing CNAM: the stored decoder and `resolve_sound_files` keep the Standard values layout. The RE (#399 comment 7869) reports that an unknown or missing CNAM leaves no runtime implementation.
- Cycles: Radicle fails closed on descriptor cycles. The RE (#399 comment 9710) reports that the engine has no working cycle guard and recurses.

## Answered RE details (OG 1.10.163)

Recorded from [#399](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/399) comments 7869, 9710 and 9761 and the [#340](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/340) comment 7870. AE and NG were compared structurally only.

- CNAM is a u32 equal to the crc32 of the runtime class name (zero seed, no final xor): Standard `1EEF540A`, Compound `54651A43`, AutoWeapon `ED157AE3`. The decoder uses exactly these values.
- REPT: the RE states that the ninth byte exists only for record versions above 0x70 (its meaning is inferred). The decoder instead keys on payload size (eight or nine bytes) and `test_sound.cpp:LastControlsAndRepeatStackableFollowPayloadSizeNotFormVersion` pins that choice. The version gate is not implemented; the installed fixtures (SOUN `0x23A65B` version 129 with nine bytes, `0x3FBA9` version 112 with eight bytes) agree with both rules, and the full-corpus agreement is not checked here.
- BNAM: byte 0 is a signed frequency shift with factor `clamp(1 + shift * 0.01, 0.1, 2.0)`. Bytes 4 and 5 are a u16 static attenuation. The field name `attenuation_hundredths` is inferred from PDB accessor naming (7869); comment 9710 reports the engine conversion as millibels, `2000 * log10(clamp(x, 1e-5, 1))`. The stored value stays raw and no converted getter is provided. Byte 1 is frequency variance (±byte/100 jitter) and byte 2 is priority; byte 3 adds a random dB variance when nonzero.
- LNAM: byte 1 loop bits `0x08`, `0x10`, `0x20` (mask `0x38`), byte 2 sidechain send, byte 3 small rumble (low nibble) and big rumble (high nibble).
- Alternate (SNAM) substitution at play time, category and output-model effects, and the Envelope Fast/Slow behavior are not implemented.

## Playback helpers

These helpers are deterministic and take the random draw and the chosen rate as inputs; none of them chooses a file on its own or plays anything. The derived values are not stored fields.

- `select_rate_file_index` (C ABI `radicle_sound_select_rate_file`): the AutoWeapon nearest-rate rule. Entries with rate 0 (INTV 0) are skipped and the result is the stored index in the rate list. At or below the first runtime entry returns the first, at or above the last returns the last, and otherwise the nearer neighbor wins with ties going to the higher entry. With no runtime entries it returns no index, and the caller uses the base file. The rule is stated for ascending rates, which every installed fixture has; the RE does not state the order of an unsorted list.
- `select_standard_file_index` (C ABI `radicle_sound_select_standard_file`): the Standard no-repeat rule with an injected draw in `[0, count)`. If `count > 1` and the draw equals the previous pick, the result is `(draw + 1) % count`; otherwise the draw. A count of one always returns 0. A draw at or above the count is rejected.
- Derived frequency factor (`sound_frequency_factor`, C ABI `radicle_sound_frequency_factor`): `clamp(1 + shift * 0.01, 0.1, 2.0)` from BNAM byte 0. Shift +127 gives 2.0 and -100 gives 0.1.
- Derived looping (`sound_is_looping`, C ABI `radicle_sound_is_looping`): true when LNAM byte 1 has any of `0x08`, `0x10` or `0x20`. This is the any-loop test only; the Envelope Fast and Slow bits are not separated.
- Derived sidechain send (`sound_sidechain_send`, C ABI `radicle_sound_sidechain_send`): LNAM byte 2 times 0.01, clamped to [0, 1].
- Derived rumble sends (`sound_small_rumble_send` and `sound_big_rumble_send`, C ABI `radicle_sound_small_rumble_send` and `radicle_sound_big_rumble_send`): LNAM byte 3 low and high nibbles, each divided by 15. Nibble 0xF gives 1.0. Per RE 7869 the engine applies rumble sends only when the rumble-enabled flag is set and the value is nonzero; the helpers return only the scaled nibble, and the caller must gate on that flag.

The C# wrappers are `RadicleEnvironment.SelectRateFileIndex`, `SelectStandardFileIndex`, `SoundFrequencyFactor`, `SoundIsLooping`, `SoundSidechainSend`, `SoundSmallRumbleSend` and `SoundBigRumbleSend`.

## Editing

All edits go through the W0 schema surfaces: `radicle_plugin_set_record_scalar_at` / `PluginFile.SetScalar`,
`radicle_plugin_set_record_string` / `SetString`, `radicle_plugin_set_record_formlink` / `SetFormLink`,
`radicle_plugin_record_field_count` / `FieldCount`, `radicle_plugin_add_subrecord` / `AddSubrecord`,
`radicle_plugin_remove_subrecord` / `RemoveSubrecord` and `radicle_plugin_create_record` / `CreateRecord`.
Every WRITE is VALIDATE: values are range-checked on write and checked by `preflight_record`; links are
type-checked against local targets on write and by the error check after save. OPAQUE means readable through the
decoders but not writable per field.

| Family | READ | WRITE | ADD | REMOVE | CREATE | REMAP |
| --- | --- | --- | --- | --- | --- | --- |
| SNDR NNAM notes | raw | `SNDR\Notes` | yes, empty string | yes | optional | none |
| SNDR CNAM kind | `SoundDescriptor.Kind` (absent when missing) | `SNDR\DescriptorType` (u32 enum row, range checked only, selects the BNAM layout) | refused (coupled) | refused (coupled) | optional | none |
| SNDR GNAM, SNAM, ONAM links | `Category`, `Alternate`, `OutputModel` | `SNDR\Category` (SNCT), `AlternateSoundFor` (SNDR), `OutputModel` (SOPM), not nullable | `no_default` | yes | optional | yes |
| SNDR ANAM files | `Files` | `SNDR\Sounds` by occurrence | yes, empty string (uncounted) | yes | optional | none |
| SNDR CTDA conditions | condition code | condition code | refused | refused | optional | condition code |
| SNDR LNAM values | `Controls` | `ValuesUnknown`, `Looping`, `Sidechain`, `RumbleSendValue` (4-byte LNAM) | yes, zeroed | yes | optional | none |
| SNDR BNAM values (Standard, Compound, no CNAM) | `Playback` | `FrequencyShift`, `FrequencyVariance` (i8), `Priority`, `DbVariance` (u8), `StaticAttenuation` (u16 hundredths); 6-byte BNAM | refused (coupled) | refused (coupled) | optional | none |
| SNDR BNAM base descriptor (AutoWeapon) | `BaseDescriptor` | `SNDR\BaseDescriptor` (SNDR, not nullable) | refused (coupled) | refused (coupled) | optional | yes |
| SNDR DNAM descriptors | `Descriptors` | `SNDR\Descriptors` (SNDR) by occurrence | `no_default` | yes (uncounted) | optional | yes |
| SNDR ITMC and rate groups | `RateFiles` | `RateOfFireRpm`, `RateOfFireFile` by group, never re-sorted; ITMC OPAQUE | refused (count; group members unlisted) | refused | optional | none |
| SNDR records | n/a | n/a | n/a | n/a | `CreateRecord("SNDR")`: EDID only | n/a |
| SOUN OBND | raw | none | required | refused (required) | default zeroed | none |
| SOUN SDSC | `Descriptor` | `SOUN\SoundDescriptor` (SNDR, not nullable) | required | refused (required) | no default | yes |
| SOUN REPT | `Repeat` | `RepeatMinTime`, `RepeatMaxTime`, `RepeatStackable` (0..1, nine-byte REPT only) | yes, nine zero bytes | yes | optional | none |
| SOUN records | n/a | n/a | n/a | n/a | refused (`no_default`) | n/a |
| MUSC FNAM, PNAM, WNAM | `Flags`, `Priority`, `DuckingHundredths`, `FadeDuration` | `MUSC\Flags`, `Priority`, `Ducking`, `FadeDuration` | required | refused (required) | 0 / priority 50, ducking 0 / 0.0 | none |
| MUSC TNAM tracks | `Tracks` | `MUSC\Tracks` (MUST, nullable) by element; insert/remove by index via `radicle_plugin_formlink_list_insert` and `radicle_plugin_formlink_list_remove` (appendable, anchored after WNAM) | yes, one NULL track | yes | optional | yes |
| MUSC records | n/a | n/a | n/a | n/a | EDID, FNAM, PNAM, WNAM | n/a |
| FSTP DATA, ANAM | `ImpactSet`, `Tag` | `FSTP\ImpactDataSet` (IPDS, nullable), `FSTP\Tag` | required | refused (required) | NULL / empty string | yes |
| FSTP records | n/a | n/a | n/a | n/a | EDID, DATA, ANAM | n/a |
| FSTS XCNT counts | category sizes | OPAQUE | refused (coupled) | refused (coupled) | n/a | none |
| FSTS DATA footsteps | five categories | `FSTS\Footsteps` (FSTP, not nullable) by stored element (reverse category order); no insert/remove | refused (coupled) | refused (coupled) | n/a | yes |
| FSTS records | n/a | n/a | n/a | n/a | refused (`not_authorable`) | n/a |

All official sound records follow the authoring order (0 violators); the 24 AutoWeapon SNDR carry the unlisted
rate-group members, so add on them is refused with `unknown_order`. Field list, defaults provenance and refusal
reasons are in `docs/code-notes/radicle-sound-writer.md`.

Malformed rate groups, unsupported fixed-link/repeat widths and inconsistent footstep counts refuse field counting and editing before any byte changes. Valid rate groups remain in their stored order; RPM edits do not sort them. Current GardenEngine adoption and its marker-to-file C ABI probe have not been rerun in this tranche; consumer work remains separate.

## GardenEngine consumer probe

Copy `Radicle/probes/gardenengine/SoundProbe.cs` and `sound_probe.tscn` into `opencw/tools/`.
Build `opencw/OpenCommonwealth.csproj` with `RadicleSourceDir` set to this Radicle checkout.
Set `FO4_DATA_DIR` to the installed data and `RADICLE_NATIVE_PATH` to the built library of the
matching ABI, then run the double-precision Mono editor with `--headless --path opencw tools/sound_probe.tscn`.

Exit codes: PASS 0; SKIP 77 when `FO4_DATA_DIR` is unset; FAIL 1, printed as `sound_probe FAIL:` with the exception.

What each assertion proves, on the first winner of each signature in `Fallout4.esm` that carries the checked link (the probe picks the first record that carries the link, and fails if none does):
- SNDR: CNAM kind is Standard, Compound or AutoWeapon; only Standard exposes Playback, and its LNAM controls are four bytes.
- SNDR links: GNAM resolves to SNCT, SNAM to SNDR, ONAM to SOPM, BNAM base and DNAM descriptors to SNDR.
- SOUN: SDSC resolves to an SNDR that `SoundDescriptor` decodes; `SoundFiles` returns a non-empty path for the marker, printed as `MarkerFile`; a REPT stackable byte is 0..1.
- MUSC: every non-null TNAM track resolves to MUST.
- FSTP: a non-null DATA impact set resolves to IPDS.

No vanilla EDID is hard-coded: this PR's tests, docs and fixtures name no vanilla SNDR, SOUN, MUSC or FSTP
record, so the probe takes the first winner of each signature that carries the checked link and prints each record's EDID and FormKey in the PASS line.

Not yet run; needs a GardenEngine checkout whose Radicle pin is this PR head (GardenEngine pins ABI 9 at 34d16d50).
