using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;
using NativeInput = Radicle.Interop.Native.RuntimeInput;

namespace Radicle.Interop;

public enum RuntimeEntryKind : byte
{
    DisplayObject = 0,
    Shape = 1,
    Bitmap = 2,
    Text = 3,
}

public readonly record struct RuntimeTextGlyph(ushort FontId, ushort GlyphId, double X, double Advance);

public sealed record RuntimeTextRun(ushort FontId, double FontSize, double X, double Y, uint Color,
                                    IReadOnlyList<RuntimeTextGlyph> Glyphs);

public sealed record RuntimeSnapshotEntry(
    RuntimeEntryKind Kind,
    uint CharacterId,
    int ClipDepth,
    byte BlendMode,
    bool Visible,
    double Alpha,
    string Path,
    string Name,
    double[] Matrix,
    double[] Colour,
    IReadOnlyList<RuntimeTextRun> Runs,
    int FilterCount);

public readonly record struct RuntimeSoundEvent(byte Kind, ushort SoundId, double Volume, uint Loops);

public sealed record RuntimeFrameSnapshot(IReadOnlyList<RuntimeSnapshotEntry> Entries,
                                          IReadOnlyList<RuntimeSoundEvent> Sounds, ulong Hash);

public readonly record struct RuntimeInput(double TimeMs, RuntimeInputKind Kind, double X, double Y,
                                           int Button, uint KeyCode, int Delta)
{
    internal NativeInput Native => new()
    {
        TimeMs = TimeMs,
        Kind = (byte)Kind,
        X = X,
        Y = Y,
        Button = Button,
        KeyCode = KeyCode,
        Delta = Delta,
    };
}

public sealed class RuntimeCallbacks
{
    public Func<string, byte[]?>? ResolveAsset { get; init; }
    public Func<string, byte[]?>? ReadStorage { get; init; }
    public Func<string, byte[], bool>? WriteStorage { get; init; }
    public Func<string, IReadOnlyList<string>, string?>? HostCall { get; init; }

    internal RuntimeResolveAssetCallback? ResolveDelegate;
    internal RuntimeReadStorageCallback? ReadDelegate;
    internal RuntimeWriteStorageCallback? WriteDelegate;
    internal RuntimeHostCallCallback? HostDelegate;
}

public sealed unsafe class RuntimeMovie : IDisposable
{
    private IntPtr _handle;
    private readonly Runtime _owner;

    internal RuntimeMovie(IntPtr handle, Runtime owner)
    {
        _handle = handle;
        _owner = owner;
    }

    public void Advance(double dtSeconds)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        Call.Check(RadicleNative.MovieAdvance(_handle, dtSeconds));
    }

    public void Input(RuntimeInput input)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        var native = input.Native;
        Call.Check(RadicleNative.MovieInput(_handle, &native));
    }

    public RuntimeFrameSnapshot Snapshot()
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        IntPtr snapshot = IntPtr.Zero;
        Call.Check(RadicleNative.MovieSnapshot(_handle, &snapshot));
        try
        {
            var entryCount = (int)RadicleNative.SnapshotEntryCount(snapshot);
            var entries = new List<RuntimeSnapshotEntry>(entryCount);
            for (var index = 0; index < entryCount; ++index)
            {
                RuntimeEntry native = default;
                Call.Check(RadicleNative.SnapshotEntryAt(snapshot, (nuint)index, &native));
                var runs = new List<RuntimeTextRun>((int)native.RunCount);
                for (nuint runIndex = 0; runIndex < native.RunCount; ++runIndex)
                {
                    var run = native.Runs[runIndex];
                    var glyphs = new List<RuntimeTextGlyph>((int)run.GlyphCount);
                    for (nuint glyphIndex = 0; glyphIndex < run.GlyphCount; ++glyphIndex)
                    {
                        var glyph = run.Glyphs[glyphIndex];
                        glyphs.Add(new RuntimeTextGlyph(glyph.FontId, glyph.GlyphId, glyph.X,
                                                       glyph.Advance));
                    }
                    runs.Add(new RuntimeTextRun(run.FontId, run.FontSize, run.X, run.Y, run.Color,
                                                glyphs));
                }
                var matrix = new[]
                {
                    native.Matrix.A, native.Matrix.B, native.Matrix.C, native.Matrix.D,
                    native.Matrix.TranslateX, native.Matrix.TranslateY,
                };
                var colour = new[]
                {
                    native.Colour[0], native.Colour[1], native.Colour[2], native.Colour[3],
                    native.Colour[4], native.Colour[5], native.Colour[6], native.Colour[7],
                };
                entries.Add(new RuntimeSnapshotEntry(
                    (RuntimeEntryKind)native.Kind, native.CharacterId, native.ClipDepth,
                    native.BlendMode, native.Visible != 0, native.Alpha,
                    Marshal.PtrToStringUTF8(native.Path) ?? string.Empty,
                    Marshal.PtrToStringUTF8(native.Name) ?? string.Empty, matrix, colour, runs,
                    (int)native.FilterCount));
            }
            var soundCount = (int)RadicleNative.SnapshotSoundCount(snapshot);
            var sounds = new List<RuntimeSoundEvent>(soundCount);
            for (var index = 0; index < soundCount; ++index)
            {
                RuntimeSound sound = default;
                Call.Check(RadicleNative.SnapshotSoundAt(snapshot, (nuint)index, &sound));
                sounds.Add(new RuntimeSoundEvent(sound.Kind, sound.SoundId, sound.Volume, sound.Loops));
            }
            return new RuntimeFrameSnapshot(entries, sounds, RadicleNative.SnapshotHash(snapshot));
        }
        finally
        {
            RadicleNative.SnapshotDestroy(snapshot);
        }
    }

    public IReadOnlyList<ulong> Replay(IReadOnlyList<RuntimeInput> inputs, double frameDtSeconds,
                                       int frameCount)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        var nativeInputs = new NativeInput[inputs.Count];
        for (var index = 0; index < inputs.Count; ++index) nativeInputs[index] = inputs[index].Native;
        var hashes = new ulong[frameCount];
        nuint written = 0;
        fixed (NativeInput* inputPointer = nativeInputs)
        fixed (ulong* hashPointer = hashes)
        {
            Call.Check(RadicleNative.MovieReplay(
                _handle, inputPointer, (nuint)nativeInputs.Length, frameDtSeconds,
                (nuint)frameCount, hashPointer, (nuint)hashes.Length, &written));
        }
        if ((int)written != frameCount) Array.Resize(ref hashes, (int)written);
        return hashes;
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        RadicleNative.MovieClose(_handle);
        _handle = IntPtr.Zero;
        _owner.Remove(this);
    }
}

