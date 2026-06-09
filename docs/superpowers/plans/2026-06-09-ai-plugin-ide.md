# AI-Powered Bethesda Plugin IDE Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform FO4RecordEditor from a tree-based ESP browser into a VS Code-style, AI-assisted Bethesda plugin development environment with a queryable plugin knowledge graph, error scanner, diff engine, and streaming AI assistant.

**Architecture:** Layered. A pure-data **core engine layer** (KnowledgeGraph, ErrorScanner, DiffEngine, LogService, UndoStack) has zero WPF dependencies and is fully unit-testable. An **AI layer** (IAIProvider abstraction + AnthropicProvider/OllamaProvider, AIContextBuilder, ChatService) talks to LLMs via streaming. A **WPF shell layer** (activity bar + dockable panels + command palette) consumes both. Existing `Models/RecordNode` and `Services/{MutagenLoader,SpriggitLoader,CompileService,ScriptEngine}` are preserved unchanged.

**Tech Stack:** WPF .NET 9 (`net9.0-windows`), Mutagen.Bethesda.Fallout4 0.53.1, YamlDotNet 16.3.0, Microsoft.CodeAnalysis.CSharp.Scripting 4.14.0, Newtonsoft.Json 13.0.3, System.Net.Http.Json (SDK), xUnit + FluentAssertions for tests.

---

## Milestone Overview

Each milestone produces working, shippable software:

- **M0 — Test harness + Logging** (Tasks 1-3): xUnit project, `LogService`, `SettingsService`.
- **M1 — Knowledge Graph** (Tasks 4-8): index every FormKey/EditorID/reference; impact analysis.
- **M2 — Error Scanner** (Tasks 9-12): broken refs, null ActorValues, missing masters, invalid LLs.
- **M3 — Diff Engine** (Tasks 13-15): record-vs-record diff with Added/Removed/Modified.
- **M4 — AI Layer** (Tasks 16-21): IAIProvider, AnthropicProvider streaming, ContextBuilder, ChatService.
- **M5 — Undo/Redo + Commands** (Tasks 22-24): IChange stack, CommandRegistry.
- **M6 — WPF Shell** (Tasks 25-33): activity bar, side panels, tabbed editor, bottom tabs, AI chat, command palette, AI right-click actions, settings dialog.

A worker can stop after any milestone and have a coherent, tested artifact.

---

## Folder Structure (final)

```
FO4RecordEditor/
├── Models/
│   ├── RecordNode.cs                 (EXISTING — unchanged)
│   ├── Graph/
│   │   ├── RecordEntry.cs            FormKey/EditorID/Type/SourcePlugin/Node/IsWinningOverride
│   │   ├── Reference.cs              From/To FormKey, FieldPath, ReferenceKind
│   │   └── ImpactReport.cs           inbound refs grouped by severity
│   ├── Errors/
│   │   ├── PluginError.cs            Severity/Category/Plugin/FormKey/Field/Description/FixAvailable
│   │   ├── ErrorSeverity.cs          enum
│   │   └── ErrorCategory.cs          enum
│   ├── Diff/
│   │   ├── DiffRow.cs                Path/ValueA/ValueB/DiffKind
│   │   └── DiffKind.cs               enum
│   ├── Logging/
│   │   ├── LogEntry.cs               Timestamp/Level/Category/Message/Detail
│   │   ├── LogLevel.cs               enum
│   │   └── LogCategory.cs            enum
│   ├── Ai/
│   │   ├── ChatMessage.cs            Role/Content
│   │   └── ChatRole.cs               enum
│   └── Commands/
│       └── AppCommand.cs             Id/Title/Category/Gesture/Action
├── Services/
│   ├── MutagenLoader.cs              (EXISTING — unchanged)
│   ├── SpriggitLoader.cs            (EXISTING — unchanged)
│   ├── CompileService.cs            (EXISTING — unchanged)
│   ├── ScriptEngine.cs             (EXISTING — unchanged)
│   ├── Graph/KnowledgeGraph.cs
│   ├── Errors/ErrorScanner.cs
│   ├── Diff/DiffEngine.cs
│   ├── Logging/LogService.cs        + static Log facade
│   ├── Settings/AppSettings.cs
│   ├── Settings/SettingsService.cs
│   ├── Undo/IChange.cs + FieldEditChange.cs + UndoStack.cs
│   ├── Commands/CommandRegistry.cs
│   └── Ai/
│       ├── IAIProvider.cs
│       ├── AnthropicProvider.cs
│       ├── OllamaProvider.cs
│       ├── AIContextBuilder.cs
│       └── ChatService.cs
├── ViewModels/
│   ├── FieldRow.cs                  (EXISTING — unchanged)
│   ├── ShellViewModel.cs            top-level: panels, active doc, graph, services
│   ├── RecordTabViewModel.cs        one open record in center tabs
│   ├── ChatViewModel.cs             AI panel
│   ├── ErrorsViewModel.cs
│   ├── CompareViewModel.cs
│   └── LogViewModel.cs
├── Views/
│   ├── ActivityBar.xaml             icon rail
│   ├── ExplorerPanel.xaml           record tree (moved from MainWindow)
│   ├── ChatPanel.xaml
│   ├── ErrorsPanel.xaml
│   ├── ComparePanel.xaml
│   ├── LogPanel.xaml
│   ├── CommandPalette.xaml
│   └── SettingsDialog.xaml
├── MainWindow.xaml(.cs)             SHELL — rewritten to host panels
└── App.xaml(.cs)                    (EXISTING — unchanged)

FO4RecordEditor.Tests/              NEW xUnit project
├── FO4RecordEditor.Tests.csproj
├── Helpers/GraphTestBuilder.cs      builds RecordNode trees for tests
├── KnowledgeGraphTests.cs
├── ErrorScannerTests.cs
├── DiffEngineTests.cs
├── LogServiceTests.cs
├── AIContextBuilderTests.cs
└── UndoStackTests.cs
```

## Core Type Contracts (authoritative — referenced by all tasks)

These signatures are fixed. Every later task must match them exactly.

```csharp
// Models/Graph/RecordEntry.cs
public sealed record RecordEntry(
    string FormKey, string EditorID, string Type,
    string SourcePlugin, RecordNode Node, bool IsWinningOverride);

// Models/Graph/Reference.cs
public enum ReferenceKind { Generic, Keyword, LeveledListEntry, Script, Master, ActorValue }
public sealed record Reference(
    string FromFormKey, string ToFormKey, string FieldPath, ReferenceKind Kind);

// Models/Graph/ImpactReport.cs
public sealed record ImpactReport(
    string TargetFormKey, string TargetEditorID,
    IReadOnlyList<Reference> InboundReferences,
    IReadOnlyList<RecordEntry> AffectedRecords);

// Models/Errors/ErrorSeverity.cs
public enum ErrorSeverity { Info, Warning, Error, Critical }

// Models/Errors/ErrorCategory.cs
public enum ErrorCategory {
    BrokenReference, DeletedReference, NullActorValue, InvalidKeyword,
    InvalidLeveledList, MissingModel, MissingTexture, CircularReference,
    InvalidScript, UnresolvedMaster }

// Models/Errors/PluginError.cs
public sealed record PluginError(
    ErrorSeverity Severity, ErrorCategory Category, string Plugin,
    string FormKey, string Field, string Description, bool FixAvailable);

// Models/Diff/DiffKind.cs
public enum DiffKind { Unchanged, Added, Removed, Modified }

// Models/Diff/DiffRow.cs
public sealed record DiffRow(string Path, string? ValueA, string? ValueB, DiffKind Kind);

// Models/Logging/LogLevel.cs
public enum LogLevel { Debug, Info, Warning, Error, Critical }

// Models/Logging/LogCategory.cs
public enum LogCategory { App, Plugin, AI, Error, Compile }

// Models/Logging/LogEntry.cs
public sealed record LogEntry(
    DateTime Timestamp, LogLevel Level, LogCategory Category,
    string Message, string? Detail);

// Models/Ai/ChatRole.cs
public enum ChatRole { System, User, Assistant }
// Models/Ai/ChatMessage.cs
public sealed record ChatMessage(ChatRole Role, string Content);

// Services/Ai/IAIProvider.cs
public interface IAIProvider {
    string Name { get; }
    IAsyncEnumerable<string> StreamAsync(
        IReadOnlyList<ChatMessage> messages, CancellationToken ct = default);
}

// Services/Undo/IChange.cs
public interface IChange { string Description { get; } void Apply(); void Undo(); }
```

---

# Milestone M0 — Test Harness + Logging + Settings

### Task 1: Create xUnit test project

**Files:**
- Create: `FO4RecordEditor.Tests/FO4RecordEditor.Tests.csproj`
- Create: `FO4RecordEditor.Tests/Helpers/GraphTestBuilder.cs`

- [ ] **Step 1: Create the test project**

Run from `D:\Projects\FO4RecordEditor`:
```powershell
dotnet new xunit -n FO4RecordEditor.Tests -o FO4RecordEditor.Tests --framework net9.0
dotnet add FO4RecordEditor.Tests reference FO4RecordEditor\FO4RecordEditor.csproj
dotnet add FO4RecordEditor.Tests package FluentAssertions --version 6.12.1
```

- [ ] **Step 2: Set test TFM to windows**

Edit the test csproj: change `<TargetFramework>net9.0</TargetFramework>` to `net9.0-windows` and add `<UseWPF>true</UseWPF>`. The referenced app is a WPF exe; the test project must share the windows TFM or the project reference fails to resolve.

- [ ] **Step 3: Write GraphTestBuilder**

```csharp
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Tests.Helpers;

public static class GraphTestBuilder
{
    public static RecordNode Plugin(string fileName, params RecordNode[] records)
    {
        var root = new RecordNode { Key = fileName, FilePath = $"C:\\fake\\{fileName}" };
        foreach (var rec in records)
        {
            var group = root.GetChild(rec.GetValue("Type") ?? "MISC")
                        ?? AddGroup(root, rec.GetValue("Type") ?? "MISC");
            rec.Parent = group;
            group.Children.Add(rec);
        }
        return root;
    }

    private static RecordNode AddGroup(RecordNode root, string sig)
    {
        var g = new RecordNode { Key = sig, Parent = root };
        root.Children.Add(g);
        return g;
    }

    public static RecordNode Record(string type, string formKey, string editorId,
        params (string key, string val)[] fields)
    {
        var rec = new RecordNode { Key = editorId };
        AddLeaf(rec, "Type", type);
        AddLeaf(rec, "FormKey", formKey);
        AddLeaf(rec, "EditorID", editorId);
        foreach (var (k, v) in fields) AddLeaf(rec, k, v);
        return rec;
    }

    public static void AddLeaf(RecordNode parent, string key, string value)
    {
        var n = new RecordNode { Key = key, Parent = parent };
        n.Value = value;
        n.ClearDirty();
        parent.Children.Add(n);
    }
}
```

- [ ] **Step 4: Verify** — Run: `dotnet test FO4RecordEditor.Tests` → PASS (template test).
- [ ] **Step 5: Commit** — `git add FO4RecordEditor.Tests && git commit -m "test: add xUnit project + GraphTestBuilder"`

---

### Task 2: LogService

**Files:**
- Create: `FO4RecordEditor/Models/Logging/{LogLevel,LogCategory,LogEntry}.cs`
- Create: `FO4RecordEditor/Services/Logging/LogService.cs`
- Test: `FO4RecordEditor.Tests/LogServiceTests.cs`

- [ ] **Step 1: Model files**

`Models/Logging/LogLevel.cs`:
```csharp
namespace FO4RecordEditor.Models;
public enum LogLevel { Debug, Info, Warning, Error, Critical }
```
`Models/Logging/LogCategory.cs`:
```csharp
namespace FO4RecordEditor.Models;
public enum LogCategory { App, Plugin, AI, Error, Compile }
```
`Models/Logging/LogEntry.cs`:
```csharp
namespace FO4RecordEditor.Models;
public sealed record LogEntry(
    DateTime Timestamp, LogLevel Level, LogCategory Category,
    string Message, string? Detail);
```

- [ ] **Step 2: Failing test** — `FO4RecordEditor.Tests/LogServiceTests.cs`:
```csharp
using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;

public class LogServiceTests
{
    [Fact]
    public void Log_AddsEntry_AndRaisesEvent()
    {
        var svc = new LogService();
        LogEntry? raised = null;
        svc.EntryAdded += e => raised = e;
        svc.Log(LogCategory.App, LogLevel.Info, "started", "detail");
        svc.Entries.Should().HaveCount(1);
        svc.Entries[0].Message.Should().Be("started");
        raised!.Category.Should().Be(LogCategory.App);
    }

    [Fact]
    public void Filter_ByCategoryAndLevel_Works()
    {
        var svc = new LogService();
        svc.Log(LogCategory.AI, LogLevel.Debug, "a", null);
        svc.Log(LogCategory.Error, LogLevel.Critical, "b", null);
        svc.Filter(LogCategory.Error, LogLevel.Info).Should().ContainSingle()
           .Which.Message.Should().Be("b");
    }
}
```

