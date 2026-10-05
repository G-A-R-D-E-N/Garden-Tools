# `.pex` reader provenance and reuse record

Task: A1, "Move the PEX reader into Radicle" (GardenEngine issue #200, parent #27).

This record answers the A1 gate: where the `.pex` model, reader and writer came from, what
established the format facts, what third-party code is involved, and what rights allow the code to be
canonical Radicle code. It distinguishes factual format references from copied implementation code.

## 1. Origin

The code moved into `Radicle/include/fo4/pex/pex.hpp` and `Radicle/src/pex/pex.cpp` was authored in
this repository, in the `PluginEditTool/` folder, by `NomadsReach
<144523850+NomadsReach@users.noreply.github.com>`.

History, in the `GARDEN/GARDEN-TOOLS` repository:

| Commit | What it did |
|---|---|
| `9a82b4c7` | `Import PluginEditTool native-m1-foundation into PluginEditTool/.` The folder entered as a `git subtree` import: `git-subtree-dir: PluginEditTool`, `git-subtree-mainline: 007b3d863333f46a28d8475c61652a1ebbe5b0f4`, `git-subtree-split: c55141ac7cfba886cefaa3918757556e83f0de23`. The native `.pex` model, reader and writer arrived with it. |
| `2d007ef3` | `Native: Papyrus engine, texture pipeline, and RPC targets` |
| `99c0fc04` | `feat: enable native Windows MCP plugin editing` |

The earlier managed implementation of the same model, `FO4RecordEditor.Core/Services/Papyrus/PexFile.cs`,
`PexStringTable.cs` and `PexWriter.cs`, is the same project's own work from the same subsystem (see
`PluginEditTool/docs/internal/PAPYRUS.md`). It stays where it is: A1 moves the native `.pex` model,
reader and writer, which is what the native host runs, and the managed copy is not touched.

## 2. Third-party code

No third-party implementation code was identified in the moved files. Specifically:

- Mutagen's Papyrus `Pex` layer is vendored elsewhere in this repository for plugin records. It was
  evaluated as a possible Papyrus back end and rejected on measured grounds: it failed to read about
  65% of a local `.pex` corpus, and of the files it did read, 5 out of 2,735 round-tripped
  byte-identically. Those numbers are recorded in `PluginEditTool/docs/internal/PAPYRUS.md`, and the
  same document states the decision plainly ("Do not build on Mutagen's `Pex` layer"). Mutagen remains
  a compatibility oracle for plugin records; it is not a source for this code.
- No Bethesda decompiler, disassembler or executable output was translated into this code. Bethesda's
  `PapyrusCompiler.exe` and `PapyrusAssembler.exe` were used only as local black-box oracles for
  differential compile comparisons, through their normal command-line interfaces.
- The one place the moved source names an outside implementation is the comment "Port of PexFile" on
  the model, which refers to this project's own managed `PexFile.cs`, not to a third-party library.

## 3. What established the format facts

The format was established by requiring byte-identical reconstruction of real files, not by
transcribing a specification:

- The `.pex` writer and reader were built as a pair and held to byte identity. Corpus sweeps are
  opt-in on `FO4RE_PEX_CORPUS` and no-op when the variable is unset, so a bare checkout stays green.
  The recorded round trip is 5,976 files found, 4,581 read, 4,581 byte-identical, 0 differing
  (`PluginEditTool/docs/internal/PAPYRUS.md`, "What is measured, and how").
- Public Papyrus documentation was used where it could settle a language or statement fact. Documented
  facts and measured facts are kept apart in that same engineering record, and the cases where the two
  disagree are recorded as measurements rather than as documentation.
- The local corpus is lawfully obtained and stays local and private. No corpus file, reconstructed
  source, disassembly or corpus-derived expressive content is committed to this repository or
  redistributed.

Facts the moved code depends on that are measurements, not assumptions:

- The container is little-endian, magic `0xFA57C0DE`, Fallout 4 is game id 2 at format 3.9; the
  big-endian Skyrim magic is rejected by name, and foreign game ids are rejected up front.
- The opcode table has 47 entries including the Fallout 4 additions; `callmethod`, `callparent` and
  `callstatic` are the three variable-argument opcodes.
- The per-object `size` field has two conventions in the wild. 1,480 of 1,496 measured loose `.pex`
  count the field's own four bytes; 16 count only the body. `PexObject::sizeIncludesItself` records
  which convention a file used, and the majority is the default. The producing toolchain is not
  identified.
- Some files carry bytes past the last object; `PexFile::trailingBytes` preserves them.
- The string table is carried verbatim rather than rebuilt on read. No measured file has a duplicate
  string, so first-occurrence lookup is safe.
- Strings are length-prefixed with a `u16` and stored one byte per character. This reader and writer
  treat those bytes as Latin-1, which is what makes the local corpus round-trip byte-identically.
  Treating bytes at or above `0x80` as anything else is **UNPROVEN** and is not implemented.

## 4. What is not claimed

- This record does not claim the `.pex` format is fully specified. Fields the local corpus never
  exercised stay unproven and are counted as such.
- It does not claim compatibility with any game other than Fallout 4.
- It does not create a repository licence.

## 5. Rights

The moved implementation originates inside `GARDEN/GARDEN-TOOLS`, was authored by this project, and
moves between two directories of the same repository. That move does not change which licence governs
the code, and it introduces no new third-party reuse question.

Open item, untouched by A1: `GARDEN-TOOLS` has no root `LICENSE`, `NOTICE.md` or `PROVENANCE.md`, and
`Radicle/` has no licence file, while per-component notices do exist (`PluginEditTool/LICENSE`,
`PluginEditTool/THIRD_PARTY_NOTICES.md`, `BGS/LICENSE`). A repository-level licence and provenance
document for `Radicle` is still owed; that is an owner decision, not a code change.
