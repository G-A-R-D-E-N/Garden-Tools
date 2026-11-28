using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

/// <summary>A plugin being written (radicle_plugin); not thread-safe.</summary>
public sealed unsafe partial class PluginFile : IDisposable
{
    private readonly PluginHandle _handle;

    static PluginFile() => RadicleLibrary.Register();

    private PluginFile(IntPtr handle) => _handle = new PluginHandle(handle);

    internal HandleUse Use() => new(_handle, nameof(PluginFile));

    internal static PluginFile FromHandle(IntPtr handle) => new(handle);

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

    public void Write(string path)
    {
        using var use = Use();
        Call.Check(RadicleNative.PluginWriteFile(use.Pointer, path));
    }

    public byte[] ToBytes()
    {
        using var use = Use();
        byte* bytes;
        nuint size;
        Call.Check(RadicleNative.PluginWriteBytes(use.Pointer, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    public PlacedCopyStatus CopyPlacedOverride(RadicleEnvironment env, Record record, bool overwrite = false)
    {
        using var use = Use();
        using var envUse = env.Use(record);
        PlacedCopyStatus status;
        Call.Check(RadicleNative.PluginCopyPlacedRefOverride(use.Pointer, envUse.Pointer, record.Handle, overwrite ? 1 : 0, &status));
        return status;
    }

    /// <summary>A new reference owned by this plugin in the same CELL; the CELL must already be in this plugin.</summary>
    public FormKey CopyPlacedNew(RadicleEnvironment env, Record record)
    {
        using var use = Use();
        using var envUse = env.Use(record);
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCopyPlacedRefNew(use.Pointer, envUse.Pointer, record.Handle, &key));
        return FormKey.FromNative(key)!.Value;
    }

    /// <summary>Constraint: the kind follows the extension, and a listed master must keep its position.</summary>
    public void AddMaster(string filename)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(filename);
        AddMaster(filename, filename.EndsWith(".esl", StringComparison.OrdinalIgnoreCase) ? PluginKind.Light : PluginKind.Full);
    }

    public void AddMaster(string filename, PluginKind kind)
    {
        using var use = Use();
        Call.Check(RadicleNative.PluginAddMaster(use.Pointer, filename, kind));
    }

    public IReadOnlyList<string> Masters
    {
        get
        {
            using var use = Use();
            IntPtr* names = null;
            nuint count = 0;
            Call.Check(RadicleNative.PluginMasters(use.Pointer, &names, &count));
            return Call.TakeStrings(names, count);
        }
    }

    public void SortMasters(RadicleEnvironment env)
    {
        using var use = Use();
        using var envUse = env.Use();
        Call.Check(RadicleNative.PluginSortMasters(use.Pointer, envUse.Pointer));
    }

    public bool SetTransform(FormKey reference, Vector3 position, Vector3 rotation) =>
        Edit(reference, (plugin, s, key) =>
        {
            var p = stackalloc float[] { position.X, position.Y, position.Z };
            var r = stackalloc float[] { rotation.X, rotation.Y, rotation.Z };
            return RadicleNative.PluginSetPlacedTransform(plugin, key, p, r);
        });

    public PlacedMoveStatus MoveTransform(FormKey reference, Vector3 position, Vector3 rotation)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        var p = stackalloc float[] { position.X, position.Y, position.Z };
        var r = stackalloc float[] { rotation.X, rotation.Y, rotation.Z };
        PlacedMoveStatus status;
        Call.Check(RadicleNative.PluginMovePlacedRef(use.Pointer, strings.Key(reference), p, r, &status));
        return status;
    }

    public ConditionWriteStatus SetConditions(FormKey record, string? scopePath, IReadOnlyList<RadicleCondition> conditions)
    {
        ArgumentNullException.ThrowIfNull(conditions);
        using var use = Use();
        using var strings = new NativeStrings();
        var rows = conditions.Select(c => new Native.Condition
        {
            FlagsByte = c.FlagsByte, ComparisonRaw = c.ComparisonRaw, Function = c.Function, Unknown2 = c.Unknown2,
            Param1 = c.Param1, Param2 = c.Param2, RunOn = c.RunOn, Reference = c.Reference, Unknown3 = c.Unknown3,
            Parameter1String = strings.Add(c.Parameter1String), Parameter2String = strings.Add(c.Parameter2String),
        }).ToArray();
        ConditionWriteStatus status;
        fixed (Native.Condition* p = rows)
            Call.Check(RadicleNative.PluginSetConditions(use.Pointer, strings.Key(record), scopePath, p, (nuint)rows.Length, &status));
        return status;
    }