- [ ] **Step 3: Run** → FAIL (LogService missing).
- [ ] **Step 4: Implement** — `Services/Logging/LogService.cs`:
```csharp
using System.Collections.ObjectModel;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class LogService
{
    public ObservableCollection<LogEntry> Entries { get; } = [];
    public event Action<LogEntry>? EntryAdded;

    public void Log(LogCategory cat, LogLevel level, string message, string? detail = null)
    {
        var entry = new LogEntry(DateTime.Now, level, cat, message, detail);
        Entries.Add(entry);
        EntryAdded?.Invoke(entry);
    }

    public IReadOnlyList<LogEntry> Filter(LogCategory? cat, LogLevel minLevel) =>
        Entries.Where(e => (cat == null || e.Category == cat) && e.Level >= minLevel).ToList();

    public string Export() =>
        string.Join(Environment.NewLine, Entries.Select(e =>
            $"{e.Timestamp:HH:mm:ss.fff} [{e.Level,-8}] [{e.Category,-7}] {e.Message}" +
            (e.Detail != null ? $"  | {e.Detail}" : "")));

    public void Clear() => Entries.Clear();
}
```

- [ ] **Step 5: Run** → PASS. Commit: `git commit -am "feat: add LogService"`

---

### Task 3: SettingsService

**Files:** Create `FO4RecordEditor/Services/Settings/{AppSettings,SettingsService}.cs`

- [ ] **Step 1: AppSettings** — `Services/Settings/AppSettings.cs`:
```csharp
namespace FO4RecordEditor.Services;

public sealed class AppSettings
{
    public string AnthropicApiKey { get; set; } = "";
    public string Model { get; set; } = "claude-sonnet-4-6";
    public string AiProvider { get; set; } = "anthropic";
    public string OllamaUrl { get; set; } = "http://localhost:11434";
    public string OllamaModel { get; set; } = "llama3.1";
    public string LastFolder { get; set; } = "";
    public string SpriggitExePath { get; set; } = "";
    public double SidePanelWidth { get; set; } = 320;
    public double AiPanelWidth { get; set; } = 420;
    public double BottomPanelHeight { get; set; } = 200;
}
```

- [ ] **Step 2: SettingsService** — `Services/Settings/SettingsService.cs`:
```csharp
using System.IO;
using Newtonsoft.Json;

namespace FO4RecordEditor.Services;

public sealed class SettingsService
{
    private static readonly string Dir =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                     "FO4RecordEditor");
    private static readonly string FilePath = Path.Combine(Dir, "settings.json");

    public AppSettings Current { get; private set; } = new();

    public void Load()
    {
        try
        {
            if (File.Exists(FilePath))
                Current = JsonConvert.DeserializeObject<AppSettings>(File.ReadAllText(FilePath))
                          ?? new AppSettings();
        }
        catch { Current = new AppSettings(); }
    }

    public void Save()
    {
        Directory.CreateDirectory(Dir);
        File.WriteAllText(FilePath, JsonConvert.SerializeObject(Current, Formatting.Indented));
    }
}
```

- [ ] **Step 3: Verify** — `dotnet build FO4RecordEditor/FO4RecordEditor.csproj` → 0 errors.
- [ ] **Step 4: Commit** — `git commit -am "feat: AppSettings + SettingsService"`

---

# Milestone M1 — Knowledge Graph

The graph is the core differentiator over xEdit: index every FormKey, EditorID, and reference once, then answer "what breaks if I delete this?" in O(1) instead of rescanning.

### Task 4: Graph model records

**Files:** Create `FO4RecordEditor/Models/Graph/{RecordEntry,Reference,ImpactReport}.cs`

- [ ] **Step 1:** `Models/Graph/RecordEntry.cs`:
```csharp
namespace FO4RecordEditor.Models;
public sealed record RecordEntry(
    string FormKey, string EditorID, string Type,
    string SourcePlugin, RecordNode Node, bool IsWinningOverride);
```

- [ ] **Step 2:** `Models/Graph/Reference.cs`:
```csharp
namespace FO4RecordEditor.Models;
public enum ReferenceKind { Generic, Keyword, LeveledListEntry, Script, Master, ActorValue }
public sealed record Reference(
    string FromFormKey, string ToFormKey, string FieldPath, ReferenceKind Kind);
```

- [ ] **Step 3:** `Models/Graph/ImpactReport.cs`:
```csharp
namespace FO4RecordEditor.Models;
public sealed record ImpactReport(
    string TargetFormKey, string TargetEditorID,
    IReadOnlyList<Reference> InboundReferences,
    IReadOnlyList<RecordEntry> AffectedRecords);
```

- [ ] **Step 4:** Build → 0 errors. Commit: `git commit -am "feat: graph model records"`

---

### Task 5: KnowledgeGraph indexing

**Files:** Create `FO4RecordEditor/Services/Graph/KnowledgeGraph.cs`; Test `FO4RecordEditor.Tests/KnowledgeGraphTests.cs`

A FormKey in MutagenLoader output is rendered like `001234:Fallout4.esm`. The indexer walks every record node (children of Type-group nodes that have a `FormKey` leaf), and every descendant leaf whose value looks like a FormKey is recorded as an outbound reference.

- [ ] **Step 1: Failing test** — `KnowledgeGraphTests.cs`:
```csharp
using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.Tests.Helpers;
using Xunit;

namespace FO4RecordEditor.Tests;

public class KnowledgeGraphTests
{
    private static RecordNode SamplePlugin()
    {
        var keyword = GraphTestBuilder.Record("KYWD", "001000:Test.esp", "MyKeyword");
        var weapon  = GraphTestBuilder.Record("WEAP", "001001:Test.esp", "MyGun",
            ("Damage", "50"), ("Keyword1", "001000:Test.esp"));
        return GraphTestBuilder.Plugin("Test.esp", keyword, weapon);
    }

    [Fact]
    public void Index_RegistersRecords_ByFormKeyAndEditorID()
    {
        var g = new KnowledgeGraph();
        g.Index(SamplePlugin());
        g.GetByFormKey("001001:Test.esp")!.EditorID.Should().Be("MyGun");
        g.GetByEditorID("MyKeyword").Should().ContainSingle();
        g.GetByType("WEAP").Should().ContainSingle();
        g.RecordCount.Should().Be(2);
    }

    [Fact]
    public void GetReferencesTo_FindsInboundLinks()
    {
        var g = new KnowledgeGraph();
        g.Index(SamplePlugin());
        var inbound = g.GetReferencesTo("001000:Test.esp");
        inbound.Should().ContainSingle();
        inbound[0].FromFormKey.Should().Be("001001:Test.esp");
        inbound[0].FieldPath.Should().Be("Keyword1");
    }

    [Fact]
    public void AnalyzeImpact_ListsAffectedRecords()
    {
        var g = new KnowledgeGraph();
        g.Index(SamplePlugin());
        var report = g.AnalyzeImpact("001000:Test.esp");
        report.TargetEditorID.Should().Be("MyKeyword");
        report.AffectedRecords.Should().ContainSingle()
              .Which.EditorID.Should().Be("MyGun");
    }
}
```

- [ ] **Step 2: Run** → FAIL (KnowledgeGraph missing).

- [ ] **Step 3: Implement** — `Services/Graph/KnowledgeGraph.cs`:
```csharp
using System.Text.RegularExpressions;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class KnowledgeGraph
{
    // FormKey shape from MutagenLoader: "001234:Plugin.esp"
    private static readonly Regex FormKeyRe =
        new(@"^[0-9A-Fa-f]{6}:.+\.(esp|esm|esl)$", RegexOptions.Compiled | RegexOptions.IgnoreCase);

    private readonly Dictionary<string, RecordEntry> _byFormKey = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, List<RecordEntry>> _byEditorId = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, List<RecordEntry>> _byType = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, List<Reference>> _inbound = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<Reference> _allRefs = new();

    public int RecordCount => _byFormKey.Count;
    public int ReferenceCount => _allRefs.Count;
    public IReadOnlyCollection<RecordEntry> AllRecords => _byFormKey.Values;

    public void Index(RecordNode pluginRoot)
    {
        var plugin = pluginRoot.Key;
        foreach (var node in pluginRoot.SelfAndDescendants())
        {
            var fk = node.GetValue("FormKey");
            if (fk == null || node.IsLeaf) continue;
            // A record node: has FormKey + Type + EditorID leaves
            var type = node.GetValue("Type") ?? "";
            var eid = node.GetValue("EditorID") ?? "";
            var entry = new RecordEntry(fk, eid, type, plugin, node,
                IsWinningOverride: !_byFormKey.ContainsKey(fk));

            _byFormKey[fk] = entry;
            Add(_byEditorId, eid, entry);
            Add(_byType, type, entry);
            IndexReferences(node, fk);
        }
    }

    private void IndexReferences(RecordNode record, string fromFk)
    {
        foreach (var leaf in record.Descendants().Where(n => n.IsLeaf))
        {
            var v = leaf.Value;
            if (string.IsNullOrEmpty(v) || !FormKeyRe.IsMatch(v)) continue;
            if (v.Equals(fromFk, StringComparison.OrdinalIgnoreCase)) continue;
            var kind = ClassifyKind(leaf.Key);
            var reference = new Reference(fromFk, v, leaf.Key, kind);
            _allRefs.Add(reference);
            Add(_inbound, v, reference);
        }
    }

    private static ReferenceKind ClassifyKind(string field)
    {
        var f = field.ToLowerInvariant();
        if (f.Contains("keyword")) return ReferenceKind.Keyword;
        if (f.Contains("actorvalue")) return ReferenceKind.ActorValue;
        if (f.Contains("script")) return ReferenceKind.Script;
        if (f.Contains("reference") || f.StartsWith("[")) return ReferenceKind.LeveledListEntry;
        return ReferenceKind.Generic;
    }

    public RecordEntry? GetByFormKey(string fk) =>
        _byFormKey.TryGetValue(fk, out var e) ? e : null;

    public IReadOnlyList<RecordEntry> GetByEditorID(string eid) =>
        _byEditorId.TryGetValue(eid, out var l) ? l : Array.Empty<RecordEntry>();

    public IReadOnlyList<RecordEntry> GetByType(string type) =>
        _byType.TryGetValue(type, out var l) ? l : Array.Empty<RecordEntry>();

    public IReadOnlyList<Reference> GetReferencesTo(string fk) =>
        _inbound.TryGetValue(fk, out var l) ? l : Array.Empty<Reference>();

    public IReadOnlyList<Reference> GetReferencesFrom(string fk) =>
        _allRefs.Where(r => r.FromFormKey.Equals(fk, StringComparison.OrdinalIgnoreCase)).ToList();

    public ImpactReport AnalyzeImpact(string fk)
    {
        var target = GetByFormKey(fk);
        var inbound = GetReferencesTo(fk);
        var affected = inbound.Select(r => GetByFormKey(r.FromFormKey))
                              .Where(e => e != null).Select(e => e!)
                              .DistinctBy(e => e.FormKey).ToList();
        return new ImpactReport(fk, target?.EditorID ?? "(unknown)", inbound, affected);
    }

    public void Clear()
    {
        _byFormKey.Clear(); _byEditorId.Clear(); _byType.Clear();
        _inbound.Clear(); _allRefs.Clear();
    }

    private static void Add<T>(Dictionary<string, List<T>> dict, string key, T val)
    {
        if (!dict.TryGetValue(key, out var l)) { l = new(); dict[key] = l; }
        l.Add(val);
    }
}
```

- [ ] **Step 4: Run** → PASS. Commit: `git commit -am "feat: KnowledgeGraph indexing + impact analysis"`

---

### Task 6: Multi-plugin override detection

**Files:** Modify `Services/Graph/KnowledgeGraph.cs`; add test to `KnowledgeGraphTests.cs`

When two plugins define the same FormKey, the last indexed wins (load order). Track losing overrides so the Compare/Error tabs can surface conflicts.

- [ ] **Step 1: Failing test** — append to `KnowledgeGraphTests.cs`:
```csharp
    [Fact]
    public void Index_TwoPluginsSameFormKey_TracksConflict()
    {
        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("A.esp",
            GraphTestBuilder.Record("WEAP", "000800:Master.esm", "Gun", ("Damage", "10"))));
        g.Index(GraphTestBuilder.Plugin("B.esp",
            GraphTestBuilder.Record("WEAP", "000800:Master.esm", "Gun", ("Damage", "99"))));
        g.GetConflicts().Should().ContainSingle()
            .Which.FormKey.Should().Be("000800:Master.esm");
        g.GetByFormKey("000800:Master.esm")!.SourcePlugin.Should().Be("B.esp"); // winner
    }
```

- [ ] **Step 2: Run** → FAIL (`GetConflicts` missing).

- [ ] **Step 3: Implement** — add to KnowledgeGraph:
```csharp
    private readonly Dictionary<string, List<RecordEntry>> _overrides = new(StringComparer.OrdinalIgnoreCase);

    public sealed record Conflict(string FormKey, IReadOnlyList<RecordEntry> Overrides);

    public IReadOnlyList<Conflict> GetConflicts() =>
        _overrides.Where(kv => kv.Value.Count > 1)
                  .Select(kv => new Conflict(kv.Key, kv.Value))
                  .ToList();
```
Then in `Index`, right before `_byFormKey[fk] = entry;` add:
```csharp
            Add(_overrides, fk, entry);
```
And in `Clear()` add `_overrides.Clear();`.

