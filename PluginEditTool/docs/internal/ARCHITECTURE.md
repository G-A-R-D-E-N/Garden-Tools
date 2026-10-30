# FO4IDE: Native Architecture and Contributor Facts

This document describes the shipping C++ architecture. User documentation lives in the root
[README](../../README.md) and [MCP setup guide](../MCP_SETUP.md).

## Runtime layout

FO4IDE is now one native C++23 process plus a React bundle:

```text
React in WebView2 (Windows) or a browser tab (Linux)
        |
        | JSON RPC / host-object calls
        v
fo4ide::rpc::RpcDispatcher
        |
        +-- app / appInterop / settings
        +-- backend / masters / cell
        +-- papyrus / graph
        +-- archive / material / audio / nif
        |
        v
Radicle + native service libraries
```

The executable target is `fo4ide`, shipped as `FO4RecordEditor.exe` on Windows for compatibility
with existing MO2 launch entries and MCP configurations. `native/apps/fo4ide/main.cpp` owns process
startup, target registration, the desktop window, HTTP mode, and stdio MCP mode.

The React application lives under `web/src/`. Windows embeds it in WebView2. Linux has no embedded
window (`NativeWindow::available()` is false off Windows): the process serves `web/dist` and the RPC
endpoint on a loopback port and opens the page in the default browser. Packaged builds place
`web/dist` beside the executable.

The old `FO4RecordEditor*` C# projects and vendored Mutagen tree are historical implementation/oracle
material during the rewrite. They are not the backend used by the native host and must not be used as
a reason to add a new runtime dependency on .NET.

## IPC contract

React uses `window.chrome.webview.hostObjects.<target>.<method>(...)`. The native window forwards
those calls into `RpcDispatcher`, which invokes a registered `fo4ide::rpc::Target`. In a browser tab,
`web/src/hostbridge.ts` installs the same `hostObjects` surface over `POST /rpc` and the `/events`
stream, sending the per-run access token (the `t` parameter of the URL the process prints) in the
`X-FO4IDE-Token` header.

Important targets:

- `backend`: plugin reads, writes, conflict data, compatibility adapters
- `masters`: master listing/reorder/light flag/save
- `cell`: CELL/WRLD search, placed references, transform edits
- `papyrus`: parser/compiler/decompiler/analysis operations
- `graph`: visual Papyrus graph operations
- `archive`, `material`, `audio`, `nif`: asset services
- `appInterop`: MO2 profile/environment lifecycle

`PluginBackendTarget.cpp` is the compatibility boundary for legacy React method names. Keep
compatibility naming there rather than polluting Radicle with UI-era names.

Most legacy GUI methods return strings, and several read APIs return JSON serialized inside those
strings. Preserve the existing React wire contract until the frontend is deliberately migrated to a
cleaner protocol.

## Plugin engine

Radicle is the native Fallout 4 plugin SDK. The runtime path does not use Mutagen.

Core responsibilities include:

- TES4/master parsing and serialization
- FormID encoding/decoding for full and light plugins
- FormLink discovery and remapping
- record mutation and safe copy-as-override operations
- nested CELL/WRLD/placed-reference containment
- schema-driven scalar and list editing
- CTDA condition encoding
- conflict/load-order graph construction
- BA2 handling

### Generic first

The editor must remain generic over Fallout 4 records. Prefer schema declarations and reusable
binary primitives over UI-specific record special cases.

Typed form helpers are appropriate where a structure has a verified binary contract, for example
CELL hierarchy, REFR/ACHR transforms, RACE lists, SCOL placements, or QUST structures. Unknown or
unverified structures must fail closed rather than guessing offsets.

## Write safety invariants

These rules are load-bearing.

### Transactional mutation

Mutations normally operate on a copy of the loaded `fo4::Plugin`. The in-memory live plugin is
replaced only after validation and a successful atomic write. A failed operation must not leave a
partially mutated live graph.

### Atomic files

Plugin and generated sidecar writes use `fo4::io::write_file_atomic` / the Radicle writer. Do not
replace this with direct truncating writes.

### External modification guard

Every loaded writable plugin records its disk modification time. `writeGuard()` refuses writes if
the file changed after load. This prevents FO4IDE from silently overwriting edits made by another
tool.

### Protected game files

Game Data files and official Bethesda/Creation Club plugin names are read-only. `isEditable()` and
new-plugin filename checks enforce this at the backend, not only in the UI.

### New patch authority

Automatically-created plugins may only be written into the trusted MO2 overwrite directory supplied
by the active profile. JavaScript cannot choose an arbitrary filesystem path.

If a write workflow auto-creates a target and the requested mutation fails or becomes a semantic
no-op, the bridge asks the backend to roll that empty target back. Rollback is allowed only when the
file is unchanged, still inside the trusted overwrite root, parses successfully, and remains
TES4-only with no masters or records.

### Master remapping

Changing the master table must remap all schema-proven FormLinks, record header FormIDs, and
FormID-bearing GRUP labels. CELL, WRLD, DIAL, and placed-reference containment depends on those GRUP
labels being re-encoded through the new master context.