    public bool SetScale(FormKey reference, float? scale) =>
        Edit(reference, (plugin, s, key) => RadicleNative.PluginSetPlacedScale(plugin, key, scale.HasValue ? 1 : 0, scale ?? 0));

    public bool SetEditorId(FormKey reference, string? editorId) =>
        Edit(reference, (plugin, s, key) => RadicleNative.PluginSetPlacedEditorId(plugin, key, editorId));

    public bool SetRecordFlags(FormKey reference, uint flags) =>
        Edit(reference, (plugin, s, key) => RadicleNative.PluginSetPlacedRecordFlags(plugin, key, flags));

    public bool SetComments(FormKey reference, string? comments) =>
        Edit(reference, (plugin, s, key) => RadicleNative.PluginSetPlacedComments(plugin, key, comments));

    public bool SetLocationReference(RadicleEnvironment env, FormKey reference, FormKey? location)
    {
        using var envUse = env.Use();
        return Edit(reference, (plugin, s, key) => RadicleNative.PluginSetPlacedLocationReference(plugin, envUse.Pointer, key, s.Key(location)));
    }

    public bool SetOwnership(RadicleEnvironment env, FormKey reference, FormKey? owner, bool noCrime, int? factionRank)
    {
        using var envUse = env.Use();
        return Edit(reference, (plugin, s, key) => RadicleNative.PluginSetPlacedOwnership(plugin, envUse.Pointer, key, s.Key(owner),
                                                                                         noCrime ? 1 : 0, factionRank.HasValue ? 1 : 0,
                                                                                         factionRank ?? 0));
    }

    public bool SetEnableParent(RadicleEnvironment env, FormKey reference, FormKey? parent, byte flags)
    {
        using var envUse = env.Use();
        return Edit(reference, (plugin, s, key) => RadicleNative.PluginSetPlacedEnableParent(plugin, envUse.Pointer, key, s.Key(parent), flags));
    }

    public bool SetLinkedReferences(RadicleEnvironment env, FormKey reference, IEnumerable<LinkedReference> links)
    {
        using var envUse = env.Use();
        return Edit(reference, (plugin, s, key) =>
        {
            var native = links.Select(l => new LinkedRef { Keyword = s.Key(l.Keyword), Reference = s.Key(l.Reference) }).ToArray();
            fixed (LinkedRef* p = native)
            {
                return RadicleNative.PluginSetPlacedLinkedRefs(plugin, envUse.Pointer, key, p, (nuint)native.Length);
            }
        });
    }

    public bool SetTeleport(RadicleEnvironment env, FormKey reference, FormKey door, Vector3 position, Vector3 rotation)
    {
        using var envUse = env.Use();
        return Edit(reference, (plugin, s, key) =>
        {
            var p = stackalloc float[] { position.X, position.Y, position.Z };
            var r = stackalloc float[] { rotation.X, rotation.Y, rotation.Z };
            return RadicleNative.PluginSetPlacedTeleport(plugin, envUse.Pointer, key, s.Key(door), p, r);
        });
    }

    public bool SetPrimitive(FormKey reference, Vector3 bounds, uint type) =>
        Edit(reference, (plugin, s, key) =>
        {
            var b = stackalloc float[] { bounds.X, bounds.Y, bounds.Z };
            return RadicleNative.PluginSetPlacedPrimitive(plugin, key, b, type);
        });

    public bool SetActivateParentDelay(FormKey reference, int index, float delay)
    {
        var at = Index(index);
        return Edit(reference, (plugin, s, key) => RadicleNative.PluginSetPlacedActivateParentDelay(plugin, key, at, delay));
    }

    public bool SetClimateTiming(FormKey climate, ClimateTiming timing)
    {
        ArgumentNullException.ThrowIfNull(timing);
        return Edit(climate, (plugin, s, key) =>
        {
            var value = new ClimateTimingEdit
            {
                SunriseBegin = timing.SunriseBegin, SunriseEnd = timing.SunriseEnd,
                SunsetBegin = timing.SunsetBegin, SunsetEnd = timing.SunsetEnd,
                Volatility = timing.Volatility, MoonsPhaseLength = timing.MoonsPhaseLength,
            };
            return RadicleNative.PluginSetClimateTiming(plugin, key, &value);
        });
    }

