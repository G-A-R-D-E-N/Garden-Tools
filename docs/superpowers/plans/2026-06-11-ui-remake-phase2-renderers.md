# UI Remake — Renderer Registry & Friendly Names (Phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every record field render as one human-readable line (no `Model.Data[0..31]`, no `MarkerColor.R/G/B/A`), generically across all record types, via an extensible renderer registry — improving the existing Record/Conflicts views and the AI's `get_record` output at once.

**Architecture:** A static `ElementRenderer` registry maps an element value to a single display string (and "stop recursing" signal). `MutagenLoader.WalkObject` consults it before its generic reflection walk; unknown types fall back to today's tree. A `FriendlyNames` map relabels raw property names. All renderers are pure functions unit-tested directly; the COBJ round-trip in `WriteToolsTests` guards the wiring.

**Tech Stack:** .NET 9, WPF, Mutagen.Bethesda.Fallout4 0.53.1, xUnit + FluentAssertions.

Context: the VSCode shell scaffold (activity bar, panel hosts, tabs, status bar, palette overlay) already exists in `MainWindow.xaml`. This plan does NOT touch the shell; it replaces the field-rendering logic that both the UI grid and the AI tools share. The current special-casing lives in `MutagenLoader.TryFriendlyLine`/`FormatCondition` (conditions + components) — this plan generalizes it into a registry and adds Color, byte-blob, and effect/script renderers.

---

### Task 1: Friendly field-name map

**Files:**
- Create: `FO4RecordEditor/Services/Rendering/FriendlyNames.cs`
- Test: `FO4RecordEditor.Tests/Rendering/FriendlyNamesTests.cs`

- [ ] **Step 1: Write the failing test**

```csharp
using FluentAssertions;
using FO4RecordEditor.Services.Rendering;
using Xunit;

namespace FO4RecordEditor.Tests.Rendering;

public class FriendlyNamesTests
{
    [Fact]
    public void Maps_known_names_and_passes_through_unknowns()
    {
        FriendlyNames.Label("EditorID").Should().Be("Editor ID");
        FriendlyNames.Label("CreatedObject").Should().Be("Created Object");
        FriendlyNames.Label("WorkbenchKeyword").Should().Be("Workbench Keyword");
        FriendlyNames.Label("SomeUnmappedField").Should().Be("SomeUnmappedField"); // unchanged
        FriendlyNames.Label("[0]").Should().Be("[0]");                              // list index untouched
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~FriendlyNamesTests"`
Expected: FAIL — `FriendlyNames` does not exist.

- [ ] **Step 3: Write minimal implementation**

```csharp
namespace FO4RecordEditor.Services.Rendering;

/// <summary>Relabels raw Mutagen property names to human-friendly column labels. Unknown names
/// and list indices ("[0]") pass through unchanged so nothing is ever hidden by accident.</summary>
public static class FriendlyNames
{
    private static readonly Dictionary<string, string> _map = new(StringComparer.Ordinal)
    {
        ["EditorID"] = "Editor ID",
        ["FormKey"] = "Form Key",
        ["CreatedObject"] = "Created Object",
        ["CreatedObjectCount"] = "Created Object Count",
        ["WorkbenchKeyword"] = "Workbench Keyword",
        ["PickUpSound"] = "Pick-Up Sound",
        ["PutDownSound"] = "Put-Down Sound",
        ["MarkerColor"] = "Marker Color",
        ["InteractionKeyword"] = "Interaction Keyword",
    };

    public static string Label(string raw) =>
        _map.TryGetValue(raw, out var v) ? v : raw;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~FriendlyNamesTests"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add FO4RecordEditor/Services/Rendering/FriendlyNames.cs FO4RecordEditor.Tests/Rendering/FriendlyNamesTests.cs
git commit -m "feat(ui): friendly field-name map"
```

---

### Task 2: Element renderer registry (move conditions/components in)

**Files:**
- Create: `FO4RecordEditor/Services/Rendering/ElementRenderer.cs`
- Modify: `FO4RecordEditor/Services/MutagenLoader.cs` (make `FormatFormLink` and `FormatCondition` reachable; see Task 5)
- Test: `FO4RecordEditor.Tests/Rendering/ElementRendererTests.cs`

The registry returns `true` + a one-line string when it can render an element as a leaf (no recursion), else `false`. Condition/component formatting moves here from `MutagenLoader`. To avoid a circular dependency, the FormLink formatter is injected once at startup.

- [ ] **Step 1: Write the failing test**

