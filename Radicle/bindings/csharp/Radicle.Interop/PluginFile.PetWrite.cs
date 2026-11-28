using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record EslCompaction(int Remapped, uint NextFormId);

public sealed record DeletedCleanup(int Undeleted, int Skipped);

public sealed unsafe partial class PluginFile
{
    public EslCompaction CompactToEsl()
    {
        using var use = Use();
        nuint remapped;
        uint next;
        Call.Check(RadicleNative.PluginCompactToEsl(use.Pointer, &remapped, &next));
        return new EslCompaction(checked((int)remapped), next);
    }

    public void SetLightFlag(bool light)
    {
        using var use = Use();
        Call.Check(RadicleNative.PluginSetLightFlag(use.Pointer, light ? 1 : 0));
    }

    public int ListSize(FormKey record, string path) =>
        TryListSize(record, path, out var count) ? count : throw new KeyNotFoundException($"Record not found: {record}");

    public bool TryListSize(FormKey record, string path, out int count)
    {
        ArgumentNullException.ThrowIfNull(path);
        using var use = Use();
        using var strings = new NativeStrings();
        nuint native;
        var found = Call.Found(RadicleNative.PluginListSize(use.Pointer, strings.Key(record), path, &native));
        count = found ? checked((int)native) : 0;
        return found;
    }

    public bool ListInsert(FormKey record, string path, int index, FormKey? target = null)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginListInsert(plugin, key, path, at, target.HasValue ? &native : null);
        });
    }

    public bool ListRemove(FormKey record, string path, int index)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginListRemove(plugin, key, path, at));
    }

    public bool SetField(FormKey record, string path, string value, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(value);
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetRecordField(plugin, key, path, at, value));
    }

    public void LoadStrings(string stringsDirectory, string language)
    {
        ArgumentNullException.ThrowIfNull(stringsDirectory);
        ArgumentNullException.ThrowIfNull(language);
        using var use = Use();
        Call.Check(RadicleNative.PluginStringsLoad(use.Pointer, stringsDirectory, language));
    }

    public void SaveStrings(string stringsDirectory, string language, string? pluginFileName = null)
    {
        ArgumentNullException.ThrowIfNull(stringsDirectory);
        ArgumentNullException.ThrowIfNull(language);
        using var use = Use();
        Call.Check(RadicleNative.PluginStringsSave(use.Pointer, pluginFileName, stringsDirectory, language));
    }

    public DeletedCleanup CleanDeleted()
    {
        using var use = Use();
        nuint undeleted;
        nuint skipped;
        Call.Check(RadicleNative.PluginCleanDeleted(use.Pointer, &undeleted, &skipped));
        return new DeletedCleanup(checked((int)undeleted), checked((int)skipped));
    }

    public bool RemoveRecord(FormKey record) =>
        Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveRecord(plugin, key));

    public FormKey? RenumberFormId(FormKey record, uint newLocalId, bool repointLinks = true)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.FormKey renamed;
        return Call.Found(RadicleNative.PluginRenumberFormId(use.Pointer, strings.Key(record), newLocalId, repointLinks ? 1 : 0, &renamed))
            ? FormKey.FromNative(renamed)
            : null;
    }

    public int RenumberAll(uint startLocalId)
    {
        using var use = Use();
        nuint count;
        Call.Check(RadicleNative.PluginRenumberAll(use.Pointer, startLocalId, &count));
        return checked((int)count);
    }

    public RecordCopyStatus CopyRecordVersion(RadicleEnvironment env, Record version, bool overwrite = false)
    {
        ArgumentNullException.ThrowIfNull(env);
        using var use = Use();
        using var envUse = env.Use(version);
        RecordCopyStatus status;
        Call.Check(RadicleNative.PluginCopyRecordVersion(use.Pointer, envUse.Pointer, version.Handle, overwrite ? 1 : 0, &status));
        return status;
    }

    public int? RetargetLinks(FormKey record, FormKey from, FormKey to)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        nuint changed;
        return Call.Found(RadicleNative.PluginRetargetLinks(use.Pointer, strings.Key(record), strings.Key(from), strings.Key(to), &changed))
            ? checked((int)changed)
            : null;
    }
}

public sealed record EslCompactionDetail(EslCompactionStatus Status, int Remapped, uint NextFormId, IReadOnlyList<string> Blockers);

public sealed record CleanedMasters(CleanMastersStatus Status, IReadOnlyList<string> Removed);

public sealed unsafe partial class PluginFile
{
    public FormKey CreatePlacedActor(FormKey cell, FormKey baseActor, Vector3 position, Vector3 rotation,
        bool persistent = false, bool initiallyDisabled = false)
    {
        using var use = Use();
        using var s = new NativeStrings();
        var p = stackalloc float[] { position.X, position.Y, position.Z };
        var r = stackalloc float[] { rotation.X, rotation.Y, rotation.Z };
        Native.FormKey created;
        Call.Check(RadicleNative.PluginCreatePlacedActor(use.Pointer, s.Key(cell), s.Key(baseActor), p, r,
            persistent ? 1 : 0, initiallyDisabled ? 1 : 0, &created));
        return FormKey.FromNative(created)!.Value;
    }

