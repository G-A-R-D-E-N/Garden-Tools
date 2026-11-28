using System;
using System.Collections.Generic;
using System.IO;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum NifSkinScan { NoSkin = 0, Skin = 1, Unknown = 2 }

/// <summary>Invariant: the header of any NetImmerse or Gamebryo file, read without parsing a block; an unreadable header has Readable false and keeps the type names read before the failure.</summary>
public sealed record NifHeader(uint FileVersion, uint UserVersion, uint BlockCount, bool Readable,
    IReadOnlyList<string> BlockTypeNames)
{
    public static unsafe NifHeader Read(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) return new NifHeader(0, 0, 0, false, Array.Empty<string>());
        uint fileVersion = 0;
        uint userVersion = 0;
        uint blockCount = 0;
        int readable = 0;
        byte* names = null;
        nuint namesSize = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.NifHeaderRead(pointer, (nuint)bytes.Length, &fileVersion, &userVersion,
                &blockCount, &readable, &names, &namesSize));
        }
        var text = Call.TakeLatin1(names, namesSize);
        return new NifHeader(fileVersion, userVersion, blockCount, readable != 0,
            text.Length == 0 ? Array.Empty<string>() : text.Split('\0'));
    }

    private const int SkinWindow = 256 * 1024;

    // Invariant: an empty span is Unknown without a native call; the export refuses size 0.
    public static unsafe NifSkinScan ScanSkin(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) return NifSkinScan.Unknown;
        int scan = 2;
        try
        {
            fixed (byte* pointer = bytes) Call.Check(RadicleNative.NifSkinScan(pointer, (nuint)bytes.Length, &scan));
            return (NifSkinScan)scan;
        }
        catch (Exception)
        {
            return NifSkinScan.Unknown;
        }
    }

    public static NifSkinScan ScanSkin(string path)
    {
        try
        {
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 4096,
                FileOptions.SequentialScan);
            return ScanSkin(file);
        }
        catch (Exception) { return NifSkinScan.Unknown; }
    }

    // Constraint: reads a 256 KiB window first and the whole stream only when a full window gave no answer.
    public static NifSkinScan ScanSkin(Stream stream)
    {
        try
        {
            var window = new byte[SkinWindow];
            int read = stream.ReadAtLeast(window, SkinWindow, throwOnEndOfStream: false);
            var scan = ScanSkin(window.AsSpan(0, read));
            if (scan != NifSkinScan.Unknown || read < SkinWindow) return scan;
            var rest = new MemoryStream();
            rest.Write(window);
            stream.CopyTo(rest);
            return ScanSkin(rest.GetBuffer().AsSpan(0, (int)rest.Length));
        }
        catch (Exception) { return NifSkinScan.Unknown; }
    }
}
