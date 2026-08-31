# Fallout 4 localized string tables

Radicle supports the container format used by Fallout 4 `.STRINGS`, `.DLSTRINGS`, and `.ILSTRINGS` files through `fo4/localization/strings.hpp`.

## Evidence

The implementation is based on the Creation Kit reverse-engineering note at:

`Resources/RE-Work/dumps/localized-string-file-format.md`

The evidence was added to `main` in the 2026-08-21 RE-Work refresh and is backed by the CK writer/finalizer plus an independent byte-level check across 33 shipped Fallout 4 files (11 languages x 3 table kinds).

The important proven invariants are:

- file header: `u32 numStrings`, `u32 dataSize`;
- exactly `numStrings` rows of `{u32 stringID, u32 offset}`;
- the data block begins at `8 + 8*numStrings`;
- total file size is exactly `8 + 8*numStrings + dataSize`;
- table rows are strictly ascending by string ID;
- data offsets are relative to the data block and are independent of ID order;
- identical text may be deduplicated so multiple IDs share one data offset.

### `.STRINGS`

Each data record is raw text bytes followed by one NUL byte. There is no length prefix.

### `.DLSTRINGS` / `.ILSTRINGS`

Each data record is:

```text
u32 payloadLength
text bytes
NUL
```

The stored length includes the trailing NUL. A three-byte text such as `abc` therefore stores a length of `4`, and the complete record footprint is `4 + payloadLength` bytes.

The RE note's annotated `Fallout4_en.DLSTRINGS` sample confirms this convention, as does the CK finalizer which writes `text length + 1` before the text payload.

## Encoding policy

The container does not impose UTF-8. Shipped Fallout 4 tables include legacy CP1252/GB2312 content. Radicle therefore stores localized text as `std::vector<std::byte>` and preserves arbitrary non-NUL bytes without transcoding.

String decoding for a UI belongs above the binary container and must have an explicit language/code-page policy.

## Reader validation

Radicle fails closed on:

- truncated headers or offset tables;
- `dataSize` / physical file-size disagreement;
- non-ascending or duplicate string IDs;
- offsets outside the data block;
- offsets that do not point at real record boundaries;
- unterminated `.STRINGS` records;
- zero/truncated DL/IL lengths;
- DL/IL records without their required trailing NUL.

The entire data block is walked to record boundaries before table offsets are accepted.

## Writer behavior

`write_string_table` emits a deterministic valid table:

- input IDs must be unique;
- table rows are sorted by ID;
- data records use caller entry order;
- identical raw text is deduplicated to one offset;
- embedded NUL bytes are rejected;
- DL/IL length fields include the trailing NUL.

The writer is logically round-trip safe but does not promise byte-identical data-block ordering to an arbitrary source file because Bethesda's writer uses insertion/hash-table order.

## QUST CNAM authoring contract (#74A)

The following ownership facts are pinned for the later localized-CNAM writer:

- Fallout 4 `QUST` stage-log `CNAM` uses the `DL` source, so its sidecar is
  `.DLSTRINGS`. This matches both the vendored Mutagen record definition at
  `PluginEditTool/Mutagen/Mutagen.Bethesda.Fallout4/Records/Major Records/Quest.xml`
  and xEdit's `LocalizedValueDecider` in `wbLocalization.pas`.
- The sidecar belongs to the plugin and is under its `Strings` directory, with
  the plugin stem and active localization language token in the filename:
  `<plugin-stem>_<language>.DLSTRINGS`, such as `Patch_en.DLSTRINGS`.
  xEdit builds this filename from its active `wbLanguage` value, normally
  derived from Fallout 4's `sLanguage` setting.
- Radicle authoring must receive the selected language and resolved writable
  sidecar root explicitly. It must not guess a language from whichever file
  happens to exist, fall back to English, or write into a protected game-data
  directory.
- The allocation domain is one plugin/language localization set. New IDs are
  the first unused value after the maximum ID observed across that set's three
  table kinds. Existing IDs are never renumbered or deleted. The file format's
  per-table row IDs remain independently validated, while the allocator keeps
  the stronger set-wide invariant used by xEdit.
- Existing localized CNAM provenance wins over text matching: an unchanged
  entry keeps its original ID. Newly authored equal text does not reuse an
  unrelated existing ID by default; only the table writer may deduplicate the
  underlying data payload. This preserves the option for distinct records to
  receive independent future translations.
- If the selected sidecar or its required localization root cannot be resolved
  as an editable loose file, authoring fails closed before either the plugin or
  sidecar is staged. Creating a new sidecar from an absent or archive-only
  source is outside this slice.

These facts define ownership and allocation; they do not claim that the
current backend already implements them. The implementation remains split into
a sidecar-aware transaction primitive and the later `SetQuestStages` CNAM
adapter.

## Coordinated plugin + sidecar persistence (#74B)

`fo4::io::write_file_pair_dependency_first` provides the reusable persistence
primitive needed before localized CNAM authoring can be enabled.

It intentionally does not pretend that two independent filesystem paths can be
committed by one atomic replace operation. Instead it makes the only partial
state allowed by the operation safe for the caller's dependency relationship:

- the new dependency bytes, new primary bytes, and a rollback copy of the
  existing dependency are all staged and verified before either destination
  changes;
- the dependency is committed first;
- the primary is committed second;
- if the primary replace reports failure, the dependency is restored from the
  pre-staged rollback copy before the error is returned;
- failed staging and failed dependency replacement leave both original
  destinations untouched;
- successful completion removes the rollback stage and leaves no
  `.radicle-tmp` file behind.

A process crash between the two replace operations can still leave the **new
dependency with the old primary**. That is deliberate, and is why callers must
supply a dependency payload that remains valid for the old primary. For the
localized QUST path, the later adapter must preserve every sidecar ID/text pair
that the currently committed plugin may still reference. In particular, a
changed CNAM must receive a newly allocated ID before the plugin is switched to
it; the old ID remains present rather than being overwritten or deleted first.
This turns the sidecar-first intermediate state into a backward-compatible
superset instead of a broken reference set.

This is process-crash consistency, not a claim of cross-file transactional
atomicity or stronger sudden-power-loss durability than the platform's existing
single-file replace/flush primitives provide.

#74B also does **not** change PluginEditTool's current write-through backend
model. Deferred in-memory dirty state and making `SavePlugin` the sole
persistence point remain tracked separately in #174.

## TES4 bit 7 and plugin fields

This support does **not** settle Radicle issue #29 (`TES4 bit 7: localized vs delocalized`). The string-table container and the plugin localization flag are separate questions.

Until bit 7 semantics are resolved or a caller supplies an explicit localization context:

- Radicle does not automatically resolve 4-byte `FULL`, `DESC`, or similar payloads through external string tables;
- typed EYES `FULL` editing continues to treat a 4-byte payload as opaque/localized and refuses text mutation;
- string-table APIs may be used explicitly by tooling that already knows which table and string ID it intends to access.

This preserves the current fail-closed behavior while making the external table format available for the later localization-editor integration.
