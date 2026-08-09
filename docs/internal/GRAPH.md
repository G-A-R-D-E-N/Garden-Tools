# The node graph compiler

A visual scripting layer over the Papyrus toolchain. A graph document becomes readable `.psc`
source, which the built-in compiler turns into a `.pex`. Everything below it already existed and is
documented in [PAPYRUS.md](PAPYRUS.md).

## Why it exists, and why it emits source

Writing a Papyrus script meant writing Papyrus. This adds an authoring surface above that.

It emits `.psc` text rather than building the syntax tree or the bytecode directly, and that is the
load-bearing decision:

- The resolver, type checker, code generator and PEX writer are reused unchanged. They are measured
  at 158 of 161 scripts instruction-identical to the Creation Kit, and that measurement carries over.
- The output is source a person can read, diff, hand-edit and check into a repository. A graph that
  produced only a binary would be a black box.
- It makes the roundtrip gate natural: generated source and hand-written source go through exactly
  the same compiler, so they can be compared instruction for instruction.

The alternative, building `PapyrusScript` nodes directly, was rejected because every mutator on that
tree is `internal` and because it would produce no readable artefact.

## Layers

| Layer | File | State |
|---|---|---|
| Document model | `GraphDocument.cs`, `GraphDocumentJson.cs` | done |
| Diagnostics | `GraphDiagnostic.cs` (`GRA####`) | done |
| Node types | `NodeDefinition.cs`, `BuiltinNodeDefinitions.cs` | done |
| Palette | `NodePalette.cs`, `WikiDocs.cs` | done |
| Type resolution | `GraphTypeResolver.cs` | done |
| Refusal engine | `GraphValidator.cs` | done |
| Control flow | `GraphExecFlow.cs` | done |
| Lowering | `GraphLinearizer.cs`, `GraphIr.cs`, `GraphNameTable.cs` | done |
| Source emission | `PapyrusSourceWriter.cs` (+ `GraphSourceMap`) | done |
| Entry point | `GraphCompiler.cs` | done |
| Tool surface | `GraphToolService.cs` | done |
| Canvas | `web/src/Blueprint/` | done |

All of it is in `FO4RecordEditor.Core`, plain `net8.0`, so it builds and tests on Linux.

## Design stances worth keeping

**Pins are never serialized.** A saved node stores its definition id, not its pins; the pins are
re-derived from the palette on load. So a parameter renamed in a base script surfaces as a dangling
wire naming the node, instead of silently compiling a call with the wrong arguments.

**Nothing is inferred pure.** Papyrus annotates no function as side-effect free, so every call,
property set and array mutation carries control flow pins and binds its result to a local at its
point in the sequence. Evaluation order is then fixed by the exec graph rather than by an ordering
analysis. The cost is that one `Game.GetPlayer()` used three times emits one local, which is what a
person would have written anyway.

**An explicit conversion is refused, never inserted.** A downcast can yield `None` at run time and a
float-to-int conversion loses information. The author owns that by placing a Cast node. Silently
inserting one would hide a real risk inside generated source they never see.

**Locals are function-scoped and hoisted.** A value produced inside one branch and read after the
merge has to outlive the branch. Hoisting makes the emitted scoping match the analysis exactly, and
it is the honest answer to "how are names mangled across nested scopes": there are no nested scopes.

**Refuse at the first stage that errors.** Inherited from `PapyrusCompiler`. A later stage would
otherwise work from a tree it has already been told is wrong, and a wrong script is worse than none
because nothing downstream reports it until the game runs it.

## Rules that are not in the documentation

Found by measurement, each now covered by a test.

- **An unwired exec output is a path out of the function.** Without modelling that, a Branch whose
  Else pin is empty appears to have one successor, its own Then target post-dominates it, and
  everything after the branch is emitted *after* the `EndIf` rather than inside it. This produced a
  silently wrong script that still compiled.