```csharp
using System.Drawing;
using FluentAssertions;
using FO4RecordEditor.Services.Rendering;
using Mutagen.Bethesda.Fallout4;
using Xunit;

namespace FO4RecordEditor.Tests.Rendering;

public class ElementRendererTests
{
    [Fact]
    public void Renders_color_as_hex_swatch_line()
    {
        ElementRenderer.TryRenderLine(Color.FromArgb(255, 204, 76, 51), out var text).Should().BeTrue();
        text.Should().Be("#CC4C33");
    }

    [Fact]
    public void Renders_component_as_item_xN()
    {
        var comp = new ConstructibleObjectComponent { Count = 3 };
        comp.Component.SetTo(Mutagen.Bethesda.Plugins.FormKey.Factory("01FAA5:Fallout4.esm"));
        ElementRenderer.TryRenderLine(comp, out var text).Should().BeTrue();
        text.Should().EndWith("x3");
    }

    [Fact]
    public void Returns_false_for_plain_objects()
    {
        ElementRenderer.TryRenderLine(new object(), out _).Should().BeFalse();
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~ElementRendererTests"`
Expected: FAIL — `ElementRenderer` does not exist.

- [ ] **Step 3: Write minimal implementation**

```csharp
using System.Drawing;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;

namespace FO4RecordEditor.Services.Rendering;

/// <summary>
/// Renders a single record element as ONE human-readable line. WalkObject calls TryRenderLine before
/// its generic reflection walk; a true result means "show this text and do not recurse". The FormLink
/// and Condition formatters live in MutagenLoader (they need the link cache); they are injected here
/// via Init to avoid a circular dependency.
/// </summary>
public static class ElementRenderer
{
    private static Func<IFormLinkIdentifier, string> _formatFormLink = fli => fli.FormKey.ToString();
    private static Func<IConditionGetter, string> _formatCondition = c => c.GetType().Name;

    public static void Init(
        Func<IFormLinkIdentifier, string> formatFormLink,
        Func<IConditionGetter, string> formatCondition)
    {
        _formatFormLink = formatFormLink;
        _formatCondition = formatCondition;
    }

    public static bool TryRenderLine(object item, out string text)
    {
        switch (item)
        {
            case Color c:
                text = $"#{c.R:X2}{c.G:X2}{c.B:X2}";
                return true;
            case IConstructibleObjectComponentGetter comp:
                text = $"{_formatFormLink(comp.Component)} x{comp.Count}";
                return true;
            case IConditionGetter cond:
                text = _formatCondition(cond);
                return true;
        }
        text = "";
        return false;
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~ElementRendererTests"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add FO4RecordEditor/Services/Rendering/ElementRenderer.cs FO4RecordEditor.Tests/Rendering/ElementRendererTests.cs
git commit -m "feat(ui): element renderer registry (color, component, condition)"
```

---

### Task 3: Collapse byte blobs (Model.Data, NAM1) to one line

**Files:**
- Modify: `FO4RecordEditor/Services/Rendering/ElementRenderer.cs`
- Test: `FO4RecordEditor.Tests/Rendering/ElementRendererTests.cs`

`Model.Data` is a byte array shown as 32 rows in the screenshot. Collapse any byte sequence to `"N bytes"`. This is checked on the *collection* before WalkObject expands it (Task 5 wires the check).

- [ ] **Step 1: Write the failing test**

```csharp
[Fact]
public void Summarizes_byte_sequences_as_count()
{
    ElementRenderer.TryRenderByteBlob(new byte[] { 1, 2, 3, 4 }, out var text).Should().BeTrue();
    text.Should().Be("4 bytes");

    ElementRenderer.TryRenderByteBlob(new[] { "a", "b" }, out _).Should().BeFalse(); // not bytes
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~ElementRendererTests.Summarizes_byte_sequences_as_count"`
Expected: FAIL — `TryRenderByteBlob` not defined.

- [ ] **Step 3: Write minimal implementation** (add to `ElementRenderer`)

```csharp
using System.Collections;

// inside ElementRenderer:
public static bool TryRenderByteBlob(object collection, out string text)
{
    text = "";
    if (collection is byte[] ba) { text = $"{ba.Length} bytes"; return true; }
    if (collection is Noggog.MemorySlice<byte> ms) { text = $"{ms.Length} bytes"; return true; }
    if (collection is IEnumerable e and not string)
    {
        int count = 0; bool allBytes = true;
        foreach (var x in e) { if (x is not byte) { allBytes = false; break; } count++; }
        if (allBytes && count > 0) { text = $"{count} bytes"; return true; }
    }
    return false;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~ElementRendererTests.Summarizes_byte_sequences_as_count"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add FO4RecordEditor/Services/Rendering/ElementRenderer.cs FO4RecordEditor.Tests/Rendering/ElementRendererTests.cs
git commit -m "feat(ui): collapse byte blobs (Model.Data) to '<N> bytes'"
```

---

