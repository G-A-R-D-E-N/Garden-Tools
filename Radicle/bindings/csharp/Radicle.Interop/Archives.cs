using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record Ba2Entry(string Path, uint NameHash, uint ExtensionHash, uint DirectoryHash, ulong UnpackedSize,
                              int ChunkCount, ushort Width, ushort Height, byte MipCount, byte DxgiFormat,
                              byte TextureFlags, byte TileMode);

public readonly record struct AssetLocation(AssetOrigin Origin, int ArchiveIndex, int EntryIndex);

public sealed unsafe class Ba2Archive : IDisposable
{
    private IntPtr _handle;
    private readonly bool _owned;
    private readonly AssetResolver? _owner;

    static Ba2Archive() => RadicleLibrary.Register();

    internal Ba2Archive(IntPtr handle, bool owned, AssetResolver? owner = null)
    {
        _handle = handle;
        _owned = owned;
        _owner = owner;
    }

    private IntPtr Handle => _handle != IntPtr.Zero && (_owner is null || _owner.Handle != IntPtr.Zero)
        ? _handle
        : throw new ObjectDisposedException(nameof(Ba2Archive));

    public static Ba2Archive Open(string path)
    {
        IntPtr handle;
        Call.Check(RadicleNative.Ba2Open(path, &handle));
        return new Ba2Archive(handle, owned: true);
    }

    public Ba2Kind Kind => Info().Kind;
    public uint Version => Info().Version;
    public bool HasNameTable => Info().HasNameTable != 0;
    public int Count => (int)Info().EntryCount;

    public Ba2Entry Entry(int index)
    {
        Ba2EntryInfo e;
        Call.Check(RadicleNative.Ba2GetEntry(Handle, (nuint)index, &e));
        return new Ba2Entry(Utf8.Read(e.Path) ?? string.Empty, e.NameHash, e.ExtensionHash, e.DirectoryHash,
                            e.UnpackedSize, (int)e.ChunkCount, e.Width, e.Height, e.MipCount, e.DxgiFormat,
                            e.TextureFlags, e.TileMode);
    }

    public IEnumerable<Ba2Entry> Entries => Enumerable.Range(0, Count).Select(Entry);

    public int? Find(string path)
    {
        nuint index;
        return Call.Found(RadicleNative.Ba2Find(Handle, path, &index)) ? (int)index : null;
    }

    public byte[] Extract(int index)
    {
        byte* bytes;
        nuint size;
        Call.Check(RadicleNative.Ba2Extract(Handle, (nuint)index, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    private Ba2Info Info()
    {
        Ba2Info info;
        Call.Check(RadicleNative.Ba2GetInfo(Handle, &info));
        return info;
    }

    public void Dispose()
    {
        var handle = System.Threading.Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero && _owned) RadicleNative.Ba2Close(handle);
    }
}

public sealed unsafe class AssetResolver : IDisposable
{
    private IntPtr _handle;

    static AssetResolver() => RadicleLibrary.Register();

    private AssetResolver(IntPtr handle) => _handle = handle;

    internal IntPtr Handle => _handle != IntPtr.Zero ? _handle : throw new ObjectDisposedException(nameof(AssetResolver));

    public static IReadOnlyList<string> ArchiveLoadOrder(string dataDirectory, IEnumerable<string> iniArchives,
                                                        IEnumerable<string> plugins)
    {
        using var s = new NativeStrings();
        var ini = iniArchives.Select(s.Add).ToArray();
        var names = plugins.Select(s.Add).ToArray();
        IntPtr* paths;
        nuint count;
        fixed (IntPtr* i = ini)
        fixed (IntPtr* p = names)
        {
            Call.Check(RadicleNative.ArchiveLoadOrder(dataDirectory, i, (nuint)ini.Length, p, (nuint)names.Length, &paths, &count));
        }
        return Call.TakeStrings(paths, count);
    }

    public static AssetResolver Open(string? dataDirectory, IEnumerable<string> archives)
    {
        using var s = new NativeStrings();
        var paths = archives.Select(s.Add).ToArray();
        IntPtr handle;
        fixed (IntPtr* p = paths) Call.Check(RadicleNative.AssetsOpen(dataDirectory, p, (nuint)paths.Length, &handle));
        return new AssetResolver(handle);
    }

    public int ArchiveCount => (int)RadicleNative.AssetsArchiveCount(Handle);

    public Ba2Archive Archive(int index)
    {
        IntPtr ba2;
        Call.Check(RadicleNative.AssetsGetArchive(Handle, (nuint)index, &ba2));
        return new Ba2Archive(ba2, owned: false, owner: this);
    }

    public AssetLocation? Locate(string path)
    {
        Native.AssetLocation location;
        return Call.Found(RadicleNative.AssetsLocate(Handle, path, &location))
            ? new AssetLocation(location.Origin, (int)location.ArchiveIndex, (int)location.EntryIndex)
            : null;
    }

    public byte[]? Read(string path)
    {
        byte* bytes;
        nuint size;
        Native.AssetLocation location;
        return Call.Found(RadicleNative.AssetsRead(Handle, path, &bytes, &size, &location)) ? Call.TakeBytes(bytes, size) : null;
    }

    public void Dispose()
    {
        var handle = System.Threading.Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle != IntPtr.Zero) RadicleNative.AssetsDestroy(handle);
    }
}
