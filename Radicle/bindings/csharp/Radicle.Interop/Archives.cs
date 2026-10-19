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
    private readonly Ba2Handle _handle;
    private readonly AssetResolver? _owner;

    static Ba2Archive() => RadicleLibrary.Register();

    internal Ba2Archive(IntPtr handle, bool owned, AssetResolver? owner = null)
    {
        _handle = new Ba2Handle(handle, owned);
        _owner = owner;
    }

    private HandleUse Use() => new(_handle, nameof(Ba2Archive));

    private HandleUse OwnerUse() => _owner is null ? default : _owner.Use();

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
        using var owner = OwnerUse();
        using var use = Use();
        Ba2EntryInfo e;
        Call.Check(RadicleNative.Ba2GetEntry(use.Pointer, (nuint)index, &e));
        return new Ba2Entry(Utf8.Read(e.Path) ?? string.Empty, e.NameHash, e.ExtensionHash, e.DirectoryHash,
                            e.UnpackedSize, (int)e.ChunkCount, e.Width, e.Height, e.MipCount, e.DxgiFormat,
                            e.TextureFlags, e.TileMode);
    }

    public IEnumerable<Ba2Entry> Entries => Enumerable.Range(0, Count).Select(Entry);

    public int? Find(string path)
    {
        using var owner = OwnerUse();
        using var use = Use();
        nuint index;
        return Call.Found(RadicleNative.Ba2Find(use.Pointer, path, &index)) ? (int)index : null;
    }

    public byte[] Extract(int index)
    {
        using var owner = OwnerUse();
        using var use = Use();
        byte* bytes;
        nuint size;
        Call.Check(RadicleNative.Ba2Extract(use.Pointer, (nuint)index, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    private Ba2Info Info()
    {
        using var owner = OwnerUse();
        using var use = Use();
        Ba2Info info;
        Call.Check(RadicleNative.Ba2GetInfo(use.Pointer, &info));
        return info;
    }

    public void Dispose() => _handle.Dispose();
}

public static unsafe class Ba2Writer
{
    static Ba2Writer() => RadicleLibrary.Register();

    public static byte[] General(params (string Path, byte[] Data)[] files)
    {
        using var strings = new NativeStrings();
        var payload = new byte[files.Sum(file => file.Data.Length)];
        var entries = new Native.Ba2WriteEntry[files.Length];
        var offset = 0;
        for (var i = 0; i < files.Length; ++i)
        {
            files[i].Data.CopyTo(payload, offset);
            entries[i].Path = strings.Add(files[i].Path);
            entries[i].Size = (nuint)files[i].Data.Length;
            offset += files[i].Data.Length;
        }
        fixed (byte* data = payload)
        fixed (Native.Ba2WriteEntry* items = entries)
        {
            var at = data;
            for (var i = 0; i < files.Length; ++i)
            {
                items[i].Bytes = (IntPtr)at;
                at += files[i].Data.Length;
            }
            byte* bytes;
            nuint size;
            Call.Check(RadicleNative.Ba2WriteGeneral(items, (nuint)entries.Length, &bytes, &size));
            return Call.TakeBytes(bytes, size);
        }
    }
}

public sealed unsafe class AssetResolver : IDisposable
{
    private readonly AssetsHandle _handle;

    static AssetResolver() => RadicleLibrary.Register();

    private AssetResolver(IntPtr handle) => _handle = new AssetsHandle(handle);

    internal HandleUse Use() => new(_handle, nameof(AssetResolver));

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

    public int ArchiveCount
    {
        get
        {
            using var use = Use();
            return (int)RadicleNative.AssetsArchiveCount(use.Pointer);
        }
    }

    public Ba2Archive Archive(int index)
    {
        using var use = Use();
        IntPtr ba2;
        Call.Check(RadicleNative.AssetsGetArchive(use.Pointer, (nuint)index, &ba2));
        return new Ba2Archive(ba2, owned: false, owner: this);
    }

    public AssetLocation? Locate(string path)
    {
        using var use = Use();
        Native.AssetLocation location;
        return Call.Found(RadicleNative.AssetsLocate(use.Pointer, path, &location))
            ? new AssetLocation(location.Origin, (int)location.ArchiveIndex, (int)location.EntryIndex)
            : null;
    }

    public byte[]? Read(string path)
    {
        using var use = Use();
        byte* bytes;
        nuint size;
        Native.AssetLocation location;
        return Call.Found(RadicleNative.AssetsRead(use.Pointer, path, &bytes, &size, &location)) ? Call.TakeBytes(bytes, size) : null;
    }

    public void Dispose() => _handle.Dispose();
}