    public bool SetClimateWeathers(FormKey climate, IReadOnlyList<ClimateWeather> weathers)
    {
        ArgumentNullException.ThrowIfNull(weathers);
        return Edit(climate, (plugin, s, key) =>
        {
            var native = weathers.Select(w => new Native.ClimateWeather { Weather = s.Key(w.Weather), Chance = w.Chance, Global = s.Key(w.Global) }).ToArray();
            fixed (Native.ClimateWeather* p = native)
            {
                return RadicleNative.PluginSetClimateWeathers(plugin, key, p, (nuint)native.Length);
            }
        });
    }

    public bool SetWeatherColors(FormKey weather, byte group, IReadOnlyList<ByteColor> times)
    {
        ArgumentNullException.ThrowIfNull(times);
        return Edit(weather, (plugin, s, key) =>
        {
            var native = times.Select(c => new Native.ByteColor { Red = c.Red, Green = c.Green, Blue = c.Blue }).ToArray();
            fixed (Native.ByteColor* p = native)
            {
                return RadicleNative.PluginSetWeatherColors(plugin, key, group, p, (nuint)native.Length);
            }
        });
    }

    public bool SetWeatherFog(FormKey weather, IReadOnlyList<float> distances)
    {
        ArgumentNullException.ThrowIfNull(distances);
        var values = distances.ToArray();
        return Edit(weather, (plugin, s, key) =>
        {
            fixed (float* p = values)
            {
                return RadicleNative.PluginSetWeatherFog(plugin, key, p, (nuint)values.Length);
            }
        });
    }

    public bool SetWeatherTransition(FormKey weather, byte transitionDelta)
    {
        return Edit(weather, (plugin, s, key) => RadicleNative.PluginSetWeatherTransition(plugin, key, transitionDelta));
    }

