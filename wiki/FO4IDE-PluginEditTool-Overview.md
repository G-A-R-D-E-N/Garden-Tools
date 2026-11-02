# FO4IDE / PluginEditTool: how it is built

Draft for new developers. Source: `nomad/dev` checkout. Paths are relative to the repo root.

## What it is

FO4IDE is a Fallout 4 plugin editor. The folder is `PluginEditTool/`. One program does two jobs
(`PluginEditTool/README.md`):

- a desktop editor (browse records across a load order, diff, edit, conflicts, Papyrus, NIF, cells)
- an MCP server over stdio for AI clients

The MCP server name is `fo4editor` (`PluginEditTool/docs/MCP_SETUP.md`). The editor "Fallout_4_plugin_editor"
name used in some MCP setups is a client-side label (unverified: not found in the files read).

The shipped program is one native C++23 executable plus a React bundle
(`PluginEditTool/docs/internal/ARCHITECTURE.md`). The target is `fo4ide`; on Windows it is renamed
to `FO4RecordEditor.exe` so old MO2 launch entries and MCP configs keep working
(`PluginEditTool/native/apps/fo4ide/CMakeLists.txt`, `OUTPUT_NAME "FO4RecordEditor"`).

## Policy (checked against the docs)

- **Radicle replaces the legacy C# / Mutagen editor.** Verified: `PluginEditTool/README.md` says the
  shipped app does not need Mutagen, xEdit or .NET, and the C# implementation "is retained ... only
  while native parity and regression coverage are completed". `Radicle/docs/FO4IDE.md` says "Do not
  add new Mutagen-only behavior"; missing coverage goes into Radicle.
- **Radicle owns plugin format; PluginEditTool owns app state.** `Radicle/docs/FO4IDE.md`: PluginEditTool
  owns application state, UI/RPC contracts, MO2 discovery and user workflow; Radicle owns parsing,
  FormID/FormKey identity, FormLink remapping and serialization.
- **"PluginEditTool is display-only, no Papyrus logic":** not stated in the docs reviewed. The native host
  does contain a Papyrus engine (`PluginEditTool/native/src/papyrus/`, Lexer, Parser, Resolver,
  CodeGenerator, Decompiler) and `README.md` says the "native Papyrus engine handles parsing,
  analysis, PEX generation/decompilation". Treat the display-only rule as unverified, and as possibly
  not true for the current native code. Ask the owners before repeating it.
- **"C# calls Radicle only through bindings":** partly verified. The C# projects reference
  `Radicle/bindings/csharp/Radicle.Interop` (`PluginEditTool/FO4RecordEditor.Core/FO4RecordEditor.Core.csproj`),
  a .NET wrapper of Radicle's C ABI (`Radicle/bindings/csharp/README.md`). Whether any C# code
  reaches Radicle another way was not audited (unverified).

## Layers: how a click becomes a Radicle call

```text
React UI (web/src)               runs in WebView2 on Windows, a browser tab on Linux
   |  window.chrome.webview.hostObjects.<target>.<method>(...)
   |  (browser: web/src/hostbridge.ts, POST /rpc + /events stream, X-FO4IDE-Token header)
   v
fo4ide::rpc::RpcDispatcher       native/src/rpc/RpcDispatcher.cpp
   |  picks a registered Target by name
   v
Target classes                   native/src/app/*Target.cpp  (PluginBackendTarget, ...)
   v
RadiclePluginBackend             native/src/plugin/RadiclePluginBackend.cpp
   |  (behind the IPluginBackend seam)
   v
Radicle C++ library              Radicle/  (parse, mutate, remap, conflict graph, write)
```

Facts for each hop:

- UI to host: `PluginEditTool/docs/internal/ARCHITECTURE.md` ("IPC contract"). Most legacy methods return
  strings, some with JSON inside; keep that wire shape until the frontend is migrated on purpose.
- `PluginBackendTarget.cpp` is the compatibility boundary for old React method names. Keep UI-era
  names there, not in Radicle (`ARCHITECTURE.md`).
- `Radicle/docs/FO4IDE.md` names `RadiclePluginBackend` as the implementation behind `IPluginBackend`,
  so the React/RPC surface needs no second plugin model.
- The native host links Radicle directly as a C++ subdirectory (`PluginEditTool/native/CMakeLists.txt`,
  `add_subdirectory("${RADICLE_DIR}" ...)`). So on the native path the call is C++ to C++. The C ABI
  (`fo4/c_api.h`, `radicle_abi_version()` first) is for consumers that cannot link C++
  (`Radicle/docs/FO4IDE.md`). The hop is therefore "C ABI" only for C# callers.

### RPC targets (from `ARCHITECTURE.md`)

| Target | Purpose |
|---|---|
| `backend` | plugin reads, writes, conflict data, compatibility adapters |
| `masters` | master list, reorder, light flag, save |
| `cell` | CELL/WRLD search, placed references, transform edits |
| `papyrus` | parser, compiler, decompiler, analysis |
| `graph` | visual Papyrus graph |
| `archive`, `material`, `audio`, `nif` | asset services |
| `appInterop` | MO2 profile and environment lifecycle |