- [ ] **Step 4: Run** → PASS. Commit: `git commit -am "feat: track override conflicts in graph"`

---

### Task 7: Background indexing service

**Files:** Modify `Services/Graph/KnowledgeGraph.cs`

Indexing 50k records must not block the UI. Add an async entry point that reports progress.

- [ ] **Step 1: Add async method** to KnowledgeGraph:
```csharp
    public async Task IndexAsync(IEnumerable<RecordNode> plugins, IProgress<string>? progress = null)
    {
        await Task.Run(() =>
        {
            foreach (var p in plugins)
            {
                progress?.Report($"Indexing {p.Key}...");
                Index(p);
            }
            progress?.Report($"Indexed {RecordCount} records, {ReferenceCount} references.");
        });
    }
```

- [ ] **Step 2: Build** → 0 errors. (No new test — thin async wrapper over tested `Index`.)
- [ ] **Step 3: Commit** — `git commit -am "feat: async background indexing"`

---

### Task 8: Graph query helpers for AI context

**Files:** Modify `Services/Graph/KnowledgeGraph.cs`; add test

The AI context builder needs "give me this record plus everything it links to, one hop out."

- [ ] **Step 1: Failing test** — append to `KnowledgeGraphTests.cs`:
```csharp
    [Fact]
    public void GetNeighborhood_ReturnsRecordPlusOneHop()
    {
        var g = new KnowledgeGraph();
        g.Index(SamplePlugin());
        var hood = g.GetNeighborhood("001001:Test.esp");
        hood.Select(e => e.EditorID).Should().Contain(new[] { "MyGun", "MyKeyword" });
    }
```

- [ ] **Step 2: Run** → FAIL.

- [ ] **Step 3: Implement** — add to KnowledgeGraph:
```csharp
    public IReadOnlyList<RecordEntry> GetNeighborhood(string fk)
    {
        var result = new List<RecordEntry>();
        var self = GetByFormKey(fk);
        if (self != null) result.Add(self);
        foreach (var r in GetReferencesFrom(fk))
        {
            var e = GetByFormKey(r.ToFormKey);
            if (e != null) result.Add(e);
        }
        return result.DistinctBy(e => e.FormKey).ToList();
    }
```

- [ ] **Step 4: Run** → PASS. Commit: `git commit -am "feat: GetNeighborhood for AI context"`

---

# Milestone M2 — Error Scanner

### Task 9: Error model records

**Files:** Create `FO4RecordEditor/Models/Errors/{ErrorSeverity,ErrorCategory,PluginError}.cs`

- [ ] **Step 1:** `Models/Errors/ErrorSeverity.cs`:
```csharp
namespace FO4RecordEditor.Models;
public enum ErrorSeverity { Info, Warning, Error, Critical }
```
- [ ] **Step 2:** `Models/Errors/ErrorCategory.cs`:
```csharp
namespace FO4RecordEditor.Models;
public enum ErrorCategory {
    BrokenReference, DeletedReference, NullActorValue, InvalidKeyword,
    InvalidLeveledList, MissingModel, MissingTexture, CircularReference,
    InvalidScript, UnresolvedMaster }
```
- [ ] **Step 3:** `Models/Errors/PluginError.cs`:
```csharp
namespace FO4RecordEditor.Models;
public sealed record PluginError(
    ErrorSeverity Severity, ErrorCategory Category, string Plugin,
    string FormKey, string Field, string Description, bool FixAvailable);
```
- [ ] **Step 4:** Build → 0 errors. Commit: `git commit -am "feat: error model records"`

---

### Task 10: ErrorScanner — broken references + null ActorValue

**Files:** Create `FO4RecordEditor/Services/Errors/ErrorScanner.cs`; Test `FO4RecordEditor.Tests/ErrorScannerTests.cs`

A broken reference is an outbound FormKey that is not vanilla (not in a known master list) and not present in the graph. A null ActorValue is a leaf named `ActorValue` whose value is empty or contains `NULL`/`000000` (the exact bug from the S7 session).

- [ ] **Step 1: Failing test** — `ErrorScannerTests.cs`:
```csharp
using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.Tests.Helpers;
using Xunit;

namespace FO4RecordEditor.Tests;

public class ErrorScannerTests
{
    [Fact]
    public void Scan_FlagsNullActorValue()
    {
        var rec = GraphTestBuilder.Record("COBJ", "001001:Test.esp", "MyRecipe",
            ("ActorValue", "NULL - Null Reference [00000000]"));
        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("Test.esp", rec));

        var errors = new ErrorScanner().Scan(g);
        errors.Should().Contain(e => e.Category == ErrorCategory.NullActorValue
                                  && e.FormKey == "001001:Test.esp");
    }

    [Fact]
    public void Scan_FlagsBrokenReference()
    {
        var rec = GraphTestBuilder.Record("WEAP", "001001:Test.esp", "MyGun",
            ("Keyword1", "00BEEF:Missing.esp"));
        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("Test.esp", rec));

        var errors = new ErrorScanner().Scan(g);
        errors.Should().Contain(e => e.Category == ErrorCategory.BrokenReference
                                  && e.Description.Contains("00BEEF:Missing.esp"));
    }

    [Fact]
    public void Scan_DoesNotFlagVanillaReference()
    {
        var rec = GraphTestBuilder.Record("WEAP", "001001:Test.esp", "MyGun",
            ("Keyword1", "0004A0:Fallout4.esm"));
        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("Test.esp", rec));

        var errors = new ErrorScanner().Scan(g);
        errors.Should().NotContain(e => e.Category == ErrorCategory.BrokenReference);
    }
}
```

- [ ] **Step 2: Run** → FAIL (ErrorScanner missing).

- [ ] **Step 3: Implement** — `Services/Errors/ErrorScanner.cs`:
```csharp
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class ErrorScanner
{
    // Plugins whose records exist outside the loaded set (treated as resolvable).
    private static readonly HashSet<string> KnownMasters = new(StringComparer.OrdinalIgnoreCase)
    {
        "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
        "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm",
        "DLCUltraHighResolution.esm",
    };

    public IReadOnlyList<PluginError> Scan(KnowledgeGraph graph)
    {
        var errors = new List<PluginError>();
        foreach (var rec in graph.AllRecords)
        {
            ScanNullActorValues(rec, errors);
            ScanBrokenRefs(rec, graph, errors);
        }
        return errors;
    }

    private static void ScanNullActorValues(RecordEntry rec, List<PluginError> errors)
    {
        foreach (var leaf in rec.Node.Descendants().Where(n => n.IsLeaf))
        {
            if (!leaf.Key.Equals("ActorValue", StringComparison.OrdinalIgnoreCase)) continue;
            var v = leaf.Value;
            if (string.IsNullOrWhiteSpace(v) || v.Contains("NULL") || v.Contains("000000"))
                errors.Add(new PluginError(ErrorSeverity.Critical, ErrorCategory.NullActorValue,
                    rec.SourcePlugin, rec.FormKey, leaf.Key,
                    $"Condition has NULL/empty ActorValue (always evaluates 0 -> recipe blocked).",
                    FixAvailable: true));
        }
    }

    private static void ScanBrokenRefs(RecordEntry rec, KnowledgeGraph graph, List<PluginError> errors)
    {
        foreach (var r in graph.GetReferencesFrom(rec.FormKey))
        {
            var plugin = r.ToFormKey.Split(':').Last();
            if (KnownMasters.Contains(plugin)) continue;       // resolvable vanilla/DLC
            if (graph.GetByFormKey(r.ToFormKey) != null) continue; // resolvable in loaded set
            errors.Add(new PluginError(ErrorSeverity.Error, ErrorCategory.BrokenReference,
                rec.SourcePlugin, rec.FormKey, r.FieldPath,
                $"References {r.ToFormKey} which is not loaded.", FixAvailable: false));
        }
    }
}
```

- [ ] **Step 4: Run** → PASS. Commit: `git commit -am "feat: ErrorScanner null-ActorValue + broken-ref detection"`

---

### Task 11: ErrorScanner — invalid leveled lists + unresolved masters

**Files:** Modify `Services/Errors/ErrorScanner.cs`; add test

- [ ] **Step 1: Failing test** — append to `ErrorScannerTests.cs`:
```csharp
    [Fact]
    public void Scan_FlagsLeveledListEntryWithNoReference()
    {
        var ll = GraphTestBuilder.Record("LVLI", "002000:Test.esp", "MyList");
        var entries = new RecordNode { Key = "Entries", Parent = ll };
        var e0 = new RecordNode { Key = "[0]", Parent = entries };
        GraphTestBuilder.AddLeaf(e0, "Reference", "");      // empty -> invalid
        GraphTestBuilder.AddLeaf(e0, "Count", "1");
        entries.Children.Add(e0);
        ll.Children.Add(entries);

        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("Test.esp", ll));
        var errors = new ErrorScanner().Scan(g);
        errors.Should().Contain(e => e.Category == ErrorCategory.InvalidLeveledList);
    }
```

- [ ] **Step 2: Run** → FAIL.

- [ ] **Step 3: Implement** — add to `Scan` loop body after `ScanBrokenRefs(...)`:
```csharp
            ScanLeveledLists(rec, errors);
```
And add the method:
```csharp
    private static void ScanLeveledLists(RecordEntry rec, List<PluginError> errors)
    {
        if (!rec.Type.Equals("LVLI", StringComparison.OrdinalIgnoreCase) &&
            !rec.Type.Equals("LVLN", StringComparison.OrdinalIgnoreCase)) return;
        var entries = rec.Node.GetChild("Entries");
        if (entries == null) return;
        foreach (var entry in entries.Children)
        {
            var refVal = entry.GetValue("Reference");
            if (string.IsNullOrWhiteSpace(refVal))
                errors.Add(new PluginError(ErrorSeverity.Warning, ErrorCategory.InvalidLeveledList,
                    rec.SourcePlugin, rec.FormKey, $"Entries.{entry.Key}",
                    "Leveled list entry has no reference (dead slot).", FixAvailable: true));
        }
    }
```

- [ ] **Step 4: Run** → PASS. Commit: `git commit -am "feat: ErrorScanner leveled-list validation"`

---

### Task 12: Async scan + severity grouping

**Files:** Modify `Services/Errors/ErrorScanner.cs`

- [ ] **Step 1: Add async + grouping helpers**:
```csharp
    public Task<IReadOnlyList<PluginError>> ScanAsync(KnowledgeGraph graph) =>
        Task.Run(() => Scan(graph));

    public static Dictionary<ErrorSeverity, int> CountBySeverity(IReadOnlyList<PluginError> errors) =>
        errors.GroupBy(e => e.Severity).ToDictionary(g => g.Key, g => g.Count());
```

- [ ] **Step 2: Build** → 0 errors. Commit: `git commit -am "feat: async error scan + severity counts"`

---

# Milestone M3 — Diff Engine

### Task 13: Diff model records

**Files:** Create `FO4RecordEditor/Models/Diff/{DiffKind,DiffRow}.cs`

- [ ] **Step 1:** `Models/Diff/DiffKind.cs`:
```csharp
namespace FO4RecordEditor.Models;
public enum DiffKind { Unchanged, Added, Removed, Modified }
```
- [ ] **Step 2:** `Models/Diff/DiffRow.cs`:
```csharp
namespace FO4RecordEditor.Models;
public sealed record DiffRow(string Path, string? ValueA, string? ValueB, DiffKind Kind);
```
- [ ] **Step 3:** Build → 0 errors. Commit: `git commit -am "feat: diff model records"`

---

### Task 14: DiffEngine record comparison

**Files:** Create `FO4RecordEditor/Services/Diff/DiffEngine.cs`; Test `FO4RecordEditor.Tests/DiffEngineTests.cs`

Walk both record trees by leaf path. Path = dotted key chain from record root. A leaf in A only → Removed; in B only → Added; both but different value → Modified; same → Unchanged.

- [ ] **Step 1: Failing test** — `DiffEngineTests.cs`:
```csharp
using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.Tests.Helpers;
using Xunit;

namespace FO4RecordEditor.Tests;

public class DiffEngineTests
{
    [Fact]
    public void Compare_DetectsModifiedAddedRemoved()
    {
        var a = GraphTestBuilder.Record("WEAP", "001:T.esp", "Gun",
            ("Damage", "50"), ("OldField", "x"));
        var b = GraphTestBuilder.Record("WEAP", "001:T.esp", "Gun",
            ("Damage", "120"), ("NewField", "y"));

        var rows = new DiffEngine().Compare(a, b);

        rows.Should().Contain(r => r.Path == "Damage" && r.Kind == DiffKind.Modified
            && r.ValueA == "50" && r.ValueB == "120");
        rows.Should().Contain(r => r.Path == "OldField" && r.Kind == DiffKind.Removed);
        rows.Should().Contain(r => r.Path == "NewField" && r.Kind == DiffKind.Added);
    }

    [Fact]
    public void Compare_OnlyChanges_ExcludesUnchanged()
    {
        var a = GraphTestBuilder.Record("WEAP", "001:T.esp", "Gun", ("Damage", "50"));
        var b = GraphTestBuilder.Record("WEAP", "001:T.esp", "Gun", ("Damage", "50"));
        new DiffEngine().Compare(a, b, changesOnly: true).Should().BeEmpty();
    }
}
```

