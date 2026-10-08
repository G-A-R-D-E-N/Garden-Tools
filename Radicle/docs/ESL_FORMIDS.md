# Light-plugin FormIDs — the actual range

**A light plugin's own records use local FormIDs `0x001`–`0xFFF`.** Not
`0x800`–`0xFFF`, and not 2048 records. This is the rule that was blocking safe
ESL authoring.

## Where it comes from

`TESFile::AdjustFormIDFileIndex(TESFile* this, uint* id)` — Fallout4.exe
1.10.155 (unpacked), RVA **`0x131f90`**, 55 bytes. Rebuilds an on-disk FormID
into a runtime one:

Build identification source: [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md), question 2's verified PE timestamps and predicate/conversion addresses. The assembly below is the older 1.10.155 image; the 1.10.163 conversion is at `0x131fa0`.

The corresponding 1.10.163 conversion is recorded in `build/RE-Work/small-file-compile-index-389.md`; see `RUNTIME_PLUGIN_INDEX_EVIDENCE.md` for its pinned provenance.

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

Two things fall straight out:

1. **Runtime light FormID = `0xFE | (smallFileCompileIndex << 12) | (local & 0xFFF)`.**
2. **The mask is destructive.** Bits 12–23 of the on-disk local are thrown away
   before the slot is OR'd in, so a local above `0xFFF` does not fail — it
   silently aliases onto another record in the same plugin. That is the whole
   validity rule.

The engine-reserved floor of `0x800` that applies to full plugins does **not**
apply here. A full plugin's runtime FormID is `compileIndex << 24 | local`, so a
low local would collide with the engine's own reserved forms. A light plugin's
carries `0xFE` in the top byte and the slot in bits 12–23, so `0x001` can never
collide with anything the engine reserves.

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
`AdjustFormIDFileIndex` above has no version check, and `commonlib_NonVR`
(`include/RE/T/TESFile.h`) has none either, so the engine may accept more.
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

- `0x000` is excluded. No shipped ESL uses it and nothing in the disassembly
  rules it out; it is left unavailable rather than guessed at.
- `smallFileCompileIndex` is a `u16` read from `TESFile+0x372`, versus the
  full-plugin `u8` at `+0x370`. OG and AE assign the position in independent
  arrays starting at zero. OG/AE list construction is now established statically;
  NG byte verification and live list behavior remain open. See
  `RUNTIME_PLUGIN_INDEX_EVIDENCE.md` for the build-scoped evidence.
- This is the *runtime* rule. `TESFile::CheckValidIDForSmallFile` is listed in
  the symbol dumps at RVA `0x130560` with `sizeof=10`, but that address holds a
  different, much larger function in the unpacked binary, and no 10-byte leaf
  matching a 12-bit range check was found. The symbol table is demonstrably
  off by `0x10` for `AdjustFormIDFileIndex` too (listed `0x131fa0`, actually
  `0x131f90`), so those RVAs need re-basing before that function can be read.
  It would only corroborate the rule above, not change it.
