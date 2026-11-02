# Radicle Papyrus: compile, decompile and graph - current status

Snapshot of `nomad/dev` at commit `dae906969`, read from source and docs only. Nothing was built or run for this page, so every "works" below means "the code and tests exist and the docs say so". Anything I could not confirm is marked **unverified**.

This page is a status snapshot. It does not repeat the background and history. For those, read:

- [[How-papyurus-works-and-where-its-at]] (how Papyrus works and where it is)
- [[Papyrus-Radicle-Lift]] (how the code moved from PluginEditTool into Radicle)

## Who owns what

- **Radicle owns compile and decompile.** The Papyrus code is in `Radicle/include/fo4/papyrus/` and `Radicle/src/papyrus/` (namespace `fo4::papyrus`). The `.pex` model is `Radicle/include/fo4/pex/pex.hpp` and `Radicle/src/pex/pex.cpp`. The graph is `Radicle/src/graph/` (`fo4::graph`). The F4SE emitter is `Radicle/src/f4se/` (`fo4::f4se`). (`PluginEditTool/docs/internal/PAPYRUS.md`, intro.)
- **PluginEditTool (PET) is meant to be display-only.** It should show results and hold no Papyrus logic. The web panels are `PluginEditTool/web/src/PapyrusPanel.tsx` and `PluginEditTool/web/src/Blueprint/`.
- **Today PET still runs its own older copies.** `PluginEditTool/native/src/papyrus/` (`fo4ide::papyrus`) and `PluginEditTool/native/src/graph/` (`fo4ide::graph`) are still there. `PAPYRUS.md` says they stay as the parity oracle until the real-data gates pass on the data machine. The PR that points PET at Radicle instead is #921, and the doc says it is held and not on `nomad/dev`. (`PluginEditTool/docs/internal/PAPYRUS.md`, intro.) I did not check PR #921 itself: **unverified**.
- Radicle also has the older C# code as a reference only (`FO4RecordEditor.Core/Services/Papyrus/`, legacy).

## The pipeline

```
 .psc source
    |  Lexer            lexer.cpp              -> tokens
    |  Parser           parser*.cpp            -> AST (ast.hpp), with PAP#### diagnostics
    |  declarationCheck declaration_check.cpp  -> duplicate-declaration check (PAP0020)
    |  Resolver         resolver.cpp           -> names bound, expressions typed
    |  TypeChecker      type_checker.cpp       -> judged: casts, calls, returns
    |  CodeGenerator    code_generator.cpp, body_*.cpp -> PexFile
    |  CreationKitFidelity creation_kit_fidelity.cpp   -> small fixes so output matches the CK
    v
 PexFile  --toBytes-->  .pex bytes          (pex.cpp writer)
 .pex bytes --fromBytes--> PexFile          (pex.cpp reader)

 PexFile --Decompiler::toSource--> .psc text   (decompiler*.cpp)
 PexFile --Decompiler::disassemble--> text listing

 Graph -> source:  GraphDocument -> validate -> linearize -> PapyrusSourceWriter -> .psc -> compileScript -> .pex
 Source -> graph:  .psc -> Parser (+ Resolver) -> GraphLifter -> GraphDocument
 .pex -> graph:    .pex -> Decompiler -> .psc -> Parser -> GraphLifter   (two steps, no direct pex-to-graph lifter)
```

The whole compile entry point is one function, `compileScript` in `Radicle/src/papyrus/compile_script.cpp`. Its pass order is: declaration check, resolve, type check, generate, then the Creation Kit fidelity pass (`docs/code-notes/Radicle.md`, entry `compile_script.cpp:compileScript`).

The `.pex` to graph path, as the code reads: the lifter takes a parsed script (`include/fo4/graph/graph_lifter.hpp:GraphLifter`), and the decompiler produces source. I did not find a direct pex-to-graph function.

## Stage by stage

### 1. Lexer and parser