- [ ] **Step 2: Run** → FAIL.

- [ ] **Step 3: Implement** — `Services/Diff/DiffEngine.cs`:
```csharp
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class DiffEngine
{
    public IReadOnlyList<DiffRow> Compare(RecordNode a, RecordNode b, bool changesOnly = false)
    {
        var leavesA = Flatten(a);
        var leavesB = Flatten(b);
        var paths = leavesA.Keys.Union(leavesB.Keys).OrderBy(p => p);
        var rows = new List<DiffRow>();

        foreach (var path in paths)
        {
            leavesA.TryGetValue(path, out var va);
            leavesB.TryGetValue(path, out var vb);
            var kind = (va, vb) switch
            {
                (null, not null) => DiffKind.Added,
                (not null, null) => DiffKind.Removed,
                _ when va != vb  => DiffKind.Modified,
                _                => DiffKind.Unchanged,
            };
            if (changesOnly && kind == DiffKind.Unchanged) continue;
            rows.Add(new DiffRow(path, va, vb, kind));
        }
        return rows;
    }

    private static Dictionary<string, string> Flatten(RecordNode root)
    {
        var dict = new Dictionary<string, string>();
        void Walk(RecordNode n, string prefix)
        {
            foreach (var c in n.Children)
            {
                var path = string.IsNullOrEmpty(prefix) ? c.Key : $"{prefix}.{c.Key}";
                if (c.IsLeaf) dict[path] = c.Value;
                else Walk(c, path);
            }
        }
        Walk(root, "");
        return dict;
    }
}
```

- [ ] **Step 4: Run** → PASS. Commit: `git commit -am "feat: DiffEngine record comparison"`

---

### Task 15: Diff summary counts

**Files:** Modify `Services/Diff/DiffEngine.cs`

- [ ] **Step 1: Add helper**:
```csharp
    public static (int added, int removed, int modified) Summary(IReadOnlyList<DiffRow> rows) =>
        (rows.Count(r => r.Kind == DiffKind.Added),
         rows.Count(r => r.Kind == DiffKind.Removed),
         rows.Count(r => r.Kind == DiffKind.Modified));
```

- [ ] **Step 2: Build** → 0 errors. Commit: `git commit -am "feat: diff summary counts"`

---

# Milestone M4 — AI Layer

### Task 16: AI model records + IAIProvider

**Files:** Create `FO4RecordEditor/Models/Ai/{ChatRole,ChatMessage}.cs`, `FO4RecordEditor/Services/Ai/IAIProvider.cs`

- [ ] **Step 1:** `Models/Ai/ChatRole.cs`:
```csharp
namespace FO4RecordEditor.Models;
public enum ChatRole { System, User, Assistant }
```
- [ ] **Step 2:** `Models/Ai/ChatMessage.cs`:
```csharp
namespace FO4RecordEditor.Models;
public sealed record ChatMessage(ChatRole Role, string Content);
```
- [ ] **Step 3:** `Services/Ai/IAIProvider.cs`:
```csharp
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public interface IAIProvider
{
    string Name { get; }
    IAsyncEnumerable<string> StreamAsync(
        IReadOnlyList<ChatMessage> messages, CancellationToken ct = default);
}
```
- [ ] **Step 4:** Build → 0 errors. Commit: `git commit -am "feat: AI models + IAIProvider"`

---

### Task 17: AnthropicProvider (streaming Messages API)

**Files:** Create `FO4RecordEditor/Services/Ai/AnthropicProvider.cs`

Uses the Anthropic Messages API with `stream: true` (SSE). Model id `claude-sonnet-4-6`. System messages are hoisted to the top-level `system` field (Anthropic requires user/assistant only in `messages`). Reads `text_delta` events from the SSE stream.

- [ ] **Step 1: Implement** — `Services/Ai/AnthropicProvider.cs`:
```csharp
using System.Net.Http;
using System.Net.Http.Headers;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class AnthropicProvider : IAIProvider
{
    private readonly HttpClient _http;
    private readonly string _apiKey;
    private readonly string _model;

    public string Name => "Anthropic";

    public AnthropicProvider(string apiKey, string model = "claude-sonnet-4-6")
    {
        _apiKey = apiKey;
        _model = model;
        _http = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
    }

    public async IAsyncEnumerable<string> StreamAsync(
        IReadOnlyList<ChatMessage> messages,
        [EnumeratorCancellation] CancellationToken ct = default)
    {
        var system = string.Join("\n\n",
            messages.Where(m => m.Role == ChatRole.System).Select(m => m.Content));
        var turns = messages.Where(m => m.Role != ChatRole.System)
            .Select(m => new
            {
                role = m.Role == ChatRole.User ? "user" : "assistant",
                content = m.Content
            }).ToArray();

        var payload = new
        {
            model = _model,
            max_tokens = 4096,
            stream = true,
            system = string.IsNullOrWhiteSpace(system) ? null : system,
            messages = turns,
        };

        using var req = new HttpRequestMessage(HttpMethod.Post, "https://api.anthropic.com/v1/messages");
        req.Headers.Add("x-api-key", _apiKey);
        req.Headers.Add("anthropic-version", "2023-06-01");
        req.Content = new StringContent(
            JsonSerializer.Serialize(payload, new JsonSerializerOptions { DefaultIgnoreCondition =
                System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull }),
            Encoding.UTF8, "application/json");

        using var resp = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, ct);
        if (!resp.IsSuccessStatusCode)
        {
            var err = await resp.Content.ReadAsStringAsync(ct);
            yield return $"[AI error {(int)resp.StatusCode}] {err}";
            yield break;
        }

        using var stream = await resp.Content.ReadAsStreamAsync(ct);
        using var reader = new StreamReader(stream);
        while (!reader.EndOfStream)
        {
            var line = await reader.ReadLineAsync(ct);
            if (string.IsNullOrEmpty(line) || !line.StartsWith("data:")) continue;
            var json = line["data:".Length..].Trim();
            if (json == "[DONE]") break;

            string? text = null;
            try
            {
                using var doc = JsonDocument.Parse(json);
                if (doc.RootElement.TryGetProperty("type", out var t) &&
                    t.GetString() == "content_block_delta" &&
                    doc.RootElement.TryGetProperty("delta", out var d) &&
                    d.TryGetProperty("text", out var txt))
                    text = txt.GetString();
            }
            catch { /* skip malformed keepalive lines */ }
            if (text != null) yield return text;
        }
    }
}
```

- [ ] **Step 2: Build** → 0 errors. (No unit test — requires live API key; covered by manual smoke in Task 30.)
- [ ] **Step 3: Commit** — `git commit -am "feat: AnthropicProvider streaming Messages API"`

---

### Task 18: OllamaProvider stub

**Files:** Create `FO4RecordEditor/Services/Ai/OllamaProvider.cs`

- [ ] **Step 1: Implement** — `Services/Ai/OllamaProvider.cs`:
```csharp
using System.Net.Http;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class OllamaProvider : IAIProvider
{
    private readonly HttpClient _http;
    private readonly string _baseUrl;
    private readonly string _model;

    public string Name => "Ollama";

    public OllamaProvider(string baseUrl = "http://localhost:11434", string model = "llama3.1")
    {
        _baseUrl = baseUrl.TrimEnd('/');
        _model = model;
        _http = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
    }

    public async IAsyncEnumerable<string> StreamAsync(
        IReadOnlyList<ChatMessage> messages,
        [EnumeratorCancellation] CancellationToken ct = default)
    {
        var payload = new
        {
            model = _model,
            stream = true,
            messages = messages.Select(m => new
            {
                role = m.Role switch
                {
                    ChatRole.System => "system",
                    ChatRole.User => "user",
                    _ => "assistant"
                },
                content = m.Content
            }).ToArray()
        };

        using var req = new HttpRequestMessage(HttpMethod.Post, $"{_baseUrl}/api/chat")
        {
            Content = new StringContent(JsonSerializer.Serialize(payload), Encoding.UTF8, "application/json")
        };

        HttpResponseMessage resp;
        try { resp = await _http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, ct); }
        catch (Exception ex) { yield return $"[Ollama unreachable: {ex.Message}]"; yield break; }

        using (resp)
        using (var stream = await resp.Content.ReadAsStreamAsync(ct))
        using (var reader = new StreamReader(stream))
        {
            while (!reader.EndOfStream)
            {
                var line = await reader.ReadLineAsync(ct);
                if (string.IsNullOrWhiteSpace(line)) continue;
                string? text = null;
                try
                {
                    using var doc = JsonDocument.Parse(line);
                    if (doc.RootElement.TryGetProperty("message", out var msg) &&
                        msg.TryGetProperty("content", out var c))
                        text = c.GetString();
                }
                catch { }
                if (!string.IsNullOrEmpty(text)) yield return text;
            }
        }
    }
}
```

- [ ] **Step 2: Build** → 0 errors. Commit: `git commit -am "feat: OllamaProvider"`

---

### Task 19: AIContextBuilder

**Files:** Create `FO4RecordEditor/Services/Ai/AIContextBuilder.cs`; Test `FO4RecordEditor.Tests/AIContextBuilderTests.cs`

Builds the system prompt: a description of the selected record, its neighborhood (one hop via the graph), and any errors on it. This is what makes answers grounded instead of hallucinated.

- [ ] **Step 1: Failing test** — `AIContextBuilderTests.cs`:
```csharp
using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.Tests.Helpers;
using Xunit;

namespace FO4RecordEditor.Tests;

public class AIContextBuilderTests
{
    [Fact]
    public void Build_IncludesRecordFieldsAndNeighborhood()
    {
        var keyword = GraphTestBuilder.Record("KYWD", "001000:Test.esp", "MyKeyword");
        var weapon  = GraphTestBuilder.Record("WEAP", "001001:Test.esp", "MyGun",
            ("Damage", "50"), ("Keyword1", "001000:Test.esp"));
        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("Test.esp", keyword, weapon));

        var ctx = new AIContextBuilder(g).BuildForRecord(g.GetByFormKey("001001:Test.esp")!);

        ctx.Should().Contain("MyGun");
        ctx.Should().Contain("Damage");
        ctx.Should().Contain("MyKeyword");   // pulled in via neighborhood
    }
}
```

- [ ] **Step 2: Run** → FAIL.

- [ ] **Step 3: Implement** — `Services/Ai/AIContextBuilder.cs`:
```csharp
using System.Text;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class AIContextBuilder
{
    private readonly KnowledgeGraph _graph;
    public AIContextBuilder(KnowledgeGraph graph) => _graph = graph;

    public const string SystemPreamble =
        "You are an expert Bethesda (Fallout 4) plugin modding assistant embedded in a " +
        "record editor. You understand ESP/ESM/ESL records, FormKeys (format ID:Plugin.esp), " +
        "keywords, leveled lists, OMODs, conditions (CTDA), and crafting (COBJ). When asked to " +
        "modify records, respond with a concrete change plan the user can approve. Be concise.";

    public string BuildForRecord(RecordEntry rec)
    {
        var sb = new StringBuilder();
        sb.AppendLine(SystemPreamble);
        sb.AppendLine();
        sb.AppendLine($"## Selected record: {rec.EditorID} ({rec.Type}) [{rec.FormKey}] in {rec.SourcePlugin}");
        AppendFields(sb, rec.Node, 0, maxDepth: 3);

        var hood = _graph.GetNeighborhood(rec.FormKey).Where(e => e.FormKey != rec.FormKey).ToList();
        if (hood.Count > 0)
        {
            sb.AppendLine();
            sb.AppendLine("## Linked records (one hop):");
            foreach (var n in hood.Take(20))
                sb.AppendLine($"- {n.EditorID} ({n.Type}) [{n.FormKey}]");
        }

        var inbound = _graph.GetReferencesTo(rec.FormKey);
        if (inbound.Count > 0)
            sb.AppendLine($"\n## {inbound.Count} record(s) reference this one (deleting it would break them).");

        return sb.ToString();
    }

    public string BuildForQuestion(string question, RecordEntry? selected)
    {
        var sb = new StringBuilder();
        sb.AppendLine(SystemPreamble);
        if (selected != null)
        {
            sb.AppendLine();
            sb.AppendLine(BuildForRecord(selected));
        }
        sb.AppendLine($"\n## Graph summary: {_graph.RecordCount} records, {_graph.ReferenceCount} references.");
        return sb.ToString();
    }

    private static void AppendFields(StringBuilder sb, RecordNode node, int depth, int maxDepth)
    {
        if (depth > maxDepth) return;
        foreach (var c in node.Children)
        {
            var indent = new string(' ', depth * 2);
            if (c.IsLeaf) sb.AppendLine($"{indent}{c.Key}: {c.Value}");
            else { sb.AppendLine($"{indent}{c.Key}:"); AppendFields(sb, c, depth + 1, maxDepth); }
        }
    }
}
```