public sealed unsafe class Runtime : IDisposable
{
    private readonly List<RuntimeMovie> _movies = new();
    private GCHandle _callbackHandle;
    private IntPtr _handle;

    private Runtime(IntPtr handle, GCHandle callbackHandle)
    {
        _handle = handle;
        _callbackHandle = callbackHandle;
    }

    public static Runtime Open(RuntimeCallbacks? callbacks = null)
    {
        RuntimeHost host = default;
        var callbackHandle = default(GCHandle);
        if (callbacks != null)
        {
            callbacks.ResolveDelegate = (user, path, outBytes, outSize) =>
                Resolve(callbacks.ResolveAsset, path, outBytes, outSize);
            callbacks.ReadDelegate = (user, name, outBytes, outSize) =>
                Resolve(callbacks.ReadStorage, name, outBytes, outSize);
            callbacks.WriteDelegate = (user, name, bytes, size) =>
            {
                if (callbacks.WriteStorage == null) return (int)Status.NotFound;
                var key = Marshal.PtrToStringUTF8((IntPtr)name) ?? string.Empty;
                var data = new byte[(int)size];
                Marshal.Copy((IntPtr)bytes, data, 0, data.Length);
                return callbacks.WriteStorage(key, data) ? (int)Status.Ok : (int)Status.NotFound;
            };
            callbacks.HostDelegate = (user, method, arguments, argumentCount, outText, outSize) =>
            {
                if (callbacks.HostCall == null) return (int)Status.NotFound;
                var name = Marshal.PtrToStringUTF8((IntPtr)method) ?? string.Empty;
                var parts = new string[(int)argumentCount];
                for (var index = 0; index < parts.Length; ++index)
                    parts[index] = Marshal.PtrToStringUTF8((IntPtr)arguments[index]) ?? string.Empty;
                var answer = callbacks.HostCall(name, parts);
                if (answer == null) return (int)Status.NotFound;
                WriteBuffer(System.Text.Encoding.UTF8.GetBytes(answer), outText, outSize);
                return (int)Status.Ok;
            };
            callbackHandle = GCHandle.Alloc(callbacks);
            host.User = (void*)GCHandle.ToIntPtr(callbackHandle);
            host.ResolveAsset = Marshal.GetFunctionPointerForDelegate(callbacks.ResolveDelegate);
            host.ReadStorage = Marshal.GetFunctionPointerForDelegate(callbacks.ReadDelegate);
            host.WriteStorage = Marshal.GetFunctionPointerForDelegate(callbacks.WriteDelegate);
            host.HostCall = Marshal.GetFunctionPointerForDelegate(callbacks.HostDelegate);
        }
        IntPtr runtimeHandle = IntPtr.Zero;
        Call.Check(RadicleNative.RuntimeOpen(&host, &runtimeHandle));
        return new Runtime(runtimeHandle, callbackHandle);
    }

    private static int Resolve(Func<string, byte[]?>? source, byte* key, byte** outBytes,
                               nuint* outSize)
    {
        if (source == null) return (int)Status.NotFound;
        var name = Marshal.PtrToStringUTF8((IntPtr)key) ?? string.Empty;
        var bytes = source(name);
        if (bytes == null) return (int)Status.NotFound;
        WriteBuffer(bytes, outBytes, outSize);
        return (int)Status.Ok;
    }

    private static void WriteBuffer(byte[] bytes, byte** outBytes, nuint* outSize)
    {
        var buffer = (byte*)NativeMemory.Alloc((nuint)(bytes.Length + 1));
        for (var index = 0; index < bytes.Length; ++index) buffer[index] = bytes[index];
        buffer[bytes.Length] = 0;
        *outBytes = buffer;
        *outSize = (nuint)bytes.Length;
    }

    public RuntimeMovie OpenMovie(ReadOnlySpan<byte> bytes)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(Runtime));
        IntPtr movie = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.RuntimeOpenMovie(_handle, pointer, (nuint)bytes.Length, &movie));
        }
        var wrapper = new RuntimeMovie(movie, this);
        _movies.Add(wrapper);
        return wrapper;
    }

    public RuntimeMovie OpenMovie(Ba2Archive archive, string path)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(Runtime));
        IntPtr movie = IntPtr.Zero;
        Call.Check(RadicleNative.RuntimeOpenMovieFromBa2(_handle, archive.NativeHandle, path, &movie));
        var wrapper = new RuntimeMovie(movie, this);
        _movies.Add(wrapper);
        return wrapper;
    }

    internal void Remove(RuntimeMovie movie) => _movies.Remove(movie);

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        foreach (var movie in _movies.ToArray()) movie.Dispose();
        RadicleNative.RuntimeClose(_handle);
        _handle = IntPtr.Zero;
        if (_callbackHandle.IsAllocated) _callbackHandle.Free();
    }
}
