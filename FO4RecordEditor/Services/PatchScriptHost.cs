using System.Text;
using Microsoft.CodeAnalysis.CSharp.Scripting;
using Microsoft.CodeAnalysis.Scripting;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Noggog;

namespace FO4RecordEditor.Services;

/// <summary>
/// The `host` object injected into every patch script. It exposes a high-level API over the loaded
/// records and a target patch plugin so a C# script can do PER-RECORD edits — the thing fixed op
/// sets (batch_patch_records) can't express. Discovery returns getters; <see cref="Cobj"/>/<see
/// cref="Override"/> forward a record into the patch as a mutable override; the component/condition
/// helpers edit that override. Nothing is written until the runner saves (apply mode); a dry run
/// executes the whole script against a throwaway patch and then discards it.
/// </summary>
public sealed class PatchScriptHost
{
    private readonly object? _env;
    private readonly string _patchPlugin;
    private readonly StringBuilder _log = new();
    private readonly HashSet<FormKey> _touched = new();

    /// <summary>Records forwarded into the patch (distinct).</summary>
    public int Applied { get; private set; }
    /// <summary>Individual field/component/condition edits performed.</summary>
    public int Edits { get; private set; }
    /// <summary>True on a preview run (the patch is discarded afterwards).</summary>
    public bool DryRun { get; }

    internal PatchScriptHost(object? env, string patchPlugin, bool dryRun)
    {
        _env = env;
        _patchPlugin = patchPlugin;
        DryRun = dryRun;
    }

    internal string LogText => _log.ToString();

    // ── discovery (getters; read-only) ────────────────────────────────────────────────────────
    /// <summary>All ConstructibleObject (COBJ) getters defined in a plugin.</summary>
    public IEnumerable<IConstructibleObjectGetter> Cobjs(string plugin) =>
        MutagenLoader.GetRecordsForBatch(_env, plugin, "ConstructibleObject").OfType<IConstructibleObjectGetter>();

    /// <summary>All records of a Mutagen type name (e.g. "Weapon", "Npc") defined in a plugin.</summary>
    public IEnumerable<IMajorRecordGetter> Records(string type, string plugin) =>
        MutagenLoader.GetRecordsForBatch(_env, plugin, type);

    // ── override (forward a getter into the patch as a mutable record) ────────────────────────
    /// <summary>Forward a COBJ getter into the patch and return the mutable override.</summary>
    public ConstructibleObject Cobj(IConstructibleObjectGetter getter) => (ConstructibleObject)OverrideRec(getter);

    /// <summary>Forward any record getter into the patch and return the mutable override.</summary>
    public IFallout4MajorRecord Override(IMajorRecordGetter getter) => OverrideRec(getter);

    private IFallout4MajorRecord OverrideRec(IMajorRecordGetter getter)
    {
        var rec = WriteService.OverrideForScript(_patchPlugin, _env, getter)
            ?? throw new InvalidOperationException(
                $"Could not override {getter.EditorID ?? getter.FormKey.ToString()} into '{_patchPlugin}' " +
                $"(type '{getter.Registration.Name}' may not be overridable).");
        if (_touched.Add(getter.FormKey)) Applied++;
        return rec;
    }

    // ── components (COBJ) ─────────────────────────────────────────────────────────────────────
    /// <summary>True if the recipe lists this component (FormKey or EditorID). Works on a getter or override.</summary>
    public bool HasComponent(IConstructibleObjectGetter rec, string component)
    {
        if (rec.Components == null || !Resolve(component, out var fk)) return false;
        foreach (var c in rec.Components) if (c.Component.FormKey == fk) return true;
        return false;
    }

    /// <summary>Append a component (material + count) to a recipe override.</summary>
    public void AddComponent(ConstructibleObject rec, string component, int count = 1)
    {
        if (!Resolve(component, out var fk)) { _log.AppendLine($"  ! AddComponent: cannot resolve '{component}'"); return; }
        rec.Components ??= new ExtendedList<ConstructibleObjectComponent>();
        var item = new ConstructibleObjectComponent { Count = (uint)Math.Max(0, count) };
        item.Component.SetTo(fk);
        rec.Components.Add(item);
        Edits++;
    }

    /// <summary>Remove every entry for a component from a recipe override. Returns how many were removed.</summary>
    public int RemoveComponent(ConstructibleObject rec, string component)
    {
        if (rec.Components == null || !Resolve(component, out var fk)) return 0;
        int removed = 0;
        for (int i = rec.Components.Count - 1; i >= 0; i--)
            if (rec.Components[i].Component.FormKey == fk) { rec.Components.RemoveAt(i); removed++; }
        if (removed > 0) Edits++;
        return removed;
    }