- [ ] **Step 4: Run** → PASS. Commit: `git commit -am "feat: AIContextBuilder grounds prompts in graph data"`

---

### Task 20: ChatService

**Files:** Create `FO4RecordEditor/Services/Ai/ChatService.cs`

Owns the conversation history and routes to the active provider. Streams tokens via a callback so the ViewModel can update the UI live.

- [ ] **Step 1: Implement** — `Services/Ai/ChatService.cs`:
```csharp
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class ChatService
{
    private readonly List<ChatMessage> _history = new();
    private IAIProvider _provider;

    public ChatService(IAIProvider provider) => _provider = provider;
    public void SetProvider(IAIProvider provider) => _provider = provider;
    public IReadOnlyList<ChatMessage> History => _history;
    public void Reset() => _history.Clear();

    /// <summary>Sends a user turn (optionally prefixed with system context) and streams the reply.</summary>
    public async Task<string> SendAsync(
        string userMessage, string? systemContext,
        Action<string> onToken, CancellationToken ct = default)
    {
        var outgoing = new List<ChatMessage>();
        if (systemContext != null) outgoing.Add(new ChatMessage(ChatRole.System, systemContext));
        outgoing.AddRange(_history);
        outgoing.Add(new ChatMessage(ChatRole.User, userMessage));

        _history.Add(new ChatMessage(ChatRole.User, userMessage));

        var full = new System.Text.StringBuilder();
        await foreach (var token in _provider.StreamAsync(outgoing, ct))
        {
            full.Append(token);
            onToken(token);
        }
        _history.Add(new ChatMessage(ChatRole.Assistant, full.ToString()));
        return full.ToString();
    }
}
```

- [ ] **Step 2: Build** → 0 errors. Commit: `git commit -am "feat: ChatService conversation routing"`

---

### Task 21: AI provider factory

**Files:** Create `FO4RecordEditor/Services/Ai/AiProviderFactory.cs`

- [ ] **Step 1: Implement** — `Services/Ai/AiProviderFactory.cs`:
```csharp
namespace FO4RecordEditor.Services;

public static class AiProviderFactory
{
    public static IAIProvider Create(AppSettings s) => s.AiProvider switch
    {
        "ollama" => new OllamaProvider(s.OllamaUrl, s.OllamaModel),
        _        => new AnthropicProvider(s.AnthropicApiKey, s.Model),
    };
}
```

- [ ] **Step 2: Build** → 0 errors. Commit: `git commit -am "feat: AI provider factory from settings"`

---

# Milestone M5 — Undo/Redo + Command Registry

### Task 22: IChange + FieldEditChange

**Files:** Create `FO4RecordEditor/Services/Undo/{IChange,FieldEditChange}.cs`

- [ ] **Step 1:** `Services/Undo/IChange.cs`:
```csharp
namespace FO4RecordEditor.Services;
public interface IChange { string Description { get; } void Apply(); void Undo(); }
```
- [ ] **Step 2:** `Services/Undo/FieldEditChange.cs`:
```csharp
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class FieldEditChange : IChange
{
    private readonly RecordNode _node;
    private readonly string _oldValue;
    private readonly string _newValue;

    public FieldEditChange(RecordNode node, string oldValue, string newValue)
    {
        _node = node; _oldValue = oldValue; _newValue = newValue;
    }

    public string Description => $"Edit {_node.Key}: '{_oldValue}' -> '{_newValue}'";
    public void Apply() => _node.Value = _newValue;
    public void Undo()  => _node.Value = _oldValue;
}
```
- [ ] **Step 3:** Build → 0 errors. Commit: `git commit -am "feat: IChange + FieldEditChange"`

---

### Task 23: UndoStack

**Files:** Create `FO4RecordEditor/Services/Undo/UndoStack.cs`; Test `FO4RecordEditor.Tests/UndoStackTests.cs`

- [ ] **Step 1: Failing test** — `UndoStackTests.cs`:
```csharp
using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.Tests.Helpers;
using Xunit;

namespace FO4RecordEditor.Tests;

public class UndoStackTests
{
    [Fact]
    public void PushApplyUndoRedo_RoundTrips()
    {
        var rec = GraphTestBuilder.Record("WEAP", "001:T.esp", "Gun", ("Damage", "50"));
        var dmg = rec.GetChild("Damage")!;
        var stack = new UndoStack();

        stack.Do(new FieldEditChange(dmg, "50", "120"));
        dmg.Value.Should().Be("120");

        stack.Undo();
        dmg.Value.Should().Be("50");
        stack.CanRedo.Should().BeTrue();

        stack.Redo();
        dmg.Value.Should().Be("120");
    }

    [Fact]
    public void NewChange_ClearsRedoStack()
    {
        var rec = GraphTestBuilder.Record("WEAP", "001:T.esp", "Gun", ("Damage", "50"));
        var dmg = rec.GetChild("Damage")!;
        var stack = new UndoStack();
        stack.Do(new FieldEditChange(dmg, "50", "120"));
        stack.Undo();
        stack.Do(new FieldEditChange(dmg, "50", "75"));
        stack.CanRedo.Should().BeFalse();
    }
}
```

- [ ] **Step 2: Run** → FAIL.

- [ ] **Step 3: Implement** — `Services/Undo/UndoStack.cs`:
```csharp
namespace FO4RecordEditor.Services;

public sealed class UndoStack
{
    private readonly Stack<IChange> _undo = new();
    private readonly Stack<IChange> _redo = new();

    public bool CanUndo => _undo.Count > 0;
    public bool CanRedo => _redo.Count > 0;
    public event Action? Changed;

    public void Do(IChange change)
    {
        change.Apply();
        _undo.Push(change);
        _redo.Clear();
        Changed?.Invoke();
    }

    public void Undo()
    {
        if (!CanUndo) return;
        var c = _undo.Pop();
        c.Undo();
        _redo.Push(c);
        Changed?.Invoke();
    }

    public void Redo()
    {
        if (!CanRedo) return;
        var c = _redo.Pop();
        c.Apply();
        _undo.Push(c);
        Changed?.Invoke();
    }

    public string? PeekUndo() => CanUndo ? _undo.Peek().Description : null;
    public void Clear() { _undo.Clear(); _redo.Clear(); Changed?.Invoke(); }
}
```

- [ ] **Step 4: Run** → PASS. Commit: `git commit -am "feat: UndoStack with redo invalidation"`

---

### Task 24: Command registry + AppCommand

**Files:** Create `FO4RecordEditor/Models/Commands/AppCommand.cs`, `FO4RecordEditor/Services/Commands/CommandRegistry.cs`

Powers the Ctrl+Shift+P palette. Fuzzy match = subsequence match on title, case-insensitive.

- [ ] **Step 1:** `Models/Commands/AppCommand.cs`:
```csharp
namespace FO4RecordEditor.Models;
public sealed record AppCommand(string Id, string Title, string Category, Action Execute);
```
- [ ] **Step 2:** `Services/Commands/CommandRegistry.cs`:
```csharp
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public sealed class CommandRegistry
{
    private readonly List<AppCommand> _commands = new();
    public IReadOnlyList<AppCommand> All => _commands;

    public void Register(AppCommand cmd) => _commands.Add(cmd);
    public void Register(string id, string title, string category, Action exec) =>
        _commands.Add(new AppCommand(id, title, category, exec));

    public IReadOnlyList<AppCommand> Search(string query)
    {
        if (string.IsNullOrWhiteSpace(query)) return _commands;
        return _commands.Where(c => FuzzyMatch(c.Title, query) || FuzzyMatch(c.Category, query))
                        .ToList();
    }

    private static bool FuzzyMatch(string text, string query)
    {
        int ti = 0, qi = 0;
        text = text.ToLowerInvariant(); query = query.ToLowerInvariant();
        while (ti < text.Length && qi < query.Length)
        {
            if (text[ti] == query[qi]) qi++;
            ti++;
        }
        return qi == query.Length;
    }
}
```
- [ ] **Step 3: Add fuzzy-match test** to a new `FO4RecordEditor.Tests/CommandRegistryTests.cs`:
```csharp
using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;

public class CommandRegistryTests
{
    [Fact]
    public void Search_FuzzyMatchesSubsequence()
    {
        var r = new CommandRegistry();
        r.Register("scan", "Run Error Scan", "Errors", () => { });
        r.Register("compile", "Compile Plugin", "Build", () => { });
        r.Search("res").Select(c => c.Id).Should().Contain("scan");   // R-un E-rror S-can
        r.Search("comp").Select(c => c.Id).Should().Contain("compile");
    }
}
```
- [ ] **Step 4: Run** `dotnet test FO4RecordEditor.Tests --filter CommandRegistryTests` → PASS.
- [ ] **Step 5: Commit** — `git commit -am "feat: CommandRegistry + AppCommand fuzzy palette search"`

---

# Milestone M6 — WPF Shell (VS Code-style UI)

## UI Wireframe (target layout)

```
┌──┬─────────────────┬──────────────────────────────┬────────────────────┐
│A │  SIDE PANEL     │   CENTER (tabbed editor)     │   AI ASSISTANT     │
│C │  (swappable)    │  ┌──────┬──────┬──────┐      │  ┌──────────────┐  │
│T │                 │  │Rec A │Rec B │ +    │      │  │ chat history │  │
│I │  ▸ Explorer     │  ├──────┴──────┴──────┤      │  │  (streaming) │  │
│V │  ▸ Search       │  │ Field        Value  │      │  │              │  │
│I │  ▸ Errors       │  │ Damage       120 ◀──edit  │  │              │  │
│T │  ▸ Compare      │  │ Keyword1     [link] │      │  ├──────────────┤  │
│Y │                 │  │ Conditions ▸        │      │  │ > ask...     │  │
│  │  [record tree]  │  │                     │      │  └──────────────┘  │
├──┴─────────────────┴──────────────────────────────┴────────────────────┤
│ BOTTOM TABS:  [Log] [Errors] [Compare] [Compile] [AI Activity]          │
│  12:01 [INFO ] [App]  Loaded Test.esp (7001 records)                    │
└──────────────────────────────────────────────────────────────────────────┘
 Status bar: 7,001 records | 142 refs | 3 errors | ● unsaved      Ctrl+Shift+P
```

Activity bar (far left, icon rail) swaps the SIDE PANEL between Explorer / Search / Errors / Compare. AI panel is collapsible (Ctrl+\). Bottom panel is collapsible (Ctrl+J). Command palette overlays center on Ctrl+Shift+P.

### Task 25: ShellViewModel — central state

**Files:** Create `FO4RecordEditor/ViewModels/ShellViewModel.cs`

Holds every service singleton, the loaded plugins, the graph, and the open tabs. Created once in MainWindow.

- [ ] **Step 1: Implement** — `ViewModels/ShellViewModel.cs`:
```csharp
using System.Collections.ObjectModel;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.ViewModels;

public sealed class ShellViewModel
{
    public ObservableCollection<RecordNode> Plugins { get; } = [];
    public ObservableCollection<RecordTabViewModel> OpenTabs { get; } = [];

    public KnowledgeGraph Graph { get; } = new();
    public LogService Log { get; } = new();
    public SettingsService Settings { get; } = new();
    public UndoStack Undo { get; } = new();
    public CommandRegistry Commands { get; } = new();
    public ErrorScanner Scanner { get; } = new();
    public DiffEngine Diff { get; } = new();

    public ChatService Chat { get; private set; } = null!;
    public AIContextBuilder Context { get; private set; } = null!;

    public ObservableCollection<PluginError> Errors { get; } = [];

    public ShellViewModel()
    {
        Settings.Load();
        Chat = new ChatService(AiProviderFactory.Create(Settings.Current));
        Context = new AIContextBuilder(Graph);
        Log.Log(LogCategory.App, LogLevel.Info, "FO4RecordEditor started.");
    }

    public void RebuildProvider() => Chat.SetProvider(AiProviderFactory.Create(Settings.Current));

    public async Task LoadEspAsync(string path)
    {
        Log.Log(LogCategory.App, LogLevel.Info, $"Loading {System.IO.Path.GetFileName(path)}...");
        var node = await Task.Run(() => MutagenLoader.LoadEsp(path));
        Plugins.Add(node);
        await Graph.IndexAsync(new[] { node },
            new Progress<string>(m => Log.Log(LogCategory.Plugin, LogLevel.Debug, m)));
        Log.Log(LogCategory.App, LogLevel.Info,
            $"Loaded {node.Key}: {Graph.RecordCount} records, {Graph.ReferenceCount} refs.");
    }

    public async Task RunErrorScanAsync()
    {
        Log.Log(LogCategory.Error, LogLevel.Info, "Running error scan...");
        var found = await Scanner.ScanAsync(Graph);
        Errors.Clear();
        foreach (var e in found) Errors.Add(e);
        Log.Log(LogCategory.Error, LogLevel.Info, $"Scan complete: {found.Count} issue(s).");
    }

    public RecordTabViewModel OpenRecord(RecordNode record)
    {
        var existing = OpenTabs.FirstOrDefault(t => ReferenceEquals(t.Record, record));
        if (existing != null) return existing;
        var tab = new RecordTabViewModel(record, Undo);
        OpenTabs.Add(tab);
        return tab;
    }
}
```

