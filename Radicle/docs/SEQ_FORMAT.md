# `.seq` container evidence

Task: A5, "Decode SEQ in Radicle with an evidence gate" (GardenEngine issue #204, parent #27).

This record answers the A5 gate: what the on-disk `.seq` format is, what was measured, and which
rules stay UNPROVEN. It covers factual interoperability only. No proprietary binary, no
third-party file and no corpus content is stored here.

## What a `.seq` is

A `.seq` file lists the start-game-enabled quests a plugin adds. It is deployed next to the plugin
as `Data/Seq/<plugin>.seq`, and the engine reads it at startup to know which quests to start.

## Evidence

1. **Local `.seq` files, measured.** Five lawfully obtained local files, sizes 4, 8 and 12 bytes:
   three project-authored probe variants from a `GardenQuestProbe` mod (named `1-A`, `2-AB` and
   `3-ACD`, sized 4, 8 and 12 bytes for one, two and three entries), the deployed copy of the first
   variant, and one third-party mod `.seq` with three entries. Every size is a multiple of four.
   Every file's first four bytes decode as a plausible runtime FormID rather than a header, magic
   or count, and the byte after the last entry does not exist.
2. **The project's own writer.** `PluginEditTool/native/src/plugin/RadiclePluginSeq.cpp` writes
   `entries * sizeof(uint32_t)` bytes with no header or count, encoding each entry as
   `(master_count << 24) | (local & 0x00FFFFFF)`, and appends entries in plugin record order,
   skipping a quest that an earlier plugin in the load order already had start-game-enabled.
   Its native test covers the "only newly start-game-enabled quests" rule.
3. **xEdit as a reference tool.** `PluginEditTool/docs/internal/XEDIT_PARITY_AUDIT_2026-08.md`
   records xEdit's `Create SEQ File` path (`mniNavCreateSEQFileClick`, `WriteService.XEdit2.cs`)
   writing each quest's `MainRecord.FixedFormID` to the file, which is the same runtime FormID the
   project's writer emits.

## PROVEN

- The container is a bare array of little-endian `uint32` runtime FormIDs: **no header, no count,
  no magic, no trailing bytes**. Entry width is four bytes and the file length is a multiple of
  four.
- An entry carries the master index in bits 24-31 and the local FormID in bits 0-23, which is the
  runtime FormID layout Radicle already models as `RawFormId` (`index_byte()`, `local_24()`).
- Entry order is file order. The project's writer emits plugin record order; the local files are
  ascending, but ascending is an observation, not a proven requirement.
- A `0xFFFFFFFF` entry, a created-index (`0xFF`) entry and a light-runtime-index (`0xFE`) entry are
  ordinary values to the container and are preserved as written.

## UNPROVEN

These rules are **not** guessed anywhere. The reader either preserves the bytes or stops.

- What the engine does with a **zero-byte** `.seq`. Radicle reads it as an empty list; no local file
  or writer produces one, because the project's writer refuses to write a SEQ with no entries.
- What the engine does with a file whose length is **not a multiple of four**. Radicle rejects it
  with a parse error at the offending offset.
- Whether entries must be **sorted** or **unique**.
- How the engine resolves an entry's **master index** at runtime, and whether that index must equal
  the plugin's load-order slot. Radicle never resolves a `.seq` entry to a `FormKey`: that needs a
  load order, which the container does not carry.
- **Precedence** between several plugins' `.seq` files, and what happens when a plugin's SEQ
  disagrees with the plugin's own record flags. The writer's "only newly enabled quests" rule comes
  from xEdit's behaviour, not from a runtime probe of the engine.

## Distribution

The local `.seq` files stay local and private. Only non-expressive results are published: byte
sizes, entry counts, the entry width and pass/fail classifications. Public test fixtures are
synthetic and live in `Radicle/tests/test_seq.cpp`.