## Write safety (in the backend, not the UI)

From `ARCHITECTURE.md` and `docs/MCP_SETUP.md`: edits work on a copy and swap in after validation;
writes are atomic; a write is refused if the file changed on disk since load; game and Creation
Club plugins are read-only; auto-created plugins go only into MO2's overwrite folder; unproven
structures fail closed.

## The MCP server

- Start: `FO4RecordEditor.exe --mcp [--mo2 <instance>]`. No window; line-delimited JSON-RPC on stdio
  (`docs/MCP_SETUP.md`). Other flag: `--health`.
- Code: `native/apps/fo4ide/main.cpp` makes the registry; `registerNativeMcpTools` in
  `native/apps/fo4ide/McpTools.cpp` fills it; `native/src/mcp/McpServer.cpp` (`fo4ide::mcp::StdioServer`)
  runs the loop. Tools dispatch to the same RPC targets the GUI uses (`RpcTool`: target plus method).
- Rule: GUI and MCP must converge on one backend operation, and `docs/MCP_SETUP.md` must list exactly
  the registered tools (`ARCHITECTURE.md`, "MCP").

Tool categories as `docs/MCP_SETUP.md` lists them (its own count is 45; not recounted):

| Category | Tools seen in `McpTools.cpp` or the reference |
|---|---|
| Plugins and records | `list_plugins`, `open_plugin`, `create_plugin`, `create_record`, `create_placed_object`, `create_interior_cell`, `get_record`, `get_record_fields`, `list_record_types`, `list_records`, `list_records_summary`, `list_plugin_worklist`, `search_records`, `set_field`, `save_plugin`, `discard_plugin`, `list_masters` |
| Typed records | `get_leveled_list`, `set_leveled_list_settings`, `edit_leveled_list_entry`, `get_npc`, `edit_npc_entries`, `get_faction`, `edit_faction_entries`, `get_weapon_damage_types` |
| Profile and host | `native_status`, `get_mo2_status`, `reload_mo2_profile`, `get_settings` |
| Papyrus | `compile_papyrus`, `decompile_papyrus`, `papyrus_check`, `papyrus_function_lookup`, `papyrus_script_info` |
| NIF | `nif_inspect`, `nif_geometry`, `nif_verify`, `nif_tree` |
| Materials | `material_inspect`, `material_set_fields` |
| Audio | `audio_convert_to_xwm`, `audio_convert_from_xwm`, `audio_make_fuz`, `audio_extract_fuz` |
| Archives | `archive_pack` |

Papyrus, material, audio and archive registrations were confirmed only from the reference doc, not
read line by line in `McpTools.cpp` (unverified). The host has no embedded model: AI is the client's job
(`README.md`). The right-hand chat panel is only a handoff for prompts.

## Legacy versus native, and what is still on Mutagen

| Piece | State |
|---|---|
| `PluginEditTool/native/` | the shipping host (C++23) |
| `PluginEditTool/FO4RecordEditor/` | historical WPF app (`App.xaml`, `MainWindow.xaml`, Views, ViewModels); csproj references Mutagen, WebView2 |
| `PluginEditTool/FO4RecordEditor.Server/` | historical C# host (`Program.cs`, `RpcDispatcher.cs`, `EventStream.cs`, `LinuxHostServices.cs`); references Mutagen, Core, Photino |
| `PluginEditTool/FO4RecordEditor.Core/` | C# models and services; references both the vendored Mutagen and `Radicle.Interop` |
| `PluginEditTool/Mutagen/` | vendored Mutagen tree (`Mutagen.Bethesda.Core`, `.Fallout4`, `.Kernel`); `ARCHITECTURE.md` calls it historical oracle material |
| `FO4RecordEditor.Tests`, `FO4RecordEditor.Core.Tests` | C# test projects |
| `native/tests/` | Catch2/CTest tests for the native host |

Docs say the C# projects and Mutagen are "not the backend used by the native host" and must not
become a reason to add a .NET dependency (`ARCHITECTURE.md`). The generic element editing section
says the legacy editor used Mutagen reflection, and the native one uses Radicle schema descriptors;
historical nested-condition paths fail with `ConditionPathUnsupported`.

Not fully clean: `PluginEditTool/docs/code-notes/PluginEditTool.md` documents a SWF editor whose host
bridge is C# (`FO4RecordEditor/Services/SwfInterop.cs`, `[RpcExpose]`, `RadicleNative.EnsureLoaded()`)
and whose document service is `FO4RecordEditor.Core/Services/SwfDocumentService.cs`, both going
through `Radicle.Interop`. So some features have a C# bridge calling Radicle through bindings.
Whether those are wired into the native host build is unverified; the README says no .NET is needed.
Radicle's own `Radicle/docs/code-notes/Radicle.md` also tracks work. The page does not summarise it.

Which features are still Mutagen-only today: unverified. The docs only say the C# tree is kept for
parity and regression coverage. See also `PluginEditTool/docs/internal/MUTAGEN.md` and
`MUTAGEN_FORK_COMPARISON.md` (not read).

## Folder map