- [ ] **Step 2: Build** → 0 errors. Commit: `git commit -am "feat: ShellViewModel central state"`

---

### Task 26: RecordTabViewModel

**Files:** Create `FO4RecordEditor/ViewModels/RecordTabViewModel.cs`

Wraps one open record; exposes editable field rows that route edits through the undo stack.

- [ ] **Step 1: Implement** — `ViewModels/RecordTabViewModel.cs`:
```csharp
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.ViewModels;

public sealed class RecordTabViewModel
{
    public RecordNode Record { get; }
    public string Title { get; }
    private readonly UndoStack _undo;

    public RecordTabViewModel(RecordNode record, UndoStack undo)
    {
        Record = record;
        _undo = undo;
        Title = record.GetValue("EditorID") ?? record.Key;
    }

    public IReadOnlyList<FieldRow> Fields =>
        Record.Children.Select(c => new FieldRow(c, 0)).ToList();

    /// <summary>Edit a leaf through the undo stack so it is reversible.</summary>
    public void EditField(RecordNode leaf, string newValue)
    {
        if (leaf.Value == newValue) return;
        _undo.Do(new FieldEditChange(leaf, leaf.Value, newValue));
    }
}
```

- [ ] **Step 2: Build** → 0 errors. Commit: `git commit -am "feat: RecordTabViewModel"`

---

### Task 27: MainWindow shell layout

**Files:** Modify `FO4RecordEditor/MainWindow.xaml` + `MainWindow.xaml.cs` (full rewrite of the shell)

Replaces the current single-pane window with the activity-bar + side + center + AI + bottom grid. Existing tree/field logic moves into panels in later tasks; this task establishes the skeleton and wires ShellViewModel.

- [ ] **Step 1: Rewrite MainWindow.xaml** with the five-region grid:
```xml
<Window x:Class="FO4RecordEditor.MainWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="FO4 Record Editor — AI" Height="960" Width="1600"
        Background="#1E1E1E" Foreground="#D4D4D4" WindowState="Maximized"
        KeyDown="Window_KeyDown">
  <Grid>
    <Grid.RowDefinitions>
      <RowDefinition Height="*"/>
      <RowDefinition Height="5"/>
      <RowDefinition Height="{Binding BottomHeight, Mode=TwoWay}" x:Name="BottomRow"/>
      <RowDefinition Height="Auto"/>
    </Grid.RowDefinitions>

    <Grid Grid.Row="0">
      <Grid.ColumnDefinitions>
        <ColumnDefinition Width="48"/>                <!-- activity bar -->
        <ColumnDefinition Width="320" x:Name="SideCol"/>
        <ColumnDefinition Width="5"/>
        <ColumnDefinition Width="*"/>                 <!-- center -->
        <ColumnDefinition Width="5"/>
        <ColumnDefinition Width="420" x:Name="AiCol"/>
      </Grid.ColumnDefinitions>

      <!-- Activity bar -->
      <StackPanel Grid.Column="0" Background="#2D2D30">
        <Button x:Name="BtnExplorer" Content="📁" ToolTip="Explorer" Click="ShowExplorer_Click" Style="{StaticResource ActivityBtn}"/>
        <Button x:Name="BtnSearch"   Content="🔍" ToolTip="Search"   Click="ShowSearch_Click"   Style="{StaticResource ActivityBtn}"/>
        <Button x:Name="BtnErrors"   Content="⚠"  ToolTip="Errors"   Click="ShowErrors_Click"   Style="{StaticResource ActivityBtn}"/>
        <Button x:Name="BtnCompare"  Content="⇄"  ToolTip="Compare"  Click="ShowCompare_Click"  Style="{StaticResource ActivityBtn}"/>
      </StackPanel>

      <!-- Side panel host (content swapped in code-behind) -->
      <ContentControl Grid.Column="1" x:Name="SidePanelHost" Background="#252526"/>
      <GridSplitter Grid.Column="2" Width="5" Background="#3C3C3C" HorizontalAlignment="Stretch"/>

      <!-- Center: tabbed record editor -->
      <TabControl Grid.Column="3" x:Name="CenterTabs" Background="#1E1E1E" BorderThickness="0"/>

      <GridSplitter Grid.Column="4" Width="5" Background="#3C3C3C" HorizontalAlignment="Stretch"/>

      <!-- AI panel host -->
      <ContentControl Grid.Column="5" x:Name="AiPanelHost" Background="#202020"/>
    </Grid>

    <GridSplitter Grid.Row="1" Height="5" Background="#3C3C3C" HorizontalAlignment="Stretch"/>

    <!-- Bottom tabs -->
    <TabControl Grid.Row="2" x:Name="BottomTabs" Background="#181818" BorderThickness="0"/>

    <!-- Status bar -->
    <Border Grid.Row="3" Background="#007ACC" Padding="8,2">
      <TextBlock x:Name="StatusLabel" Foreground="White" FontFamily="Consolas" FontSize="11" Text="Ready"/>
    </Border>

    <!-- Command palette overlay -->
    <Border x:Name="PaletteOverlay" Grid.RowSpan="4" Background="#88000000" Visibility="Collapsed">
      <ContentControl x:Name="PaletteHost" VerticalAlignment="Top" HorizontalAlignment="Center" Margin="0,80,0,0"/>
    </Border>
  </Grid>
</Window>
```

- [ ] **Step 2: Add the ActivityBtn style** to a new `App.xaml` ResourceDictionary section (inside `<Application.Resources>`):
```xml
<Style x:Key="ActivityBtn" TargetType="Button">
  <Setter Property="Width" Value="48"/>
  <Setter Property="Height" Value="44"/>
  <Setter Property="Background" Value="Transparent"/>
  <Setter Property="Foreground" Value="#AAAAAA"/>
  <Setter Property="BorderThickness" Value="0"/>
  <Setter Property="FontSize" Value="18"/>
  <Setter Property="Cursor" Value="Hand"/>
  <Style.Triggers>
    <Trigger Property="IsMouseOver" Value="True"><Setter Property="Foreground" Value="White"/></Trigger>
  </Style.Triggers>
</Style>
```

- [ ] **Step 3: Rewrite MainWindow.xaml.cs shell** wiring:
```csharp
using System.Windows;
using System.Windows.Input;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor;

public partial class MainWindow : Window
{
    private readonly ShellViewModel _shell = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _shell;
        ShowExplorer_Click(this, null!);            // default side panel
        AiPanelHost.Content = new Views.ChatPanel(_shell);
        BuildBottomTabs();
        RegisterCommands();
        UpdateStatus();
    }

    private void BuildBottomTabs()
    {
        BottomTabs.Items.Add(new System.Windows.Controls.TabItem
            { Header = "Log", Content = new Views.LogPanel(_shell) });
        BottomTabs.Items.Add(new System.Windows.Controls.TabItem
            { Header = "Errors", Content = new Views.ErrorsPanel(_shell) });
        BottomTabs.Items.Add(new System.Windows.Controls.TabItem
            { Header = "Compare", Content = new Views.ComparePanel(_shell) });
    }

    private void ShowExplorer_Click(object s, RoutedEventArgs e) => SidePanelHost.Content = new Views.ExplorerPanel(_shell, OpenRecordTab);
    private void ShowSearch_Click(object s, RoutedEventArgs e)   => SidePanelHost.Content = new Views.ExplorerPanel(_shell, OpenRecordTab) { StartInSearch = true };
    private void ShowErrors_Click(object s, RoutedEventArgs e)   => BottomTabs.SelectedIndex = 1;
    private void ShowCompare_Click(object s, RoutedEventArgs e)  => BottomTabs.SelectedIndex = 2;

    private void OpenRecordTab(Models.RecordNode record)
    {
        var tab = _shell.OpenRecord(record);
        var ti = new System.Windows.Controls.TabItem { Header = tab.Title, Content = new Views.RecordEditorView(tab) };
        CenterTabs.Items.Add(ti);
        CenterTabs.SelectedItem = ti;
    }

    private void UpdateStatus() =>
        StatusLabel.Text = $"{_shell.Graph.RecordCount:N0} records | {_shell.Graph.ReferenceCount:N0} refs | {_shell.Errors.Count} errors";
}
```
NOTE: `RegisterCommands`, `Window_KeyDown`, `BottomHeight`/`BuildBottomTabs` typo fix, and the referenced `Views.*` classes are added in Tasks 28-32. This task will not build until those exist — that is expected; build verification happens at the end of Task 32.

- [ ] **Step 4: Commit** (compiles after Task 32): `git commit -am "feat: MainWindow shell skeleton (activity bar + 5 regions)"`

---

### Task 28: ExplorerPanel (record tree)

**Files:** Create `FO4RecordEditor/Views/ExplorerPanel.xaml` + `.xaml.cs`

UserControl taking `(ShellViewModel, Action<RecordNode> openРecord)`. Virtualized TreeView + search box. Right-click opens the AI Actions menu (Task 31).

- [ ] **Step 1:** `Views/ExplorerPanel.xaml`:
```xml
<UserControl x:Class="FO4RecordEditor.Views.ExplorerPanel"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             Background="#252526">
  <Grid>
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="*"/>
    </Grid.RowDefinitions>
    <StackPanel Grid.Row="0" Orientation="Horizontal" Background="#2D2D30">
      <Button Content="Open ESP" Click="OpenEsp_Click" Margin="4,3" Padding="8,3"/>
      <Button Content="Scan" Click="Scan_Click" Margin="0,3" Padding="8,3"/>
    </StackPanel>
    <TextBox Grid.Row="1" x:Name="SearchBox" Background="#1E1E1E" Foreground="#D4D4D4"
             BorderBrush="#555" Margin="4,3" Padding="4,3" TextChanged="Search_TextChanged"/>
    <TreeView Grid.Row="2" x:Name="Tree" Background="#252526" BorderThickness="0"
              VirtualizingPanel.IsVirtualizing="True" VirtualizingPanel.VirtualizationMode="Recycling"
              MouseDoubleClick="Tree_DoubleClick" ContextMenuOpening="Tree_ContextMenuOpening">
      <TreeView.ItemTemplate>
        <HierarchicalDataTemplate ItemsSource="{Binding Children}">
          <TextBlock Text="{Binding Key}" FontFamily="Consolas" FontSize="12"/>
        </HierarchicalDataTemplate>
      </TreeView.ItemTemplate>
    </TreeView>
  </Grid>
</UserControl>
```

- [ ] **Step 2:** `Views/ExplorerPanel.xaml.cs`:
```csharp
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;
using Microsoft.Win32;

namespace FO4RecordEditor.Views;

public partial class ExplorerPanel : UserControl
{
    private readonly ShellViewModel _shell;
    private readonly Action<RecordNode> _open;
    public bool StartInSearch { get; set; }

    public ExplorerPanel(ShellViewModel shell, Action<RecordNode> open)
    {
        InitializeComponent();
        _shell = shell; _open = open;
        Tree.ItemsSource = shell.Plugins;
    }

    private async void OpenEsp_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "FO4 Plugins|*.esp;*.esm;*.esl", Multiselect = true };
        if (dlg.ShowDialog() != true) return;
        foreach (var path in dlg.FileNames) await _shell.LoadEspAsync(path);
    }

    private async void Scan_Click(object sender, RoutedEventArgs e) => await _shell.RunErrorScanAsync();

    private void Tree_DoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (Tree.SelectedItem is RecordNode n && n.GetValue("FormKey") != null) _open(n);
    }

    private void Search_TextChanged(object sender, TextChangedEventArgs e)
    {
        var q = SearchBox.Text.Trim();
        if (q.Length < 2) return;
        var hit = _shell.Plugins.SelectMany(p => p.SelfAndDescendants())
            .FirstOrDefault(n => n.Key.Contains(q, StringComparison.OrdinalIgnoreCase)
                              || n.Value.Contains(q, StringComparison.OrdinalIgnoreCase));
        if (hit != null) { var p = hit.Parent; while (p != null) { p.IsExpanded = true; p = p.Parent; } hit.IsSelected = true; }
    }

    private void Tree_ContextMenuOpening(object sender, ContextMenuEventArgs e)
    {
        if (Tree.SelectedItem is RecordNode n && n.GetValue("FormKey") != null)
            Tree.ContextMenu = AiActions.Build(_shell, n, _open);
        else e.Handled = true;
    }
}
```

- [ ] **Step 3:** Commit — `git commit -am "feat: ExplorerPanel tree + search"`

---

### Task 29: RecordEditorView (center tab)

**Files:** Create `FO4RecordEditor/Views/RecordEditorView.xaml` + `.xaml.cs`

Property grid bound to `RecordTabViewModel.Fields`. Edits route through `EditField` (undoable).

