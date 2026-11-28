using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

/// <summary>Invariant: one 16-byte Fallout 4 lodsettings record; Read throws RadicleException for any other size or an unencodable stride or level range.</summary>
public readonly record struct LodSettings(int SouthwestX, int SouthwestY, int Stride, int LevelMin, int LevelMax)
{
    public static unsafe LodSettings Read(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a lodsettings record needs 16 bytes", nameof(bytes));
        int x = 0, y = 0, stride = 0, min = 0, max = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.LodSettingsRead(pointer, (nuint)bytes.Length, &x, &y, &stride, &min, &max));
        }
        return new LodSettings(x, y, stride, min, max);
    }

    public static bool TryRead(ReadOnlySpan<byte> bytes, out LodSettings settings)
    {
        settings = default;
        if (bytes.IsEmpty) return false;
        try
        {
            settings = Read(bytes);
            return true;
        }
        catch (RadicleException)
        {
            return false;
        }
    }
}
