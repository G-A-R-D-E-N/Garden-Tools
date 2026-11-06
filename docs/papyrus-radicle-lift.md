# Papyrus lift into Radicle

Owner decision (2026-10-25, restated 2026-10-28): Radicle is the backend. It owns Papyrus compile, decompile, resolve, type-check, index, symbols and graph logic. fo4ide (PluginEditTool, PET) is the visual part only: panels, canvas, RPC glue.

Method: lift-and-shift of PET's native C++ port into Radicle. Same behaviour, same PAP#### codes and messages. Only namespace (`fo4ide::papyrus` to `fo4::papyrus`) and include paths change. PET's C# copy stays the oracle and is not edited until the Radicle copy passes the same gates. No C ABI change, and Radicle includes nothing from PluginEditTool.

Rules for every lane: no builds or tests on the authoring machine (CI reads only); each lane gets an independent review before push; files stay under 750 lines; explanations go in `Radicle/docs/code-notes/Radicle.md` anchored to file:symbol.

Working notes (kept outside this repository, not committed): a per-lane file map with its unverified list, a PET-vs-Radicle audit whose figures are unverified claims, and the lane goal prompt.

## Where things stand (2026-10-28)

Radicle already has: the Papyrus VM (interpreter, scheduler, save state, quest rules, struct opcodes) behind the C ABI (ABI 76), the `.pex` reader and writer, `.pex` analysis (CFG, call graph, disassembly listing, script set), and a separate source lexer/parser/summary (`fo4::psc`). It had no resolver, type checker, script index, symbols, compiler, decompiler to source or graph.

## Lanes

| Lane | Branch | What moves | Status |
|---|---|---|---|
| 1 Front end | `nomad/gt-papyrus-lift-front-end` | Lexer, Token, Ast, Parser, Diagnostic (`fo4::papyrus`) | Reviewed twice, merged to `nomad/dev` as PR #889. CI not read yet. |
| 2 Semantics | `nomad/gt-papyrus-lift-semantics` (stacked on 1) | Type, Binding, Resolver, TypeChecker, DeclarationCheck (PAP0020), ScriptIndex, FileWalk, Symbols | Reviewed (APPROVE, Lows on #752), merged to `nomad/dev` as PR #892. CI not read yet. |
| 3 Decompiler | `nomad/gt-papyrus-lift-decompiler` | Decompiler (`toSource`, disassemble), ScriptNames | Reviewed (APPROVE, Lows on #752), merged as PR #890. 11 of PET's 14 tests wait for the compiler. |
| 4 F4SE | `nomad/gt-papyrus-lift-f4se` (stacked on 1) | Binding model, type map, registration extractor, validator, cross-check, version diff, emitters (`fo4::f4se`) | Reviewed (APPROVE, Lows on #752), merged as PR #891. CI not read yet. |
| 5 Code generator | `nomad/gt-papyrus-lift-codegen` | CodeGenerator, user flags (`.flg`), CK fidelity, compile entry | Reviewed (APPROVE, Lows on #752), merged as PR #895. 57 tests ported; decompile fidelity sweep ported in #898 (`DecompiledCorpusRecompilesToTheSameProgram`, `FO4RE_PSC_ROOTS`, floor 80.2%, unmeasured on C++). CI build break fixed in #897. CI not read yet. |
| 6 Graph | `nomad/gt-papyrus-lift-graph` | GraphTypeResolver, NodePalette, Validator, Lifter, Compiler, ScriptLoader, PapyrusSourceWriter, tool service (107 PET graph tests ported; nlohmann_json 3.12.0 via FetchContent) | Reviewed twice (APPROVE, Lows on #752), merged as PR #896. CI not read yet. `GraphTarget.cpp` stays in PET. |
| 7 PET rewire | not started | fo4ide calls Radicle, own copies deleted, C# oracle retired | Only after the Radicle copies pass the gates below. |

What stays in fo4ide: `PapyrusPanel`, the Blueprint canvas and layout, `PapyrusTarget`, `GraphTarget`, wiki lookup glue. Compile staging has moved to Radicle (`Radicle/src/papyrus/compile_staging.cpp`: `planCompile`, `stageNamespacedScripts`, `removeCompileStaging`); `PapyrusStaging` no longer exists.

## Gates to pass before PET deletes its copy

- Corpus sweep: 18,802 scripts, 0 crashes, 18,782 clean, 20 hand-verified malformed (lane 1; exact counts run only under `FO4RE_PSC_STRICT`, the default requires a clean rate above 0.995).
- 7,832 complete vanilla scripts resolve and type-check clean; duplicate-declaration sweep over 7,854 files flags 0 (lane 2).
- Creation Kit differential 158/161 identical, vanilla corpus 96.01% (lane 5).
- Decompiler recompile fidelity over 1,498 scripts must not drop below 80.2% (lane 3 plus 5).
- Graph round-trip fixtures: 28 (lane 6).

These real-data gates need the corpus on a runner. They skip when the environment variables are unset, so they prove nothing on a machine without it. Environment variables: `FO4_PSC_CORPUS` (falls back to `FO4RE_PSC_CORPUS`), `FO4_PEX_CORPUS`, plus the CK differential variables listed in `PluginEditTool/docs/internal/PAPYRUS.md`.

## Known risks

- PET behaviour that must be kept: CK-matching output rules (temp numbering, member ordering, `DebugOnly` stripping), string-table case rule, PAP/GRA diagnostic codes and wording, duplicate-declaration key is the emitted name, user-flag table and write order, import-root rules.
- `parseQualifiedName` accepts `#` (PET's C++ does; the C# oracle rejects it). Kept as a carry-over.
- Script precedence: Radicle's script set is a "Garden rule, not engine-proven" (open RE question); PET's index is first root wins. Do not mix them silently.
- Nothing in these lanes has been compiled or run on the authoring machine. CI is the first compile. Failures get fixed in new bug PRs.

## Decisions pending

- None blocking. The F4SE move was approved on 2026-10-28.
- Open engine-side items from the 2026-10-24 decision: GardenEngine PR #544 (resolution through Radicle) and #352 (shrinks after #544). Not rechecked.

## Update log

- 2026-10-28: lanes 1-4 started; lane 1 merged (PR #889); lane 3 merged (#890); lane 4 merged (#891); lane 2 merged (#892); lane 5 started.
- 2026-10-28: lane 5 merged (#895); in-process NIF fix merged (#894); lane 6 started.
- 2026-10-28: lane 6 merged (#896). CI Native (Linux, Debug) failed on `code_generator.cpp` (missing `resolver.hpp` include); fixed in #897. Decompile fidelity sweep ported (#898). Real-data gates still not run on any machine; lane 7 not started; release on hold.
- 2026-10-28: follow-up fixes merged: #899 (dangling-else), #900 (braced vectors in gtest macros), #901 (partial designated initializer in PET NIF options), #902 (decompiler and codegen real-data env names unified, CI retry loop), #903 (code-notes corrections). First green Native (Linux, Debug) with lanes 1-6 on the #901 merge (run 26718).
- 2026-10-28: #904 (`GraphCompiler::compileToFile` writes the `.pex`), #905 (shared corpus-roots helper in four real-data tests), #906 (move the lifted graph document), #907 (F4SE emitter output compiles to pex test), #908 (`docs/papyrus-real-data-gates.md`, the data-machine runbook) merged. Lows logged on #752. The Windows MSVC, clang-cl and `fo4-data` jobs are still cancelled or waiting (the runner never picked them up), so those results are unread. Real-data gates not run on any machine; lane 7 and the alpha.2 release remain on hold.