- **A script cannot resolve its own type.** `PapyrusScriptIndex` reads disk roots and has no
  in-memory injection, so a script not on a root is unresolvable even while being compiled. As soon
  as a graph passes `Self` where a base type is wanted, the checker asks whether the script inherits
  from the parameter type, fails to find the script, and refuses a valid call. `GraphCompiler`
  therefore publishes generated source to a scratch root. The write has to precede `AddRoot`:
  `AddRoot` snapshots a directory's contents into its name map and returns early the second time it
  is given the same root.
- **A struct owned by another script is written `Owner:Struct`.** The shipped
  `ObjectReference.ApplyMaterialSwap` returns `MatSwap:RemapData[]`, and the bare name does not
  resolve from a different script.
- **`Length` is a keyword.** A local named `length` will not lex, which is why the name table
  reserves every keyword rather than only the obvious identifiers.
- **`CustomEventName` and `ScriptEventName` are lexer keywords, not scripts.** Nothing declares them,
  so `ScriptObject` reports an incomplete base chain against any root set, including a real game
  install.
- **A skipped middle optional forces named arguments** for every argument after it. A positional
  emitter gets this silently wrong.

## What is measured, and how

Run from `FO4RecordEditor/`:

```
dotnet test FO4RecordEditor.Core.Tests/FO4RecordEditor.Core.Tests.csproj
```

| What | How | Result |
|---|---|---|
| Suite total | ungated, Linux net8.0 | **773 passing, 0 failed** |
| Bare-checkout parity | same run with no env vars set | **773 / 773 identical** |
| Fixture graphs compiling clean | 25 patterns, stub base tree | **25 / 25** |
| Generated instructions vs hand-written Papyrus | `PexComparer`, 25 fixtures | **25 / 25 identical** |
| Compiled object decompiles and recompiles | 25 fixtures | **25 / 25** |
| Negative matrix | 19 invalid graphs | **19 / 19 correct code and node id** |
| Dataflow matrix | 15 graphs, refused and accepted | **15 / 15 correct code, node and pin** |
| State matrix | 10 graphs, emission and refusal | **10 / 10 correct block, code and node** |
| Graph to source | `FO4RE_GRAPH_BENCH`, 25 fixtures, 106 nodes | **1.25 ms median, 3.15 ms p95** |
| Translation throughput | derived from the above | **11,812 ns per node, 84,658 nodes per second** |
| Graph to `.pex` end to end | same sweep | **4.07 ms median for 25, 0.163 ms per fixture** |
| Palette build over the real base tree | `FO4RE_PSC_ROOTS`, 7,854 scripts | **1.91 s cold, 5.0 ms median warm search** |

Benchmarks were taken on a **Debug** build, 6 CPUs, .NET 8.0.29, PikaOS 4. Debug is the conservative
direction; the machine context is printed by the harness itself so a figure is never quoted without
it.

## The traps in reading those numbers

**"25 / 25 identical" is Oracle 2, not a fixed point.** The comparison that carries weight is
generated output against *hand-written* Papyrus for the same behaviour, both through the same
compiler. That is what says the graph produces what a modder would have written.

**The decompile check is deliberately weaker than instruction identity.** It proves the emitted
object is well formed enough that the decompiler reads it, produces Papyrus the parser accepts, and
that source compiles again. It does **not** compare instructions, because measurement showed the
decompiler renders implicit zero-initialisation as an explicit assignment (so recompiling adds a
second one) and, on `08_SharedCallBindsOneLocal`, drops a call whose result is discarded. Both are
decompiler fidelity limits, which PAPYRUS.md already records as bodies being best-effort. Normalising
them away to reach a green fixed point would have been inventing a result.

**100 percent clean would also describe a checker that never fires.** The negative matrix exists for
that reason: 19 deliberately invalid graphs, each asserting the exact `GRA` code and the exact node
and pin id as structured fields, never by searching message text.

**The gate runs against a reduced stub tree, not the real base scripts.** See below.

## The stub tree, and its risk

