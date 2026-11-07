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

    private HandleUse Use() => new(_handle, nameof(StringTables));

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