- **Works:** hand-written recursive-descent parser with error recovery. A bad member is reported (`PAP0015`) and skipped, so later declarations survive; an unclosed block is reported once (`PAP0016`). Diagnostics are capped at 200 then one `PAP0019`. (`Radicle/docs/code-notes/Radicle.md`, entry `parser.cpp:Parser::parse`.)
- **Documented real-data result:** the sweep over 18,802 `.psc` files had 0 crashes and 18,782 clean; the 20 failures were hand-checked as malformed source (`PAPYRUS.md`, "What is measured"). That figure was measured on the older implementation. Whether the Radicle gate reproduces it on the data machine is **unverified**; `docs/papyrus-real-data-gates.md` lists the gate, with the strict-mode counts.
- **Known gap:** one deliberate difference from the C# oracle. The parser accepts `#` as a name qualifier (for `Script#Struct`), which the C# rejects (code-notes entry above).
- **Files:** `Radicle/src/papyrus/lexer.cpp`, `parser.cpp`, `parser_statement.cpp`, `parser_expression.cpp`; `Radicle/include/fo4/papyrus/{token,lexer,parser,ast,diagnostic}.hpp`.
- **Tests:** `Radicle/tests/test_papyrus_lexer.cpp`, `test_papyrus_parser.cpp`; real data `test_papyrus_parse_real.cpp` (`PapyrusParseReal.ParsesEveryCorpusScriptWithoutCrashing`).
- There is a second, separate lexer and parser in `Radicle/include/fo4/papyrus_source/` and `Radicle/src/papyrus_source/` used for a source summary (`radicle_papyrus_source_summary`, tested by `test_papyrus_source.cpp`). It is not the compiler front end.

### 2. Resolver, declaration check and type checker

- **Works:** binds names and types expressions, then judges them. `declarationCheck` runs before resolution. A script whose base chain is not found is counted as "incomplete", not clean (`test_papyrus_analysis_service.cpp`: `CheckCountsIncompleteBaseChainSeparately`).
- **Documented real-data result:** 7,854 vanilla files resolved, 7,832 with complete sources, all clean; type check also clean on the 7,832 (`PAPYRUS.md`). Gate: `PapyrusSemanticReal.ResolvesAndTypeChecksEveryCompleteCorpusScriptClean` in `Radicle/tests/test_papyrus_semantic_real.cpp`. Radicle reproduction: **unverified**.
- **Caution from the docs:** "100% clean" alone proves little about a checker, so the type checker also has tests that each diagnostic fires and each silence rule stays quiet (`PAPYRUS.md`, "Two traps").
- **Files:** `Radicle/src/papyrus/resolver.cpp`, `type_checker.cpp`, `declaration_check.cpp`, `script_index.cpp`, `symbols.cpp`, `type.cpp`, `binding.cpp`.
- **Tests:** `test_papyrus_resolver.cpp`, `test_papyrus_type_checker.cpp`, `test_papyrus_declaration_check.cpp`, `test_papyrus_symbols.cpp`, `test_papyrus_script_index.cpp`, `test_papyrus_structs.cpp`.

### 3. Code generator and compile to `.pex`

