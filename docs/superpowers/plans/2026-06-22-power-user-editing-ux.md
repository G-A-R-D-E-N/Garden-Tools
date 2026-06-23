# Power-User Editing UX Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the xEdit power-user *editing* parity gap in FO4RecordEditor — FormID renumber/change, ESL flag/compact, UDR clean, conflict-status filtering, clickable references, and multi-select batch copy-as-override — and make every one of those operations AI-explainable/AI-drivable.

**Architecture:** The C# backend (`WriteService`, `MutagenLoader`, `KnowledgeGraph`) already implements most of the heavy lifting; the React/WebView2 frontend exposes only a slice of it. This plan (1) adds the one genuinely missing backend operation (`RenumberFormId`), (2) registers it as an AI tool, (3) surfaces the existing-but-hidden operations (`CompactToEsl`, `CleanPlugin`, `RenumberFormId`) through `BackendInterop` → `backend.ts`, and (4) builds the React UI: record context-menu actions, a conflict-status filter, clickable Referenced-By navigation, a multi-select Conflicts panel, and an "Ask AI: what breaks?" action that bridges into the chat. Philosophy: **AI-augmented parity** — manual buttons do the xEdit operation; the agent can do the same via tools and explain impact.

**Tech Stack:** C# (.NET 9, Mutagen.Bethesda.Fallout4), xUnit + FluentAssertions, React 19 + TypeScript + Vite, WebView2 host-object IPC.

---

## Repository Facts (read before starting)

