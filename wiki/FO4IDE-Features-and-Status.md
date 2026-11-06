# FO4IDE / PluginEditTool: feature status

This page lists the user-facing features of FO4IDE (shipped as `FO4RecordEditor.exe`) and says how far each one goes. It is based only on the `nomad/dev` checkout, read on 2026-10-28. It does not use CI results.

## How to read the status

Status is judged from code, tests and docs in the checkout. Nobody ran the app for this page.

| Status | Meaning |
|---|---|
| working | UI exists, a backend path exists in the native host, and tests or docs cover it. |
| partial | It works only in part, or the code or docs name a limit. |
| planned | Docs name it as open or not built. |
| unverified | The UI or backend exists but the checkout does not show enough to say it works. |

"Tests" lists the test files only. It does not say they pass.

## The big picture

- `PluginEditTool/native/` (CMake target `fo4ide`) is the main tool. It is a native C++ host on Radicle. It gives a desktop window, an HTTP mode and a stdio MCP server (`PluginEditTool/docs/internal/INDEX.md`, `PluginEditTool/docs/internal/ARCHITECTURE.md`).
- The React UI lives in `PluginEditTool/web/src/`. It calls the host through `window.chrome.webview.hostObjects.<target>.<method>` (`PluginEditTool/docs/internal/ARCHITECTURE.md`).
- The native host registers these RPC targets: `app`, `appInterop`, `settings`, `nif`, `papyrus`, `audio`, `material`, `archive`, `backend`, `masters`, `cell`, `strings`, `graph` (`PluginEditTool/native/src/app/ServiceTargets.cpp`, lines 254-266).
- The C# projects (`PluginEditTool/FO4RecordEditor*`) are the older code. `PluginEditTool/docs/internal/INDEX.md` says the editor "no longer" builds on Mutagen.
- The native MCP tool list is in `PluginEditTool/docs/MCP_SETUP.md`. The doc says the live answer is what `tools/list` returns, and the source is `PluginEditTool/native/apps/fo4ide/McpTools.cpp`.

## Feature table

