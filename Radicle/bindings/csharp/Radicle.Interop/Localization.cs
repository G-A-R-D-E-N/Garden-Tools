using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed unsafe class StringTables : IDisposable
{
    private IntPtr _handle;
    private readonly RadicleEnvironment _env;

    static StringTables() => RadicleLibrary.Register();

    private StringTables(IntPtr handle, RadicleEnvironment env)
    {
        _handle = handle;
        _env = env;
    }

    private IntPtr Handle => _handle != IntPtr.Zero && _env.Handle != IntPtr.Zero
        ? _handle
        : throw new ObjectDisposedException(nameof(StringTables));

    /// <summary>Loose tables from stringsDirectory, then Strings/ inside assets' archives; env must outlive this.</summary>
    public static StringTables Load(RadicleEnvironment env, string stringsDirectory, string language = "en",
                                    AssetResolver? assets = null)
    {
        IntPtr handle;
        Call.Check(RadicleNative.StringsLoadWithAssets(env.Handle, stringsDirectory, language,
                                                       assets?.Handle ?? IntPtr.Zero, &handle));
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
        IntPtr path;
        return Call.Found(RadicleNative.StringsTablePath(Handle, (nuint)pluginIndex, source, &path)) ? Utf8.Read(path) : null;
    }

    public byte[]? Lookup(int pluginIndex, StringSource source, uint stringId)
    {
        IntPtr text;
        nuint size;
        return Call.Found(RadicleNative.StringsLookup(Handle, (nuint)pluginIndex, source, stringId, &text, &size))
            ? Utf8.Raw(text, size)
            : null;
    }

    public byte[]? ResolveName(Record record)
    {
        IntPtr text;
        nuint size;
        return Call.Found(RadicleNative.StringsResolveName(Handle, record.Handle, &text, &size)) ? Utf8.Raw(text, size) : null;
    }

    public void Dispose()
    {
        var handle = System.Threading.Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero) RadicleNative.StringsDestroy(handle);
    }
}
