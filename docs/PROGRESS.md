# FO4RecordEditor — AI Plugin IDE: Build Progress

Status as of 2026-06-09. Branch: `feat/ai-plugin-ide` (local only, not pushed).
Plan: [docs/superpowers/plans/2026-06-09-ai-plugin-ide.md](superpowers/plans/2026-06-09-ai-plugin-ide.md)

**Overall: 6 of 7 milestones implemented. 19/19 tests passing. App builds clean (0 errors) and launches without crashing.**

---

## DONE

### Setup
- Git repo initialized; baseline commit of existing app; feature branch `feat/ai-plugin-ide`.
- `.gitignore` for bin/obj/.vs.

### M0 — Test Harness + Logging + Settings  ✅ reviewed (spec + quality, approved)
- `FO4RecordEditor.Tests` xUnit project (net9.0-windows, FluentAssertions) + `GraphTestBuilder` helper.
- `LogService` — categorized logging (App/Plugin/AI/Error/Compile), severity levels, filter, export.
- `SettingsService` + `AppSettings` — persists to `%APPDATA%\FO4RecordEditor\settings.json`.

### M1 — Knowledge Graph  ✅ reviewed (spec + quality, approved)
- Inverted index: by FormKey, EditorID, Type; inbound + outbound references; override conflicts.
- `AnalyzeImpact` ("what breaks if I delete this?"), `GetNeighborhood` (one-hop for AI context).
- Async background indexing (`IndexAsync`).
- Fix applied: `IsWinningOverride` corrected (was inverted).

### M2 — Error Scanner  ✅ reviewed (spec + quality, approved)
- Detects: null/empty ActorValue (Critical), broken FormKey refs not vanilla/loaded (Error), invalid leveled-list entries (Warning).
- `ScanAsync` + `CountBySeverity`.
- Perf: added O(1) `_outbound` index to KnowledgeGraph (avoids quadratic per-record scan at 50k records).

### M3 — Diff Engine  ✅ reviewed (spec + quality, approved)
- Path-based record-vs-record comparison: Added / Removed / Modified / Unchanged, `changesOnly` filter, summary counts.

### M4 — AI Layer  ✅ reviewed (spec + quality, approved)
- `IAIProvider` abstraction.
- `AnthropicProvider` — streaming Messages API (SSE), model `claude-sonnet-4-6`.
- `OllamaProvider` — local NDJSON streaming.
- `AIContextBuilder` — grounds prompts in graph data (record fields + one-hop neighborhood + inbound-ref warnings).
- `ChatService` — conversation history + streaming token callback.
- `AiProviderFactory` — builds provider from settings.
- Fixes applied: chat role-alternation preserved on stream throw/cancel (with test), preamble dedupe, field-dump cap.

### M5 — Undo/Redo + Commands  ✅ verified (controller review, approved)
- `IChange` / `FieldEditChange` / `UndoStack` (with redo invalidation).
- `CommandRegistry` + `AppCommand` (fuzzy subsequence search for the command palette).

### M6 — WPF Shell  ✅ implemented, reviewed, and approved
- `ShellViewModel` (central state owning all services), `RecordTabViewModel`.
- `MainWindow` — activity bar (Explorer/Search/Errors/Compare) + side panel + center tabs + AI panel + bottom tabs (Log/Errors/Compare) + status bar + command palette overlay.
- Views: `ExplorerPanel` (tree + search + right-click AI menu), `RecordEditorView` (property grid), `ChatPanel` (streaming AI chat), `AiActions` (Explain/Fix/Patch/Compare/Impact), `ErrorsPanel`, `ComparePanel`, `LogPanel`, `CommandPalette`, `SettingsDialog`.
- Shortcuts: Ctrl+Shift+P (palette), Ctrl+Z/Y (undo/redo).
- Integration fixes applied: BottomRow height in code; AI actions actually submit to chat (`ChatPanel.SubmitExternal`); `EnableCollectionSynchronization` on Log.Entries + Errors for cross-thread safety.
- Code-quality fixes applied: async-void crash in ChatPanel handled, ShellViewModel re-entrancy guarded, Dispatcher marshaled asynchronously, RecordEditorView TextBox now pushes changes to Undo stack, ExplorerPanel cached to prevent memory leaks, and weak event manager used in FieldRow.
- Build: 0 errors. App launches cleanly. 19/19 tests still pass.

### M7 — UI Parity, Loader Fixes, and AI Polishing  ✅ implemented
- **UI FO4Edit Parity**: Overhauled the `RecordEditorView` DataGrid to use a light theme with dynamic column highlighting (Light Yellow for base plugins, Light Green for winning overrides). Added summary fields for array types so nested collections are readable without expanding them.
- **Loader Fixes**: Improved loose plugin loading by skipping the heavy indexing phase (lazy loading). Fixed the zero-records reporting bug by extracting the true count directly from the Mutagen plugin header. Fixed an issue where sub-records of loose plugins would fail to populate by replacing dynamic `RecordSelection` with strongly typed `EnumerateMajorRecords` iteration.
- **AI Local History**: Implemented `ChatHistoryService` and `ChatSession` models to persist conversations across application restarts. Added a top bar UI to `ChatPanel` with a dropdown to select past threads and a `+ New` button.
- **AI Context Enhancements**: Fixed an issue where the AI would receive `_HasData: False` for lazy-loaded records. The system now intercepts the query, synchronously calls `MutagenLoader.PopulateNode`, and injects the full data block into the AI context without requiring user interaction.
- **AI UI Enhancements**: Corrected the `Markdig.Wpf` dark-theme styling. Injected custom resource overrides for `Styles.CodeStyleKey` (inline) and `Styles.CodeBlockStyleKey` (multiline) so code blocks sent by the AI feature proper contrast (dark grey background, light blue/grey text).
- **Settings Fixes**: Updated the default Anthropic API model to the valid `claude-3-7-sonnet-20250219`.

