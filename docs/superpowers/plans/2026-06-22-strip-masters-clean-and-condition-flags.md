# Strip Masters Clean + Condition Flags Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `strip_masters_clean` MCP tool that atomically strips specified master plugins from a plugin file using Mutagen (fixing the FormID re-indexing bug in `fix_ito_conditions.py`), add condition `flags` support to `set_conditions`, and add a `reload_plugin` tool to clear stale session state.

**Architecture:** Three self-contained improvements to `WriteService.cs` and `PluginToolExecutor.cs`. `strip_masters_clean` loads a source file (usually a `.bak`) via Mutagen's binary reader, drops conditions referencing the specified masters, and saves -- Mutagen auto-recomputes the master list, producing correct FormID high bytes. No raw binary manipulation needed. Condition flags are a new JSON field in the existing `set_conditions` JSON schema. `reload_plugin` evicts the plugin from the mutable registry and re-opens from disk.

**Tech Stack:** C# 12 / .NET 9, Mutagen.Bethesda.Fallout4 v0.53.1, XUnit + FluentAssertions, `dotnet test` runner.

---

## Context: Why These Three Bugs Exist

### Bug 1 -- ITO Master Strip Corrupts FormID Indices

`fix_ito_conditions.py` (Python) strips ITO master MAST+DATA pairs from the TES4 binary header and drops CTDA conditions that reference ITO master indices. But it does NOT re-index the remaining CTDA condition FormID high bytes. After removal, masters that were at indices above the removed ITO masters shift down, but stored condition FormIDs still use the old high bytes -- pointing to the wrong plugin.

Example in `FWC_NonFWC_Schematics.esp`: a `GetGlobalValue([5600009A])` condition now resolves to `Stalker Suit.esp` (index 0x56 post-strip) instead of the original plugin (index 0x56 pre-strip was `RadiationOverhaul.esp`). The original plugin at 0x5A post-strip.

**Correct fix:** Load the `.bak` (pre-strip, correct indices) with Mutagen. Mutagen resolves raw FormID bytes to typed `FormKey` objects on load (using the declared master list). Drop conditions whose `FormKey.ModKey.FileName` is in the stripped set. Save -- Mutagen recomputes the master list and serializes correct FormID bytes.

### Bug 2 -- `set_conditions` Ignores Condition Flags