### Records

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Open plugins, load environment, plugin tree | `web/src/MainShell.tsx`, `web/src/Shell/ExplorerTree.tsx`, `web/src/Shell/useEnvironment.ts` | native `backend`, `appInterop` targets (`ServiceTargets.cpp`); MCP `open_plugin`, `list_plugins` | `web/src/MainShell.openOrder.test.tsx`, `web/src/Shell/ExplorerTree.test.tsx`, `native/tests/unit/RadiclePluginLifecycleTests.cpp` | working | |
| Record viewer (field rows per plugin, edit values) | `web/src/RecordView.tsx`, `web/src/ScalarEditor.tsx` | native `backend`: `GetConflictMatrix`, `SetField`; MCP `get_record`, `set_field` | `web/src/RecordView.scalar.test.tsx`, `RecordView.semantic.test.tsx`, `RecordView.highlight.test.tsx`, `RecordView.loadOrder.test.tsx`, `ScalarEditor.test.tsx` | working | Enum and flag editing is tested. |
| Record picker and search | `web/src/RecordPicker.tsx`, `web/src/MainShell.tsx` | `SearchRecords`; MCP `search_records` | `web/src/RecordPicker.test.tsx`, `web/src/MainShell.recordSources.test.tsx` | working | |
| Typed editors: leveled lists (LVLI, LVLN, LVSP) | `web/src/TypedRecordPanel.tsx` | `GetLeveledList`, `SetLeveledListSettings`, `EditLeveledListEntry`; MCP `get_leveled_list`, `set_leveled_list_settings`, `edit_leveled_list_entry` | `web/src/TypedRecordPanel.leveled.test.tsx`, `TypedRecordPanel.edits.test.tsx`, `native/tests/unit/RadiclePluginLeveledTests.cpp` | working | |
| Typed editors: NPC factions, perks, inventory | `web/src/TypedRecordPanel.tsx` | `GetNpc`, `EditNpcEntries`; MCP `get_npc`, `edit_npc_entries` | `web/src/TypedRecordPanel.npc.test.tsx`, `native/tests/unit/RadiclePluginTypedRecordsTests.cpp` | working | |
| Typed editors: faction relations and ranks | `web/src/TypedRecordPanel.tsx` | `GetFaction`, `EditFactionEntries`; MCP `get_faction`, `edit_faction_entries` | `web/src/TypedRecordPanel.faction.test.tsx` | working | |
| Typed view: weapon damage types | `web/src/TypedRecordPanel.tsx` | `GetWeaponDamageTypes`; MCP `get_weapon_damage_types` | `web/src/TypedRecordPanel.weapon.test.tsx` | partial | Read-only by design. `MCP_SETUP.md` also says "(read-only)". |
| Create record, create plugin | `web/src/recordActions.ts`, `web/src/PluginTargetDialog.tsx` | `CreateRecord`, `CreatePlugin`; MCP `create_record`, `create_plugin` | `native/tests/unit/RadiclePluginCreateTests.cpp`, `RadiclePluginRichCreationTests.cpp`, `RadiclePluginSchemaAuthoringTests.cpp` | partial | `create_record` accepts a fixed list of signatures. See the payload table in `docs/MCP_SETUP.md`. |
| Create placed object, interior cell | MCP only in `docs/MCP_SETUP.md`; cell panel has its own create flow (`web/src/CellPanel.tsx`) | `create_placed_object`, `create_interior_cell` | `native/tests/unit/RadiclePluginCellTests.cpp`, `RadiclePluginExteriorCellTests.cpp`, `web/src/CellPanelGrid.test.tsx` | working | |
| Spreadsheet (bulk-edit every record of one type) | `web/src/SpreadsheetPanel.tsx` | `GetRecordsGrid`, `SetField`, `SavePlugin`; MCP `list_records_summary` | `web/src/SpreadsheetPanel.test.tsx`, `SpreadsheetPanel.scalar.test.tsx`, `native/tests/unit/RadiclePluginSchemaGridTests.cpp` | working | |
| Save and discard plugin | `web/src/MainShell.tsx`, `web/src/MastersPanel.tsx` | `SavePlugin`; MCP `save_plugin`, `discard_plugin` | `native/tests/unit/RadiclePluginPendingLifecycleTests.cpp`, `RadiclePluginPreflightTests.cpp` | working | MCP saves only to the path the plugin was loaded from (`docs/MCP_SETUP.md`). |
| Localized strings (string tables) | `web/src/LocalizationPanel.tsx` | native `strings` target; `ListStringTables`, `SetString`, `ImportStrings`, `SaveStringTables` | `web/src/LocalizationPanel.test.tsx`, `native/tests/unit/RadiclePluginStringsTests.cpp` | working | The activity-bar tooltip says "read-only string tables" (`MainShell.tsx`), but the panel code has set, add, remove, import and export calls. The tooltip is out of date or the edit path is gated. Not checked. |
| Raw View (hex or subrecord dump) | none | none | none | planned | `docs/internal/UI_REDESIGN_TASKS.md` says it was deliberately not built. |

