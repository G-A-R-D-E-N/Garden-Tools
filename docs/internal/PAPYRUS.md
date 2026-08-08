# The Papyrus subsystem

Everything the tool owns for reading, understanding and writing Papyrus, with no Creation Kit
involved. It lives in `FO4RecordEditor.Core/Services/Papyrus/` (about 6,500 lines) and it is Core, so
it builds and tests on Linux.

This is the engineering record for issue #78. The issue thread is the decision history; this is the
current state.

## Why it exists

`compile_papyrus` shells out to the Creation Kit's `PapyrusCompiler.exe`. No CK, no Papyrus work at
all. That dependency is the same shape as the `Archive2.exe` one, and it is why the tool could not
take a mod from source to shippable on a machine without the CK installed.

The work was split into a front end (phase 1) and a semantic half plus back end (phase 2), so value
landed before any commitment to a whole compiler. Codegen is the only piece not built.

## The layers

Read top to bottom; each consumes the one above.

| File | What it is | State |
|---|---|---|
| `PapyrusToken.cs`, `PapyrusLexer.cs` | Tokens. 45 keywords, case-insensitive. | done |
| `PapyrusParser.cs`, `PapyrusAst.cs` | Hand-written recursive descent over 56 BNF productions. | done |
| `PapyrusScriptIndex.cs` | Script name to file, namespace colons as folder separators, roots in priority order, first match wins. Parse cache keyed on size + mtime. | done |
| `PapyrusFileWalk.cs` | The recursive directory walk everything else uses. | done |
| `PapyrusType.cs` | The type model and `PapyrusConversions`, the cast rules. | done |
| `PapyrusBinding.cs` | What a name turned out to be; `PapyrusResolution`, the per-script result. | done |
| `PapyrusResolver.cs` | Binds every name, types every expression. | done |
| `PapyrusTypeChecker.cs` | Judges what the resolver resolved. | done |
| **codegen** | **AST to `List<PexInstruction>`.** | **not built** |
| `PexFile.cs` | `.pex` object model and reader. 47 opcodes including the FO4 additions. | done |
| `PexWriter.cs` | `.pex` writer. | done |
| `PapyrusDecompiler.cs` | `.pex` to `.psc`. | done |
| `PapyrusSymbols.cs`, `PapyrusAnalysisService.cs` | Outline, hover, go-to-definition for the panel and the MCP tools. | done |

Codegen is bracketed on both sides. A resolved, type-checked AST sits above it and a writer that
reproduces real compiler output byte for byte sits below it.

## What is measured, and how

Every number here comes from a real corpus sweep, not from a unit test. The sweeps are opt-in on
environment variables holding roots separated by `Path.PathSeparator` (`:` on Linux, `;` on Windows),
and they no-op when unset so a bare checkout stays green.

| Sweep | Variable | Result |
|---|---|---|
| Parse | `FO4RE_PSC_CORPUS` | 18,802 files, 0 crashes, 18,782 clean; all 20 failures hand-verified as genuinely malformed source |
| Resolve | `FO4RE_PSC_CORPUS` | 7,854 vanilla files, 7,832 with complete sources, **100.00% clean** |
| Type check | `FO4RE_PSC_CORPUS` | 7,832 scripts with complete sources, **100.00% clean** |
| `.pex` round trip | `FO4RE_PEX_CORPUS` | 5,976 found, 4,581 read, **4,581 byte-identical, 0 differing** |

`FO4RecordEditor.Core.Tests` is 243 tests and runs on Linux. `FO4RecordEditor.Tests` targets
`net9.0-windows` through its WPF reference and its host does not start here, so put anything testable
without WPF in Core.Tests.

### Two traps in reading those numbers

**A whole-drive root set is not a root set.** Resolving against both drives at once scores 58.8%, and
that measures the corpus rather than the resolver: 98.2% of the 20,330 reachable `.psc` share a bare
script name with another file (29 copies of `Game.psc`, 35 of `ObjectReference.psc`), so `Game` binds
to whichever copy the walk reached first. **A differential harness must compile each script against
its own roots, never against everything at once.**

**100% clean would also be a checker that never fires.** The corpus number alone proves nothing about
a judging layer. The type checker's other half is 25 tests: one per diagnostic proving it fires, one
per silence rule proving it stays quiet.

## Rules that are not in the documentation

The published Papyrus Language Reference under-describes what the compiler accepts. Every one of
these was found by running against the corpus, not by reading harder, and each is real code the
Creation Kit compiled. Expect more.

- **A local variable may carry `const`**: `string filename = "S7System" const`. The in-function
  production has no flags at all.
- **A float literal may carry an `f` suffix**: `60.0f`. Absent from the Literals production, present
  in the wiki's own Statement Reference example.
- **A `bool` casts to `int` and to `float`**. The Cast Reference says "Floats, strings, and vars can
  be cast to integers" and omits bool, but `false as int` and `bStartAtTop as float` are in eleven
  vanilla scripts, `LightbulbOnOffScript`, `SimpleElevatorMasterScript` and `MQ206Script` among them.