    public void SetNextFormId(uint nextFormId)
    {
        using var use = Use();
        Call.Check(RadicleNative.PluginSetHedrNextFormId(use.Pointer, nextFormId));
    }

    public EslCompactionDetail CompactToEslDetailed()
    {
        using var use = Use();
        EslCompactionStatus status;
        nuint remapped;
        uint next;
        IntPtr* blockers = null;
        nuint blockerCount = 0;
        Call.Check(RadicleNative.PluginCompactToEslDetailed(use.Pointer, &status, &remapped, &next, &blockers, &blockerCount));
        return new EslCompactionDetail(status, checked((int)remapped), next, Call.TakeStrings(blockers, blockerCount));
    }

    public RecordFlagStatus SetRecordFlagsDetailed(FormKey record, uint flags)
    {
        using var use = Use();
        using var s = new NativeStrings();
        RecordFlagStatus status;
        Call.Check(RadicleNative.PluginSetRecordFlags(use.Pointer, s.Key(record), flags, &status));
        return status;
    }

    public MarkDeletedStatus MarkOverrideDeleted(FormKey record)
    {
        using var use = Use();
        using var s = new NativeStrings();
        MarkDeletedStatus status;
        Call.Check(RadicleNative.PluginMarkOverrideDeleted(use.Pointer, s.Key(record), &status));
        return status;
    }

    public RecordDeleteStatus DeleteLocalRecord(FormKey record)
    {
        using var use = Use();
        using var s = new NativeStrings();
        RecordDeleteStatus status;
        Call.Check(RadicleNative.PluginDeleteLocalRecord(use.Pointer, s.Key(record), &status));
        return status;
    }

    public UndeleteStatus UndeleteAndDisableReference(RadicleEnvironment env, FormKey record)
    {
        ArgumentNullException.ThrowIfNull(env);
        using var use = Use();
        using var envUse = env.Use();
        using var s = new NativeStrings();
        UndeleteStatus status;
        Call.Check(RadicleNative.PluginUndeleteAndDisableReference(use.Pointer, envUse.Pointer, s.Key(record), &status));
        return status;
    }

    public RecordCopyStatus CopyRecordDeep(PluginFile source, FormKey record, bool overwrite = false)
    {
        ArgumentNullException.ThrowIfNull(source);
        using var use = Use();
        using var sourceUse = source.Use();
        using var s = new NativeStrings();
        RecordCopyStatus status;
        Call.Check(RadicleNative.PluginCopyRecordDeep(use.Pointer, sourceUse.Pointer, s.Key(record), overwrite ? 1 : 0, &status));
        return status;
    }

    public void RenumberRecord(FormKey record, uint newLocalId)
    {
        using var use = Use();
        using var s = new NativeStrings();
        Call.Check(RadicleNative.PluginRenumberRecord(use.Pointer, s.Key(record), newLocalId));
    }

    public void InjectLocalRecord(FormKey record, FormKey newKey)
    {
        using var use = Use();
        using var s = new NativeStrings();
        Call.Check(RadicleNative.PluginInjectLocalRecord(use.Pointer, s.Key(record), s.Key(newKey)));
    }

    /// <summary>Requires at least one plugin; from and to must share one owner.</summary>
    public static int RepointRecordReferences(IReadOnlyList<PluginFile> plugins, FormKey from, FormKey to)
    {
        ArgumentNullException.ThrowIfNull(plugins);
        if (plugins.Count == 0) throw new ArgumentException("At least one plugin is required.", nameof(plugins));
        var uses = new List<HandleUse>(plugins.Count);
        try
        {
            foreach (var plugin in plugins) uses.Add(plugin.Use());
            using var s = new NativeStrings();
            var handles = uses.Select(u => u.Pointer).ToArray();
            nuint count;
            fixed (IntPtr* p = handles)
                Call.Check(RadicleNative.PluginRepointRecordReferences(p, (nuint)handles.Length, s.Key(from), s.Key(to), &count));
            return checked((int)count);
        }
        finally { foreach (var use in uses) use.Dispose(); }
    }

