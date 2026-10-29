using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record Ba2Entry(string Path, uint NameHash, uint ExtensionHash, uint DirectoryHash, ulong UnpackedSize,
                              int ChunkCount, ushort Width, ushort Height, byte MipCount, byte DxgiFormat,
                              byte TextureFlags, byte TileMode);

public readonly record struct AssetLocation(AssetOrigin Origin, int ArchiveIndex, int EntryIndex);

public sealed record AssetListing(string Path, AssetLocation Location, ulong Size, bool Shadowed);

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

    internal HandleUse Use() => new(_handle, nameof(Ba2Archive));

    // The runtime C ABI stays valid only while the archive is open, so this handle must not outlive it.
    internal IntPtr NativeHandle => _handle.DangerousGetHandle();

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

    public static byte[] General(params (string Path, byte[] Data)[] files) => General(files, 1, false);

    public static byte[] General(IEnumerable<(string Path, byte[] Data)> files, uint version, bool compress)
    {
        ArgumentNullException.ThrowIfNull(files);
        var items = files.ToArray();
        for (var i = 0; i < items.Length; i++)
        {
            if (items[i].Path is null || items[i].Data is null) throw new ArgumentException($"File {i} has a null Path or Data.", nameof(files));
        }
        using var strings = new NativeStrings();
        var pins = new List<GCHandle>();
        try
        {
            var native = new Ba2File[items.Length];
            for (var i = 0; i < items.Length; i++)
            {
                var pin = GCHandle.Alloc(items[i].Data, GCHandleType.Pinned);
                pins.Add(pin);
                native[i] = new Ba2File { Path = strings.Add(items[i].Path), Data = pin.AddrOfPinnedObject(), Size = (nuint)items[i].Data.Length };
            }
            byte* bytes;
            nuint size;
            fixed (Ba2File* first = native)
                Call.Check(RadicleNative.Ba2WriteGeneral(first, (nuint)native.Length, version, compress ? 1 : 0, &bytes, &size));
            return Call.TakeBytes(bytes, size);
        }
        finally
        {
            foreach (var pin in pins) pin.Free();
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

    public static AssetResolver OpenRoots(IEnumerable<string> roots, IEnumerable<string> archives)
    {
        using var s = new NativeStrings();
        var rootPaths = roots.Select(s.Add).ToArray();
        var archivePaths = archives.Select(s.Add).ToArray();
        IntPtr handle;
        fixed (IntPtr* r = rootPaths)
        fixed (IntPtr* p = archivePaths)
            Call.Check(RadicleNative.AssetsOpenRoots(r, (nuint)rootPaths.Length, p, (nuint)archivePaths.Length, &handle));
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

    public IReadOnlyList<AssetListing> List(string? prefix = null, string? extension = null, bool includeShadowed = false)
    {
        using var use = Use();
        IntPtr list;
        Call.Check(RadicleNative.AssetsList(use.Pointer, prefix, extension, includeShadowed ? 1 : 0, &list));
        try
        {
            var count = (int)RadicleNative.AssetListCount(list);
            var items = new List<AssetListing>(count);
            for (var i = 0; i < count; i++)
            {
                Native.AssetListEntry e;
                Call.Check(RadicleNative.AssetListGet(list, (nuint)i, &e));
                items.Add(new AssetListing(Utf8.Read(e.Path) ?? string.Empty,
                    new AssetLocation(e.Location.Origin, (int)e.Location.ArchiveIndex, (int)e.Location.EntryIndex), e.Size, e.Shadowed != 0));
            }
            return items;
        }
        finally { RadicleNative.AssetListDestroy(list); }
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