### Nested records

Do not generalize top-level record operations to nested records by copying bytes into a random GRUP.
Use a containment-aware primitive. REFR/ACHR currently have a verified CELL-aware copy path. Other
nested families stay fail-closed until their hierarchy is proven.

### FormID wire format

The user-facing FormKey format is:

```text
001234:Fallout4.esm
```

Local IDs are six hexadecimal digits followed by the owner plugin. Light-plugin allocation is
bounded by the engine's 12-bit object-ID behavior.

## Load order and conflicts

`fo4::ConflictGraph` is rebuilt from the loaded plugin vector. Later plugins win. Read APIs should
use the winning version when the user asks about the effective load order and preserve explicit
plugin-version selection when the UI asks for a particular column.

Reference impact analysis must use the schema-proven FormLink graph. Do not infer references from
arbitrary 4-byte values.

Renumbering a plugin fixes references inside that plugin. It cannot rewrite other plugins in place;
`ChangeReferencingRecords` creates/copies overrides into a selected patch for that purpose.

## Generic element editing

The legacy C# editor used reflection over Mutagen object graphs. Native generic editing instead uses
Radicle schema descriptors.

`DescribeElement`, `AddElement`, `RemoveElement`, `MoveElement`, and `ClearElement` expose only
schema-proven list structures. Unsupported reflection-only paths return a capability error rather
than inventing binary layouts.

The same rule currently applies to conditions: root CTDA `Conditions` is native. Historical nested
Mutagen reflection paths fail with `ConditionPathUnsupported` until an equivalent native schema is
implemented.

## CELL and worldspace editing

The Cell viewer is native. Verified support includes:

- CELL and WRLD search
- interior/exterior CELL resolution
- REFR and ACHR enumeration
- persistent/temporary child groups
- placed-reference transforms
- nested override creation with CELL/WRLD containment
- SCOL child placements
- TXST diffuse texture lookup

Do not add another placed-record family until its binary layout is verified against xEdit/Mutagen
oracle material and covered by round-trip tests.

## AI integration

The native desktop host does not embed Anthropic/OpenAI/provider session code. AI clients connect to
FO4IDE through the native stdio MCP server:

```text
FO4RecordEditor.exe --mcp
```

The right-side React panel is only an MCP handoff surface. `fo4:ask-ai` events place generated prompts
there for copying into an MCP-connected client. Do not restore `hostObjects.chat` or a managed
provider stack as part of backend work.

## MCP

`native/apps/fo4ide/main.cpp` creates the MCP registry and `registerNativeMcpTools` in
`native/apps/fo4ide/McpTools.cpp` fills it with the native tools, backed by the same service targets
used by the GUI. `fo4ide::mcp::StdioServer` owns the stdio protocol loop.

When adding a tool:

1. identify the native target/method that owns the behavior,
2. register the tool schema in the MCP registry,
3. dispatch to the same tested native service rather than duplicating logic,
4. mark/desribe mutation semantics accurately in the tool schema,
5. add a native test for the underlying operation and, where useful, the MCP dispatcher,
6. add the tool to the reference in `docs/MCP_SETUP.md`, which ships to users and must list exactly
   the registered tools.

The GUI and MCP should converge on one backend operation, not maintain separate implementations.

## Testing

Native tests use Catch2/CTest under `native/tests/`; Radicle has its own test suite under
`Radicle/tests/`.

Typical Linux inner loop:

```bash
cmake -S PluginEditTool/native -B build/fo4ide -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON
cmake --build build/fo4ide
ctest --test-dir build/fo4ide --output-on-failure
```

Radicle standalone:

```bash
cmake -S Radicle -B build/radicle -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DRADICLE_BUILD_TESTS=ON
cmake --build build/radicle
ctest --test-dir build/radicle --output-on-failure
```

Build the React bundle with:

```bash
cd PluginEditTool/web
npm ci
npm run build
```

CI is `.github/workflows/native-ci.yml`. It builds/tests Radicle on Linux, cross-builds Radicle for
Windows, and builds/tests the PluginEditTool native host on Linux.

## Release paths

- `package.ps1`: Windows C++ release zip. Helper programs are bundled from the `-TexconvPath`,
  `-TexconvPath`, `-CkWikiPath`, `-AudioToolsDir` and `-BaseScriptsPath` parameters or their
  environment variables; an unset one is skipped with a warning.
- `publish.bat [deploy-folder]`: Windows local build/deploy loop for MO2 testing. The folder can also
  come from `FO4IDE_DEPLOY_DIR`; with neither, it builds without deploying.
- `packaging/linux/build-deb.sh`: Linux C++ `.deb`

None of these release paths should invoke `dotnet` after the native cutover.

## UI color semantics

`web/src/index.css` is the source of truth for conflict colors. Preserve xEdit-style semantic
meaning such as winner/loser/override/critical conflict even if actual colors are retuned.

Do not use orphaned WPF views or old managed brushes as UI sources of truth.