### M8 — Power-User Editing UX  ✅ implemented (2026-06-23)
- **WriteService.RenumberFormId** + the `renumber_formid` AI tool: xEdit-style "Change FormID" within a plugin, repointing every in-plugin reference. References in OTHER plugins are not rewritten (the result message and tool description point the user/AI to Referenced By).
- **BackendInterop**: exposed `CompactToEsl`, `CleanPlugin`, and `RenumberFormId` to the WebView2 host bridge, plus a `ConflictEntry` TS type for the conflicts panel.
- **RecordView**: conflict-status filter (Winning / Overridden / Override / Identical); context-menu actions Change FormID, Compact to ESL, and Clean (UDR) — the two destructive plugin-wide ops (Compact, Clean) are gated behind `window.confirm` guards. Referenced-By rows are now clickable to jump-navigate to the referencing record, and an "Ask AI: what breaks?" action fires an impact prompt into the chat panel.
- **MainShell**: Conflicts panel with multi-select batch copy-as-override.
- **ChatPanel**: `fo4:ask-ai` external-prompt bridge — a `CustomEvent` dispatched from anywhere in the UI is received by the chat panel and submitted as a prompt.
- **Polish**: removed em-dashes from feature-authored strings; updated the tool-count assertion to match the current 31 tools.
- **Tests**: 84 total (83 passing, 1 integration test skipped), including the 3 `RenumberFormId*` tests.
- **Known follow-up**: the batch copy-as-override success count is inferred from the human-readable result string — a structured `CopyAsOverride` return would be more robust. A manual in-game smoke test is still pending (requires a real modlist + Anthropic API key).

---

## STILL REQUIRED

### Immediate (finish M6 + close out the plan)
1. **M6 code-quality review** — ✅ DONE. All findings addressed.
2. **Address any M6 review findings**, then commit. — ✅ DONE.
3. **Final full-implementation code review** (whole branch).
4. **Finish the branch** — decide merge to `main` / PR / keep as branch (superpowers:finishing-a-development-branch).

### Manual verification (not automatable)
5. **In-game / real-data smoke test** (requires a real plugin + Anthropic API key):
   - Open a real ESP (e.g. `FallenWorldCrafting_S7.esp`), confirm tree loads and a record opens in a center tab.
   - Run Scan → confirm NullActorValue rows populate the Errors tab.
   - Set API key in Settings → ask the AI "why are these recipes blocked?" → confirm a streaming, grounded answer.
   - Right-click a record → AI Actions → confirm the prompt reaches the chat.

### Minor deferred items (from reviews — non-blocking polish)
- `HttpClient` lifecycle: make it `static readonly` shared per provider (currently one-per-instance, leaked on settings change — immaterial at current usage).
- Anthropic raw error body: truncate before surfacing to chat.
- Streaming loop: replace `reader.EndOfStream` with `while ((line = await ReadLineAsync()) != null)` (fully-async idiom).
- `OllamaProvider`: check `IsSuccessStatusCode` for a clearer message on 404 (model not pulled).
- Optional: `ChatService` test for the success path is covered; add inbound-ref-warning + neighborhood-cap tests for AIContextBuilder.

### Future roadmap (NOT in the core plan — separate work)
- AI write-back actions (model emits structured edit list → one-click apply via UndoStack).
- Dedicated Patch Generator tab (source plugins + desired behavior → conflict resolution → compile via CompileService).
- Leveled-list / keyword / FormList merge automation on top of `GetConflicts()`.
- Persistent index (LiteDB) for 200k+ record load orders + cross-session cache.
- Missing model/texture scanners (resolve mesh/texture paths against data folder + BA2).
- Circular-reference + invalid-script detectors.
- Favorites + Recently Viewed in Explorer.
- Layout persistence (splitter sizes → AppSettings).

---

## Test Summary
- **19 tests, all passing.** Coverage: LogService (2), KnowledgeGraph (6), ErrorScanner (4), DiffEngine (2), AIContextBuilder (1), ChatService (2), UndoStack (2), CommandRegistry (1). UI is not unit-tested (verified via clean launch).

## Key files
- Engines (no WPF dependency, fully tested): `Services/Graph/KnowledgeGraph.cs`, `Services/Errors/ErrorScanner.cs`, `Services/Diff/DiffEngine.cs`, `Services/Logging/LogService.cs`, `Services/Undo/`, `Services/Commands/`.
- AI: `Services/Ai/{IAIProvider,AnthropicProvider,OllamaProvider,AIContextBuilder,ChatService,AiProviderFactory}.cs`.
- UI: `MainWindow.xaml(.cs)`, `ViewModels/`, `Views/`.
- Preserved from original app: `Models/RecordNode.cs`, `Services/{MutagenLoader,SpriggitLoader,CompileService,ScriptEngine}.cs`, `ViewModels/FieldRow.cs`.