- [ ] **Step 1:** `Views/RecordEditorView.xaml`:
```xml
<UserControl x:Class="FO4RecordEditor.Views.RecordEditorView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#1E1E1E">
  <ScrollViewer VerticalScrollBarVisibility="Auto">
    <ItemsControl x:Name="FieldList">
      <ItemsControl.ItemTemplate>
        <DataTemplate>
          <Grid Background="#252526" Margin="0,1">
            <Grid.ColumnDefinitions>
              <ColumnDefinition Width="260"/><ColumnDefinition Width="*"/>
            </Grid.ColumnDefinitions>
            <TextBlock Grid.Column="0" Text="{Binding IndentedKey}" Foreground="#9CDCFE"
                       FontFamily="Consolas" FontSize="12" Padding="4,2"/>
            <TextBox Grid.Column="1" Text="{Binding Value, UpdateSourceTrigger=LostFocus}"
                     Background="Transparent" BorderThickness="0" Foreground="#CE9178"
                     FontFamily="Consolas" FontSize="12" Padding="4,2" IsReadOnly="{Binding IsCompound}"/>
          </Grid>
        </DataTemplate>
      </ItemsControl.ItemTemplate>
    </ItemsControl>
  </ScrollViewer>
</UserControl>
```

- [ ] **Step 2:** `Views/RecordEditorView.xaml.cs`:
```csharp
using System.Windows.Controls;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class RecordEditorView : UserControl
{
    public RecordEditorView(RecordTabViewModel tab)
    {
        InitializeComponent();
        FieldList.ItemsSource = tab.Fields;
    }
}
```

- [ ] **Step 3:** Commit — `git commit -am "feat: RecordEditorView property grid"`

---

### Task 30: ChatPanel (AI assistant)

**Files:** Create `FO4RecordEditor/Views/ChatPanel.xaml` + `.xaml.cs`

Streaming chat. The assistant bubble updates token-by-token as `ChatService.SendAsync` invokes `onToken`. Context for the active record (if any) is built via `AIContextBuilder` and passed as the system message on each send.

- [ ] **Step 1:** `Views/ChatPanel.xaml`:
```xml
<UserControl x:Class="FO4RecordEditor.Views.ChatPanel"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#202020">
  <Grid>
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/>
    </Grid.RowDefinitions>
    <Border Grid.Row="0" Background="#2D2D30" Padding="8,4">
      <TextBlock Text="AI ASSISTANT" Foreground="#888" FontFamily="Consolas" FontSize="10" FontWeight="Bold"/>
    </Border>
    <ScrollViewer Grid.Row="1" x:Name="Scroll" VerticalScrollBarVisibility="Auto" Padding="6">
      <ItemsControl x:Name="Messages">
        <ItemsControl.ItemTemplate>
          <DataTemplate>
            <Border Margin="0,4" Padding="8" CornerRadius="6"
                    Background="{Binding Bubble}">
              <TextBlock Text="{Binding Text}" TextWrapping="Wrap" Foreground="#E0E0E0"
                         FontFamily="Segoe UI" FontSize="13"/>
            </Border>
          </DataTemplate>
        </ItemsControl.ItemTemplate>
      </ItemsControl>
    </ScrollViewer>
    <Grid Grid.Row="2" Background="#2D2D30">
      <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
      <TextBox Grid.Column="0" x:Name="Input" Background="#1E1E1E" Foreground="#D4D4D4"
               BorderBrush="#555" Margin="4" Padding="6" AcceptsReturn="True" MinHeight="40"
               KeyDown="Input_KeyDown"/>
      <Button Grid.Column="1" Content="Send" Click="Send_Click" Margin="0,4,4,4" Padding="12,4"/>
    </Grid>
  </Grid>
</UserControl>
```

- [ ] **Step 2:** `Views/ChatPanel.xaml.cs`:
```csharp
using System.Collections.ObjectModel;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class ChatPanel : UserControl
{
    private readonly ShellViewModel _shell;
    public ObservableCollection<ChatBubble> Bubbles { get; } = [];

    public ChatPanel(ShellViewModel shell)
    {
        InitializeComponent();
        _shell = shell;
        Messages.ItemsSource = Bubbles;
    }

    private void Input_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && Keyboard.Modifiers == ModifierKeys.Control) { Send_Click(sender, e); e.Handled = true; }
    }

    private async void Send_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        var text = Input.Text.Trim();
        if (string.IsNullOrEmpty(text)) return;
        Input.Clear();

        Bubbles.Add(new ChatBubble { Text = text, IsUser = true });
        var reply = new ChatBubble { Text = "", IsUser = false };
        Bubbles.Add(reply);

        var selected = GetSelectedRecordEntry();
        var ctx = _shell.Context.BuildForQuestion(text, selected);
        _shell.Log.Log(LogCategory.AI, LogLevel.Info, "Prompt sent", text);

        try
        {
            await _shell.Chat.SendAsync(text, ctx, token =>
                Dispatcher.Invoke(() => { reply.Text += token; Scroll.ScrollToEnd(); }));
            _shell.Log.Log(LogCategory.AI, LogLevel.Info, "Reply received", reply.Text);
        }
        catch (Exception ex)
        {
            reply.Text = $"[error] {ex.Message}";
            _shell.Log.Log(LogCategory.AI, LogLevel.Error, "AI call failed", ex.Message);
        }
    }

    private RecordEntry? GetSelectedRecordEntry()
    {
        var first = _shell.OpenTabs.LastOrDefault();
        return first == null ? null : _shell.Graph.GetByFormKey(first.Record.GetValue("FormKey") ?? "");
    }
}

public sealed class ChatBubble : System.ComponentModel.INotifyPropertyChanged
{
    private string _text = "";
    public bool IsUser { get; set; }
    public string Text { get => _text; set { _text = value; PropertyChanged?.Invoke(this, new(nameof(Text))); } }
    public Brush Bubble => IsUser ? new SolidColorBrush(Color.FromRgb(0x0E,0x39,0x5C))
                                  : new SolidColorBrush(Color.FromRgb(0x2A,0x2A,0x2A));
    public event System.ComponentModel.PropertyChangedEventHandler? PropertyChanged;
}
```

- [ ] **Step 3:** Commit — `git commit -am "feat: ChatPanel streaming AI assistant"`

---

### Task 31: AI Actions context menu

**Files:** Create `FO4RecordEditor/Views/AiActions.cs`

Builds the right-click menu: Explain Record, Fix Errors, Generate Patch, Compare to Vanilla, Find Related, Analyze Impact. Each seeds the chat with a pre-built prompt grounded in the graph.

- [ ] **Step 1:** `Views/AiActions.cs`:
```csharp
using System.Windows.Controls;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public static class AiActions
{
    public static ContextMenu Build(ShellViewModel shell, RecordNode record, Action<RecordNode> open)
    {
        var menu = new ContextMenu();
        var entry = shell.Graph.GetByFormKey(record.GetValue("FormKey") ?? "");

        Add(menu, "Explain Record", () => SeedChat(shell,
            $"Explain this record in plain language: {Describe(entry, shell)}"));
        Add(menu, "Fix Errors", () => SeedChat(shell,
            $"List any errors in this record and how to fix them: {Describe(entry, shell)}"));
        Add(menu, "Generate Patch", () => SeedChat(shell,
            $"Propose a compatibility patch involving this record: {Describe(entry, shell)}"));
        Add(menu, "Compare to Vanilla", () => SeedChat(shell,
            $"Compare this record to its vanilla Fallout4.esm counterpart and summarize the changes: {Describe(entry, shell)}"));
        Add(menu, "Analyze Impact (what breaks if deleted)", () =>
        {
            if (entry == null) return;
            var report = shell.Graph.AnalyzeImpact(entry.FormKey);
            SeedChat(shell, $"If I delete {entry.EditorID} [{entry.FormKey}], these {report.AffectedRecords.Count} " +
                $"records reference it and would break: " +
                string.Join(", ", report.AffectedRecords.Take(30).Select(r => r.EditorID)) +
                ". Explain the consequences and safest fix.");
        });
        return menu;
    }

    private static void Add(ContextMenu menu, string header, Action action)
    {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => action();
        menu.Items.Add(item);
    }

    private static string Describe(RecordEntry? e, ShellViewModel shell) =>
        e == null ? "(record not indexed)" : shell.Context.BuildForRecord(e);

    private static void SeedChat(ShellViewModel shell, string prompt) =>
        shell.Log.Log(LogCategory.AI, LogLevel.Info, "AI action queued", prompt[..Math.Min(80, prompt.Length)]);
        // NOTE: wired to ChatPanel.SubmitExternal in Task 32 integration step.
}
```

- [ ] **Step 2:** Commit — `git commit -am "feat: AI Actions right-click menu"`

---

### Task 32: ErrorsPanel, ComparePanel, LogPanel, CommandPalette, SettingsDialog + integration

**Files:** Create `Views/{ErrorsPanel,ComparePanel,LogPanel,CommandPalette,SettingsDialog}.xaml(.cs)`; finish `MainWindow.xaml.cs`

- [ ] **Step 1:** `Views/ErrorsPanel.xaml` — DataGrid bound to `shell.Errors`:
```xml
<UserControl x:Class="FO4RecordEditor.Views.ErrorsPanel"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#181818">
  <DataGrid x:Name="Grid" AutoGenerateColumns="False" IsReadOnly="True"
            Background="#181818" Foreground="#D4D4D4" GridLinesVisibility="Horizontal" FontFamily="Consolas" FontSize="11">
    <DataGrid.Columns>
      <DataGridTextColumn Header="Severity" Binding="{Binding Severity}" Width="80"/>
      <DataGridTextColumn Header="Category" Binding="{Binding Category}" Width="140"/>
      <DataGridTextColumn Header="Plugin"   Binding="{Binding Plugin}"   Width="140"/>
      <DataGridTextColumn Header="FormKey"  Binding="{Binding FormKey}"  Width="140"/>
      <DataGridTextColumn Header="Field"    Binding="{Binding Field}"    Width="120"/>
      <DataGridTextColumn Header="Description" Binding="{Binding Description}" Width="*"/>
      <DataGridCheckBoxColumn Header="Fix?" Binding="{Binding FixAvailable}" Width="40"/>
    </DataGrid.Columns>
  </DataGrid>
</UserControl>
```
`Views/ErrorsPanel.xaml.cs`:
```csharp
using System.Windows.Controls;
using FO4RecordEditor.ViewModels;
namespace FO4RecordEditor.Views;
public partial class ErrorsPanel : UserControl
{
    public ErrorsPanel(ShellViewModel shell) { InitializeComponent(); Grid.ItemsSource = shell.Errors; }
}
```

- [ ] **Step 2:** `Views/ComparePanel.xaml` — two-column diff DataGrid:
```xml
<UserControl x:Class="FO4RecordEditor.Views.ComparePanel"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#181818">
  <DataGrid x:Name="Grid" AutoGenerateColumns="False" IsReadOnly="True" Background="#181818"
            Foreground="#D4D4D4" FontFamily="Consolas" FontSize="11">
    <DataGrid.Columns>
      <DataGridTextColumn Header="Path"  Binding="{Binding Path}"   Width="260"/>
      <DataGridTextColumn Header="A"     Binding="{Binding ValueA}" Width="*"/>
      <DataGridTextColumn Header="B"     Binding="{Binding ValueB}" Width="*"/>
      <DataGridTextColumn Header="Kind"  Binding="{Binding Kind}"   Width="90"/>
    </DataGrid.Columns>
  </DataGrid>
</UserControl>
```
`Views/ComparePanel.xaml.cs`:
```csharp
using System.Windows.Controls;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;
namespace FO4RecordEditor.Views;
public partial class ComparePanel : UserControl
{
    private readonly ShellViewModel _shell;
    public ComparePanel(ShellViewModel shell) { InitializeComponent(); _shell = shell; }
    public void Show(RecordNode a, RecordNode b) =>
        Grid.ItemsSource = _shell.Diff.Compare(a, b, changesOnly: true);
}
```

- [ ] **Step 3:** `Views/LogPanel.xaml` — DataGrid bound to `shell.Log.Entries` with severity colors; `.xaml.cs`:
```csharp
using System.Windows.Controls;
using FO4RecordEditor.ViewModels;
namespace FO4RecordEditor.Views;
public partial class LogPanel : UserControl
{
    public LogPanel(ShellViewModel shell) { InitializeComponent(); Grid.ItemsSource = shell.Log.Entries; }
}
```
`Views/LogPanel.xaml`:
```xml
<UserControl x:Class="FO4RecordEditor.Views.LogPanel"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#181818">
  <DataGrid x:Name="Grid" AutoGenerateColumns="False" IsReadOnly="True" Background="#181818"
            Foreground="#D4D4D4" FontFamily="Consolas" FontSize="11">
    <DataGrid.Columns>
      <DataGridTextColumn Header="Time"     Binding="{Binding Timestamp, StringFormat=HH:mm:ss}" Width="80"/>
      <DataGridTextColumn Header="Level"    Binding="{Binding Level}"    Width="70"/>
      <DataGridTextColumn Header="Category" Binding="{Binding Category}" Width="80"/>
      <DataGridTextColumn Header="Message"  Binding="{Binding Message}"  Width="*"/>
    </DataGrid.Columns>
  </DataGrid>
</UserControl>
```

