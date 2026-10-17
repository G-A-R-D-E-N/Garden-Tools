# Light-plugin FormIDs — the actual range

**A light plugin's own records use local FormIDs `0x001`–`0xFFF`**, the 12-bit
object field of the engine's light encoding. Own IDs below `0x800` also need the
plugin to qualify under xEdit's `GetAllowHardcodedRangeUse` (HEDR at least 1.0 and at
least one master; `Core/wbImplementation.pas` at `93cc0bc5`, lines 4070-4090). A plugin
that does not qualify is limited to `0x800`–`0xFFF`. Not 2048 records.

xEdit's `TwbMainRecord.SetLoadOrderFormID` refuses a non-hardcoded object ID below
`0x800` in a file with no masters (`Core/wbImplementation.pas` at `93cc0bc5`, lines
14277-14279: `if _File.MasterCount[GetMastersUpdated] < 1 then raise`).

## Where it comes from

`TESFile::AdjustFormIDFileIndex(TESFile* this, uint* id)` — Fallout4.exe
1.10.155 (unpacked), address **`0x131f90`**, 55 bytes. Rebuilds an on-disk form ID
into a runtime one:

Build identification source: [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md), question 2's verified PE timestamps and predicate/conversion addresses. The assembly below is the older 1.10.155 image; the 1.10.163 conversion is at `0x131fa0`.

The corresponding 1.10.163 conversion is recorded in the RE note linked from `RUNTIME_PLUGIN_INDEX_EVIDENCE.md`; see `RUNTIME_PLUGIN_INDEX_EVIDENCE.md` for its pinned provenance.

```asm
140131f90:  mov   byte [rdx+3], 0          ; drop the on-disk master-index byte
140131f94:  mov   eax, [rcx+0x334]         ; TESFile flags
140131f9a:  shr   eax, 9
140131f9d:  test  al, 1                    ; bit 9 = SmallFile
140131f9f:  je    full
            ; --- light ---
140131fa1:  and   dword [rdx], 0xFFF       ; keep 12 bits, discard 12..23
140131fa7:  or    dword [rdx], 0xFE000000
140131fad:  movzx eax, word [rcx+0x372]    ; smallFileCompileIndex (u16)
140131fb4:  shl   eax, 12
140131fb7:  or    dword [rdx], eax
140131fb9:  ret
full:
140131fba:  movzx eax, byte [rcx+0x370]    ; compileIndex (u8)
140131fc1:  shl   eax, 24
140131fc4:  or    dword [rdx], eax
140131fc6:  ret
```

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

RE-016 (GARDEN-TOOLS #520 comment 13469, evidence commit `78e013b2`,
`RE-Work/subsystems/plugin-io-garden-tools/small-file-compile-index-389/`) answers this.
`AddCompileIndex` (OG `0x1549F0`, NG `0x2BEFB0`, AE `0x3136E0`) skips IDs 1 to `0x7FF`.
A light plugin reaches the remap straight from the header read (OG `0x132740`, NG
`0x2A5420`, AE `0x2F9A30`), which applies the reserved test `(id-1) <= 0x7FE` to the raw
32-bit ID: in a masterless plugin an own ID such as `0x005` stays in the hardcoded range
(it never resolves to the plugin's own record); with masters the stored ID carries a
nonzero master index, so it is remapped to the light runtime ID (verified by model from the instruction bytes). The plugin is never
rejected. So the engine allows own IDs `0x001`-`0x7FF` in a light plugin exactly when it
has a master; xEdit's rule (and Radicle's `allows_hardcoded_range`) also requires HEDR
version 1.0 or above, which is narrower and safe.

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
(`Core/wbImplementation.pas` at `93cc0bc5`, `TwbFile.GetAllowHardcodedRangeUse`, lines 4070-4090).
The traced remap path has no version test. RE-016 (GARDEN-TOOLS #520 comment 13469, evidence
commit `78e013b2`) verifies that the engine rule depends only on whether the plugin
has masters:

- A masterless plugin's own IDs `0x001`-`0x7FF` stay in the engine's hardcoded
  range. The header-read remap applies the reserved test `(id-1) <= 0x7FE` to the
  raw 32-bit ID, so a stored `0x00000005` stays `0x00000005` and never resolves to
  the plugin's own records. The engine never rejects a plugin over them.
  `AddCompileIndex` (OG `0x1549F0`, NG `0x2BEFB0`, AE `0x3136E0`) skips IDs 1 to `0x7FF`.
- With k masters, a stored `0x0k000005` is remapped to the light runtime ID (verified by model from the instruction bytes).
- The Creation Kit's ESL title-bar counter is `0xFFF` minus the forms owned, and its
  convert-to-light refuses IDs above `0xFFF`.

Radicle keeps xEdit's narrower rule for allocation, so a light plugin that meets it
allocates from `0x001` while the Creation Kit clamps a stored next at or below
`0x7FF` to `0x800` for every plugin. `0x800`-`0xFFF` is valid in every light
plugin. Every shipped ESL in the table has HEDR 1.00 and one master, so all of them
still qualify. xEdit also reads a non-qualifying plugin's own IDs below `0x800` as
the game master's hardcoded records (`TwbFile.FileFormIDtoLoadOrderFormID`).
Radicle's form ID resolution (`resolve_plugin_form_id`) reads them as the plugin's
own key instead, which differs from the engine. The light-flag refusal stops Radicle
from writing such a plugin. `create_local_record` and `renumber_local_record` both
honor the xEdit rule, so a light plugin allocates from `0x001` and refuses anything
above `0xFFF` rather than emitting a record the engine would alias away.

The HEDR next object ID is read as its low 24 bits (`k_full_local_mask`) by
`reserve_local_form_key`, `lowered_light_next_form_id` and `renumber_local_record`.
Shipped plugins store a `0xFF` prefix there. The engine discards it and ORs in the
compile index, so the prefix must not block allocation. Radicle writes the value back
unprefixed.

This replaces the blanket light-plugin refusals those two functions carried
while #26 was open.

## Caveats

- `0x000` is excluded. No shipped ESL uses it and nothing in the recorded pseudocode
  rules it out; it is left unavailable rather than guessed at.
- `smallFileCompileIndex` is a `u16` read from `TESFile+0x372`, versus the
  full-plugin `u8` at `+0x370`. OG and AE assign the position in independent
  arrays starting at zero. OG/AE list construction is now established statically;
  NG byte verification and live list behavior remain open. How NG assigns it is
  still open in [#28](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/28).
  See `RUNTIME_PLUGIN_INDEX_EVIDENCE.md` for the build-scoped evidence.
- `TESFile::CheckValidIDForSmallFile` is recovered: `(id & 0x00FFF000) == 0`,
  a 10-byte predicate at `0x130560` in the timestamp `0x5ddc40fa` image
  (`0x130550` in the timestamp `0x5dac778e` image). See `OPEN_QUESTIONS.md` Q2
  and the verified `26-RE-TARGETS.md` record above. It corroborates the
  12-bit ceiling. It does not say which IDs below `0x800` a plugin may own; that is
  `GetAllowHardcodedRangeUse` (`Core/wbImplementation.pas` at `93cc0bc5`, lines
  4070-4090). Zero remains excluded by Radicle's allocation policy.
- The next-gen cross-check remains [#28](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/28).