```text
PluginEditTool/
  native/                 C++23 host (CMake). Builds Radicle as a subdirectory
    apps/fo4ide/          main.cpp (startup, window, HTTP mode, stdio MCP mode), McpTools.cpp
    include/fo4ide/       public headers, one folder per module below
    src/app/              RPC targets: AppInterop, Archive, Audio, Graph, Material, Papyrus,
                          PluginBackend, ServiceTargets, WikiService
    src/rpc/              RpcDispatcher
    src/mcp/              McpServer (stdio)
    src/http/             HttpServer (loopback transport; Linux and browser mode)
    src/events/           EventBus
    src/plugin/           RadiclePluginBackend and its feature files (Cell, Compare, Check, Armor,
                          ConditionCodec, ...)
    src/graph/            visual Papyrus graph (document, validator, compiler, lifter, ...)
    src/papyrus/          Papyrus lexer, parser, resolver, codegen, decompiler, analysis
    src/archive/          BA2 reader, Radicle archive backend
    src/assets/ textures/ NIF service and rendering, texture resolve, DDS/BCn/PNG
    src/audio/ material/  audio and BGSM/BGEM services
    src/mo2/              MO2 profile loader
    src/f4se/             F4SE binding scanner, emitters, validators
    src/platform/         NativeWindow, platform services
    src/settings/ logging/ process/ tools/   settings, logger, child processes, helper locator
    tests/                native tests
  web/                    React 19 + Vite + TypeScript + Vitest UI
    src/App.tsx           HashRouter; "/" redirects to "/main" (MainShell)
    src/MainShell.tsx     the shell; imports the panels (Papyrus, Swf, Nif, Masters, Archive,
                          Localization, Audio, Cell, Spreadsheet, Blueprint)
    src/Shell/            shell pieces: TopBar, StatusBar, DetailRail, ExplorerTree, ConflictsPanel,
                          CompareView, hooks useEnvironment, useRecordOpening, useConflictBatch
    src/hostbridge.ts     browser-mode hostObjects over HTTP
  FO4RecordEditor*/ Mutagen/   legacy C# (see above)
  docs/                   MCP_SETUP.md (ships to users), internal/ (contributor docs), code-notes/
  packaging/linux/        build-deb.sh, .desktop file, launcher script
  package.ps1 publish.bat  Windows packaging and local deploy
  tools/                  bundled-tools.json, fetch script, source-policy check, ci/
Radicle/                  C++ plugin SDK, C ABI, bindings/csharp (Radicle.Interop)
```

## Where each concern lives

| Concern | Place |
|---|---|
| Plugin parse, write, FormKey, remap, conflict graph | `Radicle/` |
| Legacy UI method names, wire shape | `native/src/app/PluginBackendTarget.cpp` |
| Backend glue to Radicle | `native/src/plugin/RadiclePluginBackend*.cpp` |
| MO2 profile discovery | `native/src/mo2/Mo2Profile.cpp`, `AppInteropTarget.cpp` |
| MCP tool list | `native/apps/fo4ide/McpTools.cpp`, doc `docs/MCP_SETUP.md` |
| Conflict colours | `web/src/index.css` is the source of truth (`ARCHITECTURE.md`) |
| Settings | `%APPDATA%\FO4RecordEditor\settings.json` on Windows (`README.md`) |
| Contributor facts | `docs/internal/ARCHITECTURE.md`; index at `docs/internal/README.md` |
| Per-feature design notes | `docs/code-notes/PluginEditTool.md` |

## Packaging

- Windows: `package.ps1` builds the web UI (unless `-SkipWeb`), builds the C++ exe with CMake, then
  stages `FO4RecordEditor.exe`, `web\dist\`, optional `tools\` helpers, `mcp.sample.json` and
  `docs\MCP_SETUP.md` into a zip (`package.ps1`, `README.md` "Package layout"). The exe looks for
  `web\dist\` beside itself first. No `dotnet` call (`README.md`, `ARCHITECTURE.md`).
- Helpers: texconv and ffmpeg are always bundled (pinned versions in `tools/bundled-tools.json`,
  SHA256 checked); niftool, CK wiki mirror, other audio tools and base scripts are optional
  and skipped with a warning when unset (`README.md`).
- Local loop: `publish.bat [deploy-folder]` builds and optionally copies into an MO2 tools folder.
- Linux: `packaging/linux/build-deb.sh <version>` makes a `.deb`. There is no embedded window;
  the process serves the UI and RPC on a loopback port and opens the browser
  (`README.md`, `ARCHITECTURE.md`).
- Direct build: `cmake -S native -B native/build/release ... --target fo4ide` (`README.md`).

## Testing, in one line each

- Native: Catch2/CTest in `native/tests/`. Radicle: its own suite in `Radicle/tests/`.
- Web: `npm run test` (Vitest) in `web/`; `npm run build` makes `web/dist`.
- Where each suite goes is set by the repo-root `AGENTS.md` ("Where tests go").
- CI status: not covered here.

## Read next

`PluginEditTool/docs/internal/README.md`, `ARCHITECTURE.md`, `PluginEditTool/docs/MCP_SETUP.md`,
`Radicle/docs/FO4IDE.md`, `Radicle/bindings/csharp/README.md`.