And two language facts that are documented but easy to get backwards:

- **A global function has no `Self`, but its own script's global functions stay callable
  unqualified.** Reading "no Self variable" as "no members at all" reported 104 vanilla call sites as
  undefined. `Debug`, `Game` and `Utility` are entirely globals calling each other by bare name.
- **A name in call position is never a local.** Papyrus has no function values and is
  case-insensitive, so a call can match a local by accident.
  `Inst307_ZoneQuestRespawnScript` calls `RespawnCollection(...)` from a function whose parameter is
  named `respawnCollection`.

### The cast table, which is not guessable

From the Cast Reference's "Compiler auto-cast from" line per target type. Implemented once in
`PapyrusConversions` and shared by the resolver and the checker.

| Target | Implicit from | Explicit also from |
|---|---|---|
| `bool` | anything | |
| `string` | anything | |
| `float` | `int` | `string`, `bool`, `var` |
| `int` | nothing | `float`, `string`, `bool`, `var` |
| object | a child object, `None` | a parent object (may be `None` at runtime), `var` |
| array | `None` | another array whose elements cast |
| struct | `None` | nothing |
| `var` | everything but arrays | |

## Design stances worth keeping

**Missing sources are never errors.** A script whose parent or import the index cannot find has
reporting switched off, and `PapyrusResolution.BaseChainComplete` says so. Otherwise every inherited
member looks undefined. This is the single decision that makes the layer usable on a real modlist.

**Silence beats a false positive.** The checker reports nothing when sources were incomplete, when
either side is the `Error` type, or when a callee has no source declaration to check against. Array
built-ins are that last case: real members with real signatures declared in no `.psc`, so their
arguments go unchecked rather than wrongly checked. A checker that cries wolf on working code gets
ignored, and an ignored checker is worth nothing. Hold codegen to the same bar.

**Do not build on Mutagen's `Pex` layer.** It has a writer and looks like a free back end. It fails
**65%** of the corpus on read, and of what it does read, 5 files round-trip byte-identical out of
2,735. Ours reads 100% and the writer inverts it.

## `.pex` format facts

Established by making a writer and demanding byte-identity, not from a spec.

- **The per-object `size` field has two conventions in the wild.** 1,480 of 1,496 loose `.pex`
  measured count the field's own four bytes; 16 count only the body. `PexObject.SizeIncludesItself`
  records which, and the default is the majority. The producing toolchain is not identified: the
  `Papyrus`/`Compiler` user/machine stamp does not separate them, since 1,321 differently-stamped
  files still use the majority convention.
- **Some files carry bytes past the last object.** `PexFile.TrailingBytes`.
- **The string table is reused verbatim rather than rebuilt**, so indices land where they were. No
  file in the corpus has a duplicate string, so first-occurrence lookup is safe.
- **Other games share the magic.** Starfield is game id 4 at format 3.12 and has value type tags
  Fallout 4 never emits; Skyrim LE is big-endian. Both are rejected up front by name now, because a
  Starfield file used to fail hundreds of bytes late as `Invalid value type tag 14`. One such file
  exists on the development machine: a Fallout 4 script built with a Starfield compiler.

## What is left

**Codegen**, and one dependency that survives it.

`Institute_Papyrus_Flags.flg` defines the user flags (`Auto`, `Const`, `Hidden`, `Conditional`,
`Mandatory`, ...) and the `.pex` user-flag table is built from it. It ships with the Creation Kit and
is **not** in the game archives. It is off the critical path for everything above, because the parser
accepts any identifier in a flag position, which is exactly what lets it read scripts with no CK
present. It is on the critical path for "compiles with no CK installed".

The differential test for codegen writes itself: compile a `.psc` and compare the emitted `.pex`
against the one the CK produced from the same source. The writer's byte-identity result means that
comparison can be exact. Start structurally, per-function instruction sequences, then tighten.

Known gaps in the layers that are built, stated rather than hidden:

- Array built-in call arguments are unchecked.
- Override checking compares parameter **count**, not parameter types or return type.
- No definite-assignment and no all-paths-return analysis.

## Gotchas

- `dotnet build` on `FO4RecordEditor.csproj` needs `-p:EnableWindowsTargeting=true` on Linux.
- An XML comment in a `.csproj` cannot contain `--` (MSB4025).
- .NET's directory enumeration skips `Hidden` and `System` by default, and its recursive form aborts
  the whole walk on the first directory it cannot open. A drive that has run a game under Proton
  carries compatdata symlinks into `/proc` that throw, and a prefix's `dosdevices/z:` points at the
  filesystem root, so following symlinks turns a per-drive walk into a whole-machine one. Use
  `PapyrusFileWalk`.
- The Papyrus panel gutter's CSS `line-height` must equal `EDITOR_LINE_HEIGHT` in `PapyrusPanel.tsx`
  (19px), or every jump-to-line drifts.
