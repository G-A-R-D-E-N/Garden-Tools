# Light-plugin FormIDs — the actual range

**A light plugin's own records use local FormIDs `0x001`–`0xFFF`.** Not
`0x800`–`0xFFF`, and not 2048 records. This is the rule that was blocking safe
ESL authoring.

## Where it comes from

`TESFile::AdjustFormIDFileIndex(TESFile* this, uint* id)` — Fallout4.exe
1.10.163 (unpacked, PE timestamp `0x5ddc40fa`), RVA **`0x131fa0`**. Rebuilds an on-disk FormID
into a runtime one. The RE repo records this as pseudocode only, not bytes: the on-disk master byte is dropped; for a SmallFile the result is `formID & 0xFFF | 0xFE000000 | smallCompileIndex(+0x372) << 12`; otherwise it is `compileIndex(+0x370) << 24` OR'd in.

Source: private RE repository `ReverseEng/commonlib_NonVR` (re.nomadicinteractive.dev) at commit
`dbaeca7486300f4f1cc15e4607f8208f0df93013`, `RE-Work/Runtime/03-FINDINGS.md`, section "FormID compile-index remap decoded". Inspected read-only.
`RE-Work/docs/26-RE-TARGETS.md` lists the routine at `0x131f90` in the timestamp `0x5dac778e` image, which the RE doc labels "Tool-Build" (build table only, no bytes). Mixing builds caused
the earlier apparent symbol-table shift.

Two things fall straight out:

1. **Runtime light FormID = `0xFE | (smallFileCompileIndex << 12) | (local & 0xFFF)`.**
2. **The mask is destructive.** Bits 12–23 of the on-disk local are thrown away
   before the slot is OR'd in, so a local above `0xFFF` does not fail — it
   silently aliases onto another record in the same plugin. That is the whole
   validity rule.

The engine-reserved floor of `0x800` that applies to full plugins does **not**
obviously apply here. A full plugin's runtime FormID is `compileIndex << 24 | local`, so a
low local would collide with the engine's own reserved forms. A light plugin's
carries `0xFE` in the top byte and the slot in bits 12–23, so `0x001` should not
collide with the engine's reserved forms.

Unreconciled (unverified for light plugins): the same RE finding,
`RE-Work/Runtime/03-FINDINGS.md`, section "FormID compile-index remap decoded", says
`AddCompileIndex` (`0x1549f0`, OG 1.10.163) only remaps `formID >= 0x800` and calls the
low 2048 engine-reserved. That record does not say whether light plugins go through that
path. The `0x001` floor therefore rests on xEdit's rule and the scan of shipped ESLs below,
not on engine evidence. Open RE request: `re-requests/2026-10-09-esl-light-floor-and-hedr-next.md`, question 4.

## Verification

Every `.esl` Bethesda ships, own records only (overrides excluded by FormKey
owner):

| Plugin | Own records | Local range |
|---|---|---|
| ccBGSFO4044-HellfirePowerArmor | 297 | `0x001`–`0x91C` |
| ccBGSFO4046-TesCan | 189 | `0x001`–`0xF9A` |
| ccBGSFO4096-AS_Enclave | 16 | `0x800`–`0xF9B` |
| ccBGSFO4110-WS_Enclave | 4 | `0xF99`–`0xF9C` |
| ccBGSFO4115-X02 | 747 | `0x001`–`0xAD4` |
| ccBGSFO4116-HeavyFlamer | 170 | `0x800`–`0xFFF` |
| ccFSVFO4007-Halloween | **2078** | `0x001`–`0x81E` |
| ccOTMFO4001-Remnants | 1288 | `0x001`–`0x508` |
| ccSBJFO4003-Grenade | 725 | `0x001`–`0xFA2` |

Six of the nine allocate below `0x800`, and Halloween carries **2078** own
records — more than the folklore limit of 2048. Both facts are only consistent
with a `0x001`–`0xFFF` range. `Mutation.LightPluginRangeStartsAtOneAndStopsAtTwelveBits`,
`Mutation.LightPluginAllocatesBelowTheFullPluginFloor`,
`Mutation.LightPluginExhaustsAtTwelveBits` and the two
`RecordLifecycle` renumber tests cover the range and its boundaries.

## What this implementation does

`fo4::local_form_id_range(plugin)` (`fo4/plugin/mutation.hpp`) returns
`[0x001, 0xFFF]` when TES4 SmallFile (bit 9) is set and
`fo4::allows_hardcoded_range(plugin)` holds, `[0x800, 0xFFF]` for a light
plugin where it does not, and `[0x800, 0xFFFFFF]` otherwise.

`allows_hardcoded_range` is xEdit's rule, not an engine finding: HEDR version
at least 1.0 and at least one master
(source:TES5Edit/TES5Edit@dev-4.1.6 `Core/wbImplementation.pas:TwbFile.GetAllowHardcodedRangeUse`).
The `AdjustFormIDFileIndex` pseudocode above has no version check, so the engine may
accept more. This is not confirmed either way.
Following xEdit only narrows what Radicle allocates: `0x800`-`0xFFF` is valid
in every light plugin. Every shipped ESL in the table has HEDR 1.00 and one
master, so all of them still qualify. xEdit also reads a non-qualifying
plugin's own IDs below `0x800` as the game master's hardcoded records
(`TwbFile.FileFormIDtoLoadOrderFormID`); Radicle's FormID resolution does not
do that, and it stays unmodelled until the engine side is confirmed. `create_local_record` and `renumber_local_record` both honour it, so
a light plugin allocates from `0x001` and refuses anything above `0xFFF` rather
than emitting a record the engine would alias away.

This replaces the blanket light-plugin refusals those two functions carried
while #26 was open.

## Caveats

- `0x000` is excluded. No shipped ESL uses it and nothing in the recorded pseudocode
  rules it out; it is left unavailable rather than guessed at.
- `smallFileCompileIndex` is a `u16` read from `TESFile+0x372`, versus the
  full-plugin `u8` at `+0x370`. How 1.11.x assigns it is still open.
- `TESFile::CheckValidIDForSmallFile` is recovered: `(id & 0x00FFF000) == 0`,
  a 10-byte predicate at `0x130560` in the timestamp `0x5ddc40fa` image
  (`0x130550` in the timestamp `0x5dac778e` image). See `OPEN_QUESTIONS.md` Q2
  and the verified `26-RE-TARGETS.md` record above. It corroborates the
  12-bit ceiling; zero remains excluded by Radicle's allocation policy.
- The next-gen cross-check remains [#28](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/28).
