# Runtime FormID address space

Radicle keeps two FormID address spaces separate:

1. **On-disk plugin FormIDs** use the plugin's own `MAST` ordinal table.
2. **Runtime FormIDs** use the engine-assigned full-plugin compile index or the `0xFE` light-plugin encoding.

A raw 32-bit value is not meaningful until the caller identifies which address space it came from.

## Runtime layout

The CommonLib `TESDataHandler` / `TESFileCollection` model is the compatibility oracle. Its lookup path constructs a runtime ID from `TESFile::compileIndex` and `TESFile::smallFileCompileIndex`, and keeps regular files and small files in separate compiled collections.

The supported runtime layout is:

- regular/full plugins: `compileIndex` `0x00` through `0xFD`
- light plugins: top byte `0xFE`
- light slot: bits 12 through 23, `0x000` through `0xFFF`
- light local ID: bits 0 through 11
- `0xFFxxxxxx`: generated/save IDs, not plugin identities

CommonLib-family `TESDataHandler::LookupFormID` expresses the runtime composition as the compile index shifted by 24 plus the small-file compile index shifted by 12 plus the local ID. `LookupLoadedLightModByIndex` resolves against the separate `smallFiles` collection.

Radicle therefore builds `RuntimeFormIdContext` as two ordered tables:

- `full_plugins`: at most 254 entries
- `light_plugins`: at most 4096 entries

Duplicate plugin keys are rejected rather than being assigned two runtime identities.

## On-disk layout is different

Inside an ESP/ESM/ESL, the high byte is a `MAST` ordinal, not the runtime compile index. A reference to an ESL master does **not** become `0xFE...` on disk; it still uses that master's ordinal in the current file's master table.

This means `0xFE` must not be special-cased by `resolve_plugin_form_id`. At the extreme master-table boundary it can simply be ordinal 254, including the current plugin when it has 254 masters. Runtime `0xFE` interpretation belongs only in `resolve_runtime_form_id`.

Likewise, Radicle does not truncate a light plugin's `LocalFormId` when reading on-disk identity. `TESFile::CheckValidIDForSmallFile` (`(id & 0x00FFF000) == 0`, recovered; see `FORMID_MODEL.md` and `ESL_FORMIDS.md`) corroborates the 12-bit ceiling. The library retains the local value and applies the 12-bit mask only when encoding for a known light owner.

## Tests

- `tests/test_formid.cpp`
  - on-disk MAST resolution
  - runtime full indexes
  - runtime `FE` light slots
  - highest `FD` and `FFF` runtime boundaries
  - disk `FE` remains an ordinal rather than runtime-light syntax
- `tests/test_load_order.cpp`
  - separate full/light table construction
  - duplicate rejection
  - 254-full-plugin limit
  - 4096-light-plugin limit

The unresolved next-generation-vs-OG assignment details remain a separate reverse-engineering question. They must not change the on-disk MAST encoding rules.