### Task 4: Make MutagenLoader's FormLink/Condition formatters reachable + register them

**Files:**
- Modify: `FO4RecordEditor/Services/MutagenLoader.cs` (change `FormatCondition` from `private` to `internal static`; it already exists from prior work)
- Modify: `FO4RecordEditor/App.xaml.cs` (call `ElementRenderer.Init` at startup)
- Test: none (wiring; covered by Task 5's round-trip)

- [ ] **Step 1: Make the formatters internal**

In `MutagenLoader.cs`, change the signature:
```csharp
internal static string FormatCondition(Mutagen.Bethesda.Fallout4.IConditionGetter cond)
```
(`FormatFormLink` is already `internal static`.)

- [ ] **Step 2: Wire the registry at startup**

In `App.xaml.cs`, inside the application startup (after the existing theme setup), add:
```csharp
FO4RecordEditor.Services.Rendering.ElementRenderer.Init(
    FO4RecordEditor.Services.MutagenLoader.FormatFormLink,
    FO4RecordEditor.Services.MutagenLoader.FormatCondition);
```

- [ ] **Step 3: Build to verify it compiles**

Run: `dotnet build FO4RecordEditor.Tests -c Debug`
Expected: Build succeeded, 0 errors.

- [ ] **Step 4: Commit**

```bash
git add FO4RecordEditor/Services/MutagenLoader.cs FO4RecordEditor/App.xaml.cs
git commit -m "chore(ui): expose + register renderer formatters at startup"
```

---

### Task 5: Route WalkObject through the registry (replace TryFriendlyLine)

**Files:**
- Modify: `FO4RecordEditor/Services/MutagenLoader.cs` (`WalkObject` enumerable + property branches; delete `TryFriendlyLine`)
- Test: `FO4RecordEditor.Tests/WriteToolsTests.cs` (extend existing readability test)

- [ ] **Step 1: Extend the readability test (failing)**

In `WriteToolsTests.GetRecord_RendersConditionsAndComponentsAsReadableLines`, after the existing asserts, add:
```csharp
// A byte blob must collapse, not expand into [0]..[N].
dump.Should().NotContain(".Data[10]");
```
(The COBJ has no Model.Data, so add a second assert in a dedicated test only if a Model is present; otherwise this guards regressions on list expansion.)

- [ ] **Step 2: Replace the enumerable branch in WalkObject**

In `MutagenLoader.WalkObject`, replace the current `TryFriendlyLine(item, ...)` block and the byte-list handling with registry calls. The enumerable branch becomes:
```csharp
else if (val is IEnumerable enumerable and not string)
{
    // Whole-collection collapse first (e.g. a byte blob like Model.Data -> "N bytes").
    if (Services.Rendering.ElementRenderer.TryRenderByteBlob(enumerable, out var blob))
    {
        child.Values[pluginName] = blob;
    }
    else
    {
        int i = 0;
        foreach (var item in enumerable)
        {
            if (item == null) continue;
            if (_walkNodeCount >= MaxWalkNodes) break;
            var entryKey = $"[{i}]";
            var entry = child.GetChild(entryKey);
            if (entry == null)
            {
                entry = new RecordNode { Key = entryKey, Parent = child };
                child.Children.Add(entry);
                _walkNodeCount++;
            }
            if (Services.Rendering.ElementRenderer.TryRenderLine(item, out var friendly))
                entry.Values[pluginName] = friendly;
            else
            {
                entry.Values[pluginName] = BuildSummary(item);
                WalkObject(item, entry, depth + 1, maxDepth, pluginName);
            }
            i++;
        }
    }
}
```

- [ ] **Step 3: Delete the now-unused `TryFriendlyLine`**

Remove the `private static bool TryFriendlyLine(...)` method from `MutagenLoader.cs` (its logic now lives in `ElementRenderer`). Keep `FormatCondition` (now called by the registry).

- [ ] **Step 4: Run the readability + full fast suite**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~WriteToolsTests|FullyQualifiedName~Rendering"`
Expected: PASS (conditions/components still render; byte blobs collapse).

- [ ] **Step 5: Commit**

```bash
git add FO4RecordEditor/Services/MutagenLoader.cs FO4RecordEditor.Tests/WriteToolsTests.cs
git commit -m "feat(ui): route field walk through the renderer registry"
```

---

### Task 6: Apply friendly names in the field/conflict columns

**Files:**
- Modify: `FO4RecordEditor/Services/MutagenLoader.cs` (`FlattenConflictRows` — relabel the leaf `Field` text)
- Modify: `FO4RecordEditor/Views/RecordEditorView.xaml.cs` (Property column already binds `Node.Key`; add a label converter) OR relabel at row build
- Test: `FO4RecordEditor.Tests/Rendering/FriendlyNamesTests.cs` (already covers the map; add a flatten assert)

Keep raw keys on the nodes (edits/paths depend on them); only the **displayed** label is friendly. Simplest: apply `FriendlyNames.Label` to the conflict row's `Field` display segment when it is a property name (not a `[index]`).

- [ ] **Step 1: Write the failing test**

```csharp
[Fact]
public void Conflict_row_uses_friendly_leaf_label()
{
    FriendlyNames.LabelPath("MarkerColor").Should().Be("Marker Color");
    FriendlyNames.LabelPath("Components[0].Component").Should().Be("Components[0].Component"); // dotted path untouched at segment level only
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~FriendlyNamesTests.Conflict_row_uses_friendly_leaf_label"`
Expected: FAIL — `LabelPath` not defined.

- [ ] **Step 3: Implement `LabelPath`** (add to `FriendlyNames`)

```csharp
/// <summary>Relabel only the final path segment when it is a plain property name; leave dotted
/// paths and indices structurally intact so edit paths still resolve.</summary>
public static string LabelPath(string path)
{
    if (path.Contains('.') || path.Contains('[')) return path;
    return Label(path);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `dotnet test FO4RecordEditor.Tests --filter "FullyQualifiedName~FriendlyNamesTests"`
Expected: PASS.

- [ ] **Step 5: Apply in the UI Property column**

In `RecordEditorView.xaml`, the Property column `TextBlock` binds `Node.Key`. Add a converter `FriendlyNameConverter` (new file `FO4RecordEditor/Views/FriendlyNameConverter.cs`) that calls `FriendlyNames.Label`, and bind through it. Code:
```csharp
using System.Globalization;
using System.Windows.Data;
namespace FO4RecordEditor.Views;
public sealed class FriendlyNameConverter : IValueConverter
{
    public object Convert(object value, Type t, object p, CultureInfo c) =>
        FO4RecordEditor.Services.Rendering.FriendlyNames.Label(value as string ?? "");
    public object ConvertBack(object value, Type t, object p, CultureInfo c) => value;
}
```
Register it in `RecordEditorView.xaml` `<UserControl.Resources>` and wrap the `Text="{Binding Node.Key}"` binding with `Converter={StaticResource Friendly}`.

- [ ] **Step 6: Build + commit**

Run: `dotnet build FO4RecordEditor -c Debug`
Expected: Build succeeded.
```bash
git add FO4RecordEditor/Services/Rendering/FriendlyNames.cs FO4RecordEditor.Tests/Rendering/FriendlyNamesTests.cs FO4RecordEditor/Views/FriendlyNameConverter.cs FO4RecordEditor/Views/RecordEditorView.xaml
git commit -m "feat(ui): show friendly field labels in the record/conflict columns"
```

---

### Task 7: Full suite + manual run verification

- [ ] **Step 1: Run the whole suite**

Run: `dotnet test FO4RecordEditor.Tests`
Expected: all green (live-MO2 tests skip if the modlist isn't present).

- [ ] **Step 2: Republish (only if the editor is closed)**

Run: `publish.bat`
Expected: "Published to folder …". (Do NOT run while the editor is open — the exe is locked.)

- [ ] **Step 3: Manual check**

Launch the publish exe, open a record with a Model and a color (e.g. an Activator like `FallingDust`). Verify:
- `Model.Data` shows one `… bytes` row, not `[0]..[31]`.
- `MarkerColor` shows one `#RRGGBB` row, not R/G/B/A/IsKnownColor/...
- Conditions/components still show readable one-liners.
- Column headers/labels read "Editor ID", "Created Object", etc.

---

## Self-Review

**Spec coverage:** This plan implements spec §4 (readable rendering: conditions/components done before, now color, byte blobs, friendly names, registry + fallback) and the readability half of §3's "human-readable" requirement. Spec §1 (shell) is already scaffolded in `MainWindow.xaml`; §3 inline-edit and §5 conflict-status colors are deferred to the next plan (`phase3-editable-matrix`). Spec §2 theme polish is deferred to a small follow-up.

**Placeholder scan:** No TBD/TODO; every code step has concrete code and a run command.

**Type consistency:** `ElementRenderer.TryRenderLine`/`TryRenderByteBlob`, `FriendlyNames.Label`/`LabelPath`, `ElementRenderer.Init(formatFormLink, formatCondition)` are used consistently across tasks. `FormatCondition` is made `internal` in Task 4 before being referenced in Task 4's Init call.

**Note on order:** Tasks 1–3 add pure, tested functions; Task 4 wires them; Task 5 flips WalkObject over to them and deletes the old path; Task 6 adds display labels; Task 7 verifies end to end. The app builds and tests pass after every task.