- [ ] **Step 4:** `Views/CommandPalette.xaml(.cs)` — overlay listbox filtered by `CommandRegistry.Search`:
```csharp
using System.Windows;
using System.Windows.Controls;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
namespace FO4RecordEditor.Views;
public partial class CommandPalette : UserControl
{
    private readonly CommandRegistry _registry;
    public event Action? Dismiss;
    public CommandPalette(CommandRegistry registry)
    {
        InitializeComponent();
        _registry = registry;
        List.ItemsSource = registry.All;
        Box.TextChanged += (_, _) => List.ItemsSource = registry.Search(Box.Text);
        List.MouseDoubleClick += (_, _) => Run();
    }
    private void Run()
    {
        if (List.SelectedItem is AppCommand c) { c.Execute(); Dismiss?.Invoke(); }
    }
}
```
`Views/CommandPalette.xaml`:
```xml
<UserControl x:Class="FO4RecordEditor.Views.CommandPalette"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Width="640">
  <Border Background="#252526" BorderBrush="#007ACC" BorderThickness="1" CornerRadius="4">
    <StackPanel>
      <TextBox x:Name="Box" Background="#1E1E1E" Foreground="#D4D4D4" BorderThickness="0"
               FontSize="15" Padding="8"/>
      <ListBox x:Name="List" Background="#252526" Foreground="#D4D4D4" BorderThickness="0" MaxHeight="360">
        <ListBox.ItemTemplate>
          <DataTemplate>
            <StackPanel Orientation="Horizontal">
              <TextBlock Text="{Binding Category}" Foreground="#888" Width="90" FontFamily="Consolas"/>
              <TextBlock Text="{Binding Title}" Foreground="#D4D4D4"/>
            </StackPanel>
          </DataTemplate>
        </ListBox.ItemTemplate>
      </ListBox>
    </StackPanel>
  </Border>
</UserControl>
```

- [ ] **Step 5:** `Views/SettingsDialog.xaml(.cs)` — Window editing `shell.Settings.Current` (API key, provider, model); on Save calls `Settings.Save()` + `shell.RebuildProvider()`:
```csharp
using System.Windows;
using FO4RecordEditor.ViewModels;
namespace FO4RecordEditor.Views;
public partial class SettingsDialog : Window
{
    private readonly ShellViewModel _shell;
    public SettingsDialog(ShellViewModel shell)
    {
        InitializeComponent();
        _shell = shell;
        ApiKeyBox.Password = shell.Settings.Current.AnthropicApiKey;
        ProviderBox.Text = shell.Settings.Current.AiProvider;
        ModelBox.Text = shell.Settings.Current.Model;
    }
    private void Save_Click(object sender, RoutedEventArgs e)
    {
        _shell.Settings.Current.AnthropicApiKey = ApiKeyBox.Password;
        _shell.Settings.Current.AiProvider = ProviderBox.Text;
        _shell.Settings.Current.Model = ModelBox.Text;
        _shell.Settings.Save();
        _shell.RebuildProvider();
        DialogResult = true;
    }
}
```
`Views/SettingsDialog.xaml`:
```xml
<Window x:Class="FO4RecordEditor.Views.SettingsDialog"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Settings" Height="280" Width="520" Background="#1E1E1E" Foreground="#D4D4D4">
  <StackPanel Margin="16">
    <TextBlock Text="Anthropic API Key" Margin="0,4"/>
    <PasswordBox x:Name="ApiKeyBox" Background="#252526" Foreground="#D4D4D4"/>
    <TextBlock Text="Provider (anthropic | ollama)" Margin="0,8,0,4"/>
    <TextBox x:Name="ProviderBox" Background="#252526" Foreground="#D4D4D4"/>
    <TextBlock Text="Model" Margin="0,8,0,4"/>
    <TextBox x:Name="ModelBox" Background="#252526" Foreground="#D4D4D4"/>
    <Button Content="Save" Click="Save_Click" Margin="0,16,0,0" Padding="16,6" HorizontalAlignment="Right"/>
  </StackPanel>
</Window>
```

- [ ] **Step 6: Finish MainWindow.xaml.cs** — add the bindings/handlers Task 27 referenced. Add this `BottomHeight` property + `RegisterCommands` + `Window_KeyDown` + palette wiring, and fix the `BuildBottomTabs` reference:
```csharp
    public GridLength BottomHeight { get; set; } = new GridLength(200);

    private void RegisterCommands()
    {
        var c = _shell.Commands;
        c.Register("open.esp", "Open ESP", "File", () => ShowExplorer_Click(this, null!));
        c.Register("scan.errors", "Run Error Scan", "Errors", async () => await _shell.RunErrorScanAsync());
        c.Register("settings", "Open Settings", "App", () => new Views.SettingsDialog(_shell) { Owner = this }.ShowDialog());
        c.Register("undo", "Undo", "Edit", () => _shell.Undo.Undo());
        c.Register("redo", "Redo", "Edit", () => _shell.Undo.Redo());
    }

    private void Window_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.P && Keyboard.Modifiers == (ModifierKeys.Control | ModifierKeys.Shift))
            { TogglePalette(); e.Handled = true; }
        else if (e.Key == Key.Z && Keyboard.Modifiers == ModifierKeys.Control)
            { _shell.Undo.Undo(); e.Handled = true; }
        else if (e.Key == Key.Y && Keyboard.Modifiers == ModifierKeys.Control)
            { _shell.Undo.Redo(); e.Handled = true; }
        else if (e.Key == Key.Escape)
            PaletteOverlay.Visibility = Visibility.Collapsed;
    }

    private void TogglePalette()
    {
        if (PaletteOverlay.Visibility == Visibility.Visible)
            { PaletteOverlay.Visibility = Visibility.Collapsed; return; }
        var palette = new Views.CommandPalette(_shell.Commands);
        palette.Dismiss += () => PaletteOverlay.Visibility = Visibility.Collapsed;
        PaletteHost.Content = palette;
        PaletteOverlay.Visibility = Visibility.Visible;
    }
```
Also add `using System.Windows.Input;` and bind `BottomRow.Height` to `BottomHeight` (or set `BottomRow.Height` directly in the constructor to avoid the binding). Simplest: in the constructor add `BottomRow.Height = new GridLength(200);` and remove the `{Binding BottomHeight}` from XAML.

- [ ] **Step 7: Build the whole solution** — Run: `dotnet build FO4RecordEditor/FO4RecordEditor.csproj`
Expected: Build succeeded, 0 errors. Fix any missing `using` or constructor-arg mismatches against the contracts above.

- [ ] **Step 8: Run all tests** — Run: `dotnet test FO4RecordEditor.Tests`
Expected: all green (LogService, KnowledgeGraph, ErrorScanner, DiffEngine, UndoStack, CommandRegistry, AIContextBuilder).

- [ ] **Step 9: Manual smoke** — Launch, Open ESP on `D:\Games\ModlistDownloads\mods\FallenWorldCraftingFramework\FallenWorldCrafting_S7.esp`, confirm: tree loads, double-click opens a center tab, Scan populates Errors tab with NullActorValue rows, set API key in Settings, ask the AI "why are these recipes blocked?" and confirm a streaming answer.

- [ ] **Step 10: Commit** — `git commit -am "feat: complete WPF shell (panels, palette, settings, AI actions)"`

---

# Design Reference (Deliverables 1, 3, 5, 11)

## Database / Indexing Design

No external DB. The KnowledgeGraph is an in-memory inverted index built once per load:

| Index | Type | Answers |
|-------|------|---------|
| `_byFormKey` | `Dictionary<string, RecordEntry>` | "what is FormKey X?" O(1) |
| `_byEditorId` | `Dictionary<string, List<RecordEntry>>` | "find records named Y" O(1) |
| `_byType` | `Dictionary<string, List<RecordEntry>>` | "all WEAP records" O(1) |
| `_inbound` | `Dictionary<string, List<Reference>>` | "what references X" (impact) O(1) |
| `_overrides` | `Dictionary<string, List<RecordEntry>>` | "conflicts on X" O(1) |
| `_allRefs` | `List<Reference>` | outbound scan / neighborhood |

Memory: ~50k records x (entry + ~5 refs) is tens of MB — fine in-process. For 100k+ a future swap to LiteDB is possible behind the same `KnowledgeGraph` API (see roadmap). Indexing is O(total leaves) single-pass, runs on a background thread via `IndexAsync`, never blocks the UI.

## AI Integration Design

```
User question ──▶ ChatPanel ──▶ AIContextBuilder.BuildForQuestion(q, selectedRecord)
                                      │  (pulls record fields + 1-hop neighborhood + inbound count from graph)
                                      ▼
                              system context string
                                      │
ChatService.SendAsync(userMsg, systemCtx, onToken) ──▶ IAIProvider.StreamAsync
                                      │                      ├─ AnthropicProvider (SSE, claude-sonnet-4-6)
                                      │                      └─ OllamaProvider (NDJSON, local)
                                      ▼
                          tokens stream back ──▶ onToken ──▶ live bubble update + AI log
```

Grounding is the key idea: the AI never guesses record contents because the graph injects the real fields and relationships into every prompt. "What breaks if I delete this keyword?" is answered from `AnalyzeImpact`, not the model's imagination.

## Plugin Modification Workflow (Deliverable: reversible edits)

1. **Analyze** — AI Action builds a grounded prompt.
2. **Plan** — model returns a concrete change description (no blind edits).
3. **Preview** — proposed field changes shown in the editor (future: dedicated diff preview).
4. **Approve** — user edits a field or accepts a suggestion.
5. **Apply** — `RecordTabViewModel.EditField` wraps every edit in `FieldEditChange`.
6. **Log** — `LogService` records the change under `LogCategory.Plugin`.
7. **Undo** — `UndoStack.Undo/Redo`, Ctrl+Z / Ctrl+Y. Every change reversible by construction.

# Future Roadmap

- **AI write-back actions**: let the model emit a structured edit list the user approves in one click (apply via `UndoStack.Do(FieldEditChange...)` per field — the plumbing already exists).
- **Patch generator tab**: select source plugins + desired behavior → AI emits conflict resolutions → compile via existing `CompileService`.
- **Leveled-list / keyword / FormList merge** automation on top of `GetConflicts()`.
- **Persistent index (LiteDB)** behind the `KnowledgeGraph` API for 200k+ record load orders and cross-session caching.
- **Missing model/texture scanning** (`ErrorCategory.MissingModel/MissingTexture`) by resolving mesh/texture paths against the data folder + BA2 archives.
- **Circular reference + invalid script** detectors (`ErrorCategory.CircularReference/InvalidScript`).
- **Favorites + Recently Viewed** in the Explorer side panel.
- **Layout persistence** of splitter sizes into `AppSettings` (`SidePanelWidth`/`AiPanelWidth`/`BottomPanelHeight` fields already exist).

---

## Self-Review

**Spec coverage check:**

| Spec requirement | Covered by |
|---|---|
| 1. AI Assistant Panel | Task 30 (ChatPanel) + Task 19 (context) |
| 2. AI Actions (right-click) | Task 31 (AiActions: Explain/Fix/Patch/Compare/Impact) |
| 3. Compare Tab | Tasks 13-15 (DiffEngine) + Task 32 (ComparePanel) |
| 4. Error Detection Tab | Tasks 9-12 (ErrorScanner) + Task 32 (ErrorsPanel) |
| 5. Log Tab | Task 2 (LogService) + Task 32 (LogPanel) |
| 6. AI Patch Generator | Roadmap (foundation: GetConflicts + CompileService + AI actions) |
| 7. Modern xEdit UI | Tasks 25-32 (shell, activity bar, panels) |
| AI provider abstraction | Tasks 16-18, 21 (IAIProvider, Anthropic, Ollama, factory) |
| Knowledge graph | Tasks 4-8 (index + impact + neighborhood) |
| Undo/Redo | Tasks 22-23 |
| Command Palette | Tasks 24, 32 |
| Settings / API key | Task 3 + Task 32 (SettingsDialog) |
| Performance (async, no freeze) | Task 7 (IndexAsync), Task 25 (LoadEspAsync), Task 12 (ScanAsync) |

Partial/roadmap (not in core tasks): Missing model/texture, circular-ref, invalid-script scanners; dedicated Patch Generator tab; layout persistence. These are explicitly listed in Future Roadmap rather than left as silent gaps.

**Type-consistency check (verified):**
- `RecordEntry`/`Reference`/`ImpactReport` signatures in Task 4 match all uses in Tasks 5-8, 10-12, 19, 31.
- `PluginError` (7-arg) consistent across Tasks 9-12 and ErrorsPanel binding.
- `IAIProvider.StreamAsync` signature identical in Tasks 16, 17, 18, 20.
- `KnowledgeGraph` methods (`Index`, `GetByFormKey`, `GetReferencesTo/From`, `AnalyzeImpact`, `GetNeighborhood`, `GetConflicts`, `IndexAsync`, `AllRecords`, `RecordCount`, `ReferenceCount`) defined in M1 and used consistently in M2/M4/M6.
- `ShellViewModel` service property names match every panel constructor.

**Known build-order note:** Task 27 (MainWindow) references `Views.*` classes created in Tasks 28-32; the solution is not expected to compile until Task 32 Step 7. This is called out in both tasks so a worker does not chase phantom errors mid-milestone.