- **Works:** turns the resolved AST into a `PexFile`; returns nothing plus diagnostics when it cannot emit faithfully (`include/fo4/papyrus/code_generator.hpp:CodeGenerator`). Options (debug info, DebugOnly, BetaOnly code, flag file, import roots) are in `include/fo4/papyrus/compile_script.hpp:PapyrusCompileOptions`.
- **Documented fidelity (older implementation):** against Bethesda output, 158 of 161 compiled scripts were instruction-identical in a small differential run, and 7,242 of 7,543 (96.01%) on one fixed vanilla corpus at an earlier commit (`PAPYRUS.md`, "What is measured"). Those numbers are from the PET-native/C# era. The Radicle gate is `PapyrusCodegenReal.CompiledOutputMatchesCreationKitInstructionForInstruction` (`test_papyrus_codegen_real.cpp`); its pass floor is 0.95 and whether it reaches the documented figure on Radicle is **unverified** (`docs/papyrus-real-data-gates.md`).
- **Known gaps:**
  - Scripts with incomplete roots are refused, not guessed (37 refusals in the small run).
  - About 106 differences come from state-name casing in the string table. The docs say the cause is only partly reverse-engineered and the string insertion order is still open (`PAPYRUS.md`, issue #176 note).
  - Use `FO4RE_PEX_RELEASE=1` when comparing against shipped vanilla `.pex`, or debug-only code makes noise (`PAPYRUS.md`).
- **Files:** `Radicle/src/papyrus/code_generator.cpp`, `body_statement.cpp`, `body_expression.cpp`, `body_call.cpp`, `user_flags.cpp`, `creation_kit_fidelity.cpp`, `compile_script.cpp`.
- **Tests:** `test_papyrus_codegen.cpp`, `test_papyrus_codegen_corners.cpp`, helpers `papyrus_pex_comparer.hpp`, `papyrus_codegen_fixture.hpp`.
- **C API:** `radicle_papyrus_compile_source` (`Radicle/include/fo4/c_api_papyrus_compiler.h`), `radicle_papyrus_compile_path` and `radicle_papyrus_check` (`Radicle/include/fo4/c_api_papyrus_analysis.h`).

### 4. `.pex` reader and writer

- **Works:** a read/write pair held to byte-identical round trips. Recorded result: 5,976 files found, 4,581 read, 4,581 byte-identical, 0 differing (`Radicle/docs/PEX_PROVENANCE.md`, section 3). Fallout 4 only (game id 2, format 3.9); the Skyrim big-endian magic and foreign game ids are rejected.
- **Not proven (the doc says so):** fields the local corpus never used; compatibility with any other game; non-Latin-1 string bytes at or above `0x80`.
- **Files:** `Radicle/include/fo4/pex/pex.hpp`, `Radicle/src/pex/pex.cpp`. Provenance: `Radicle/docs/PEX_PROVENANCE.md` (the code is this project's own; no third-party implementation code was found).
- **Tests:** `Radicle/tests/test_pex.cpp`, `test_pex_hardening.cpp`, `test_pex_oracle.cpp`; corpus gate `test_pex_corpus.cpp` (reads `FO4_PEX_CORPUS`, skips when unset).
- Related but separate: pex analysis (disassembly, control-flow graph, call graph, script set resolution) in `Radicle/src/pex_analysis/`, tested by `test_pex_disasm.cpp`, `test_pex_analysis.cpp`.

### 5. Decompiler

- **Works:** `Decompiler::toSource` writes one `ScriptName` block per object (structs, properties, variables, states, functions); `Decompiler::disassemble` writes a text listing. It inlines temporaries, folds `&&`/`||` jump pairs, hoists locals, and keeps calls whose result is unused. If one function fails, it prints a failure comment plus the listing and carries on with the rest (`docs/code-notes/Radicle.md`, `decompiler.cpp` entries). Object names are checked by `safeScriptObjectName` so a bad name cannot make an unsafe path.
- **Partial:** the documented decompile-then-recompile fidelity is 80.2% (1,202 of 1,498 scripts instruction-identical, 275 differing, 21 not recompiled) (`PAPYRUS.md`; `PluginEditTool/docs/internal/GRAPH.md`, "What is left"). That was measured on the old implementation and a different sample. `docs/papyrus-real-data-gates.md` says the 0.802 floor on Radicle is unmeasured and needs re-measuring: treat as **unverified** on Radicle.
- **Known gap:** output is "best effort" for the remaining roughly 20% of bodies.
- **Files:** `Radicle/src/papyrus/decompiler.cpp`, `decompiler_expr.cpp`, `decompiler_stmt.cpp`, `decompiler_internal.hpp`, `script_names.hpp`.
- **Tests:** `test_papyrus_decompiler.cpp` (hand-built `PexFile` values, exact output); real data `test_papyrus_decompiler_real.cpp` (no-throw sweep, needs `FO4_PEX_CORPUS`); fidelity `PapyrusCodegenReal.DecompiledCorpusRecompilesToTheSameProgram` in `test_papyrus_codegen_real.cpp`.
- **C API:** `radicle_papyrus_decompile` (`Radicle/include/fo4/c_api_papyrus_compiler.h`).

### 6. Analysis service (what the panel and tools call)

- **Works:** check (clean / error counts), outline, go-to-definition (follows `Extends` into the parent file; says "not resolved" instead of guessing), line/column to offset (LF and CRLF), folder walk, and refusing a `.pex` with a hint to decompile. Test names in `Radicle/tests/test_papyrus_analysis_service.cpp` (listed in `docs/code-notes/Radicle.md`, entry `analysis_service.cpp:AnalysisService`).
- **Files:** `Radicle/src/papyrus/analysis_service.cpp`, `file_walk.cpp`. **C API:** `radicle_papyrus_outline`, `radicle_papyrus_definition`, `radicle_papyrus_analyze_json` (`c_api_papyrus_analysis.h`).

### 7. Graph: compile (graph to `.pex`)

- **Works:** the graph compiler validates, linearizes, writes `.psc` source and compiles it with the Papyrus compiler, then maps Papyrus errors back to graph nodes through a source map (`include/fo4/graph/graph_compiler.hpp:GraphCompiler`; code-notes entry). Emitting readable `.psc` and reusing the normal compiler is a deliberate design choice (`PluginEditTool/docs/internal/GRAPH.md`, "Why it exists"). `GraphCompiler::compileToFile` writes the `.psc` and the `.pex`.
- **Evidence:** 28 fixture graphs in `Radicle/tests/graph_fixtures.hpp`, run by `test_graph_compiler.cpp` against stub base scripts in `Radicle/testdata/graph/BaseStubs`. They need no game data (`docs/papyrus-real-data-gates.md`, gate 7). Other tests: `test_graph_validator.cpp`, `test_graph_flow.cpp`, `test_graph_document.cpp`, `test_graph_node_palette.cpp`, `test_graph_script_loader.cpp`, `test_graph_tool_service.cpp`.
- **Errors:** `GRA####` codes (`Radicle/include/fo4/graph/graph_diagnostic.hpp`). The F4SE emitter reuses the same code table.
- **Files:** `Radicle/src/graph/graph_compiler.cpp`, `graph_validator.cpp`, `graph_linearizer*.cpp`, `graph_exec_flow.cpp`, `graph_definite_assignment.cpp`, `graph_type_resolver.cpp`, `papyrus_source_writer.cpp`, `graph_document.cpp`, `node_palette.cpp`.
- **C API:** `radicle_graph_validate`, `radicle_graph_compile`, `radicle_graph_palette_search`, `radicle_graph_describe_node`, `radicle_graph_load_script` (`Radicle/include/fo4/c_api_graph.h`).

### 8. Graph: lift (source back to a graph)

- **Works:** `GraphLifter` turns a parsed script into a `GraphDocument`; it is the inverse of the compiler. It refuses constructs the graph cannot express with `GRA0001` instead of mangling them (`include/fo4/graph/graph_lifter.hpp:GraphLifter`). Refusals seen in `Radicle/src/graph/graph_lifter.cpp` and `graph_lifter_expr.cpp` include: source that does not parse, some compound assignments, assignment to certain member targets, a loop condition that calls a function, some unary/binary operators, and reads the graph cannot express yet.
- **Round trip evidence:** the same 28 fixtures run through lift and relinearize in `Radicle/tests/test_graph_lift.cpp`. The older C# doc reports 28 of 28 instruction-identical for both source and compiled-`.pex` round trips (`GRAPH.md`). I did not re-run these: **unverified** on Radicle.
- **Real-data sweep:** `GraphLiftReal.LiftsAndRelinearizesSampledCorpusFunctionsToTheirOriginalBodies` (`test_graph_lift_real.cpp`). Its pass floor (half the callables lifted) is an unmeasured placeholder, and round trip is only reported, not asserted (`docs/papyrus-real-data-gates.md`, gate 9). The C# doc gives 1,414 of 1,500 lifted (94.3%); not measured on Radicle: **unverified**.
- **Known gap:** There is no `radicle_graph_lift` export in `Radicle/include/fo4/c_api_graph.h`, so the lifter is reachable from C++ only. Check before relying on it from C#.
- **Because the `.pex` to graph path goes through the decompiler,** a `.pex` whose body falls in the decompiler's roughly 20% gap gives a graph that is not exactly the original program (`GRAPH.md`, "What is left").
- **Files:** `Radicle/src/graph/graph_lifter.cpp`, `graph_lifter_expr.cpp`, `graph_lifter_detail.hpp`.

### 9. F4SE binding emitter

- **Works:** from a plugin binding model it emits per-module `.h`, `.cpp` and `.psc`, plus registrations and shell files (`main.cpp`, CMake, README). It validates first, and keeps user-written bodies inside `FO4IDE_BODY` regions across re-emits. It can also recover bindings from existing C++ and cross-check them against Papyrus declarations (`docs/code-notes/Radicle.md`, entry `emitter.cpp:F4SEEmitter`).
- **Compile proof:** `Radicle/tests/test_f4se_emitter.cpp:TheEmittedScriptsCompileToPexWithZeroDiagnostics` compiles the emitted `.psc` files with the Radicle compiler and requires zero diagnostics and a byte-identical `.pex` round trip.
- **Other tests:** `test_f4se_extractor.cpp`, `test_f4se_cross_check.cpp`, `test_f4se_region_merge.cpp`, `test_f4se_type_map.cpp`; real data `test_f4se_corpus_real.cpp` (needs `FO4RE_F4SE_SRC`, skips when unset).
- **Files:** `Radicle/src/f4se/emitter.cpp`, `binding_validator.cpp`, `cpp_emitter.cpp`, `plugin_emitter.cpp`, `papyrus_header_emitter.cpp`, `region_merge.cpp`, `registration_extractor.cpp`, `type_map.cpp`.

## Next to the toolchain (not the compiler)

- **Runtime:** Radicle also has a Papyrus interpreter and runtime (`Radicle/include/fo4/papyrus/interpreter.hpp`, `runtime.hpp`, `save_state.hpp`) with tests such as `test_papyrus_vm.cpp`, `test_papyrus_runtime.cpp`, `test_papyrus_save_state.cpp`. It is not covered in this page; see the first linked page.
- **VMAD (scripts attached to records):** `Radicle/docs/VMAD.md` holds the capability table for reading and writing stored script data on records.

## What the PET UI shows

- `PluginEditTool/web/src/PapyrusPanel.tsx` has four modes: `decompile`, `compile`, `lookup` (wiki lookup) and `analyze` (an editor with check and symbol lookup). It calls the host through `getPapyrus()` from `./backend`. Test: `PapyrusPanel.test.tsx`.
- `PluginEditTool/web/src/Blueprint/` is the node-graph canvas (`BlueprintPanel.tsx`, `BlueprintCanvas.tsx`, `graphReducer.ts`, `clipboard.ts`, `layout.ts`), with tests `BlueprintPanel.test.tsx`, `graphReducer.test.ts`, `clipboard.test.ts`, `layout.test.ts`.
- The RPC and MCP surface for Papyrus is `PluginEditTool/native/src/app/PapyrusTarget.cpp` (named in `PAPYRUS.md`; not opened for this page). Per `PAPYRUS.md`, it still reaches PET's native copies, not Radicle (PR #921 held).

## How to try it

The only documented run instructions are for the real-data tests, in `docs/papyrus-real-data-gates.md`. They need a data machine, a staged `.pex` and `.psc` corpus, environment variables (for example `FO4RE_PSC_CORPUS`, `FO4RE_PEX_CORPUS`), and the `fo4_tests` binary. For example (from that doc):

```
Set-Location <repo>\Radicle
fo4_tests.exe --gtest_filter=PapyrusSemanticReal.ResolvesAndTypeChecksEveryCompleteCorpusScriptClean
```

Unset variables make a gate skip, and a skip is not a pass. There is no short "compile one script" command line for Radicle; use `radicle_papyrus_compile_path` through the C API or the PET panel. Whether there is a Radicle command-line tool for this: **unverified**.

## Summary table

| Stage | State on `nomad/dev` (code and tests exist) | Real-data proof on Radicle |
|---|---|---|
| Lexer, parser | done | gate exists; result unverified |
| Resolver, type check, declaration check | done | gate exists; result unverified |
| Code generator, compile to `.pex` | done, with known diffs vs the Creation Kit | gate exists; result unverified |
| `.pex` reader and writer | done | round trip gate exists; documented 4,581 of 4,581 |
| Decompiler | works, about 80% fidelity documented | floor unmeasured on Radicle |
| Graph compile | done, 28 fixtures | no game data needed |
| Graph lift | works, refuses what it cannot express | floor is a placeholder; no C export found |
| F4SE emitter | done, emitted scripts compile to `.pex` in a test | corpus gate needs F4SE sources |
| PET wiring to Radicle | held (PR #921), PET still uses own copies | not applicable |

## Sources read

`Radicle/docs/PEX_PROVENANCE.md`, `Radicle/docs/VMAD.md`, `Radicle/docs/code-notes/Radicle.md`, `docs/papyrus-real-data-gates.md`, `PluginEditTool/docs/internal/PAPYRUS.md`, `PluginEditTool/docs/internal/GRAPH.md`, the Radicle headers named above, and `PluginEditTool/web/src/PapyrusPanel.tsx`.