    /// <summary>Set the count for an existing component (no-op if the recipe doesn't list it). Returns true if changed.</summary>
    public bool SetCount(ConstructibleObject rec, string component, int count)
    {
        if (rec.Components == null || !Resolve(component, out var fk)) return false;
        bool any = false;
        foreach (var c in rec.Components)
            if (c.Component.FormKey == fk) { c.Count = (uint)Math.Max(0, count); any = true; }
        if (any) Edits++;
        return any;
    }

    /// <summary>Replace a component's FormLink with another, keeping its count unless <paramref name="count"/> >= 0.
    /// Returns true if at least one entry was swapped.</summary>
    public bool Swap(ConstructibleObject rec, string oldComponent, string newComponent, int count = -1)
    {
        if (rec.Components == null || !Resolve(oldComponent, out var oldFk) || !Resolve(newComponent, out var newFk))
            return false;
        bool any = false;
        foreach (var c in rec.Components)
            if (c.Component.FormKey == oldFk) { c.Component.SetTo(newFk); if (count >= 0) c.Count = (uint)count; any = true; }
        if (any) Edits++;
        return any;
    }

    /// <summary>Clear all components on a recipe override.</summary>
    public void ClearComponents(ConstructibleObject rec) { rec.Components?.Clear(); Edits++; }

    // ── conditions (COBJ and any record with a Conditions list) ───────────────────────────────
    /// <summary>Append a condition. function = HasPerk/GetItemCount/GetGlobalValue/GetBaseValue/...;
    /// param1/param2 = a FormKey/EditorID or an integer (as a string); op = == != &gt; &gt;= &lt; &lt;=;
    /// value = constant compared against; runOn = Subject/Target/Reference; reference = ref FormKey when
    /// runOn=Reference; compareGlobal = global FormKey to compare against instead of a constant;
    /// flags = e.g. "UseOr".</summary>
    public void AddCondition(ConstructibleObject rec, string function, string? param1 = null, string? param2 = null,
        string op = "==", float value = 1f, string? runOn = null, string? reference = null,
        string? compareGlobal = null, string? flags = null)
    {
        var cond = WriteService.BuildConditionTyped(_env, function, param1, param2, op, value, runOn, reference, compareGlobal, flags, out var err);
        if (cond == null) { _log.AppendLine($"  ! AddCondition: {err}"); return; }
        rec.Conditions.Add(cond);
        Edits++;
    }

    /// <summary>Remove conditions matching a function (and optionally a param1 FormKey/EditorID or integer).
    /// Returns how many were removed.</summary>
    public int RemoveConditions(ConstructibleObject rec, string function, string? param1 = null)
    {
        if (!Enum.TryParse<Condition.Function>(function, ignoreCase: true, out var fn))
        { _log.AppendLine($"  ! RemoveConditions: unknown function '{function}'"); return 0; }

        FormKey? pfk = null; int? pnum = null;
        if (!string.IsNullOrWhiteSpace(param1))
        {
            if (int.TryParse(param1, out var n)) pnum = n;
            else if (Resolve(param1!, out var f)) pfk = f;
        }

        int removed = 0;
        for (int i = rec.Conditions.Count - 1; i >= 0; i--)
        {
            if (rec.Conditions[i].Data is not IFunctionConditionDataGetter d || d.Function != fn) continue;
            if (pfk.HasValue && d.ParameterOneRecord.FormKey != pfk.Value) continue;
            if (pnum.HasValue && d.ParameterOneNumber != pnum.Value) continue;
            rec.Conditions.RemoveAt(i); removed++;
        }
        if (removed > 0) Edits++;
        return removed;
    }

    /// <summary>Clear all conditions on a recipe override.</summary>
    public void ClearConditions(ConstructibleObject rec) { rec.Conditions.Clear(); Edits++; }

    // ── scalar / nested fields (any record) ───────────────────────────────────────────────────
    /// <summary>Set a scalar / nested field on an override (same path syntax as set_field). Returns true on success.</summary>
    public bool Set(IFallout4MajorRecord rec, string field, string value)
    {
        bool ok = WriteService.TrySetField(rec, field, value, _env, out var msg);
        if (ok) Edits++; else _log.AppendLine($"  ! Set {field}: {msg}");
        return ok;
    }

