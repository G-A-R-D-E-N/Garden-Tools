# FormID model

## Rule

Never treat a raw load-order-dependent FormID as canonical identity.

`0x5600009A` is a runtime encoding. The identity is:

```text
00009A:RadiationOverhaul.esp
```

The library stores `FormKey`. Raw values exist only at the plugin-bytes boundary.

The two types are not interchangeable:

| Type | Meaning |
|------|---------|
| `RawFormId` | 32-bit value as stored in a plugin or as seen in a loaded TESForm |
| `FormKey` | `{ owner PluginKey, local LocalFormId }` |

## Two encodings

### 1. On-disk plugin-relative IDs

Used inside `.esp` / `.esm` / `.esl` files.

- High byte = index into **that file's** `MAST` list
- High byte `== master_count` = a record owned by the file being parsed
- Low 24 bits = local ID for full plugins
- Light masters still use the `MAST` ordinal in dependents. They do **not** use the runtime `FE` slot on disk.

Evidence: FO4IDE `Fallout4FileFormIdTests.SavePlugin_CustomActorValueUsesFinalMastOrdinal_AfterFallout4IsForced`. A light master listed third (`index 2`) with local `0x812` is written as `0x02000812`, not as `0xFExxx812`.

APIs:

- `resolve_plugin_form_id(raw, PluginFormIdContext)`
- `encode_plugin_form_id(FormKey, PluginFormIdContext)`

Master removal / reorder test: resolve against the old table, encode against the new table. High bytes change only because the destination master table changed.

### 2. Runtime load-order IDs

Used after Fallout 4 assigns compile indexes.

CommonLibF4 `TESFile` / `TESDataHandler` (oracle, OG 1.10.163 layout):

```text
full:  (compileIndex << 24) | local24
light: 0xFE000000 | (smallFileCompileIndex << 12) | local12
```

`TESFile::IsFormInMod`:

- full: `(formID >> 24) == compileIndex`
- light: `(formID >> 24) == 0xFE` and `((formID & 0x00FFF000) >> 12) == smallFileCompileIndex`

`TESFileCollection` keeps `files` and `smallFiles` separately. `TESFile::SetCompileIndex` is a `uint8`. `TESFile::SetSmallCompileIndex` is a `uint16`. `TESFile::RecordFlag::kSmallFile = 1 << 9`.

`0xFF......` is created / save-game (not a plugin identity).

APIs:

- `resolve_runtime_form_id(raw, RuntimeFormIdContext)`

Do not call this on plugin bytes unless a file actually stored an `FE` FormID.

**On-disk ESL sample (confirmed, `ccBGSFO4096-AS_Enclave.esl`):**

- TES4 flags `0x281` (Master | 0x80 | SmallFile)
- one master `Fallout4.esm`
- override of a Fallout4 form: `0x0024A0FE` (high byte 0 = MAST index 0)
- local records: `0x01000800` … `0x01000F9B` (high byte 1 = `master_count`; this sample's local IDs are in `0x800`–`0xFFF`. The allocation range is `0x001`–`0xFFF` when `GetAllowHardcodedRangeUse` holds (HEDR >= 1.0 and at least one master), else `0x800`–`0xFFF`; see `ESL_FORMIDS.md`)
- record headers are **not** `0xFExxxxxx`

So a light plugin's own records still use the plugin-relative MAST index encoding on disk. `FE` is the runtime encoding after `SetSmallCompileIndex`.

## Local IDs

| Plugin kind | Local width | Mask |
|-------------|-------------|------|
| full | 24 bits | `0x00FFFFFF` |
| light | 12 bits at runtime | `0x00000FFF` |

`TESFile::CheckValidIDForSmallFile` is recovered as `(id & 0x00FFF000) == 0` (verified record: RE repo `ReverseEng/commonlib_NonVR` at `dbaeca7`, `RE-Work/docs/26-RE-TARGETS.md` section 4; summary in `ESL_FORMIDS.md`). Light local IDs are stored in a 32-bit `LocalFormId` and masked to 12 bits only when encoding a **light** owner (the current plugin when `current_kind` is light, or a master whose `MasterDescriptor.kind` is light). An ESP with TES4 SmallFile is light even if its filename is `.esp`. Filename `.esl` is only the fallback when the master's TES4 flags are not known.

## What this prevents

Given:

```text
0 Fallout4.esm
1 DLC01.esm
2 RadiationOverhaul.esp
3 AnotherMod.esp
```

A reference `0x0200009A` means `00009A:RadiationOverhaul.esp`.

If `DLC01.esm` is removed, encoding that same `FormKey` into the new table produces `0x0100009A`. The library never "subtracts 1 from the high byte" as a standalone operation.

The same encode is used for known FormLink slots (`NAME`, `KWDA`, `CTDA` parameter 1 for `GetBaseValue` = 277, and other functions whose parameter types are FormIDs). A `GetLocked` comparison float is not a FormID and is left alone.

## Open edges

- Whether an ESL's own record headers use `00xxxxxx`, `FExxxxxx`, or `master_count << 24` on disk
- Whether ESP-flagged-light files follow the same on-disk local-width rules as `.esl`
- Next-gen vs OG differences in small compile index assignment

Those are listed in `OPEN_QUESTIONS.md`. Do not guess them in the writer.