### Conflicts and copy workflows

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Conflict scan and conflict list | `web/src/Shell/ConflictsPanel.tsx`, `web/src/Shell/useConflictBatch.ts`, `web/src/Workspace/ConflictsView.tsx` | `ScanConflicts`, `GetConflicts` | `web/src/Shell/ConflictsPanel.test.tsx`, `web/src/Shell/useConflictBatch.test.tsx`, `web/src/Workspace/ConflictsView.test.tsx` | working | Cards are the main layout, matrix is a toggle (`UI_REDESIGN_TASKS.md`). |
| Conflict detail rail and donut | `web/src/Shell/DetailRail.tsx`, `web/src/Shell/ConflictDonut.tsx` | `GetRecordDetails`, `GetRecordPluginMatrix` | `web/src/Shell/DetailRail.test.tsx` | working | |
| Compare selected records | `web/src/Shell/CompareView.tsx` | none beyond the matrix | `web/src/Shell/CompareView.test.tsx` | working | Needs exactly two records (`ConflictsPanel.test.tsx`). |
| Compare to another plugin file | `web/src/PluginCompareDialog.tsx` | `ComparePluginTo` | `web/src/PluginCompareDialog.test.tsx`, `native/tests/unit/RadiclePluginCompareTests.cpp` | working | |
| Copy as override (single, many) | `web/src/RecordContextMenu.tsx`, `web/src/recordActions.ts` | `CopyAsOverride`, `CopyAsOverrideMany` | `web/src/recordActions.test.ts`, `web/src/overrideConfirmation.test.tsx`, `native/tests/unit/RadiclePluginMasterOverrideTests.cpp` | working | Result `LOSES:` is returned without writing when a later plugin also overrides (`docs/xedit-copy-workflows.md`). |
| Copy as disabled override | context menu | `CopyAsDisabledOverride` | not found in a dedicated test file | unverified | `docs/xedit-copy-workflows.md` says implemented in `native/src/plugin/RadiclePluginDisabledOverride.cpp`. I did not find a test file named for it. |
| Copy as wrapper (LVLI, LVLN, LVSP only) | context menu | `CopyAsWrapper` | `native/tests/unit/RadiclePluginCopyAsWrapperTests.cpp` | partial | xEdit also offers LVLB, LVLC, LVLP, LVSC. This port refuses them with `WrapperUnsupportedType`. |
| Inject forms into master | context menu | `InjectFormsIntoMaster`, Radicle `fo4::inject_local_record` | `native/tests/unit/RadiclePluginRenumberTests.cpp`, `RadiclePluginReferenceRemapTests.cpp` | unverified | `docs/xedit-copy-workflows.md` says implemented. Other plugins' references are not repointed. A dedicated test was not confirmed. |
| Deep copy as override | context menu | `DeepCopyAsOverride` | `native/tests/unit/RadiclePluginDeepCopyTests.cpp` | working | |
| Copy as new record, remove identical to master (ITM) | context menu | `CopyAsNewRecord`, `RemoveIdenticalToMaster` | `native/tests/unit/RadiclePluginItmNestedTests.cpp` | partial | Only ITM has a test file I could match. Copy as new has none by name. |
| Create merged patch | context menu | `CreateMergedPatch` | `native/tests/unit/RadiclePluginOverridePatchTests.cpp` | unverified | Test file is named for override patches. Link to this action not checked. |
| Change referencing records | context menu | `ChangeReferencingRecords` | `native/tests/unit/RadiclePluginReferenceRemapTests.cpp` | unverified | |
| Revert overrides | context menu | `RevertOverrides` | none matched | unverified | |
| Fixup race-specific leveled items | none | none | none | planned | Declined (#364): xEdit hides it for every game. `docs/xedit-copy-workflows.md`. |
| Copy idle animations into... | none | none | none | planned | Declined (#364): xEdit shows it only for games up to FNV. `docs/xedit-copy-workflows.md`. |
| Broken references scan | `web/src/Shell/BrokenRefsModal.tsx` | `ScanBrokenRefs` | none matched | unverified | |

### Cells and world

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Cell viewer (3D, worldspace and cell search, placed references) | `web/src/CellPanel.tsx`, `web/src/CellViewport.tsx` | native `cell` target: `SearchWorldspaces`, `SearchCells`, `GetPlacedReferences`, `GetGeometryBatch`, `GetTexture` | `web/src/CellPanel.test.ts`, `CellPanel.loadOrder.test.tsx`, `CellPanel.searchError.test.tsx`, `CellPanelGrid.test.tsx` | working | The 3D rendering is not covered by the web tests listed. |
| Move placed references (transform edit) | `web/src/CellViewport.tsx` (uses `TransformControls`) | `SetPlacedReferenceTransform` | `native/tests/unit/RadiclePluginCellTests.cpp` | partial | The activity-bar tooltip calls the viewer "read-only 3D" (`MainShell.tsx`), yet a transform call exists. Which one is right was not checked. |
| Navmesh, LOD, whole-map authoring | none | none | none | planned | `docs/MODKIT21_PULLS.md` lists LOD generation and the 3D world viewer as out of scope. |

### Conditions

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Conditions editor | `web/src/ConditionsEditor.tsx` | `GetConditions`, `SetConditions`, `GetConditionsAt`, `SetConditionsAt`, `GetConditionFunctions`, `GetConditionFunctionParams`, `GetConditionRunOnTypes` | `web/src/ConditionsEditor.test.tsx`, `native/tests/unit/ConditionBackendTests.cpp`, `ConditionCodecTests.cpp`, `ConditionFunctionsTests.cpp`, `ConditionPreservationBackendTests.cpp`, `QuestOpaqueConditionTests.cpp` | working | Preserved rows lock the function select (`ConditionsEditor.test.tsx`). |
| Quest stages and aliases | via `web/src/recordActions.ts` bridge | `SetQuestStages`, `SetQuestAliases` | `native/tests/unit/RadiclePluginQuestAuthoringTests.cpp`, `RadiclePluginQuestConditionDefaultsTests.cpp` | unverified | UI entry point not traced. |

### Masters and load order

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Masters panel: list, reorder, ESL flag, clean unused | `web/src/MastersPanel.tsx` | native `masters` target; `CleanUnused`, `SetLight`, `Reorder`; MCP `list_masters` | `web/src/MastersPanel.test.tsx`, `MastersPanelPluginSwitch.test.tsx`, `native/tests/unit/RadiclePluginMastersTests.cpp`, `RadiclePluginAddMastersTests.cpp` | working | Clean unused previews first (`MastersPanel.test.tsx`). |
| Add masters, renumber FormIDs, compact to ESL, check ESL eligibility | context menu | `AddMasters`, `RenumberPluginFormIds`, `CompactToEsl`, `CheckEslEligibility` | `native/tests/unit/RadiclePluginRenumberTests.cpp`, `RadiclePluginCompactTests.cpp`, `RadiclePluginEslTests.cpp` | working | |
| Create SEQ file | context menu | `CreateSeqFile` | `native/tests/unit/RadiclePluginSeqTests.cpp` | working | |
| MO2 profile load and reload | `web/src/MainShell.tsx`, `web/src/Shell/mo2Freshness.ts` | `appInterop`; MCP `get_mo2_status`, `reload_mo2_profile` | `native/tests/unit/Mo2ProfileTests.cpp`, `AppInteropTargetTests.cpp` | working | `docs/internal/MO2_SETUP.md` has a banner about an unresolved contradiction on running through MO2. |

### Problems and checks

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Problems list for a record (code, severity, detail, filter) | `web/src/ProblemsList.tsx`, `web/src/RecordView.tsx` | `GetProblems` | `web/src/ProblemsList.test.tsx` | working | |
| Check for errors, plugin-wide | context menu | `CheckForErrors` (`native/src/plugin/RadiclePluginCheck.cpp`) | `native/tests/unit/RadiclePluginMaintenanceTests.cpp` | partial | The list of check codes was not enumerated. How close it is to xEdit is covered in `docs/internal/XEDIT_PARITY_AUDIT_2026-08.md`, which I did not read in full. |
| Check circular leveled lists | context menu | `CheckCircularLeveledLists` | none matched | unverified | |
| Build reachable info, find unreachable | context menu | `FindUnreachable` | `native/tests/unit/RadiclePluginReachableTests.cpp` | working | |
| LOOT cleaning report | context menu | `LootCleaningReport` | none matched | unverified | |
| Clean plugin, update ONAM | context menu | `CleanPlugin`, `UpdateOnam` | `native/tests/unit/RadiclePluginMaintenanceTests.cpp`, `RadiclePluginUndecodableMaintenanceTests.cpp` | unverified | |

### Papyrus and Blueprint

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Papyrus editor with live syntax errors, outline, go to definition | `web/src/PapyrusPanel.tsx` | native `papyrus` target: `Analyze`, `SymbolAt`, `ReadScript`, `WriteScript`; MCP `papyrus_check` | `web/src/PapyrusPanel.test.tsx`, `native/tests/unit/PapyrusParserTests.cpp`, `PapyrusTypeCheckerTests.cpp`, `PapyrusResolverTests.cpp`, `PapyrusSymbolsTests.cpp` | working | Panel tests cover dirty-buffer prompts. Analysis is covered natively. |
| Compile `.psc` to `.pex` | `web/src/PapyrusPanel.tsx` | `Compile`; MCP `compile_papyrus`; built-in engine or CK `PapyrusCompiler.exe` (`engine` argument) | `native/tests/unit/PapyrusCodeGenTests.cpp`, `PapyrusPexTests.cpp`, `PapyrusTargetTests.cpp` | working | The built-in engine still needs the vanilla `.psc` sources on the import path (`docs/internal/INDEX.md`). |
| Decompile `.pex` | `web/src/PapyrusPanel.tsx` | `Decompile`; MCP `decompile_papyrus` | `Radicle/tests/test_papyrus_decompiler.cpp`, `PapyrusDifferentialTests.cpp`, `PapyrusCorpusTests.cpp` | working | |
| Wiki lookup (functions, scripts) | `web/src/PapyrusPanel.tsx` (Wiki Lookup mode) | `LookupFunction`, `LookupScriptInfo`; MCP `papyrus_function_lookup`, `papyrus_script_info` | `native/tests/unit/WikiServiceTests.cpp` | working | Uses an offline CK wiki mirror that ships in the release package (`docs/MODKIT21_PULLS.md`). |
| Blueprint node graph (compiles to Papyrus or `.pex`) | `web/src/Blueprint/BlueprintPanel.tsx`, `BlueprintCanvas.tsx` | native `graph` target: `ValidateGraph`, `CompileToSource`, `CompileToPex`, `LoadGraph`, `SaveGraph`, `LoadScript`, `SearchPalette` | `web/src/Blueprint/BlueprintPanel.test.tsx`, `graphReducer.test.ts`, `layout.test.ts`, `clipboard.test.ts`, `native/tests/unit/GraphCompilerTests.cpp`, `GraphValidatorTests.cpp`, `GraphLiftTests.cpp` | working | Docs: `docs/internal/GRAPH.md`, `docs/internal/GRAPH_F4SE.md`. No MCP tool for the graph appears in `docs/MCP_SETUP.md`. |
| F4SE-aware native functions in the graph | `web/src/Blueprint/` | `native/src/f4se/` | `native/tests/unit/F4SEExtractorTests.cpp`, `F4SEEmitterTests.cpp`, `F4SETypeMapTests.cpp` | unverified | Which F4SE functions reach the palette was not traced. |

### SWF (interface)

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| SWF editor: characters, labels, placements, transforms, edit text, undo and redo, save | `web/src/SwfPanel.tsx` | host object `swf` is implemented in C# `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs` over `SwfDocumentService.cs` | `web/src/SwfPanel.test.tsx` (one test, rename and undo and save), `FO4RecordEditor.Core.Tests/SwfDocumentServiceTests.cs`, `FO4RecordEditor.Tests/SwfInteropTests.cs` | partial | The native host's target list has no `swf` target and no `swf` text anywhere under `PluginEditTool/native`. So the panel depends on the C# host. Whether it works in the native build is unverified. |
| ABC disassembly, AVM1 action tags, shape and bitmap export | `web/src/SwfPanel.tsx` (`AbcBlocks`, `Disassemble`, `Avm1Disassemble`, `ShapeSvg`, `BitmapPng`) | same C# `swf` host | `SwfPanel.test.tsx` does not exercise them | unverified | |
| SWF icon batch injection | none | none | none | planned | `docs/MODKIT21_PULLS.md` item 9 ("low priority"). |

### NIF and materials

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| NIF inspect, tree, verify, geometry | `web/src/NifPanel.tsx`, `web/src/NifViewport.tsx` | native `nif` target; MCP `nif_inspect`, `nif_geometry`, `nif_verify`, `nif_tree` | `native/tests/unit/NifServiceTests.cpp`, `web/src/NifPanelDrop.test.tsx` | working | Reads in-process when niftool is not configured (`docs/MCP_SETUP.md`). |
| NIF edit and save (ApplyEdits), import OBJ, fix | `web/src/NifPanel.tsx`, `web/src/NifEditor.tsx` | `ApplyEdits`, `Import`, `Fix` | `web/src/NifPanelEdits.test.tsx` | partial | Web side is well tested. Native side has no dedicated mutation test I could match. No MCP tool for NIF write or fix appears in `docs/MCP_SETUP.md`. `docs/internal/NIF_TOOLCHAIN.md` was not read, and it covers the niftool. |
| Auto-skinning, weight transfer, dismemberment partitions | none | none | none | planned | `docs/MODKIT21_PULLS.md` item 7 ("Large"). |
| Cloth physics tweak, HKX pack, NIF block copy, texture recolor | none | none | none | planned | `docs/MODKIT21_PULLS.md` items 8, 10, 11, 12. |
| BGSM and BGEM material editor | `web/src/MaterialEditor.tsx`, tab in `web/src/NifPanel.tsx` | native `material` target; MCP `material_inspect`, `material_set_fields` | `native/tests/unit/MaterialServiceTests.cpp`, `MaterialTargetTests.cpp`, `Radicle/tests/test_material_file_real.cpp` | working | Byte round-trip is checked against vanilla materials when `FO4_DATA_DIR` is set (`docs/MCP_SETUP.md`). |
| Texture decode for previews | viewports | `TextureCodec`, `TextureResolver` | `native/tests/unit/TextureCodecTests.cpp`, `TextureResolverTests.cpp` | working | |

### Archives, audio and strings

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Archive browse, filter, extract, compare | `web/src/ArchivePanel.tsx` | native `archive` target: `List`, `ExtractSelected`, `ExtractAll`, `Compare` | `native/tests/unit/RadicleArchiveBackendTests.cpp` | partial | No colocated web test file for the panel. The old MCP list shows `archive_list` and `archive_extract`, but the current `docs/MCP_SETUP.md` lists only `archive_pack` under archives. |
| Archive pack to BA2 | `web/src/ArchivePanel.tsx` | `Pack`; MCP `archive_pack`; needs `Archive2.exe` | none matched | unverified | Needs `Archive2.exe` on the machine. |
| Audio: convert to and from XWM, make and extract FUZ | `web/src/AudioPanel.tsx` | native `audio` target; MCP `audio_convert_to_xwm`, `audio_convert_from_xwm`, `audio_make_fuz`, `audio_extract_fuz` | `native/tests/unit/AudioServiceTests.cpp` | partial | Needs helper programs (`xWMAEncode.exe`, `BmlFuzEncode.exe`, `BmlFuzDecode.exe`). No web test file for the panel. |
| Mod folder catalog | MCP only | `catalog_mod_folder` | none | planned | Deliberately no GUI (`docs/MODKIT21_PULLS.md` item 6). That doc dates from the C# era, so check whether the tool still exists. It is not in the current `docs/MCP_SETUP.md` tool table. |

### MCP and AI chat

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Native stdio MCP server (`fo4ide --mcp`) | none (command line) | `native/src/mcp/McpServer.cpp`, tools in `native/apps/fo4ide/McpTools.cpp` | `native/tests/unit/McpServerTests.cpp`, `native/tests/native_mcp_smoke.py` | working | Write safety rules are in `docs/MCP_SETUP.md`. |
| Live MCP activity feed in the shell | `web/src/MainShell.tsx` (`McpLive` events) | event bus (`native/src/events`) | `native/tests/unit/EventBusTests.cpp` | unverified | |
| AI chat panel | `web/src/ChatPanel.tsx` | none | none | partial | It is not a chat. It shows the text `fo4ide --mcp`, a copy button and a prompt box fed by the `fo4:ask-ai` event. The panel says embedded provider chat is not part of the native host. |
| MCP tool set coverage | | `docs/MCP_SETUP.md` | `McpServerTests.cpp` | partial | The MCP list has records, typed records, masters, Papyrus, NIF read, materials, audio and archive pack. It has no conflict, copy, conditions or graph tools. |

### Settings

| Feature | Where in UI (file) | Backend | Tests | Status | Notes |
|---|---|---|---|---|---|
| Settings modal: folders, tool paths, flags | `web/src/SettingsModal.tsx` | native `settings` target; MCP `get_settings` (secrets redacted) | `web/src/SettingsModal.test.tsx`, `native/tests/unit/SettingsTests.cpp`, `SettingsTargetSecurityTests.cpp` | working | |
| AI provider, model and API keys in Settings | `web/src/SettingsModal.tsx` (Anthropic, Gemini, `claudecode`) | `TestClaude` in the web bridge | `web/src/SettingsModal.test.tsx` (key save payload) | unverified | The fields exist, but `ChatPanel.tsx` says provider chat is not in the native host. What the saved keys do in the native build was not traced. |
| Help, status bar, top bar, tab shell | `web/src/Shell/HelpModal.tsx`, `StatusBar.tsx`, `TopBar.tsx` | none | `web/src/Shell/StatusBar.test.tsx` | working | |

## Known gaps

Taken from code and docs only.

1. SWF has no native target. `web/src/SwfPanel.tsx` uses a `swf` host object, and no `swf` code exists under `PluginEditTool/native`. The only host is C#, in `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs`.
2. Two UI strings may be out of date. The Localization tooltip says "read-only" and the Cell Viewer tooltip says "read-only 3D" (`web/src/MainShell.tsx`), but both panels have write calls.
3. The AI chat panel is only MCP instructions. Settings still shows AI provider and API key fields.
4. Raw View (hex or subrecord dump) was not built (`docs/internal/UI_REDESIGN_TASKS.md`).
5. Copy as wrapper covers only LVLI, LVLN and LVSP. xEdit also covers LVLB, LVLC, LVLP and LVSC (`docs/xedit-copy-workflows.md`).
6. Inject Forms into master does not repoint references in other plugins. They dangle until patched (`docs/xedit-copy-workflows.md`).
7. The MCP tool list has no conflict, copy workflow, conditions or Blueprint tools (`docs/MCP_SETUP.md`).
8. Open modkit21 candidates are not built: auto-skinning, cloth physics, texture recolor, HKX pack, SWF icon injection, NIF validate backport (`docs/MODKIT21_PULLS.md`, items 7 to 12).
9. `docs/MODKIT21_PULLS.md` still describes C# and Mutagen services, so it is background only. `docs/internal/INDEX.md` marks `KNOWLEDGE.md` and `MUTAGEN.md` as older C# background.
10. `docs/internal/UI_REDESIGN_TASKS.md` is listed as a "Live backlog" in `docs/internal/INDEX.md`, but the file's own header says all six phases are implemented. The two documents disagree.
11. Archives, audio and cell panels have no colocated `*.test.tsx` file in `web/src`.
12. Native tests were matched by file name only. Many context-menu actions have no test file matched by name (see "unverified" rows).

## Where to read more

- `PluginEditTool/docs/MCP_SETUP.md`: MCP tools and write safety.
- `PluginEditTool/docs/internal/ARCHITECTURE.md`: native host and the React contract.
- `PluginEditTool/docs/xedit-copy-workflows.md`: copy workflow status.
- `PluginEditTool/docs/internal/XEDIT_PARITY_AUDIT_2026-08.md`: xEdit parity audit (not read in full for this page).