    public bool SetVmadObject(FormKey record, int script, int property, FormKey? target, short alias = -1, int element = 0)
    {
        var si = Index(script); var pi = Index(property); var ei = Index(element);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetVmadObject(plugin, key, si, pi, ei, s.Key(target), alias));
    }

    public bool SetVmadString(FormKey record, int script, int property, string value, int element = 0)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.Contains('\0')) throw new ArgumentException("VMAD text cannot hold an embedded NUL through this API.", nameof(value));
        var si = Index(script); var pi = Index(property); var ei = Index(element);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetVmadString(plugin, key, si, pi, ei, value));
    }

    public bool SetVmadInt(FormKey record, int script, int property, int value, int element = 0)
    {
        var si = Index(script); var pi = Index(property); var ei = Index(element);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetVmadInt(plugin, key, si, pi, ei, value));
    }

    public bool SetVmadFloat(FormKey record, int script, int property, float value, int element = 0)
    {
        var si = Index(script); var pi = Index(property); var ei = Index(element);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetVmadFloat(plugin, key, si, pi, ei, value));
    }

    public bool SetVmadBool(FormKey record, int script, int property, bool value, int element = 0)
    {
        var si = Index(script); var pi = Index(property); var ei = Index(element);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetVmadBool(plugin, key, si, pi, ei, value ? 1 : 0));
    }

    public bool SetQuestVmadFragment(FormKey record, ushort stage, int stageIndex, string script, string function)
    {
        ArgumentNullException.ThrowIfNull(script);
        ArgumentNullException.ThrowIfNull(function);
        if (script.Contains('\0')) throw new ArgumentException("VMAD fragment script name cannot hold an embedded NUL through this API.", nameof(script));
        if (function.Contains('\0')) throw new ArgumentException("VMAD fragment function name cannot hold an embedded NUL through this API.", nameof(function));
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetQuestVmadFragment(plugin, key, stage, stageIndex, script, function));
    }

    public int AddPackageProcedure(FormKey record, int parentIndex, string procedure)
    {
        ArgumentNullException.ThrowIfNull(procedure);
        if (procedure.Contains('\0')) throw new ArgumentException("Procedure name cannot hold an embedded NUL through this API.", nameof(procedure));
        var parent = Index(parentIndex);
        using var use = Use();
        using var strings = new NativeStrings();
        nuint added;
        Call.Check(RadicleNative.PackageProcedureAdd(use.Pointer, strings.Key(record), parent, procedure, &added));
        return (int)added;
    }

    public void RemovePackageProcedure(FormKey record, int nodeIndex)
    {
        var at = Index(nodeIndex);
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PackageProcedureRemove(use.Pointer, strings.Key(record), at));
    }

    public int AddSoundFile(FormKey record, string file)
    {
        ArgumentNullException.ThrowIfNull(file);
        if (file.Contains('\0')) throw new ArgumentException("Sound file name cannot hold an embedded NUL through this API.", nameof(file));
        using var use = Use();
        using var strings = new NativeStrings();
        nuint added;
        Call.Check(RadicleNative.PluginAddSoundFile(use.Pointer, strings.Key(record), file, &added));
        return (int)added;
    }

    public void RemoveSoundFile(FormKey record, int fileIndex)
    {
        var at = Index(fileIndex);
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginRemoveSoundFile(use.Pointer, strings.Key(record), at));
    }

    public void SetSoundFile(FormKey record, int fileIndex, string file)
    {
        ArgumentNullException.ThrowIfNull(file);
        if (file.Contains('\0')) throw new ArgumentException("Sound file name cannot hold an embedded NUL through this API.", nameof(file));
        var at = Index(fileIndex);
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginSetSoundFile(use.Pointer, strings.Key(record), at, file));
    }

    public void AddSoundBaseLink(FormKey record, FormKey target)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginAddSoundBaseLink(use.Pointer, strings.Key(record), strings.Key(target)));
    }

    public void RemoveSoundBaseLink(FormKey record)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginRemoveSoundBaseLink(use.Pointer, strings.Key(record)));
    }

    public FormKey CreateSoundRecord(string signature, string editorId)
    {
        ArgumentNullException.ThrowIfNull(signature);
        ArgumentNullException.ThrowIfNull(editorId);
        using var use = Use();
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateSoundRecord(use.Pointer, signature, editorId, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public bool RemoveQuestVmadFragment(FormKey record, ushort stage, int stageIndex) =>
        Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveQuestVmadFragment(plugin, key, stage, stageIndex));

    public void AddVmad(FormKey record)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginAddVmad(use.Pointer, strings.Key(record)));
    }

    public int AddVmadScript(FormKey record, int index, string scriptName, byte flags = 0)
    {
        ArgumentException.ThrowIfNullOrEmpty(scriptName);
        if (scriptName.Contains('\0')) throw new ArgumentException("VMAD script name cannot hold an embedded NUL through this API.", nameof(scriptName));
        var at = Index(index);
        using var use = Use();
        using var strings = new NativeStrings();
        nuint added;
        Call.Check(RadicleNative.PluginAddVmadScript(use.Pointer, strings.Key(record), at, scriptName, flags, &added));
        return (int)added;
    }

    public void RemoveVmadScript(FormKey record, int scriptIndex)
    {
        var at = Index(scriptIndex);
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginRemoveVmadScript(use.Pointer, strings.Key(record), at));
    }

    public int AddVmadProperty(FormKey record, int scriptIndex, int index, string name, byte type, byte flags = 0)
    {
        ArgumentNullException.ThrowIfNull(name);
        if (name.Contains('\0')) throw new ArgumentException("VMAD property name cannot hold an embedded NUL through this API.", nameof(name));
        var script = Index(scriptIndex);
        var at = Index(index);
        using var use = Use();
        using var strings = new NativeStrings();
        nuint added;
        Call.Check(RadicleNative.PluginAddVmadProperty(use.Pointer, strings.Key(record), script, at, name, type, flags, &added));
        return (int)added;
    }

    public void RemoveVmadProperty(FormKey record, int scriptIndex, int propertyIndex)
    {
        var script = Index(scriptIndex);
        var property = Index(propertyIndex);
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginRemoveVmadProperty(use.Pointer, strings.Key(record), script, property));
    }

    public void AddVmadFragment(FormKey record, uint fragmentKey, string script, string function)
    {
        ArgumentNullException.ThrowIfNull(script);
        ArgumentNullException.ThrowIfNull(function);
        if (script.Contains('\0')) throw new ArgumentException("VMAD fragment script name cannot hold an embedded NUL through this API.", nameof(script));
        if (function.Contains('\0')) throw new ArgumentException("VMAD fragment function name cannot hold an embedded NUL through this API.", nameof(function));
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginAddVmadFragment(use.Pointer, strings.Key(record), fragmentKey, script, function));
    }

    public void RemoveVmadFragment(FormKey record, uint fragmentKey)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginRemoveVmadFragment(use.Pointer, strings.Key(record), fragmentKey));
    }

    public bool SetScalar(FormKey record, string path, string value, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(value);
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetRecordScalarAt(plugin, key, path, at, value));
    }

    public bool SetFormLink(FormKey record, string path, FormKey? target, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginSetRecordFormlink(plugin, key, path, at, target.HasValue ? &native : null);
        });
    }

    // Constraint: path names one schema record-flag row such as STAT\HasDistantLOD, so no other header bit moves; true means the header word changed, and an absent record, an unknown path or a row of another record type throws instead of reading as "unchanged".
    public bool SetRecordFlag(FormKey record, string path, bool value)
    {
        ArgumentNullException.ThrowIfNull(path);
        using var use = Use();
        using var strings = new NativeStrings();
        int changed;
        Call.Check(RadicleNative.PluginSetRecordFlag(use.Pointer, strings.Key(record), path, value ? 1 : 0, &changed));
        return changed != 0;
    }

    public bool GetRecordFlag(FormKey record, string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        using var use = Use();
        using var strings = new NativeStrings();
        int value;
        Call.Check(RadicleNative.PluginGetRecordFlag(use.Pointer, strings.Key(record), path, &value));
        return value != 0;
    }

    public bool SetString(FormKey record, string path, string value, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(value);
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginSetRecordString(plugin, key, path, at, value));
    }

    public bool AddSubrecord(FormKey record, string signature)
    {
        ArgumentNullException.ThrowIfNull(signature);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginAddSubrecord(plugin, key, signature));
    }

    public bool AddPackageInput(FormKey record, string typeName, PackageInputEdit value)
    {
        ArgumentNullException.ThrowIfNull(typeName);
        ArgumentNullException.ThrowIfNull(value);
        var bytes = value.Payload ?? Array.Empty<byte>();
        return Edit(record, (plugin, s, key) =>
        {
            fixed (byte* payload = bytes)
            {
                var native = value.ToNative(bytes.Length == 0 ? IntPtr.Zero : (IntPtr)payload, bytes.Length);
                return RadicleNative.PackageInputAdd(plugin, key, typeName, &native);
            }
        });
    }

    public bool SetPackageInput(FormKey record, byte id, PackageInputEdit value)
    {
        ArgumentNullException.ThrowIfNull(value);
        var bytes = value.Payload ?? Array.Empty<byte>();
        return Edit(record, (plugin, s, key) =>
        {
            fixed (byte* payload = bytes)
            {
                var native = value.ToNative(bytes.Length == 0 ? IntPtr.Zero : (IntPtr)payload, bytes.Length);
                return RadicleNative.PackageInputSet(plugin, key, id, &native);
            }
        });
    }

    public bool RemovePackageInput(FormKey record, byte id) =>
        Edit(record, (plugin, s, key) => RadicleNative.PackageInputRemove(plugin, key, id));

    public bool RemoveSubrecord(FormKey record, string signature, int occurrence = 0)
    {
        ArgumentNullException.ThrowIfNull(signature);
        var at = Index(occurrence);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveSubrecord(plugin, key, signature, at));
    }

    public FormKey CreateRecord(string signature, string editorId)
    {
        ArgumentNullException.ThrowIfNull(signature);
        ArgumentNullException.ThrowIfNull(editorId);
        using var use = Use();
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateRecord(use.Pointer, signature, editorId, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public FormKey CreateRawRecord(string signature, ushort formVersion = 0)
    {
        ArgumentNullException.ThrowIfNull(signature);
        using var use = Use();
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateRawRecord(use.Pointer, signature, formVersion, &key));
        return FormKey.FromNative(key)!.Value;
    }

    public bool SetSubrecordBytes(FormKey record, string signature, ReadOnlySpan<byte> bytes, int occurrence = 0)
    {
        ArgumentNullException.ThrowIfNull(signature);
        var at = Index(occurrence);
        using var use = Use();
        using var strings = new NativeStrings();
        fixed (byte* payload = bytes)
        {
            return Call.Found(RadicleNative.PluginSetSubrecordBytes(use.Pointer, strings.Key(record), signature, at, payload, (nuint)bytes.Length));
        }
    }

    // Constraint: the bytes are written verbatim after declaredSize with no compression; call it last because the record's subrecords are ignored on write.
    public bool SetRecordRawCompressedPayload(FormKey record, uint declaredSize, ReadOnlySpan<byte> bytes)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        fixed (byte* payload = bytes)
        {
            return Call.Found(RadicleNative.PluginSetRecordRawCompressedPayload(use.Pointer, strings.Key(record), declaredSize, payload, (nuint)bytes.Length));
        }
    }

    public int FieldCount(FormKey record, string path) =>
        TryFieldCount(record, path, out var count) ? count : throw new KeyNotFoundException($"Record not found: {record}");

    public bool TryFieldCount(FormKey record, string path, out int count)
    {
        ArgumentNullException.ThrowIfNull(path);
        using var use = Use();
        using var strings = new NativeStrings();
        nuint native;
        var found = Call.Found(RadicleNative.PluginRecordFieldCount(use.Pointer, strings.Key(record), path, &native));
        count = found ? checked((int)native) : 0;
        return found;
    }

