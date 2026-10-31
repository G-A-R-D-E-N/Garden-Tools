# #174C pending-lifecycle audit (base `e9a24478`)

Classification of plugin-associated mutation and persistence paths after #174B.
No behavior changes are implied by this matrix; it is the pre-change inventory.

## Matrix

| Path | Entry | Classification | Notes |
| --- | --- | --- | --- |
| Ordinary record edits (`SetField`, list/FormLink/component/condition/override/quest-non-localized, ...) | `stagePluginMutationLocked` | in-memory only | Sets `LoadedPlugin.dirty`; discard later re-reads the plugin from disk (`RadiclePluginBackend.cpp:discardPlugin`). Disk untouched. |
| Localized QUST CNAM with table change | `RadiclePluginQuest.cpp:setQuestStages` | immediate disk write + transaction/dependency-first | Calls `write_file_pair_dependency_first(DLSTRINGS, plugin)`. Bypasses deferred model. Response: `Written to disk.` |
| Localized QUST CNAM with unchanged text / no table change | `setQuestStages` | in-memory only | `localizedTableChanged == false` then `stagePluginMutationLocked`. |
| Localized QUST while ordinary pending on localized plugin | `invokeBackend("SetQuestStages")` guard | destructive reject (no write) | #174B fail-closed: `PluginHasPendingChanges`. Leaves disk alone; does not stage sidecars. |
| STRINGS / ILSTRINGS allocation participants | freshness re-read in `setQuestStages` | read + fail-closed | Not mutated by CNAM path; change on disk aborts before write. |
| `SavePlugin` / MCP `save_plugin` (`SavePatch`) | `RadiclePluginBackend::savePlugin` | explicit save | Writes plugin bytes only via `write_plugin_file`. Clears `dirty`. No staged-sidecar flush (none exists yet). |
| `masters.SavePlugin` | `saveMastersPlugin` | explicit save | Same as SavePlugin for light-flag path; clears dirty. |
| `CreatePlugin` | `createPlugin` | immediate disk write | Creates empty ESP on disk, loads clean (`dirty=false`). |
| `discardCreatedPlugin` | create-target rollback | destructive discard (disk delete) | Only empty auto-created overwrite plugins. Not a general pending discard. |
| Same-path `OpenPlugin` while dirty | `openPlugin` | fail-closed | `PluginHasPendingChanges`; message mentions discard, but no general discard API exists. |
| Same-path `OpenPlugin` while clean | `openPlugin` | destructive reload | Replaces in-memory plugin from disk. |
| `environment.LoadMo2` / MCP `reload_mo2_profile` | `PluginEditTool/native/src/plugin/RadiclePluginBackend.cpp:loadEnvironment` | fail-closed reload; explicit discard | Replaces entire `plugins_` from disk. Default refuses `PluginHasPendingChanges`. `discardPending` / MCP `discard_pending=true` is the explicit destructive path. |
| Profile open (`OpenMo2Profile` then LoadMo2) | `PluginEditTool/native/src/app/AppInteropTarget.cpp:loadProfile` | fail-closed reload; explicit discard | Same policy; UI confirms before `discardPending`. |
| Per-plugin pending status for UI/MCP | `getActivePlugins` / `GetPlugins` | missing | `dirty` is internal only; not exposed. |
| Explicit per-plugin discard of pending edits | none | missing | No `DiscardPlugin`. Only profile reload or created-plugin delete. |
| Close/unload single plugin | none | absent | No dedicated close API; unload only via full environment reload. |

## Proven #174C gaps (from call-path audit)

1. Localized DLSTRINGS + plugin still commit immediately when CNAM text changes.
2. No explicit per-plugin discard that restores the plugin by re-reading it from disk (`RadiclePluginBackend.cpp:discardPlugin`) without writing disk.
3. Pending state is not exposed to UI/MCP consumers.
4. Localized authoring cannot participate in deferred pending state alongside ordinary edits; it is rejected instead of staged.
5. `SavePlugin` cannot flush staged sidecars because staging does not exist yet.

## Out of scope (unless a concrete #174C proof requires it)

- MCP singleton/daemon process sharing
- CodeAgentSwarm orchestration
- FO4IDE UI redesign
- New record editing features
- Unrelated helper-tool changes

## Post-#274 blocker fix

Repeated localized CNAM edits before `SavePlugin` must consume `stagedLocalization.dlstringsBytes` as the working DLSTRINGS table. Disk-only reloads miss newly allocated pending IDs and fail with `LocalizedLogTableInvalid`. Freshness baselines remain the original on-disk sibling bytes captured at first stage.

## Post-#274 MO2 reload policy

`PluginEditTool/native/src/plugin/RadiclePluginBackend.cpp:loadEnvironment` refuses to replace `plugins_` while any loaded plugin is dirty unless `discardPending` is true. `PluginEditTool/native/src/app/AppInteropTarget.cpp:loadProfile` checks `backend.GetActivePlugins` first so a blocked reload does not rewrite settings or trusted save roots. MCP `PluginEditTool/native/apps/fo4ide/McpTools.cpp:registerNativeMcpTools` `reload_mo2_profile` defaults to that fail-closed path; `discard_pending=true` is the explicit destructive reload. Desktop `PluginEditTool/web/src/MainShell.tsx:handleLoadEnv` / `handleLoadMo2` confirm before retrying with discard.
