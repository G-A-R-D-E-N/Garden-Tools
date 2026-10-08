using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

/// <summary>A plugin being written (radicle_plugin); not thread-safe.</summary>
public sealed unsafe class PluginFile : IDisposable
{
    private IntPtr _handle;

    static PluginFile() => RadicleLibrary.Register();

    private PluginFile(IntPtr handle) => _handle = handle;

    internal IntPtr Handle => _handle != IntPtr.Zero ? _handle : throw new ObjectDisposedException(nameof(PluginFile));

    public static PluginFile Create(string filename)
    {
        IntPtr handle;
        Call.Check(RadicleNative.PluginCreate(filename, &handle));
        return new PluginFile(handle);
    }

    public static PluginFile Read(string path)
    {
        IntPtr handle;
        Call.Check(RadicleNative.PluginReadFile(path, &handle));
        return new PluginFile(handle);
    }

    public void Write(string path) => Call.Check(RadicleNative.PluginWriteFile(Handle, path));

    public byte[] ToBytes()
    {
        byte* bytes;
        nuint size;
        Call.Check(RadicleNative.PluginWriteBytes(Handle, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    public PlacedCopyStatus CopyPlacedOverride(RadicleEnvironment env, Record record, bool overwrite = false)
    {
        PlacedCopyStatus status;
        Call.Check(RadicleNative.PluginCopyPlacedRefOverride(Handle, env.Handle, record.Handle, overwrite ? 1 : 0, &status));
        return status;
    }

    /// <summary>A new reference owned by this plugin in the same CELL; the CELL must already be in this plugin.</summary>
    public FormKey CopyPlacedNew(RadicleEnvironment env, Record record)
    {
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCopyPlacedRefNew(Handle, env.Handle, record.Handle, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public void SortMasters(RadicleEnvironment env) => Call.Check(RadicleNative.PluginSortMasters(Handle, env.Handle));

    public bool SetTransform(FormKey reference, Vector3 position, Vector3 rotation) =>
        Edit(reference, (s, key) =>
        {
            var p = stackalloc float[] { position.X, position.Y, position.Z };
            var r = stackalloc float[] { rotation.X, rotation.Y, rotation.Z };
            return RadicleNative.PluginSetPlacedTransform(Handle, key, p, r);
        });

    public bool SetScale(FormKey reference, float? scale) =>
        Edit(reference, (s, key) => RadicleNative.PluginSetPlacedScale(Handle, key, scale.HasValue ? 1 : 0, scale ?? 0));

    public bool SetEditorId(FormKey reference, string? editorId) =>
        Edit(reference, (s, key) => RadicleNative.PluginSetPlacedEditorId(Handle, key, editorId));

    public bool SetRecordFlags(FormKey reference, uint flags) =>
        Edit(reference, (s, key) => RadicleNative.PluginSetPlacedRecordFlags(Handle, key, flags));

    public bool SetComments(FormKey reference, string? comments) =>
        Edit(reference, (s, key) => RadicleNative.PluginSetPlacedComments(Handle, key, comments));

    public bool SetLocationReference(RadicleEnvironment env, FormKey reference, FormKey? location) =>
        Edit(reference, (s, key) => RadicleNative.PluginSetPlacedLocationReference(Handle, env.Handle, key, s.Key(location)));

    public bool SetOwnership(RadicleEnvironment env, FormKey reference, FormKey? owner, bool noCrime, int? factionRank) =>
        Edit(reference, (s, key) => RadicleNative.PluginSetPlacedOwnership(Handle, env.Handle, key, s.Key(owner), noCrime ? 1 : 0,
                                                                             factionRank.HasValue ? 1 : 0, factionRank ?? 0));

    public bool SetEnableParent(RadicleEnvironment env, FormKey reference, FormKey? parent, byte flags) =>
        Edit(reference, (s, key) => RadicleNative.PluginSetPlacedEnableParent(Handle, env.Handle, key, s.Key(parent), flags));

    public bool SetLinkedReferences(RadicleEnvironment env, FormKey reference, IEnumerable<LinkedReference> links) =>
        Edit(reference, (s, key) =>
        {
            var native = links.Select(l => new LinkedRef { Keyword = s.Key(l.Keyword), Reference = s.Key(l.Reference) }).ToArray();
            fixed (LinkedRef* p = native)
            {
                return RadicleNative.PluginSetPlacedLinkedRefs(Handle, env.Handle, key, p, (nuint)native.Length);
            }
        });

    public bool SetTeleport(RadicleEnvironment env, FormKey reference, FormKey door, Vector3 position, Vector3 rotation) =>
        Edit(reference, (s, key) =>
        {
            var p = stackalloc float[] { position.X, position.Y, position.Z };
            var r = stackalloc float[] { rotation.X, rotation.Y, rotation.Z };
            return RadicleNative.PluginSetPlacedTeleport(Handle, env.Handle, key, s.Key(door), p, r);
        });

    public bool SetPrimitive(FormKey reference, Vector3 bounds, uint type) =>
        Edit(reference, (s, key) =>
        {
            var b = stackalloc float[] { bounds.X, bounds.Y, bounds.Z };
            return RadicleNative.PluginSetPlacedPrimitive(Handle, key, b, type);
        });

    public bool SetActivateParentDelay(FormKey reference, int index, float delay) =>
        Edit(reference, (s, key) => RadicleNative.PluginSetPlacedActivateParentDelay(Handle, key, (nuint)index, delay));

    public bool SetScalar(FormKey record, string path, string value, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(value);
        var at = Index(index);
        return Edit(record, (s, key) => RadicleNative.PluginSetRecordScalarAt(Handle, key, path, at, value));
    }

    public bool SetFormLink(FormKey record, string path, FormKey? target, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginSetRecordFormlink(Handle, key, path, at, target.HasValue ? &native : null);
        });
    }

    public bool SetString(FormKey record, string path, string value, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(value);
        if (value.Contains('\0')) throw new ArgumentException("A string field cannot hold an embedded NUL.", nameof(value));
        var at = Index(index);
        return Edit(record, (s, key) => RadicleNative.PluginSetRecordString(Handle, key, path, at, value));
    }

    public bool AddSubrecord(FormKey record, string signature)
    {
        ArgumentNullException.ThrowIfNull(signature);
        return Edit(record, (s, key) => RadicleNative.PluginAddSubrecord(Handle, key, signature));
    }

    public bool RemoveSubrecord(FormKey record, string signature, int occurrence = 0)
    {
        ArgumentNullException.ThrowIfNull(signature);
        var at = Index(occurrence);
        return Edit(record, (s, key) => RadicleNative.PluginRemoveSubrecord(Handle, key, signature, at));
    }

    public FormKey CreateRecord(string signature, string editorId)
    {
        ArgumentNullException.ThrowIfNull(signature);
        ArgumentNullException.ThrowIfNull(editorId);
        if (editorId.Contains('\0')) throw new ArgumentException("An editor ID cannot hold an embedded NUL.", nameof(editorId));
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateRecord(Handle, signature, editorId, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public int FieldCount(FormKey record, string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        using var strings = new NativeStrings();
        nuint count;
        return Call.Found(RadicleNative.PluginRecordFieldCount(Handle, strings.Key(record), path, &count)) ? checked((int)count) : 0;
    }

    public int FormLinkListCount(FormKey record, string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        using var strings = new NativeStrings();
        nuint count;
        return Call.Found(RadicleNative.PluginFormlinkListSize(Handle, strings.Key(record), path, &count)) ? checked((int)count) : 0;
    }

    public bool FormLinkListInsert(FormKey record, string path, int index, FormKey? target)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginFormlinkListInsert(Handle, key, path, at, target.HasValue ? &native : null);
        });
    }

    public bool FormLinkListRemove(FormKey record, string path, int index)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (s, key) => RadicleNative.PluginFormlinkListRemove(Handle, key, path, at));
    }

    public bool FormLinkListSet(FormKey record, string path, int index, FormKey? target)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginFormlinkListSet(Handle, key, path, at, target.HasValue ? &native : null);
        });
    }

    public bool FormLinkListClear(FormKey record, string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return Edit(record, (s, key) => RadicleNative.PluginFormlinkListClear(Handle, key, path));
    }

    public void Dispose()
    {
        var handle = System.Threading.Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero) RadicleNative.PluginDestroy(handle);
    }

    private static nuint Index(int index) =>
        index >= 0 ? (nuint)index : throw new ArgumentOutOfRangeException(nameof(index));

    private delegate Status NativeEdit(NativeStrings strings, Native.FormKey reference);

    private static bool Edit(FormKey reference, NativeEdit edit)
    {
        using var strings = new NativeStrings();
        return Call.Found(edit(strings, strings.Key(reference)));
    }
}