    public bool InsertGroupElement(FormKey record, string groupPath, int index)
    {
        ArgumentNullException.ThrowIfNull(groupPath);
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSubrecordGroupInsert(plugin, key, groupPath, at));
    }

    public bool RemoveGroupElement(FormKey record, string groupPath, int index)
    {
        ArgumentNullException.ThrowIfNull(groupPath);
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSubrecordGroupRemove(plugin, key, groupPath, at));
    }

    public bool InsertGroupRunElement(FormKey record, string groupPath, int run, int runIndex)
    {
        ArgumentNullException.ThrowIfNull(groupPath);
        var runAt = Index(run);
        var at = Index(runIndex);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSubrecordGroupRunInsert(plugin, key, groupPath, runAt, at));
    }

    public bool RemoveGroupRunElement(FormKey record, string groupPath, int run, int runIndex)
    {
        ArgumentNullException.ThrowIfNull(groupPath);
        var runAt = Index(run);
        var at = Index(runIndex);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSubrecordGroupRunRemove(plugin, key, groupPath, runAt, at));
    }

    public int RemoveIdenticalToPreviousOverrides(RadicleEnvironment env)
    {
        ArgumentNullException.ThrowIfNull(env);
        using var use = Use();
        using var envUse = env.Use();
        nuint removed;
        Call.Check(RadicleNative.PluginRemoveIdenticalToPreviousOverride(use.Pointer, envUse.Pointer, &removed));
        return checked((int)removed);
    }

    public MasterAddStatus AddMasterDetailed(string filename, PluginKind kind)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(filename);
        using var use = Use();
        MasterAddStatus status;
        Call.Check(RadicleNative.PluginAddMasterDetailed(use.Pointer, filename, kind, &status));
        return status;
    }

    public CleanedMasters CleanUnusedMasters()
    {
        using var use = Use();
        CleanMastersStatus status;
        IntPtr* removed = null;
        nuint count = 0;
        Call.Check(RadicleNative.PluginCleanUnusedMasters(use.Pointer, &status, &removed, &count));
        return new CleanedMasters(status, Call.TakeStrings(removed, count));
    }

    public static void WritePluginList(string path, IReadOnlyList<(string Name, bool Active)> entries)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(entries);
        using var s = new NativeStrings();
        var names = entries.Select(e => s.Add(e.Name)).ToArray();
        var active = entries.Select(e => e.Active ? 1 : 0).ToArray();
        fixed (IntPtr* n = names)
        fixed (int* a = active)
            Call.Check(RadicleNative.PluginListWrite(path, n, a, (nuint)names.Length));
    }
}

public sealed unsafe partial class RadicleEnvironment
{
    public IReadOnlyList<IdenticalToMasterRow> GetIdenticalToPreviousOverride(string plugin, int limit = 0)
    {
        ArgumentNullException.ThrowIfNull(plugin);
        if (limit < 0) throw new ArgumentOutOfRangeException(nameof(limit));
        using var use = Use();
        IntPtr* items = null;
        nuint count = 0;
        Call.Check(RadicleNative.EnvIdenticalToPreviousOverride(use.Pointer, plugin, (nuint)limit, &items, &count));
        return Call.TakeStrings(items, count).Select(ParseIdenticalRow).ToArray();
    }
}

public sealed record LandAlphaLayerResult(LandMutationStatus Status, int Index);

public sealed unsafe partial class PluginFile
{
    public LandMutationStatus SetLandHeights(FormKey land, float[] heights)
    {
        ArgumentNullException.ThrowIfNull(heights);
        using var use = Use();
        using var s = new NativeStrings();
        LandMutationStatus status;
        fixed (float* p = heights)
            Call.Check(RadicleNative.PluginSetLandHeights(use.Pointer, s.Key(land), p, (nuint)heights.Length, &status));
        return status;
    }

    public LandMutationStatus SetLandNormals(FormKey land, byte[] normals)
    {
        ArgumentNullException.ThrowIfNull(normals);
        using var use = Use();
        using var s = new NativeStrings();
        LandMutationStatus status;
        fixed (byte* p = normals)
            Call.Check(RadicleNative.PluginSetLandNormals(use.Pointer, s.Key(land), p, (nuint)normals.Length, &status));
        return status;
    }

    public LandMutationStatus SetLandVertexColors(FormKey land, byte[] colors)
    {
        ArgumentNullException.ThrowIfNull(colors);
        using var use = Use();
        using var s = new NativeStrings();
        LandMutationStatus status;
        fixed (byte* p = colors)
            Call.Check(RadicleNative.PluginSetLandVertexColors(use.Pointer, s.Key(land), p, (nuint)colors.Length, &status));
        return status;
    }

    public LandMutationStatus SetLandBaseTexture(FormKey land, byte quadrant, FormKey texture, ushort layer = 0)
    {
        using var use = Use();
        using var s = new NativeStrings();
        LandMutationStatus status;
        Call.Check(RadicleNative.PluginSetLandBaseTexture(use.Pointer, s.Key(land), quadrant, s.Key(texture), layer, &status));
        return status;
    }

    public LandAlphaLayerResult AddLandAlphaLayer(FormKey land, byte quadrant, FormKey texture, ushort layer, float[] alpha)
    {
        ArgumentNullException.ThrowIfNull(alpha);
        using var use = Use();
        using var s = new NativeStrings();
        LandMutationStatus status;
        nuint index = 0;
        fixed (float* p = alpha)
            Call.Check(RadicleNative.PluginAddLandAlphaLayer(use.Pointer, s.Key(land), quadrant, s.Key(texture), layer, p,
                (nuint)alpha.Length, &index, &status));
        return new LandAlphaLayerResult(status, checked((int)index));
    }

    public LandMutationStatus RemoveLandAlphaLayer(FormKey land, int index)
    {
        var at = Index(index);
        using var use = Use();
        using var s = new NativeStrings();
        LandMutationStatus status;
        Call.Check(RadicleNative.PluginRemoveLandAlphaLayer(use.Pointer, s.Key(land), at, &status));
        return status;
    }
}