// Constraint: <summary>The stored text of one scalar occurrence, or empty when it is absent.</summary>
    public bool TryFieldText(FormKey record, string path, out string text, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        text = string.Empty;
        using var use = Use();
        using var strings = new NativeStrings();
        IntPtr native;
        var status = RadicleNative.PluginRecordScalarText(use.Pointer, strings.Key(record), path, at, &native);
        if (status == Status.NotFound) return false;
        Call.Check(status);
        try
        {
            text = Utf8.Read(native) ?? string.Empty;
        }
        finally
        {
            if (native != IntPtr.Zero) RadicleNative.FreeText(native);
        }
        return true;
    }

// Constraint: <summary>The stored text of one scalar occurrence; throws when the record or field is absent.</summary>
    public string FieldText(FormKey record, string path, int index = 0) =>
        TryFieldText(record, path, out var text, index)
            ? text
            : throw new KeyNotFoundException($"Field not found: {record} {path}[{index}]");

    public bool SetSceneScalar(FormKey scene, string path, string value, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(value);
        var at = Index(index);
        return Edit(scene, (plugin, s, key) => RadicleNative.PluginSetSceneScalar(plugin, key, path, at, value));
    }

    public bool SetSceneFormLink(FormKey scene, string path, FormKey target)
    {
        ArgumentNullException.ThrowIfNull(path);
        return Edit(scene, (plugin, s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginSetSceneFormlink(plugin, key, path, &native);
        });
    }

    public bool SetSceneActionFormLink(FormKey scene, int index, string path, FormKey target)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(scene, (plugin, s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginSetSceneActionFormlink(plugin, key, at, path, &native);
        });
    }

    public bool SetSceneActionHeadtracking(FormKey scene, int index, int[] aliases)
    {
        ArgumentNullException.ThrowIfNull(aliases);
        var at = Index(index);
        return Edit(scene, (plugin, s, key) =>
        {
            fixed (int* values = aliases)
                return RadicleNative.PluginSetSceneActionHeadtracking(plugin, key, at, values, (nuint)aliases.Length);
        });
    }

    public int SceneGroupCount(FormKey scene, ScenePart part)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        nuint count;
        return Call.Found(RadicleNative.PluginSceneGroupCount(use.Pointer, strings.Key(scene), part, &count))
            ? checked((int)count)
            : throw new KeyNotFoundException($"Record not found: {scene}");
    }

    // Constraint: a type-4 Start Scene entry is a repeated LCEP/INTT/SSPN/CITC/CTDA run inside one action, so it is addressed by action index and entry index instead of a subrecord path.
    public IReadOnlyList<SceneStartSceneEntry> SceneStartScenes(FormKey scene, int actionIndex)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.SceneStartEntries* view;
        Call.Check(RadicleNative.PluginSceneStartScenes(use.Pointer, strings.Key(scene), Index(actionIndex), &view));
        try
        {
            return Call.Copy<Native.SceneStartEntry>(view->Entries, view->Count).Select(e =>
                new SceneStartSceneEntry(FormKey.FromNative(e.Scene), e.PhaseIndex,
                    e.HasPhaseName == 0 ? null : Utf8.Read(e.PhaseName),
                    Call.Copy<Native.DialogueCondition>(e.Conditions, e.ConditionCount).Select(c =>
                        new DialogueCondition(c.FlagsByte, c.ComparisonRaw, c.Function, c.Unknown2, c.Param1,
                            c.Param2, c.RunOn, c.Reference, c.Unknown3, Utf8.Read(c.Parameter1String),
                            Utf8.Read(c.Parameter2String))).ToArray())).ToArray();
        }
        finally { RadicleNative.SceneStartEntriesFree(view); }
    }

    public bool InsertSceneStartScene(FormKey scene, int actionIndex, int index, SceneStartSceneEntry entry) =>
        WriteSceneStartScene(scene, actionIndex, index, entry, insert: true);

    public bool UpdateSceneStartScene(FormKey scene, int actionIndex, int index, SceneStartSceneEntry entry) =>
        WriteSceneStartScene(scene, actionIndex, index, entry, insert: false);

    public bool RemoveSceneStartScene(FormKey scene, int actionIndex, int index) =>
        Edit(scene, (plugin, s, key) =>
            RadicleNative.PluginRemoveSceneStartScene(plugin, key, Index(actionIndex), Index(index)));

    private bool WriteSceneStartScene(FormKey scene, int actionIndex, int index, SceneStartSceneEntry entry,
        bool insert)
    {
        ArgumentNullException.ThrowIfNull(entry);
        using var use = Use();
        using var strings = new NativeStrings();
        var rows = entry.Conditions.Select(c => new Native.DialogueCondition
        {
            FlagsByte = c.FlagsByte, ComparisonRaw = c.ComparisonRaw, Function = c.Function, Unknown2 = c.Unknown2,
            Param1 = c.Param1, Param2 = c.Param2, RunOn = c.RunOn, Reference = c.Reference, Unknown3 = c.Unknown3,
            Parameter1String = strings.Add(c.Parameter1String),
            Parameter2String = strings.Add(c.Parameter2String),
        }).ToArray();
        var spec = new Native.SceneStartEntrySpec
        {
            Scene = strings.Key(entry.Scene),
            PhaseIndex = entry.PhaseIndex,
            HasPhaseName = entry.PhaseName is null ? 0 : 1,
            PhaseName = strings.Add(entry.PhaseName),
            ConditionCount = (nuint)rows.Length,
        };
        fixed (Native.DialogueCondition* conditions = rows)
        {
            spec.Conditions = (nint)conditions;
            var at = Index(actionIndex);
            var position = Index(index);
            return insert
                ? Call.Found(RadicleNative.PluginInsertSceneStartScene(use.Pointer, strings.Key(scene), at, position, &spec))
                : Call.Found(RadicleNative.PluginUpdateSceneStartScene(use.Pointer, strings.Key(scene), at, position, &spec));
        }
    }

    public int FormLinkListCount(FormKey record, string path) =>
        TryFormLinkListCount(record, path, out var count) ? count : throw new KeyNotFoundException($"Record not found: {record}");

    public bool TryFormLinkListCount(FormKey record, string path, out int count)
    {
        ArgumentNullException.ThrowIfNull(path);
        using var use = Use();
        using var strings = new NativeStrings();
        nuint native;
        var found = Call.Found(RadicleNative.PluginFormlinkListSize(use.Pointer, strings.Key(record), path, &native));
        count = found ? checked((int)native) : 0;
        return found;
    }

    public bool FormLinkListInsert(FormKey record, string path, int index, FormKey? target)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginFormlinkListInsert(plugin, key, path, at, target.HasValue ? &native : null);
        });
    }

    public bool FormLinkListRemove(FormKey record, string path, int index)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginFormlinkListRemove(plugin, key, path, at));
    }

    public bool FormLinkListSet(FormKey record, string path, int index, FormKey? target)
    {
        ArgumentNullException.ThrowIfNull(path);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
        {
            var native = s.Key(target);
            return RadicleNative.PluginFormlinkListSet(plugin, key, path, at, target.HasValue ? &native : null);
        });
    }

    public bool FormLinkListClear(FormKey record, string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginFormlinkListClear(plugin, key, path));
    }

    public void Dispose() => _handle.Dispose();

    private static nuint Index(int index) =>
        index >= 0 ? (nuint)index : throw new ArgumentOutOfRangeException(nameof(index));

    private delegate Status NativeEdit(IntPtr plugin, NativeStrings strings, Native.FormKey reference);

    private bool Edit(FormKey reference, NativeEdit edit)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        return Call.Found(edit(use.Pointer, strings, strings.Key(reference)));
    }
}
