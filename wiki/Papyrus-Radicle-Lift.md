# Papyrus lift into Radicle

Owner decision (2026-10-25, restated 2026-10-28): Radicle is the backend. It owns Papyrus compile, decompile, resolve, type-check, index, symbols and graph logic. fo4ide (PluginEditTool, PET) is the visual part only: panels, canvas, RPC glue.

Method: lift-and-shift of PET's native C++ port into Radicle. Same behaviour, same PAP#### codes and messages. Only namespace (`fo4ide::papyrus` to `fo4::papyrus`) and include paths change. PET's copies stay the parity oracle and are not deleted until the Radicle copy passes the same gates. Radicle includes nothing from PluginEditTool.

Rules for every lane: no builds or tests on the authoring machine (CI reads only); each lane gets an independent review before push; files stay under 750 lines; explanations go in `Radicle/docs/code-notes/Radicle.md` anchored to file:symbol.

## Where things stand (2026-10-28)

Radicle now has the whole Papyrus stack (front end, semantics, decompiler, F4SE emitters, code generator, graph) plus a single `fo4::papyrus::AnalysisService` (#913) that PET calls for check, analyze, symbol-at and compile. All of it is exported through the C ABI and wrapped for C#. The ABI is now 80 on `nomad/dev` (77 at #919, 78 at #925, 79 at #931, 80 at #937).

## Lanes

| Lane | What moves | Status |
|---|---|---|
| 1 Front end | Lexer, Token, Ast, Parser, Diagnostic (`fo4::papyrus`) | Merged, PR #889 |
| 2 Semantics | Type, Binding, Resolver, TypeChecker, DeclarationCheck (PAP0020), ScriptIndex, FileWalk, Symbols | Merged, PR #892 |
| 3 Decompiler | Decompiler (`toSource`, disassemble), ScriptNames | Merged, PR #890 |
| 4 F4SE | Binding model, type map, registration extractor, validator, cross-check, version diff, emitters (`fo4::f4se`) | Merged, PR #891; emitter output compiles to pex (#907) |
| 5 Code generator | CodeGenerator, user flags (`.flg`), CK fidelity, compile entry | Merged, PR #895; decompile fidelity sweep #898; build fix #897 |
| 6 Graph | GraphTypeResolver, NodePalette, Validator, Lifter, Compiler, ScriptLoader, PapyrusSourceWriter, tool service | Merged, PR #896; `compileToFile` writes the `.pex` (#904) |
| AnalysisService | One service over parse, resolve, type-check, outline, definition, symbols, compile | Merged, PR #913 |
| C ABI: compile and decompile | `radicle_papyrus_compile_source`, `radicle_papyrus_decompile` (`c_api_papyrus_compiler.h`) | Merged, PR #916; decompile export clears outputs on a NULL pex (#924) |
| C ABI: graph | `radicle_graph_validate`, `_compile`, `_palette_search`, `_describe_node` (`c_api_graph.h`) | Merged, PR #917 |
| C ABI: analysis | line/column to offset, check, outline, definition, analyze JSON, symbol-at JSON, compile path, natural roots (`c_api_papyrus_analysis.h`) | Merged, PR #918 |
| ABI bump | `RADICLE_ABI_VERSION` 76 to 77, regenerated C# interop | Merged, PR #919 |
| C# wrappers | `PapyrusCompiler`, `PapyrusAnalysis`, `GraphTools` in `Radicle.Interop`, plus 7 tests | Merged, PR #920 |
| 7 PET rewire | PET `PapyrusTarget` Check, Analyze, SymbolAt and built-in Compile call `AnalysisService` | Authored and approved, open as held PR #921. Do not merge until the real-data gates pass. |

Still on PET after #921: Decompile, and `GraphCompiler.cs` / `GraphCompiler.cpp` (calls the old copy). Deleting PET's copies and retiring the C# oracle come after the gates.

What stays in fo4ide: `PapyrusPanel`, the Blueprint canvas and layout, `PapyrusTarget`, `PapyrusStaging`, `GraphTarget`, wiki lookup glue.

## SWF and AVM work merged after #921 (to `nomad/dev`)

| PR | What it delivered |
|---|---|
| #922 | Managed wrappers for ABC instruction edits and font glyph replace |
| #923 | PlaceObject3 filter lists decoded into typed placement data |
| #924 | Papyrus decompile export clears its outputs when it refuses a NULL pex |
| #925 | C ABI open for SWF movies with the BGS bridge (ABI 78) |
| #926 | AVM2 generic inclocal/declocal/negate_i, number promotion, locale case |
| #927 | PET SWF panel ABC view (script blocks and method disassembly) |
| #928 | Docs refresh for Papyrus/SWF notes and gate runbooks |
| #929 | SWF shape-to-SVG exporter with fill/line presence flags |
| #930 | AVM1 action decoder, encoder and disassembler |
| #931 | SWF edit verbs exported through the C ABI (ABI 79) |
| #932 | SVG exporter hardening (edge index, to_chars, closed strokes) |
| #933 | SWF runtime applies placed ratio, blend, visible, cacheAsBitmap and filters |
| #934 | AVM1 tests for ConstantPool and function parameter count refusals |
| #935 | SWF verbs keep unchanged tags in the plan, guard the plan, locale-free doubles |
| #936 | Docs: closed the schema "still open" notes the current rows and tests cover |
| #937 | `radicle_swf_shape_svg` exports a shape as SVG text (ABI 80) |
| #938 | PET SWF editor: label and edit-text edits go through Radicle `SwfVerbs` |

All of the above are merged. Each was independently reviewed and none was compiled or run on the authoring machine.

## Held

- #921 (PET rewire) stays open until the real-data gates pass on the Fallout 4 data runner.
- Held with #921: the stacked PET lanes `nomad/gt-pet-decompile-rewire` and `nomad/gt-pet-graph-rewire`, the alpha.2 release, deleting PET's copies, and retiring the C# oracle.

## Gates to pass before PET deletes its copy

- Corpus sweep: 18,802 scripts, 0 crashes, 18,782 clean, 20 hand-verified malformed (exact counts only under `FO4RE_PSC_STRICT`).
- 7,832 complete vanilla scripts resolve and type-check clean; duplicate-declaration sweep over 7,854 files flags 0.
- Creation Kit differential 158/161 identical (96.01%), threshold 0.95.
- Decompiler recompile fidelity over 1,498 scripts must not drop below 80.2%.
- Graph round-trip fixtures: 28 (unit tests over stub scripts, not a data gate).

These real-data gates need the corpus on a runner and skip when `FO4_DATA_DIR` is unset (optional `FO4_GAME_BUILD`). Runbook: `docs/papyrus-real-data-gates.md` (#908). SWF/ABC gates have their own runbook, `docs/swf-real-data-gates.md` (#915).

## Known risks

- PET behaviour that must be kept: CK-matching output rules, string-table case rule, PAP/GRA diagnostic codes and wording, duplicate-declaration key, user-flag table and write order, import-root rules.
- `parseQualifiedName` accepts `#` (PET's C++ does; the C# oracle rejects it).
- Script precedence: Radicle's script set is a "Garden rule, not engine-proven"; PET's index is first root wins. Do not mix them silently.
- Not exported through the C ABI: the ignored `all` parameter of `check`, and `buildIndex`.
- Nothing has been compiled or run on the authoring machine. CI is the first compile.

## Update log

- 2026-10-28: lanes 1-6 merged (#889-#896).
- 2026-10-28: CI fixes #897, #899, #900, #901, #902, #903. First green Native (Linux, Debug) with lanes 1-6 (run 26718).
- 2026-10-28: #904-#908 merged (compileToFile writes pex, shared corpus-roots helper, move fix, F4SE pex test, gates runbook). Lows logged on #752.
- 2026-10-28: #913 AnalysisService, #916-#918 C ABI exports, #919 ABI 77, #920 C# wrappers merged to `nomad/dev`. #921 PET rewire open and held. Lows logged on #752.
- 2026-10-28: #922-#938 merged to `nomad/dev` (SWF runtime, AVM1 decoder, SVG export, SWF edit verbs, PET SWF editor on verbs; ABI 80). Lows logged on #752.
- Open: Windows MSVC, clang-cl and `fo4-data` jobs were cancelled or waiting (runner never picked them up) at the last read, so unread. Real-data gates not run on any machine. #921 and the alpha.2 release on hold.
