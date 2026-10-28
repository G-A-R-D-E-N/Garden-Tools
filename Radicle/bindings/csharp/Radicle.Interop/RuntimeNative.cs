using System;
using System.Runtime.InteropServices;

namespace Radicle.Interop.Native;

// The hand-written declarations must follow fo4/c_api_runtime.h because its host struct has C function pointers.

public enum RuntimeInputKind : byte
{
    MouseMove = 0,
    MouseDown = 1,
    MouseUp = 2,
    KeyDown = 3,
    KeyUp = 4,
    Char = 5,
    Wheel = 6,
}

[StructLayout(LayoutKind.Sequential)]
public struct RuntimeMatrix
{
    public double A;
    public double B;
    public double C;
    public double D;
    public double TranslateX;
    public double TranslateY;
}

[StructLayout(LayoutKind.Sequential)]
public struct RuntimeGlyph
{
    public ushort FontId;
    public ushort GlyphId;
    public double X;
    public double Advance;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RuntimeRun
{
    public ushort FontId;
    public double FontSize;
    public double X;
    public double Y;
    public uint Color;
    public nuint GlyphCount;
    public RuntimeGlyph* Glyphs;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RuntimeEntry
{
    public byte Kind;
    public uint CharacterId;
    public int ClipDepth;
    public byte BlendMode;
    public byte Visible;
    public double Alpha;
    public IntPtr Path;
    public IntPtr Name;
    public RuntimeMatrix Matrix;
    public fixed double Colour[8];
    public nuint RunCount;
    public RuntimeRun* Runs;
    public nuint FilterCount;
}

[StructLayout(LayoutKind.Sequential)]
public struct RuntimeSound
{
    public byte Kind;
    public ushort SoundId;
    public double Volume;
    public uint Loops;
}

[StructLayout(LayoutKind.Sequential)]
public struct RuntimeInput
{
    public double TimeMs;
    public byte Kind;
    public double X;
    public double Y;
    public int Button;
    public uint KeyCode;
    public int Delta;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RuntimeHost
{
    public void* User;
    public IntPtr ResolveAsset;
    public IntPtr ReadStorage;
    public IntPtr WriteStorage;
    public IntPtr HostCall;
}

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public unsafe delegate int RuntimeResolveAssetCallback(void* user, byte* path, byte** outBytes,
                                                      nuint* outSize);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public unsafe delegate int RuntimeReadStorageCallback(void* user, byte* name, byte** outBytes,
                                                      nuint* outSize);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public unsafe delegate int RuntimeWriteStorageCallback(void* user, byte* name, byte* bytes,
                                                       nuint size);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public unsafe delegate int RuntimeHostCallCallback(void* user, byte* method, byte** arguments,
                                                   nuint argumentCount, byte** outText,
                                                   nuint* outSize);

public static unsafe partial class RadicleNative
{
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_open")]
    public static extern Status RuntimeOpen(RuntimeHost* host, IntPtr* outRuntime);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_close")]
    public static extern void RuntimeClose(IntPtr runtime);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_open_movie")]
    public static extern Status RuntimeOpenMovie(IntPtr runtime, byte* bytes, nuint size,
                                                 IntPtr* outMovie);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_open_movie_from_ba2", CharSet = CharSet.Ansi)]
    public static extern Status RuntimeOpenMovieFromBa2(IntPtr runtime, IntPtr ba2,
                                                        [MarshalAs(UnmanagedType.LPUTF8Str)] string path,
                                                        IntPtr* outMovie);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_movie_close")]
    public static extern void MovieClose(IntPtr movie);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_movie_advance")]
    public static extern Status MovieAdvance(IntPtr movie, double dtSeconds);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_movie_input")]
    public static extern Status MovieInput(IntPtr movie, RuntimeInput* input);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_movie_snapshot")]
    public static extern Status MovieSnapshot(IntPtr movie, IntPtr* outSnapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_snapshot_destroy")]
    public static extern void SnapshotDestroy(IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_snapshot_entry_count")]
    public static extern nuint SnapshotEntryCount(IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_snapshot_entry_at")]
    public static extern Status SnapshotEntryAt(IntPtr snapshot, nuint index, RuntimeEntry* outEntry);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_snapshot_sound_count")]
    public static extern nuint SnapshotSoundCount(IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_snapshot_sound_at")]
    public static extern Status SnapshotSoundAt(IntPtr snapshot, nuint index, RuntimeSound* outSound);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_runtime_snapshot_hash")]
    public static extern ulong SnapshotHash(IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "radicle_movie_replay")]
    public static extern Status MovieReplay(IntPtr movie, RuntimeInput* inputs, nuint inputCount,
                                            double frameDtSeconds, nuint frameCount,
                                            ulong* outHashes, nuint hashCapacity,
                                            nuint* outHashCount);
}