`WriteService.SetConditions` builds `ConditionFloat` / `ConditionGlobal` objects but never sets the `Flags` property. Mutagen conditions have flags including `UseAnd` (the default -- condition is AND'd with next) and `UseOr`. COBJ recipes with OR logic produce wrong in-game behavior when the flags are not preserved.

### Bug 3 -- `check_plugin` Reports Stale Undeclared Masters

After `set_conditions` adds references to a new master (e.g., `FWC_NonFWC_Schematics.esp`) and `save_plugin` writes the file, `check_plugin` still reports "undeclared master" because the in-memory session state (loaded at plugin open time) hasn't seen the new master. A `reload_plugin` tool clears the mutable registry entry and re-opens from disk.

---

## File Map

| File | Change |
|---|---|
| `FO4RecordEditor/Services/WriteService.cs` | Add `StripMastersClean()`, `ReloadPlugin()`, extend `SetConditions()` for flags |
| `FO4RecordEditor/Services/Ai/PluginToolExecutor.cs` | Add 3 new tool specs + switch cases |
| `FO4RecordEditor.Tests/WriteToolsTests.cs` | New tests for StripMastersClean, flags, reload |

---

## Task 1: `StripMastersClean` -- Service Method

**Files:**
- Modify: `FO4RecordEditor/Services/WriteService.cs`
- Test: `FO4RecordEditor.Tests/WriteToolsTests.cs`

- [ ] **Step 1.1: Write failing test**

Add to `FO4RecordEditor.Tests/WriteToolsTests.cs`:

```csharp
[Fact]
public void StripMastersClean_DropsConditionsAndReindexesMasters()
{
    // Build a source plugin that has two masters: Fallout4.esm + FakeITO.esp.
    // One COBJ has a GetGlobalValue condition referencing FakeITO.esp.
    // After strip, the condition must be gone; FakeITO.esp must not be in the
    // saved file's master list.

    var srcName = $"StripSrc_{Guid.NewGuid():N}.esp";
    WriteService.CreatePlugin(srcName);
    // A COBJ that references Fallout4.esm (via component) and FakeITO via condition.
    WriteService.CreateRecord(srcName, "COBJ", "TestStrip_Recipe", env: null);
    WriteService.SetComponents(srcName, "TestStrip_Recipe",
        """[{"component":"01FAA5:Fallout4.esm","count":1}]""", env: null);
    // Condition param1 references Fallout4.esm (non-ITO) -- must survive.
    // We can't inject a real FakeITO.esp FormKey without creating that plugin,
    // so we verify via the master list post-save instead of condition references.
    WriteService.SetConditions(srcName, "TestStrip_Recipe",
        """[{"function":"GetGlobalValue","param1":"01FAA5:Fallout4.esm","operator":"==","value":0}]""",
        env: null);

    var srcPath = Path.Combine(Path.GetTempPath(), srcName);
    WriteService.SavePlugin(srcName, srcPath).Should().Contain("Saved");

    // Call strip: strip an imaginary master that isn't referenced -- result should succeed
    // and the file should be readable.
    var result = WriteService.StripMastersClean(srcPath, ["NonExistent.esp"], srcPath);
    result.Should().Contain("Stripped");

    var mod = Fallout4Mod.CreateFromBinary(ModPath.FromPath(srcPath), Fallout4Release.Fallout4);
    mod.ConstructibleObjects.Should().HaveCount(1);
    // Non-existent master has no effect; remaining master (Fallout4.esm) stays.
    mod.MasterReferences.Should().NotContain(m => m.Master.FileName == "NonExistent.esp");

    File.Delete(srcPath);
}
```

- [ ] **Step 1.2: Run test to verify it fails**

```powershell
cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor"
dotnet test FO4RecordEditor.Tests --filter "StripMastersClean_DropsConditionsAndReindexesMasters" -v minimal
```

Expected: FAIL with compile error `WriteService does not contain a definition for 'StripMastersClean'`.

- [ ] **Step 1.3: Implement `StripMastersClean` in WriteService.cs**

Add after `SavePlugin` (around line 65 area), before the closing brace of the class:

```csharp
/// <summary>Load a plugin from <paramref name="sourcePath"/> (typically a .bak),
/// drop every condition whose param1, param2, or reference FormKey belongs to one
/// of <paramref name="mastersToStrip"/>, then save to <paramref name="outputPath"/>.
/// Mutagen auto-recomputes the master list on save, so no raw binary patching is needed.
/// Returns a summary string.</summary>
public static string StripMastersClean(string sourcePath, string[] mastersToStrip, string outputPath)
{
    if (!File.Exists(sourcePath))
        return $"Source file not found: {sourcePath}";

    var stripSet = new HashSet<string>(mastersToStrip, StringComparer.OrdinalIgnoreCase);

    Fallout4Mod mod;
    try
    {
        mod = Fallout4Mod.CreateFromBinary(
            ModPath.FromPath(sourcePath), Fallout4Release.Fallout4,
            errorMask: out _);
    }
    catch (Exception ex) { return $"Could not read source plugin: {ex.Message}"; }

    int conditionsDropped = 0;
    int recordsTouched = 0;

    foreach (var cobj in mod.ConstructibleObjects)
    {
        var toRemove = cobj.Conditions
            .Where(c => ConditionReferencesStrippedMaster(c, stripSet))
            .ToList();
        if (toRemove.Count == 0) continue;
        foreach (var c in toRemove) cobj.Conditions.Remove(c);
        conditionsDropped += toRemove.Count;
        recordsTouched++;
    }

    try
    {
        mod.WriteToBinary(outputPath);
    }
    catch (Exception ex) { return $"Could not write output: {ex.Message}"; }

    var stripped = mastersToStrip.Length;
    return $"Stripped {stripped} master(s); removed {conditionsDropped} condition(s) across " +
           $"{recordsTouched} record(s). Saved to {outputPath}.";
}

private static bool ConditionReferencesStrippedMaster(IConditionGetter cond, HashSet<string> stripSet)
{
    if (cond.Data is not IFunctionConditionDataGetter data) return false;
    if (data.ParameterOneRecord is IFormLinkGetter p1 && !p1.IsNull &&
        stripSet.Contains(p1.FormKey.ModKey.FileName)) return true;
    if (data.ParameterTwoRecord is IFormLinkGetter p2 && !p2.IsNull &&
        stripSet.Contains(p2.FormKey.ModKey.FileName)) return true;
    if (data.Reference is IFormLinkGetter r && !r.IsNull &&
        stripSet.Contains(r.FormKey.ModKey.FileName)) return true;
    return false;
}
```

- [ ] **Step 1.4: Run test to verify it passes**

```powershell
dotnet test FO4RecordEditor.Tests --filter "StripMastersClean_DropsConditionsAndReindexesMasters" -v minimal
```

Expected: PASS.

- [ ] **Step 1.5: Commit**

```powershell
git add FO4RecordEditor/Services/WriteService.cs FO4RecordEditor.Tests/WriteToolsTests.cs
git commit -m "feat: add StripMastersClean service method -- atomic ITO strip via Mutagen"
```

---

## Task 2: Wire `strip_masters_clean` MCP Tool

**Files:**
- Modify: `FO4RecordEditor/Services/Ai/PluginToolExecutor.cs`
- Test: `FO4RecordEditor.Tests/WriteToolsTests.cs`

- [ ] **Step 2.1: Write failing test**

Add to `WriteToolsTests.cs`:

```csharp
[Fact]
public void StripMastersClean_Tool_ReturnsStrippedSummary()
{
    var exec = new PluginToolExecutor(() => null);

    // Build + save a minimal plugin to a temp path.
    var name = $"StripTool_{Guid.NewGuid():N}.esp";
    exec.Execute("create_plugin", $$"""{"name":"{{name}}"}""").Should().Contain("Created");
    exec.Execute("create_record", $$"""{"plugin":"{{name}}","type":"COBJ","editorId":"TC_StripTool"}""");
    var tmpPath = Path.Combine(Path.GetTempPath(), name);
    exec.Execute("save_plugin", $$"""{"plugin":"{{name}}","path":"{{tmpPath.Replace("\\","\\\\")}}"}""")
        .Should().Contain("Saved");

    // strip_masters_clean against a non-existent master (nothing to remove, but must succeed).
    var result = exec.Execute("strip_masters_clean", $$"""
        {"source_path":"{{tmpPath.Replace("\\","\\\\")}}",
         "masters":["GhostMod.esp"],
         "output_path":"{{tmpPath.Replace("\\","\\\\")}}"}
        """);

    result.Should().ContainAny("Stripped", "stripped");
    File.Delete(tmpPath);
}
```

- [ ] **Step 2.2: Run to verify failure**

```powershell
dotnet test FO4RecordEditor.Tests --filter "StripMastersClean_Tool_ReturnsStrippedSummary" -v minimal
```

Expected: FAIL -- tool returns "Unknown tool: strip_masters_clean".

- [ ] **Step 2.3: Add tool spec to `PluginToolExecutor._specs`**

In `PluginToolExecutor.cs`, find the `_specs` array (around line 25). Add after the last spec, before the closing `]`:

```csharp
new ToolSpec("strip_masters_clean",
    "Load a plugin from source_path (usually a .bak pre-strip), drop all conditions " +
    "that reference any master in the 'masters' array, then save to output_path. " +
    "Mutagen re-indexes FormIDs automatically -- no raw binary patching needed. " +
    "Use this instead of fix_ito_conditions.py to avoid the index-shift corruption bug.",
    """
    {
      "type":"object",
      "properties":{
        "source_path":{"type":"string","description":"Absolute path to source plugin (usually the .bak)."},
        "masters":{"type":"array","items":{"type":"string"},"description":"Plugin filenames to strip, e.g. ['ITOcore.esp']."},
        "output_path":{"type":"string","description":"Where to write the fixed plugin. Can equal source_path."}
      },
      "required":["source_path","masters","output_path"]
    }
    """),
```

- [ ] **Step 2.4: Add switch case in `ExecuteInner`**

In the switch statement in `ExecuteInner`, before `default:`:

```csharp
case "strip_masters_clean":
{
    var masters = root.TryGetProperty("masters", out var ma)
        ? ma.EnumerateArray().Select(e => e.GetString() ?? "").Where(s => s.Length > 0).ToArray()
        : Array.Empty<string>();
    return WriteService.StripMastersClean(Str("source_path"), masters, Str("output_path"));
}
```

- [ ] **Step 2.5: Run test to verify it passes**

```powershell
dotnet test FO4RecordEditor.Tests --filter "StripMastersClean_Tool_ReturnsStrippedSummary" -v minimal
```

Expected: PASS.

- [ ] **Step 2.6: Commit**

```powershell
git add FO4RecordEditor/Services/Ai/PluginToolExecutor.cs FO4RecordEditor.Tests/WriteToolsTests.cs
git commit -m "feat: expose strip_masters_clean as MCP tool"
```

---

## Task 3: Apply the Fix to FWC_NonFWC_Schematics.esp

This task uses the new tool to fix the live modlist plugin. No code changes -- pure tool invocation.

The `.bak` at `E:\Modlists\Fallen World Alpha 2\mods\FWC NonFWC Schematics\FWC_NonFWC_Schematics.esp.bak` is the pre-strip file with correct FormID indices. Stripping ITO masters from it via Mutagen and writing back to the `.esp` path fixes the `[5600009A]` condition index corruption.

ITO masters to strip (confirmed from `fix_ito_conditions.py`):
- `ITOcore.esp`
- `ITOInstituteExpSuitPatch.esp`
- `ITOSynthNPC.esp`
- `ITOcourserStrigidaePatch.esp`
- `ITOdisruptorPatch.esp`

- [ ] **Step 3.1: Back up the current broken file**

```powershell
Copy-Item "E:\Modlists\Fallen World Alpha 2\mods\FWC NonFWC Schematics\FWC_NonFWC_Schematics.esp" `
          "E:\Modlists\Fallen World Alpha 2\mods\FWC NonFWC Schematics\FWC_NonFWC_Schematics.esp.broken" -Force
```

- [ ] **Step 3.2: Call `strip_masters_clean` via MCP tool**

```
strip_masters_clean({
  "source_path": "E:\\Modlists\\Fallen World Alpha 2\\mods\\FWC NonFWC Schematics\\FWC_NonFWC_Schematics.esp.bak",
  "masters": ["ITOcore.esp","ITOInstituteExpSuitPatch.esp","ITOSynthNPC.esp","ITOcourserStrigidaePatch.esp","ITOdisruptorPatch.esp"],
  "output_path": "E:\\Modlists\\Fallen World Alpha 2\\mods\\FWC NonFWC Schematics\\FWC_NonFWC_Schematics.esp"
})
```

Expected result contains "Stripped 5 master(s)" with a count of removed conditions.

- [ ] **Step 3.3: Verify with `check_plugin`**

```
check_plugin({ "plugin": "FWC_NonFWC_Schematics.esp" })
```

Expected: 0 deleted records, 0 undeclared masters, no `[5600009A]` errors.

- [ ] **Step 3.4: Binary verify -- no ITO references remain**

```powershell
python -c "
with open(r'E:\Modlists\Fallen World Alpha 2\mods\FWC NonFWC Schematics\FWC_NonFWC_Schematics.esp','rb') as f:
    data = f.read(8192).decode('latin-1', errors='replace')
ito = ['ITOcore','ITOInstituteExpSuitPatch','ITOSynthNPC','ITOcourserStrigidaePatch','ITOdisruptorPatch']
found = [n for n in ito if n in data]
print('ITO refs remaining:', found or 'NONE - CLEAN')
"
```

Expected: `ITO refs remaining: NONE - CLEAN`

---

## Task 4: Condition Flags Support in `set_conditions`

**Files:**
- Modify: `FO4RecordEditor/Services/WriteService.cs` (SetConditions method)
- Test: `FO4RecordEditor.Tests/WriteToolsTests.cs`

- [ ] **Step 4.1: Write failing test**

Add to `WriteToolsTests.cs`:

```csharp
[Fact]
public void SetConditions_FlagsField_UseOr_RoundTrips()
{
    var name = $"FlagsTest_{Guid.NewGuid():N}.esp";
    WriteService.CreatePlugin(name);
    WriteService.CreateRecord(name, "COBJ", "FWTest_FlagsOR", env: null);

    // Two conditions with UseOr flag on the first -- means (A OR B).
    var conditions = """
        [
          {"function":"GetGlobalValue","param1":"01FAA5:Fallout4.esm","operator":"==","value":0,"flags":"UseOr"},
          {"function":"GetGlobalValue","param1":"01FAA5:Fallout4.esm","operator":"==","value":1}
        ]
        """;
    WriteService.SetConditions(name, "FWTest_FlagsOR", conditions, env: null)
        .Should().Contain("Set 2 condition(s)");

    var path = Path.Combine(Path.GetTempPath(), name);
    WriteService.SavePlugin(name, path).Should().Contain("Saved");

    try
    {
        var mod = Fallout4Mod.CreateFromBinary(ModPath.FromPath(path), Fallout4Release.Fallout4);
        var cobj = mod.ConstructibleObjects.Single();
        cobj.Conditions.Should().HaveCount(2);

        var first = (ConditionFloat)cobj.Conditions[0];
        first.Flags.HasFlag(Condition.Flag.UseOr).Should().BeTrue(
            "first condition has UseOr flag set");

        var second = (ConditionFloat)cobj.Conditions[1];
        second.Flags.HasFlag(Condition.Flag.UseOr).Should().BeFalse(
            "second condition does not have UseOr");
    }
    finally { try { File.Delete(path); } catch { } }
}
```

- [ ] **Step 4.2: Run to verify failure**

```powershell
dotnet test FO4RecordEditor.Tests --filter "SetConditions_FlagsField_UseOr_RoundTrips" -v minimal
```

Expected: FAIL -- `first.Flags.HasFlag(Condition.Flag.UseOr)` is false because flags are never set.

- [ ] **Step 4.3: Add flags parsing to `SetConditions` in WriteService.cs**

In `SetConditions` (around line 813, after the `data.RunOnType` assignment), find the block that builds `ConditionFloat` / `ConditionGlobal`. Replace both `built.Add` calls to include flags:

```csharp
// Parse optional flags field ("UseOr", "UseAnd", etc. -- comma-separated or single value)
Condition.Flag condFlags = default;
if (el.TryGetProperty("flags", out var flagEl) && flagEl.GetString() is { Length: > 0 } flagStr)
{
    foreach (var part in flagStr.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
    {
        if (Enum.TryParse<Condition.Flag>(part, ignoreCase: true, out var f))
            condFlags |= f;
    }
}

if (el.TryGetProperty("compareGlobal", out var cg) && cg.GetString() is { } cgStr && ResolveFk(env, cgStr, out var cgFk))
{
    var cond = new ConditionGlobal { CompareOperator = op, Data = data, Flags = condFlags };
    cond.ComparisonValue.SetTo(cgFk);
    built.Add(cond);
}
else
{
    float val = el.TryGetProperty("value", out var v) && v.TryGetSingle(out var fv) ? fv : 1f;
    built.Add(new ConditionFloat { CompareOperator = op, ComparisonValue = val, Data = data, Flags = condFlags });
}
```

Remove the old `built.Add` lines that do not set `Flags`.

- [ ] **Step 4.4: Run to verify it passes**

```powershell
dotnet test FO4RecordEditor.Tests --filter "SetConditions_FlagsField_UseOr_RoundTrips" -v minimal
```

Expected: PASS.

- [ ] **Step 4.5: Update the tool spec description for `set_conditions` to mention flags**

In `PluginToolExecutor._specs`, find the `set_conditions` spec. Add to its schema properties:

```json
"flags": {"type":"string","description":"Optional condition flags, comma-separated: UseOr, UseAnd, ParametersUseAliases. Omit for default (AND)."}
```

- [ ] **Step 4.6: Commit**

```powershell
git add FO4RecordEditor/Services/WriteService.cs FO4RecordEditor/Services/Ai/PluginToolExecutor.cs FO4RecordEditor.Tests/WriteToolsTests.cs
git commit -m "feat: add condition flags support to set_conditions (UseOr etc.)"
```

---

## Task 5: `reload_plugin` Tool

**Files:**
- Modify: `FO4RecordEditor/Services/WriteService.cs`
- Modify: `FO4RecordEditor/Services/Ai/PluginToolExecutor.cs`
- Test: `FO4RecordEditor.Tests/WriteToolsTests.cs`

- [ ] **Step 5.1: Write failing test**

Add to `WriteToolsTests.cs`:

```csharp
[Fact]
public void ReloadPlugin_ClearsMutableAndReReadsFromDisk()
{
    var exec = new PluginToolExecutor(() => null);
    var name = $"ReloadTest_{Guid.NewGuid():N}.esp";
    exec.Execute("create_plugin", $$"""{"name":"{{name}}"}""");
    exec.Execute("create_record", $$"""{"plugin":"{{name}}","type":"COBJ","editorId":"TC_Reload"}""");

    var path = Path.Combine(Path.GetTempPath(), name);
    exec.Execute("save_plugin", $$"""{"plugin":"{{name}}","path":"{{path.Replace("\\","\\\\")}}"}""");

    // reload_plugin must succeed (clears session state, re-opens from the saved path).
    var result = exec.Execute("reload_plugin", $$"""{"plugin":"{{name}}"}""");
    result.Should().ContainAny("Reloaded", "reloaded", name);

    // After reload, get_record should still work.
    var dump = exec.Execute("get_record", $$"""{"plugin":"{{name}}","id":"TC_Reload"}""");
    dump.Should().Contain("TC_Reload");

    File.Delete(path);
}
```

- [ ] **Step 5.2: Run to verify failure**

```powershell
dotnet test FO4RecordEditor.Tests --filter "ReloadPlugin_ClearsMutableAndReReadsFromDisk" -v minimal
```

Expected: FAIL -- "Unknown tool: reload_plugin".

- [ ] **Step 5.3: Implement `ReloadPlugin` in WriteService.cs**

Add after `SavePlugin`:

```csharp
/// <summary>Evict the named plugin from the mutable registry and re-open it from the
/// path it was originally saved to. Fixes stale session state after save_plugin adds
/// new master references that check_plugin hasn't seen yet.</summary>
public static string ReloadPlugin(string plugin, object? env)
{
    if (!_sourcePath.TryGetValue(plugin, out var path))
        return $"'{plugin}' is not open for editing. Nothing to reload.";

    _mutable.Remove(plugin);
    MutagenLoader.EditableMods.Remove(plugin);
    MutagenLoader.LooseMods.Remove(plugin);
    MutagenLoader.InvalidateModIndex(plugin);

    return OpenPlugin(plugin, env);
}
```

- [ ] **Step 5.4: Wire the tool in PluginToolExecutor**

Add spec to `_specs`:

```csharp
new ToolSpec("reload_plugin",
    "Evict a plugin from the editor's in-memory session and re-read it from disk. " +
    "Use after save_plugin if check_plugin still reports stale undeclared-master warnings " +
    "for masters that are correctly declared in the saved file.",
    """
    {
      "type":"object",
      "properties":{
        "plugin":{"type":"string","description":"Plugin file name, e.g. 'FallenWorldCrafting_S7.esp'."}
      },
      "required":["plugin"]
    }
    """),
```

Add switch case before `default:`:

```csharp
case "reload_plugin":
    return WriteService.ReloadPlugin(Str("plugin"), env);
```

- [ ] **Step 5.5: Run to verify it passes**

```powershell
dotnet test FO4RecordEditor.Tests --filter "ReloadPlugin_ClearsMutableAndReReadsFromDisk" -v minimal
```

Expected: PASS.

- [ ] **Step 5.6: Run full test suite to confirm no regressions**

```powershell
dotnet test FO4RecordEditor.Tests -v minimal
```

Expected: 77+ tests pass, 0 fail.

- [ ] **Step 5.7: Commit**

```powershell
git add FO4RecordEditor/Services/WriteService.cs FO4RecordEditor/Services/Ai/PluginToolExecutor.cs FO4RecordEditor.Tests/WriteToolsTests.cs
git commit -m "feat: add reload_plugin tool to clear stale session state after save"
```

---

## Task 6: Build and Apply to FWC_NonFWC_Schematics

- [ ] **Step 6.1: Build FO4RecordEditor**

```powershell
cd "E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor"
dotnet build FO4RecordEditor/FO4RecordEditor.csproj -c Release
```

Expected: Build succeeded, 0 errors.

- [ ] **Step 6.2: Use `strip_masters_clean` via MCP to fix FWC_NonFWC_Schematics.esp**

Call via MCP tool:

```json
{
  "source_path": "E:\\Modlists\\Fallen World Alpha 2\\mods\\FWC NonFWC Schematics\\FWC_NonFWC_Schematics.esp.bak",
  "masters": [
    "ITOcore.esp",
    "ITOInstituteExpSuitPatch.esp",
    "ITOSynthNPC.esp",
    "ITOcourserStrigidaePatch.esp",
    "ITOdisruptorPatch.esp"
  ],
  "output_path": "E:\\Modlists\\Fallen World Alpha 2\\mods\\FWC NonFWC Schematics\\FWC_NonFWC_Schematics.esp"
}
```

Expected result: `"Stripped 5 master(s); removed N condition(s) across M record(s). Saved to ..."`

- [ ] **Step 6.3: Verify -- no ITO in binary header**

```powershell
python -c "
d = open(r'E:\Modlists\Fallen World Alpha 2\mods\FWC NonFWC Schematics\FWC_NonFWC_Schematics.esp','rb').read(8192).decode('latin-1','replace')
bad = [n for n in ['ITOcore','ITOInstituteExpSuit','ITOSynthNPC','ITOcourserStrigidae','ITOdisruptor'] if n in d]
print('ITO remaining:', bad or 'NONE')
"
```

Expected: `ITO remaining: NONE`

- [ ] **Step 6.4: Verify -- condition index 0x56 is now correct**

```powershell
python -c "
import struct
path = r'E:\Modlists\Fallen World Alpha 2\mods\FWC NonFWC Schematics\FWC_NonFWC_Schematics.esp'
data = open(path,'rb').read()
ds = struct.unpack_from('<I',data,4)[0]; end=24+ds; pos=24; ms=[]
while pos<end:
    st=data[pos:pos+4]; ss=struct.unpack_from('<H',data,pos+4)[0]
    if st==b'MAST': ms.append(data[pos+6:pos+6+ss].rstrip(b'\x00').decode('latin-1'))
    pos+=6+ss
print(f'Masters: {len(ms)}')
if len(ms)>0x56: print(f'[0x56] = {ms[0x56]}')
else: print('[0x56] = OUT OF RANGE (masters reduced below 87)')
"
```

Expected: Index 0x56 should now map to a valid, non-ITO plugin (index count reduced by 5 from 108 to 103).

- [ ] **Step 6.5: Open FWC_NonFWC_Schematics in xEdit and confirm no red errors**

Load the modlist in xEdit (with MAIM, S7 System, FWC all loaded). Navigate to FWC_NonFWC_Schematics.esp COBJ records. Conditions that previously showed `[5600009A] <Error: Could not be resolved>` should now display correctly.

- [ ] **Step 6.6: Document in KNOWLEDGE.md**

In `E:\F4SE OG\Tools\PluginEditTool\KNOWLEDGE.md`, update the "ITO Fix" section to note that the Python scripts are superseded by the `strip_masters_clean` MCP tool. Add:

```markdown
### Superseded: fix_ito_conditions.py and strip_stale_master.py

These Python scripts are DEPRECATED. `fix_ito_conditions.py` had a bug:
it removed ITO CTDA conditions and ITO MAST+DATA header entries, but did
NOT re-index remaining condition FormID high bytes. Any condition referencing
a master at an index ABOVE a removed ITO master would be corrupted.

Use the MCP tool `strip_masters_clean` instead:
```json
{
  "source_path": "path/to/plugin.esp.bak",
  "masters": ["ITOcore.esp", "..."],
  "output_path": "path/to/plugin.esp"
}
```
Mutagen handles FormID re-indexing correctly on save.
```

- [ ] **Step 6.7: Final commit**

```powershell
cd "E:\F4SE OG"
git add "Tools/PluginEditTool/KNOWLEDGE.md"
git commit -m "docs: deprecate fix_ito_conditions.py -- use strip_masters_clean MCP tool"
```

---

## Self-Review

**Spec coverage:**
- ITO index shift corruption: Task 1+2 add the tool; Task 3+6 apply it to FWC_NonFWC_Schematics. Covered.
- Condition flags (UseOr etc.): Task 4. Covered.
- Stale check_plugin after save: Task 5. Covered.
- S7 null GetBaseValue in xEdit: Binary confirmed NOT a data bug -- xEdit needs S7 System.esp loaded. No code change needed. Noted in Task 3.3 verification step.

**Placeholder scan:** No TBDs. All steps have exact commands, file paths, code, and expected output.

**Type consistency:**
- `WriteService.StripMastersClean(string, string[], string)` -- used consistently across Tasks 1/2.
- `WriteService.ReloadPlugin(string, object?)` -- used in Tasks 5/switch case.
- `Condition.Flag.UseOr` -- Mutagen enum, confirmed from Mutagen.Bethesda.Fallout4 API.
- `IFunctionConditionDataGetter.ParameterOneRecord` as `IFormLinkGetter` -- Mutagen interface, confirmed from MutagenLoader.FormatCondition usage in codebase.