`FO4RecordEditor.Core.Tests/Fixtures/BaseStubs/` holds 33 hand-authored stand-ins for the base
scripts. They exist because `PapyrusScriptIndex` resolves from disk and has no injection path, so
without them the whole gate would only run where a game install is present, and the headline result
could not be reproduced from a bare checkout.

They are declarations only: no bodies, no documentation comments, nothing taken from the shipped
sources. Signatures are API facts also published on the Creation Kit wiki.

This is a second source of truth and it can drift. `BaseStubFidelityTests`, gated on
`FO4RE_PSC_ROOTS`, checks every declared member against the real tree: currently **135 members, 0
missing, 0 shape mismatches**. The residual risk is that nobody runs the gated sweep.

## The mistakes that compile anyway

Every graph below produces a script Papyrus accepts. None of them does what the canvas shows, and the
failure only appears once the game runs it, so the graph compiler is the only place these can be
caught. Each was found by building the graph and reading the emitted source, not by inspection.

**A returning function that falls off the end.** Papyrus hands back the return type's zero value
rather than refusing. `GraphExecFlow.PathsLeavingWithoutReturn` answers this as a greatest fixpoint
over "this node returns on every path", and `GRA0040` names the entry plus the nodes control actually
leaves through. A loop is judged by its `Completed` pin alone: a body that always returns still does
not make the loop return, because a condition false on the first test skips the body entirely.

**A value wired out of one arm of a branch.** An impure node's output becomes a local, Papyrus
declares locals at function scope and zero-initialises them, so reading one on an arm that never ran
yields 0. `GraphDefiniteAssignment` refuses this with `GRA0041`, and with `GRA0044` for the special
case of a loop reading its own body, where the fix is different.

The question is answered with **dominance**, not with emission order. Lowering walks each arm before
the merge, so by the time it reaches the merge the local looks bound; only the control flow graph
knows one arm was optional. `GraphExecFlow` therefore computes forward dominators alongside the
post-dominators the region emitter already needed. Pure producers are exempt, because they are
rebuilt inline at each use and never become a local; a read through a chain of pure nodes is
attributed to the exec node at the end of the chain, which is where the expression is evaluated.

The order-based check inside `Pull` is kept as a backstop for a producer that is not sequenced on any
path at all, which dominance does not cover because the producer is not in the flow graph.

**A named state that never reaches the source.** `StateName` was carried from the entry node all the
way to `IrCallable` and then dropped by the writer, so every handler landed in the empty state and a
two state machine ran both arms at once. `PapyrusSourceWriter` now emits the empty state first and
then one `State` block per name, with `Auto State` for the one the header names. The auto state is a
header field rather than a per entry flag, because Papyrus allows exactly one and two entries could
otherwise both claim it with no non-arbitrary way to resolve that.

**Reachability cannot be asked one entry at a time.** `GRA0022` was raised per callable, and each
entry is its own root, so every node belonging to a sibling event looked unreachable. Any script with
two events, which is most of them, got a warning per node in the other one. It is now a document-wide
pass over the union of every entry's reachable set.

All 25 fixtures still compile clean under every pass above, which is what says the checks are not
merely strict.

## What is left

- **`Break` and `Continue`.** Papyrus has no `goto`, so both need a sentinel-bool rewrite of the
  enclosing loop. Deferred rather than half-built. `ForEach` ships, lowered to a `While` over an index.
- **The canvas has no copy and paste, no minimap and no auto-layout.** The reducer supports paste;
  nothing is wired to it.

## Gotchas

- Node ids are opaque strings, not GUIDs. The canvas serves from a loopback origin on Linux, and
  `crypto.randomUUID` is not dependable there.
- JSON is camelCase on both sides, set once in `GraphDocumentJson`. This is the most likely place
  the C# and TypeScript halves drift.
- `PapyrusResolver.ArrayMember` is private, so the array-builtin parity test asserts behaviourally
  by resolving each member on a real array rather than by reading a list.
- The generated header uses `;` line comments, never a `{ }` doc comment: a doc comment lands in the
  compiled object's `DocString` and would ship to anyone who opens the script.