- **Git root / tool repo:** `E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor` (this folder has its own `.git`; commits here do NOT touch the workspace repo). All `git add` paths below are relative to this root.
- **Backend project:** `FO4RecordEditor/` (csproj `FO4RecordEditor.csproj`, net9.0-windows).
- **Test project:** `FO4RecordEditor.Tests/` (xUnit, FluentAssertions, `[assembly: CollectionBehavior(DisableTestParallelization=true)]` — tests run serially because `WriteService`/`MutagenLoader` use process-global registries).
- **React frontend:** `web/` (Vite). Build: `npm run build` (outputs `web/dist`, copied into the C# build output by the csproj).
- **IPC pattern:** React calls `window.chrome.webview.hostObjects.backend.<Method>()` which maps to `FO4RecordEditor/Services/BackendInterop.cs`. Every `BackendInterop` method wraps its body in `DebugLog.Guard(name, () => ...)` (sync) or `DebugLog.GuardAsync` (async) and returns a `string` (JSON or a human message).
- **AI tools:** `FO4RecordEditor/Services/Ai/PluginToolExecutor.cs` holds a `ToolSpec[] _specs` array (schemas) and a `switch (toolName)` dispatch. Helper accessors in scope inside the switch: `Str("key")`, `Int("key", default)`, `Bool("key")`, and the parsed `root` (`JsonElement`) + `env`. Write tools are also listed in two places: an `IsWrite` set (~line 524) and a summary `switch` (~line 546).
- **Existing `WriteService` methods you will reuse (do NOT reimplement):** `CompactToEsl(plugin, env)`, `CleanPlugin(plugin, env)`, `CopyAsOverride(env, sourcePlugin, id, patchPlugin)`, `DeleteRecord`, `SavePlugin`, `GetMutable`, `CreatePlugin`, `CreateRecord`, and the private `EnsureOpen` / `FindMutableRecord` helpers. The mutable mod supports `mod.RemapLinks(Dictionary<FormKey,FormKey>)` and group `DuplicateInAsNewUntypedRecord(IMajorRecord, FormKey)` + `Remove(FormKey)` (see `CompactToEsl` for the exact reflection pattern over `IGroup` properties).
- **Test harness pattern (mirror `FO4RecordEditor.Tests/WriteServiceTests.cs`):** create a uniquely-named plugin with `WriteService.CreatePlugin($"X_{Guid.NewGuid():N}.esp")`, build records directly via Mutagen types and `WriteService.GetMutable(plugin)`, drive tools via `new PluginToolExecutor(() => null).Execute("tool", "{json}")`, then save to `Path.GetTempPath()` and reload with `Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(outPath), Fallout4Release.Fallout4)` to assert on real bytes.

### Run/verify commands (used throughout)

```powershell
# Run one test (fast inner loop)
dotnet test "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor.Tests\FO4RecordEditor.Tests.csproj" --filter "FullyQualifiedName~RenumberFormId"

# Run the whole backend test suite
dotnet test "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor.Tests\FO4RecordEditor.Tests.csproj"

# Type-check + build the React frontend
cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\web"; npm run build

# Build the desktop app
dotnet build "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor\FO4RecordEditor.csproj"
```

---

## File Structure

**Created:**
- (none — all changes extend existing files)

**Modified — backend:**
- `FO4RecordEditor/Services/WriteService.cs` — add `RenumberFormId(...)`.
- `FO4RecordEditor/Services/Ai/PluginToolExecutor.cs` — add `renumber_formid` ToolSpec + dispatch + write-set/summary entries.
- `FO4RecordEditor/Services/BackendInterop.cs` — expose `CompactToEsl`, `CleanPlugin`, `RenumberFormId`.

**Modified — frontend:**
- `web/src/backend.ts` — add the three new bridge method signatures + a `ConflictEntry` type.
- `web/src/RecordView.tsx` — conflict-status filter, record/plugin context actions (Renumber/Compact/Clean), clickable Referenced-By, "Ask AI: what breaks?".
- `web/src/MainShell.tsx` — Conflicts sidebar panel (multi-select batch copy-as-override), open-by-FormKey helper passed to `RecordView`.
- `web/src/ChatPanel.tsx` — listen for the `fo4:ask-ai` CustomEvent and send the prompt into the active chat.
- `web/src/RecordView.css` / `web/src/MainShell.css` — styles for the new controls (append-only).

**Modified — tests:**
- `FO4RecordEditor.Tests/WriteServiceTests.cs` — `RenumberFormId` characterization tests.

---

## Task 1: Backend — `WriteService.RenumberFormId`

Renumber a single record's FormID within its plugin and repoint all in-plugin references. External (cross-plugin) references are intentionally NOT rewritten (that is the AI-augmented follow-up, Task 8); the result message says so.

**Files:**
- Modify: `FO4RecordEditor/Services/WriteService.cs` (add method near `CompactToEsl`, around line 307)
- Test: `FO4RecordEditor.Tests/WriteServiceTests.cs`

- [ ] **Step 1: Write the failing test**

Append to `FO4RecordEditor.Tests/WriteServiceTests.cs` (before the closing brace):

```csharp
    [Fact]
    public void RenumberFormId_ChangesId_AndFixesInPluginReferences()
    {
        var plugin = $"RenumTest_{Guid.NewGuid():N}.esp";
        WriteService.CreatePlugin(plugin);
        var mod = WriteService.GetMutable(plugin)!;
        var rel = Mutagen.Bethesda.Fallout4.Fallout4Release.Fallout4;

        // A keyword at 0x000801 and a weapon that links it.
        var kw = new Mutagen.Bethesda.Fallout4.Keyword(
            new Mutagen.Bethesda.Plugins.FormKey(mod.ModKey, 0x801), rel) { EditorID = "Renum_Kw" };
        mod.Keywords.Add(kw);
        var gun = new Mutagen.Bethesda.Fallout4.Weapon(
            new Mutagen.Bethesda.Plugins.FormKey(mod.ModKey, 0x802), rel) { EditorID = "Renum_Gun" };
        gun.Keywords = new() {
            new Mutagen.Bethesda.Plugins.FormLink<Mutagen.Bethesda.Fallout4.IKeywordGetter>(kw.FormKey) };
        mod.Weapons.Add(gun);

        // Renumber the keyword 0x801 -> 0x0F0F.
        WriteService.RenumberFormId(plugin, "Renum_Kw", "000F0F", null)
            .Should().Contain("Renumbered").And.Contain("000F0F");

        // Save + reload, assert the new id AND that the weapon's link followed.
        var outPath = System.IO.Path.Combine(System.IO.Path.GetTempPath(), plugin);
        WriteService.SavePlugin(plugin, outPath);
        using var reload = Mutagen.Bethesda.Fallout4.Fallout4Mod.CreateFromBinaryOverlay(
            Mutagen.Bethesda.Plugins.ModPath.FromPath(outPath), rel);
        var rkw = reload.Keywords.First(k => k.EditorID == "Renum_Kw");
        var rgun = reload.Weapons.First(w => w.EditorID == "Renum_Gun");
        rkw.FormKey.ID.Should().Be(0x0F0Fu);
        rgun.Keywords!.Select(k => k.FormKey).Should().Contain(rkw.FormKey);

        try { System.IO.File.Delete(outPath); } catch { }
    }

    [Fact]
    public void RenumberFormId_TargetIdInUse_Rejected()
    {
        var plugin = $"RenumDup_{Guid.NewGuid():N}.esp";
        WriteService.CreatePlugin(plugin);
        var mod = WriteService.GetMutable(plugin)!;
        var rel = Mutagen.Bethesda.Fallout4.Fallout4Release.Fallout4;
        mod.Keywords.Add(new Mutagen.Bethesda.Fallout4.Keyword(
            new Mutagen.Bethesda.Plugins.FormKey(mod.ModKey, 0x801), rel) { EditorID = "Dup_A" });
        mod.Keywords.Add(new Mutagen.Bethesda.Fallout4.Keyword(
            new Mutagen.Bethesda.Plugins.FormKey(mod.ModKey, 0x802), rel) { EditorID = "Dup_B" });

        WriteService.RenumberFormId(plugin, "Dup_A", "000802", null)
            .Should().Contain("already used");
    }
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `dotnet test "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor.Tests\FO4RecordEditor.Tests.csproj" --filter "FullyQualifiedName~RenumberFormId"`
Expected: FAIL — compile error `'WriteService' does not contain a definition for 'RenumberFormId'`.

- [ ] **Step 3: Implement `RenumberFormId`**

In `FO4RecordEditor/Services/WriteService.cs`, add this method immediately after `CompactToEsl` (after its closing brace, ~line 307). It mirrors `CompactToEsl`'s group-rekey + `RemapLinks` pattern but for one record and an explicit target id:

```csharp
    /// <summary>
    /// Change one record's FormID within its plugin (xEdit "Change FormID") and repoint every
    /// reference to it that lives in the SAME plugin. References in OTHER plugins are not rewritten
    /// (that needs a patch); the message flags how many exist so the caller / AI can fix them.
    /// In memory until save_plugin. newIdHex is the 6-digit object id, e.g. "000F99" or "0xF99".
    /// </summary>
    public static string RenumberFormId(string plugin, string recordId, string newIdHex, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;
        var rec = FindMutableRecord(mod, recordId);
        if (rec == null) return $"Record '{recordId}' not found in {plugin}.";

        var hex = (newIdHex ?? "").Trim();
        if (hex.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) hex = hex[2..];
        if (!uint.TryParse(hex, System.Globalization.NumberStyles.HexNumber, Inv, out var newId))
            return $"'{newIdHex}' is not a valid hex FormID (use the 6-digit object id, e.g. '000F99').";
        if (newId == 0) return "FormID 000000 is the null id and can't be assigned.";

        var oldFk = rec.FormKey;
        var newFk = new FormKey(mod.ModKey, newId);
        if (newFk == oldFk) return "The new FormID is the same as the current one.";
        if (mod.EnumerateMajorRecords().Any(r => r.FormKey == newFk))
            return $"FormID {newId:X6} is already used by another record in {plugin}. Pick a free id.";

        // Re-key: duplicate the record into its group under the new FormKey, then drop the old one.
        bool rekeyed = false;
        foreach (var groupProp in mod.GetType().GetProperties()
                     .Where(p => typeof(Mutagen.Bethesda.Plugins.Records.IGroup).IsAssignableFrom(p.PropertyType)))
        {
            if (groupProp.GetValue(mod) is not Mutagen.Bethesda.Plugins.Records.IGroup group) continue;
            var match = ((System.Collections.IEnumerable)group)
                .Cast<Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter>()
                .FirstOrDefault(r => r.FormKey == oldFk);
            if (match == null) continue;
            group.DuplicateInAsNewUntypedRecord((Mutagen.Bethesda.Plugins.Records.IMajorRecord)match, newFk);
            group.GetType().GetMethod("Remove", new[] { typeof(FormKey) })?.Invoke(group, new object[] { oldFk });
            rekeyed = true;
            break;
        }
        if (!rekeyed) return $"Could not locate the group holding {oldFk} in {plugin}.";

        // Repoint every in-plugin reference to the new id.
        mod.RemapLinks(new Dictionary<FormKey, FormKey> { [oldFk] = newFk });
        MutagenLoader.InvalidateModIndex(plugin);
        NotifyChanged(plugin);

        return $"Renumbered {rec.EditorID} from {oldFk} to {newFk} in {plugin} and fixed in-plugin references. " +
               $"NOTE: references to {oldFk} in OTHER plugins are NOT updated — check 'Referenced By' (or ask the " +
               $"AI to find and patch them). save_plugin to persist.";
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `dotnet test "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor.Tests\FO4RecordEditor.Tests.csproj" --filter "FullyQualifiedName~RenumberFormId"`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add FO4RecordEditor/Services/WriteService.cs FO4RecordEditor.Tests/WriteServiceTests.cs
git commit -m "feat(write): add RenumberFormId — change a record's FormID + fix in-plugin refs"
```

---

## Task 2: AI tool — register `renumber_formid`

Make the agent able to perform the renumber (AI-augmented parity). Reuses the Task 1 service method.

**Files:**
- Modify: `FO4RecordEditor/Services/Ai/PluginToolExecutor.cs` (3 edits: ToolSpec, dispatch case, write-set + summary)
- Test: `FO4RecordEditor.Tests/WriteServiceTests.cs`

- [ ] **Step 1: Write the failing test**

Append to `FO4RecordEditor.Tests/WriteServiceTests.cs`:

```csharp
    [Fact]
    public void RenumberFormIdTool_ChangesId()
    {
        var plugin = $"RenumTool_{Guid.NewGuid():N}.esp";
        var exec = new PluginToolExecutor(() => null);
        exec.Execute("create_plugin", $"{{\"name\":\"{plugin}\"}}");
        exec.Execute("create_record", $"{{\"plugin\":\"{plugin}\",\"type\":\"MISC\",\"editorId\":\"Renum_Widget\"}}");

        exec.Execute("renumber_formid",
            $"{{\"plugin\":\"{plugin}\",\"record\":\"Renum_Widget\",\"new_id\":\"000ABC\"}}")
            .Should().Contain("Renumbered").And.Contain("000ABC");

        exec.Execute("get_record", $"{{\"plugin\":\"{plugin}\",\"id\":\"Renum_Widget\"}}")
            .Should().Contain("ABC:");   // FormKey line now shows the new object id
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `dotnet test "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor.Tests\FO4RecordEditor.Tests.csproj" --filter "FullyQualifiedName~RenumberFormIdTool"`
Expected: FAIL — the tool returns the unknown-tool default message (no `"Renumbered"`).

- [ ] **Step 3a: Add the ToolSpec**

In `FO4RecordEditor/Services/Ai/PluginToolExecutor.cs`, add this entry to the `_specs` array immediately after the `compact_to_esl` spec (after its closing `}),`, ~line 270):

```csharp
        new("renumber_formid",
            "Change one record's FormID within its plugin (xEdit 'Change FormID') and repoint every " +
            "reference to it in the SAME plugin. References in OTHER plugins are NOT rewritten — call " +
            "get_referenced_by first and patch external referrers if any. In memory until save_plugin.",
            new
            {
                type = "object",
                properties = new
                {
                    plugin = new { type = "string" },
                    record = new { type = "string", description = "EditorID or current FormKey of the record." },
                    new_id = new { type = "string", description = "New 6-digit hex object id, e.g. '000F99'." }
                },
                required = new[] { "plugin", "record", "new_id" }
            }),
```

- [ ] **Step 3b: Add the dispatch case**

In the `switch (toolName)` block, add after the `compact_to_esl` case (~line 630):

```csharp
            case "renumber_formid":
                return WriteService.RenumberFormId(Str("plugin"), Str("record"), Str("new_id"), env);
```

- [ ] **Step 3c: Mark it a write tool + add a summary**

In the `IsWrite`-style membership check (~line 524, the `"create_record" or "delete_record" or ...` chain), add `renumber_formid`:

```csharp
            "create_record" or "delete_record" or "copy_as_override" or "clean_plugin" or
            "compact_to_esl" or "renumber_formid" or "reload_plugin" or "strip_masters_clean" or
```

And in the summary `switch` (~line 546), add a case beside `compact_to_esl`:

```csharp
            "renumber_formid"     => $"renumber a record in {plugin}",
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `dotnet test "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor.Tests\FO4RecordEditor.Tests.csproj" --filter "FullyQualifiedName~RenumberFormIdTool"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add FO4RecordEditor/Services/Ai/PluginToolExecutor.cs FO4RecordEditor.Tests/WriteServiceTests.cs
git commit -m "feat(ai): register renumber_formid tool"
```

---

## Task 3: Backend bridge — expose Compact/Clean/Renumber to React

`CompactToEsl` and `CleanPlugin` already exist in `WriteService` but are not reachable from the React UI. Add three `BackendInterop` methods (sync, `DebugLog.Guard`-wrapped, matching the file's style) and their `backend.ts` signatures.

**Files:**
- Modify: `FO4RecordEditor/Services/BackendInterop.cs`
- Modify: `web/src/backend.ts`

- [ ] **Step 1: Add the BackendInterop methods**

In `FO4RecordEditor/Services/BackendInterop.cs`, add after `RevertOverrides` (after line 127, before the `// ---- save ----` comment):

```csharp
    public string CompactToEsl(string plugin) =>
        DebugLog.Guard(nameof(CompactToEsl), () => WriteService.CompactToEsl(plugin, Env), plugin);
    public string CleanPlugin(string plugin) =>
        DebugLog.Guard(nameof(CleanPlugin), () => WriteService.CleanPlugin(plugin, Env), plugin);
    public string RenumberFormId(string plugin, string record, string newId) =>
        DebugLog.Guard(nameof(RenumberFormId), () => WriteService.RenumberFormId(plugin, record, newId, Env),
            $"{plugin} {record}->{newId}");
```

- [ ] **Step 2: Add the `backend.ts` signatures**

In `web/src/backend.ts`, inside the `Backend` interface in the `// ---- edit ----` group (after the `RevertOverrides` line, ~line 26), add:

```typescript
  CompactToEsl(plugin: string): Promise<string>;
  CleanPlugin(plugin: string): Promise<string>;
  RenumberFormId(plugin: string, record: string, newId: string): Promise<string>;
```

Also add a `ConflictEntry` type (used by Task 7) at the end of the type-export section, after the `ConflictMatrix` interface (~line 71):

```typescript
/** One conflicting record across the load order (matches Models/ConflictEntry). */
export interface ConflictEntry {
  FormKey: string;
  EditorID: string;
  Type: string;
  Plugins: string[];
  Winner: string;
  InvolvesMod: boolean;
}
```

- [ ] **Step 3: Build both sides to verify they compile**

Run: `dotnet build "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor\FO4RecordEditor.csproj"`
Expected: Build succeeded, 0 errors.

Run: `cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\web"; npm run build`
Expected: `tsc` + `vite build` complete with no type errors.

- [ ] **Step 4: Commit**

```bash
git add FO4RecordEditor/Services/BackendInterop.cs web/src/backend.ts
git commit -m "feat(bridge): expose CompactToEsl/CleanPlugin/RenumberFormId + ConflictEntry type"
```

---

## Task 4: RecordView — conflict-status filter + plugin/record context actions

Add (a) a status filter to the matrix toolbar and (b) new entries to the right-click context menu: Change FormID, Compact plugin to ESL, Clean plugin (UDR). UI is verified manually (the repo does not unit-test React).

**Files:**
- Modify: `web/src/RecordView.tsx`
- Modify: `web/src/RecordView.css` (append-only)

- [ ] **Step 1: Add the status filter state + predicate**

In `web/src/RecordView.tsx`, after the existing `const [onlyDiffing, setOnlyDiffing] = useState(false);` (line 41), add:

```typescript
  // Conflict-status filter: 'all' or one of the per-cell statuses. When set, keep only rows that
  // have at least one cell of that status across the plugin columns.
  const [statusFilter, setStatusFilter] = useState<'all' | 'win' | 'lose' | 'override' | 'identical'>('all');
```

Then change the `matched` memo (lines 86-90) to also apply the status filter:

```typescript
  const matched = useMemo(() => matrix.Rows.filter(r => {
    if (onlyDiffing && !r.Differs) return false;
    if (f && !(r.DisplayLabel || r.Field).toLowerCase().includes(f)) return false;
    if (statusFilter !== 'all') {
      const hit = r.Values.some((_, ci) => cellStatus(r.Values, ci) === statusFilter);
      if (!hit) return false;
    }
    return true;
  }), [matrix, f, onlyDiffing, statusFilter]);
```

- [ ] **Step 2: Add the status dropdown to the toolbar**

In the `rv-toolbar` block (after the `Only conflicts` button, ~line 205), add:

```tsx
        <select
          className="rv-status-filter"
          value={statusFilter}
          onChange={e => setStatusFilter(e.target.value as typeof statusFilter)}
          title="Show only rows that have a cell of this conflict status"
        >
          <option value="all">All statuses</option>
          <option value="win">Winning</option>
          <option value="lose">Overridden (losing)</option>
          <option value="override">Override (sole/last)</option>
          <option value="identical">Identical to master</option>
        </select>
```

- [ ] **Step 3: Add a `plugin`-scoped section to the context menu**

In the `menu &&` context-menu block, add before the final `</div>` of `rv-context` (after the Delete button, ~line 322), a plugin-operations section. It targets the plugin of the right-clicked column (`matrix.Plugins[menu.col]`):

```tsx
          <div className="rv-context-sep" />
          <div className="rv-context-label">Plugin: {matrix.Plugins[menu.col]}</div>
          <button onClick={() => renumberFormId(menu.col)}><Hash size={13} /> Change FormID…</button>
          <button onClick={() => compactEsl(menu.col)}><Minimize2 size={13} /> Compact to ESL</button>
          <button onClick={() => cleanPlugin(menu.col)}><Eraser size={13} /> Clean (UDR)</button>
```

Add the three icons to the existing `lucide-react` import at the top (line 2-5):

```typescript
import {
  Folder, ChevronRight, ChevronDown, Filter, EyeOff, Pencil, Copy, Save, Trash2,
  Link2, AlertTriangle, Hash, Minimize2, Eraser,
} from 'lucide-react';
```

- [ ] **Step 4: Add the three handlers**

Next to the existing `deleteRecord` handler (after line 162), add:

```typescript
  const renumberFormId = async (col: number) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    const plug = matrix.Plugins[col];
    const cur = matrix.FormKey.split(':')[0];
    const next = window.prompt(`New FormID for ${matrix.EditorID || matrix.FormKey} in ${plug} (6-digit hex):`, cur);
    if (!next) return;
    setStatus('Renumbering…');
    try { setStatus(await b.RenumberFormId(plug, matrix.FormKey, next)); await onReload(); }
    catch (e: any) { setStatus('Renumber failed: ' + (e?.message || e)); }
  };

  const compactEsl = async (col: number) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    setStatus('Compacting…');
    try { setStatus(await b.CompactToEsl(matrix.Plugins[col])); await onReload(); }
    catch (e: any) { setStatus('Compact failed: ' + (e?.message || e)); }
  };

  const cleanPlugin = async (col: number) => {
    setMenu(null);
    const b = back();
    if (!b) return;
    setStatus('Cleaning…');
    try { setStatus(await b.CleanPlugin(matrix.Plugins[col])); await onReload(); }
    catch (e: any) { setStatus('Clean failed: ' + (e?.message || e)); }
  };
```

- [ ] **Step 5: Add styles**

Append to `web/src/RecordView.css`:

```css
.rv-status-filter {
  background: var(--bg-secondary, #252526);
  color: var(--text-primary, #ccc);
  border: 1px solid var(--border-color, #3c3c3c);
  border-radius: 4px;
  font-size: 11px;
  padding: 2px 6px;
  margin-left: 8px;
}
```

- [ ] **Step 6: Build + manual verify**

Run: `cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\web"; npm run build`
Expected: 0 type errors.

Manual verify (after Task 9 build/deploy, or `npm run dev`): open a conflicting record; the status dropdown filters rows; right-click a cell → the plugin section shows Change FormID / Compact to ESL / Clean; each runs and updates the status line + matrix.

- [ ] **Step 7: Commit**

```bash
git add web/src/RecordView.tsx web/src/RecordView.css
git commit -m "feat(ui): conflict-status filter + Change FormID/Compact/Clean context actions"
```

---

## Task 5: RecordView — clickable Referenced-By navigation

Make the Referenced-By drawer rows open the referencing record (xEdit jump-to-reference). `RecordView` gains an `onOpenRecord` prop; `MainShell` provides it (Task 6 wires the supplier; this task adds the prop + a temporary inline supplier so it works standalone).

**Files:**
- Modify: `web/src/RecordView.tsx`
- Modify: `web/src/MainShell.tsx`

- [ ] **Step 1: Add the prop to RecordView's signature**

In `web/src/RecordView.tsx`, change the component signature (lines 37-39) to accept an optional opener:

```typescript
export default function RecordView(
  { matrix, plugin, onReload, onOpenRecord }: {
    matrix: ConflictMatrix; plugin: string;
    onReload: () => Promise<void>;
    onOpenRecord?: (formKey: string, plugin: string) => void;
  }
) {
```

- [ ] **Step 2: Make the refBy rows clickable**

In the drawer body, replace the `referenced` branch rows (lines 290-296) with clickable rows:

```tsx
              : refBy.map((e, i) => (
                <div
                  key={i}
                  className={`rv-ref-row ${onOpenRecord ? 'rv-ref-clickable' : ''}`}
                  onClick={() => onOpenRecord?.(e.FormKey, e.Plugin)}
                  title={onOpenRecord ? `Open ${e.EditorID || e.FormKey}` : undefined}
                >
                  <span className="rv-ref-type">{e.Type}</span>
                  <span className="rv-ref-id">{e.EditorID || e.FormKey}</span>
                  <span className="rv-ref-plugin">{e.Plugin}</span>
                </div>
              ))
```

- [ ] **Step 3: Add a hover style**

Append to `web/src/RecordView.css`:

```css
.rv-ref-clickable { cursor: pointer; }
.rv-ref-clickable:hover { background: var(--bg-hover, #2a2d2e); }
```

- [ ] **Step 4: Provide the opener from MainShell**

In `web/src/MainShell.tsx`, add an open-by-FormKey helper next to `openHit` (after line 286). It reuses the same matrix-fetch path:

```typescript
  // Open any record by FormKey (used by Referenced-By jump navigation and the Conflicts panel).
  const openByFormKey = async (formKey: string, pluginName: string) => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return;
    setRecordLoading(true);
    try {
      const json = await b.GetConflictMatrix(formKey);
      const matrix: ConflictMatrix | null = json ? JSON.parse(json) : null;
      if (!matrix) { setErrorStr(`No record for ${formKey}.`); return; }
      setOpenRecord({ title: matrix.EditorID || formKey, plugin: pluginName || matrix.Winner, matrix });
    } catch (err: any) {
      setErrorStr(err?.message || String(err));
    } finally {
      setRecordLoading(false);
    }
  };
```

Then pass it to `RecordView` (line 414):

```tsx
            <RecordView matrix={openRecord.matrix} plugin={openRecord.plugin} onReload={reloadMatrix} onOpenRecord={openByFormKey} />
```

- [ ] **Step 5: Build + manual verify**

Run: `cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\web"; npm run build`
Expected: 0 type errors.

Manual verify: open a record that is referenced by others → the "Referenced By" drawer lists referrers → clicking one opens that record in the center pane.

- [ ] **Step 6: Commit**

```bash
git add web/src/RecordView.tsx web/src/RecordView.css web/src/MainShell.tsx
git commit -m "feat(ui): clickable Referenced-By jump navigation"
```

---

## Task 6: MainShell — Conflicts panel with multi-select batch copy-as-override

Add a Conflicts sidebar panel (the activity bar already has a `compare` button that currently shows nothing distinct). It lists conflicting records via `backend.GetConflicts()`, supports multi-select with checkboxes, click-to-open, and a batch "Copy selected as override into <patch>" action that loops `CopyAsOverride`.

**Files:**
- Modify: `web/src/MainShell.tsx`
- Modify: `web/src/MainShell.css` (append-only)

- [ ] **Step 1: Add Conflicts state + loader**

In `web/src/MainShell.tsx`, import the `ConflictEntry` type — change the existing type import (line 20) to:

```typescript
import type { ConflictMatrix, SearchHit, ConflictEntry } from './backend';
```

Add state after the search state (after line 105):

```typescript
  // Conflicts panel
  const [conflicts, setConflicts] = useState<ConflictEntry[] | null>(null);
  const [conflictSel, setConflictSel] = useState<Record<string, boolean>>({});
  const [conflictBusy, setConflictBusy] = useState(false);

  const loadConflicts = async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return;
    setConflicts(null);
    try { setConflicts(JSON.parse(await b.GetConflicts())); }
    catch (e: any) { setErrorStr(e?.message || String(e)); setConflicts([]); }
  };

  // Load the conflict list the first time the Conflicts tab is opened.
  useEffect(() => {
    if (activeTab === 'compare' && conflicts === null) loadConflicts();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeTab]);

  const batchCopyAsOverride = async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b || !conflicts) return;
    const picked = conflicts.filter(c => conflictSel[c.FormKey]);
    if (picked.length === 0) { setStatus('Select at least one conflict first.'); return; }
    const patch = window.prompt(`Copy ${picked.length} record(s) as override into which patch plugin?`, 'ConflictPatch.esp');
    if (!patch) return;
    setConflictBusy(true);
    let ok = 0;
    try {
      for (const c of picked) {
        const res = await b.CopyAsOverride(c.Winner, c.FormKey, patch);
        if (!/fail|could not|not an overridable|cannot be overridden/i.test(res)) ok++;
      }
      setStatus(`Copied ${ok}/${picked.length} into ${patch}. Save it from a record's context menu, then enable it last.`);
      setConflictSel({});
    } catch (e: any) {
      setStatus('Batch copy failed: ' + (e?.message || e));
    } finally { setConflictBusy(false); }
  };
```

- [ ] **Step 2: Render the Conflicts panel**

Replace the sidebar body so that when `activeTab === 'compare'` it shows the Conflicts panel instead of the Explorer tree. Wrap the existing `<div className="sidebar-tree">…</div>` (lines 353-391) so it only renders for the explorer/search tabs, and add the conflicts panel. Change the `sidebar-header` title to reflect the tab and add the panel after the tree:

In the `sidebar-header` (lines 335-337), make the heading dynamic:

```tsx
        <div className="sidebar-header">
          <h2>{activeTab === 'compare' ? 'CONFLICTS' : 'EXPLORER'}</h2>
        </div>
```

Then, immediately after the closing `</div>` of `sidebar-tree` (line 391), add the conflicts panel:

```tsx
        {activeTab === 'compare' && (
          <div className="conflicts-panel">
            <div className="conflicts-actions">
              <button className="sidebar-action-btn" onClick={loadConflicts}>Rescan</button>
              <button className="sidebar-action-btn" disabled={conflictBusy} onClick={batchCopyAsOverride}>
                Copy selected → patch
              </button>
            </div>
            {conflicts === null ? (
              <div className="sidebar-empty">Scanning conflicts…</div>
            ) : conflicts.length === 0 ? (
              <div className="sidebar-empty">No conflicts. Load a modlist and Scan Conflicts.</div>
            ) : (
              <div className="conflicts-list">
                {conflicts.map((c, i) => (
                  <div key={`${c.FormKey}:${i}`} className="conflict-row">
                    <input
                      type="checkbox"
                      checked={!!conflictSel[c.FormKey]}
                      onChange={e => setConflictSel(s => ({ ...s, [c.FormKey]: e.target.checked }))}
                    />
                    <span className="conflict-id" onClick={() => openByFormKey(c.FormKey, c.Winner)} title={`${c.FormKey} · ${c.Plugins.length} plugins`}>
                      {c.EditorID || c.FormKey}
                    </span>
                    <span className="conflict-type">{c.Type}</span>
                    <span className="conflict-count">{c.Plugins.length}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
```

Note: `openByFormKey` is defined in Task 5. If executing Task 6 before Task 5, add `openByFormKey` first.

- [ ] **Step 3: Add styles**

Append to `web/src/MainShell.css`:

```css
.conflicts-panel { display: flex; flex-direction: column; overflow: hidden; flex: 1; }
.conflicts-actions { display: flex; gap: 6px; padding: 6px 8px; }
.conflicts-list { overflow-y: auto; flex: 1; }
.conflict-row { display: flex; align-items: center; gap: 6px; padding: 3px 8px; font-size: 12px; }
.conflict-row:hover { background: var(--bg-hover, #2a2d2e); }
.conflict-id { flex: 1; cursor: pointer; color: #569CD6; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.conflict-type { color: #888; font-size: 10px; }
.conflict-count { color: #c79a5e; font-size: 10px; min-width: 16px; text-align: right; }
```

- [ ] **Step 4: Build + manual verify**

Run: `cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\web"; npm run build`
Expected: 0 type errors.

Manual verify: load a modlist + Scan Conflicts; click the Compare (conflicts) activity button → the panel lists conflicts; check several; "Copy selected → patch" prompts for a patch name and reports `Copied N/M`; clicking a conflict id opens it.

- [ ] **Step 5: Commit**

```bash
git add web/src/MainShell.tsx web/src/MainShell.css
git commit -m "feat(ui): Conflicts panel with multi-select batch copy-as-override"
```

---

## Task 7: ChatPanel — accept an external "ask AI" prompt

Add a decoupled bridge so any component can push a grounded question into the active chat via a `window` CustomEvent. This is the integration point for Task 8.

**Files:**
- Modify: `web/src/ChatPanel.tsx`

- [ ] **Step 1: Add a ref for the current session id**

In `web/src/ChatPanel.tsx`, after `const [currentId, setCurrentId] = useState('');` (line 70), add a ref that always holds the latest id (the event handler is registered once and must not capture a stale id):

```typescript
  const currentIdRef = useRef(currentId);
  currentIdRef.current = currentId;
  const busyMapRef = useRef(busyMap);
  busyMapRef.current = busyMap;
```

- [ ] **Step 2: Add a `sendExternal` function**

After the existing `send` function (after line 239), add a variant that sends an explicit text into the active session (no attachments, not a slash command):

```typescript
  // Send a prompt supplied by another component (e.g. "Ask AI: what breaks?"). Uses refs so the
  // once-registered event listener always targets the current, non-busy session.
  const sendExternal = async (text: string) => {
    const sid = currentIdRef.current;
    if (!text.trim() || !sid || busyMapRef.current[sid]) return;
    const c = host();
    if (!c) return;
    setMsgs(sid, p => [...p, { role: 'user', text }, { role: 'assistant', text: '' }]);
    setBusyFor(sid, true);
    try { await c.SendMessage(sid, text, '[]'); }
    catch (e: any) { setBusyFor(sid, false); setMsgs(sid, p => appendToLastAssistant(p, '\n⚠️ ' + (e?.message || e))); }
    refreshSessions();
  };
```

- [ ] **Step 3: Listen for the CustomEvent**

Add a `useEffect` after the initial-load effect (after line 153):

```typescript
  // Bridge: other components dispatch window CustomEvent('fo4:ask-ai', {detail: prompt}).
  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      if (typeof detail === 'string') sendExternal(detail);
    };
    window.addEventListener('fo4:ask-ai', handler);
    return () => window.removeEventListener('fo4:ask-ai', handler);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
```

- [ ] **Step 4: Build to verify it compiles**

Run: `cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\web"; npm run build`
Expected: 0 type errors.

- [ ] **Step 5: Commit**

```bash
git add web/src/ChatPanel.tsx
git commit -m "feat(ui): ChatPanel accepts external prompts via fo4:ask-ai event"
```

---

## Task 8: RecordView — "Ask AI: what breaks if I change this?"

Wire the AI-augmented-parity payoff: a context-menu action that asks the agent to analyze impact of editing/renumbering the current record. It dispatches a grounded prompt naming the FormKey so the agent calls `get_referenced_by` and explains downstream breakage.

**Files:**
- Modify: `web/src/RecordView.tsx`

- [ ] **Step 1: Add the action handler**

In `web/src/RecordView.tsx`, next to the other handlers (after `cleanPlugin` from Task 4), add:

```typescript
  const askImpact = () => {
    setMenu(null);
    const id = matrix.EditorID || matrix.FormKey;
    const prompt =
      `What breaks if I change or renumber the record ${id} [${matrix.FormKey}] (${matrix.Type})? ` +
      `Use get_referenced_by on ${matrix.FormKey} to list every record and plugin that references it, ` +
      `then tell me which would break and how to patch them.`;
    window.dispatchEvent(new CustomEvent('fo4:ask-ai', { detail: prompt }));
    setStatus('Asked Claude about impact — see the chat panel.');
  };
```

- [ ] **Step 2: Add the menu entry**

In the `menu &&` context menu, add to the plugin section added in Task 4 (after the Clean button):

```tsx
          <div className="rv-context-sep" />
          <button onClick={askImpact}><Brain size={13} /> Ask AI: what breaks?</button>
```

Add the `Brain` icon to the `lucide-react` import (the line edited in Task 4):

```typescript
import {
  Folder, ChevronRight, ChevronDown, Filter, EyeOff, Pencil, Copy, Save, Trash2,
  Link2, AlertTriangle, Hash, Minimize2, Eraser, Brain,
} from 'lucide-react';
```

- [ ] **Step 3: Build + manual verify**

Run: `cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\web"; npm run build`
Expected: 0 type errors.

Manual verify (needs an Anthropic API key configured in Settings): right-click a referenced record → "Ask AI: what breaks?" → the chat panel shows the question and Claude streams an answer that calls `get_referenced_by` and lists affected records.

- [ ] **Step 4: Commit**

```bash
git add web/src/RecordView.tsx
git commit -m "feat(ui): 'Ask AI: what breaks?' impact action wired into chat"
```

---

## Task 9: Full build, deploy, and end-to-end verification

Build everything together, confirm the whole backend suite is green, and do an in-app smoke test.

- [ ] **Step 1: Run the full backend test suite**

Run: `dotnet test "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor.Tests\FO4RecordEditor.Tests.csproj"`
Expected: all tests pass (the prior 19 + the 3 new `RenumberFormId*` tests = 22+), 0 failures.

- [ ] **Step 2: Build the React bundle + desktop app**

Run: `cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\web"; npm run build`
Expected: vite build succeeds.

Run: `dotnet build "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\FO4RecordEditor\FO4RecordEditor.csproj" -c Release`
Expected: Build succeeded, 0 errors. (The csproj copies `web/dist` into the output.)

- [ ] **Step 3: Launch + smoke test**

Run the app (`run.bat` in `FO4RecordEditor/`, or the built exe). Verify in order:
1. Load MO2 / Load Env → plugins populate the Explorer.
2. Scan Conflicts → Compare tab → Conflicts panel lists records; multi-select + "Copy selected → patch" reports `Copied N/M`.
3. Open a conflicting record → status-filter dropdown narrows rows; Referenced-By rows are clickable and jump.
4. Right-click a cell → Change FormID / Compact to ESL / Clean (UDR) each run and update the status line.
5. (With API key) "Ask AI: what breaks?" → chat answers with referenced-by analysis.

- [ ] **Step 4: Update PROGRESS docs**

Append a dated section to `docs/PROGRESS.md` summarizing the power-user editing UX milestone (FormID renumber, ESL compact/clean exposure, conflict-status filter, clickable refs, Conflicts batch panel, AI impact action) and the new test count.

- [ ] **Step 5: Final commit**

```bash
git add docs/PROGRESS.md
git commit -m "docs: record power-user editing UX milestone"
```

---

## Self-Review Notes

- **Spec coverage (the chosen subsystem: "Power-user editing UX"):**
  - Conflict-status filters → Task 4 (matrix) + Task 6 (Conflicts panel by record).
  - References / referenced-by → Task 5 (clickable jump).
  - FormID renumber/change → Tasks 1–3 (backend + tool + bridge) + Task 4 (UI).
  - ESL flag/compact → Task 3 (bridge) + Task 4 (UI); engine already existed.
  - Copy-as-override (batch / multi-select) → Task 6.
  - AI-augmented parity → Task 2 (tool) + Tasks 7–8 (impact action).
  - *Deferred (note to executor):* drag-and-drop copy-as-override was in the subsystem brief but is redundant with the existing context-menu "Copy as Override into" and the Task 6 batch flow; skip unless requested, to avoid HTML5 DnD complexity in WebView2.
- **Type consistency:** bridge methods `CompactToEsl` / `CleanPlugin` / `RenumberFormId` use identical names in `WriteService`, `BackendInterop`, and `backend.ts`. The CustomEvent name `fo4:ask-ai` matches between `RecordView` (dispatch) and `ChatPanel` (listen). `ConflictEntry` fields match `Models/ConflictEntry.cs` (`FormKey/EditorID/Type/Plugins/Winner/InvolvesMod`).
- **No placeholders:** every code step contains complete, runnable code; commands include expected output.
```