    // ── utility ───────────────────────────────────────────────────────────────────────────────
    /// <summary>Resolve a FormKey or EditorID to its "XXXXXX:Plugin.esp" string, or null if unknown.</summary>
    public string? Resolve(string idOrEditorId) => Resolve(idOrEditorId, out var fk) ? fk.ToString() : null;

    /// <summary>Write a line to the script's output log (shown back to you when the run finishes).</summary>
    public void Log(string message) => _log.AppendLine(message);

    private bool Resolve(string id, out FormKey fk)
    {
        if (FormKey.TryFactory(id, out fk)) return true;
        var cache = MutagenLoader.LinkCache;
        if (cache != null && cache.TryResolve(id, out var rec)) { fk = rec.FormKey; return true; }
        fk = MutagenLoader.ResolveEditorIdToFormKey(_env, id);
        return !fk.IsNull;
    }
}

/// <summary>Globals surface for a patch script: the script references <c>host</c>.</summary>
public sealed class PatchScriptGlobals
{
    public PatchScriptHost host { get; set; } = null!;
}

/// <summary>Compiles and runs a write-capable C# patch script authored by the AI.</summary>
public static class PatchScriptRunner
{
    private static readonly ScriptOptions _options = ScriptOptions.Default
        .AddImports(
            "System",
            "System.Linq",
            "System.Collections.Generic",
            "Mutagen.Bethesda.Fallout4",
            "Mutagen.Bethesda.Plugins",
            "Mutagen.Bethesda.Plugins.Records",
            "FO4RecordEditor.Services")
        .AddReferences(
            typeof(PatchScriptHost).Assembly,
            typeof(IFallout4Mod).Assembly,            // Mutagen.Bethesda.Fallout4
            typeof(FormKey).Assembly,                 // Mutagen.Bethesda.Core / Plugins
            typeof(IMajorRecordGetter).Assembly,
            typeof(ExtendedList<>).Assembly,          // Noggog
            typeof(Enumerable).Assembly);

    /// <summary>
    /// Run <paramref name="code"/> against the loaded env, forwarding edits into <paramref name="patchPlugin"/>.
    /// On a dry run the script executes against a throwaway "<patch>.preview" plugin that is discarded
    /// afterwards, so you get accurate counts and logs with zero writes and zero in-memory pollution.
    /// </summary>
    public static string Run(string code, object? env, string patchPlugin, bool dryRun)
    {
        if (string.IsNullOrWhiteSpace(code)) return "Provide a script in 'script'.";
        if (string.IsNullOrWhiteSpace(patchPlugin)) return "Provide 'patch_plugin' (the plugin to write overrides into).";

        // Route a dry run into a disposable patch so the real one is never touched.
        var target = dryRun ? AddPreviewSuffix(patchPlugin) : patchPlugin;
        var host = new PatchScriptHost(env, target, dryRun);
        var sw = System.Diagnostics.Stopwatch.StartNew();

        string? error = null;
        try
        {
            CSharpScript.RunAsync(code, _options, new PatchScriptGlobals { host = host })
                .GetAwaiter().GetResult();
        }
        catch (CompilationErrorException ex)
        {
            error = "Compile error:\n" + string.Join("\n", ex.Diagnostics);
        }
        catch (Exception ex)
        {
            error = "Runtime error: " + (ex.InnerException?.Message ?? ex.Message);
        }
        sw.Stop();

        var sb = new StringBuilder();
        var log = host.LogText;
        if (log.Length > 0) sb.Append(log);

        if (error != null)
        {
            // A failed run must not leave half-built overrides behind.
            WriteService.DiscardScriptPatch(target);
            sb.AppendLine($"\nSCRIPT FAILED ({sw.ElapsedMilliseconds} ms) — nothing saved.");
            sb.AppendLine(error);
            return sb.ToString();
        }

        sb.AppendLine($"\n{(dryRun ? "DRY RUN" : "APPLIED")}: {host.Applied} record(s) overridden, " +
                      $"{host.Edits} edit(s), {sw.ElapsedMilliseconds} ms.");

        if (dryRun)
        {
            WriteService.DiscardScriptPatch(target);
            sb.AppendLine($"Nothing was written. Re-run with dry_run=false to apply into '{patchPlugin}'.");
        }
        else
        {
            sb.Append(WriteService.SaveScriptPatch(patchPlugin));
        }
        return sb.ToString();
    }

    private static string AddPreviewSuffix(string plugin)
    {
        var dot = plugin.LastIndexOf('.');
        return dot < 0 ? plugin + ".preview.esp" : plugin[..dot] + ".preview" + plugin[dot..];
    }
}
