using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed unsafe class StringTables : IDisposable
{
    private readonly StringsHandle _handle;
    private readonly RadicleEnvironment _env;

    static StringTables() => RadicleLibrary.Register();

    private StringTables(IntPtr handle, RadicleEnvironment env)
    {
        _handle = new StringsHandle(handle);
        _env = env;
    }

    internal RadicleEnvironment Owner => _env;

    private HandleUse Use() => new(_handle, nameof(StringTables));

    internal HandleUse Borrow() => Use();

    public string? RecordText(Record record, string path, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
        if (record.Owner != _env)
            throw new ArgumentException("The record does not belong to this environment.", nameof(record));
        using var envUse = _env.Use();
        using var recordUse = _env.Use(record);
        using var stringsUse = Use();
        IntPtr text;
        if (!Call.Found(RadicleNative.RecordGetText(envUse.Pointer, stringsUse.Pointer, record.Handle, path, (nuint)index,
                &text)))
            return null;
        try
        {
            return Utf8.Read(text);
        }
        finally
        {
            RadicleNative.RecordScalarFree(text);
        }
    }

    /// <summary>Loose tables from stringsDirectory, then Strings/ inside assets' archives; env must outlive this.</summary>
    public static StringTables Load(RadicleEnvironment env, string stringsDirectory, string language = "en",
                                    AssetResolver? assets = null)
    {
        IntPtr handle;
        using var use = env.Use();
        using var assetUse = assets is null ? default : assets.Use();
        Call.Check(RadicleNative.StringsLoadWithAssets(use.Pointer, stringsDirectory, language,
                                                       assets is null ? IntPtr.Zero : assetUse.Pointer, &handle));
        return new StringTables(handle, env);
    }

    /// <summary>Strings/ tables found loose over assets' roots (first root wins), then in its archives; no directory is read; env must outlive this.</summary>
    public static StringTables Load(RadicleEnvironment env, AssetResolver assets, string language = "en")
    {
        ArgumentNullException.ThrowIfNull(assets);
        IntPtr handle;
        using var use = env.Use();
        using var assetUse = assets.Use();
        Call.Check(RadicleNative.StringsLoadFromAssets(use.Pointer, language, assetUse.Pointer, &handle));
        return new StringTables(handle, env);
    }

    public static StringSource SourceFor(string recordSignature, string subrecordSignature)
    {
        StringSource source;
        Call.Check(RadicleNative.StringSourceFor(recordSignature, subrecordSignature, &source));
        return source;
    }

    public string? TablePath(int pluginIndex, StringSource source)
    {
        using var env = _env.Use();
        using var use = Use();
        IntPtr path;
        return Call.Found(RadicleNative.StringsTablePath(use.Pointer, (nuint)pluginIndex, source, &path)) ? Utf8.Read(path) : null;
    }

    public byte[]? Lookup(int pluginIndex, StringSource source, uint stringId)
    {
        using var env = _env.Use();
        using var use = Use();
        IntPtr text;
        nuint size;
        return Call.Found(RadicleNative.StringsLookup(use.Pointer, (nuint)pluginIndex, source, stringId, &text, &size))
            ? Utf8.Raw(text, size)
            : null;
    }

    public byte[]? ResolveName(Record record)
    {
        using var env = _env.Use(record);
        using var use = Use();
        IntPtr text;
        nuint size;
        return Call.Found(RadicleNative.StringsResolveName(use.Pointer, record.Handle, &text, &size)) ? Utf8.Raw(text, size) : null;
    }

    public byte[]? ResolveDialogueText(Record record, string subrecordSignature, DialogueText value)
    {
        if (value.Kind == NameKind.Inline) return System.Text.Encoding.UTF8.GetBytes(value.Text ?? string.Empty);
        if (value.Kind != NameKind.StringId) return null;
        var info = _env.Info(record);
        return Lookup(info.PluginIndex, SourceFor(info.Signature, subrecordSignature), value.StringId);
    }

    public void Dispose() => _handle.Dispose();
}

internal sealed class LocalizationTablesHandle : Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid
{
    public LocalizationTablesHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.LocalizationTablesDestroy(handle);
        return true;
    }
}

public sealed record PluginLocalizationOutcome(PluginLocalizationStatus Status, int Converted, int EmptyText);

public sealed unsafe class LocalizationTables : IDisposable
{
    private readonly LocalizationTablesHandle _handle;

    static LocalizationTables() => RadicleLibrary.Register();

    private LocalizationTables(IntPtr handle) => _handle = new LocalizationTablesHandle(handle);

    internal HandleUse Use() => new(_handle, nameof(LocalizationTables));

    public static LocalizationTables Create()
    {
        IntPtr handle;
        Call.Check(RadicleNative.LocalizationTablesCreate(&handle));
        return new LocalizationTables(handle);
    }

    public static LocalizationTables Load(string dataDirectory, string pluginFileName, string language = "en")
    {
        ArgumentNullException.ThrowIfNull(dataDirectory);
        ArgumentNullException.ThrowIfNull(pluginFileName);
        ArgumentNullException.ThrowIfNull(language);
        IntPtr handle;
        Call.Check(RadicleNative.LocalizationTablesLoad(dataDirectory, pluginFileName, language, &handle));
        return new LocalizationTables(handle);
    }

    public void Write(string dataDirectory, string pluginFileName, string language = "en")
    {
        ArgumentNullException.ThrowIfNull(dataDirectory);
        ArgumentNullException.ThrowIfNull(pluginFileName);
        ArgumentNullException.ThrowIfNull(language);
        using var use = Use();
        Call.Check(RadicleNative.LocalizationTablesWrite(use.Pointer, dataDirectory, pluginFileName, language));
    }

    public void Dispose() => _handle.Dispose();
}

public sealed unsafe partial class PluginFile
{
    public PluginLocalizationOutcome Localize(LocalizationTables tables) => ConvertLocalization(tables, localize: true);

    public PluginLocalizationOutcome Delocalize(LocalizationTables tables) => ConvertLocalization(tables, localize: false);

    public LocalizedValueStatus SetLocalizedString(FormKey record, string subrecordSignature, int index, string text,
        LocalizationTables tables)
    {
        ArgumentNullException.ThrowIfNull(subrecordSignature);
        ArgumentNullException.ThrowIfNull(text);
        ArgumentNullException.ThrowIfNull(tables);
        var at = Index(index);
        using var use = Use();
        using var tableUse = tables.Use();
        using var s = new NativeStrings();
        LocalizedValueStatus status;
        Call.Check(RadicleNative.PluginSetLocalizedString(use.Pointer, s.Key(record), subrecordSignature, at, text,
            tableUse.Pointer, &status));
        return status;
    }

    private PluginLocalizationOutcome ConvertLocalization(LocalizationTables tables, bool localize)
    {
        ArgumentNullException.ThrowIfNull(tables);
        using var use = Use();
        using var tableUse = tables.Use();
        PluginLocalizationResult result;
        Call.Check(localize
            ? RadicleNative.PluginLocalize(use.Pointer, tableUse.Pointer, &result)
            : RadicleNative.PluginDelocalize(use.Pointer, tableUse.Pointer, &result));
        return new PluginLocalizationOutcome(result.Status, checked((int)result.Converted), checked((int)result.EmptyText));
    }
}
