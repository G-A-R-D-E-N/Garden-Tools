using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;
using NativeCall = Radicle.Interop.Call;
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
    int FilterCount,
    ushort Ratio,
    bool CacheAsBitmap)
{
    public RuntimeEntryExtra Extra { get; init; } = new(0, 0, Array.Empty<RuntimeFilter>());
}

public sealed record RuntimeFilter(byte Kind, uint Color, uint HighlightColor, double BlurX, double BlurY,
                                   double Angle, double Distance, double Strength, bool Inner, bool Knockout,
                                   bool CompositeSource, bool OnTop, byte Passes, byte MatrixColumns,
                                   byte MatrixRows, float Divisor, float Bias, uint DefaultColor, bool Clamp,
                                   bool PreserveAlpha, uint[] GradientColors, byte[] GradientRatios,
                                   float[] Matrix);

public sealed record RuntimeEntryExtra(ulong DrawnKey, ulong DrawnRevision,
                                       IReadOnlyList<RuntimeFilter> PlacedFilters);

public sealed record RuntimeRootState(uint Frame, uint Total, bool Playing, string Label);

public sealed record RuntimeArtStop(byte Ratio, uint Color);

public sealed record RuntimeArtContour(double[] Points, bool Closed);

public sealed record RuntimeArtFill(byte Kind, uint Color, ushort BitmapId, byte Spread, byte Interpolation,
                                    double FocalPoint, double[] Matrix, IReadOnlyList<RuntimeArtStop> Stops,
                                    IReadOnlyList<RuntimeArtContour> Contours, bool BitmapMissing = false);

public sealed record RuntimeArtLine(double Width, uint Color, byte Caps, byte Joins, double MiterLimit,
                                    IReadOnlyList<RuntimeArtContour> Contours);

public sealed record RuntimeArtShape(ushort CharacterId, IReadOnlyList<RuntimeArtFill> Fills,
                                     IReadOnlyList<RuntimeArtLine> Lines);

public sealed record RuntimeArtBitmap(ushort CharacterId, ushort Width, ushort Height, byte[] Png);

public sealed record RuntimeArtFont(ushort FontId, IReadOnlyList<RuntimeArtShape> Glyphs);

public sealed record RuntimeArt(IReadOnlyList<RuntimeArtShape> Shapes, IReadOnlyList<RuntimeArtBitmap> Bitmaps,
                                IReadOnlyList<RuntimeArtFont> Fonts);

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
}

public sealed unsafe class RuntimeMovie : IDisposable
{
    private IntPtr _handle;
    private IntPtr _snapshot;
    private readonly Runtime _owner;

    internal RuntimeMovie(IntPtr handle, Runtime owner)
    {
        _handle = handle;
        _owner = owner;
    }

    public void Advance(double dtSeconds)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        _owner.CheckAfterCallbacks(RadicleNative.MovieAdvance(_handle, dtSeconds));
    }

    public void Input(RuntimeInput input)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        var native = input.Native;
        _owner.CheckAfterCallbacks(RadicleNative.MovieInput(_handle, &native));
    }

    public RuntimeFrameSnapshot Snapshot()
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        IntPtr snapshot = IntPtr.Zero;
        NativeCall.Check(RadicleNative.MovieSnapshot(_handle, &snapshot));
        ReleaseSnapshot();
        _snapshot = snapshot;
        var entryCount = (int)RadicleNative.SnapshotEntryCount(snapshot);
        var entries = new List<RuntimeSnapshotEntry>(entryCount);
        for (var index = 0; index < entryCount; ++index)
        {
            RuntimeEntry native = default;
            NativeCall.Check(RadicleNative.SnapshotEntryAt(snapshot, (nuint)index, &native));
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
            SnapshotEntryExtra extra = default;
            NativeCall.Check(RadicleNative.RuntimeSnapshotEntryExtraAt(snapshot, (nuint)index, &extra));
            entries.Add(new RuntimeSnapshotEntry(
                (RuntimeEntryKind)native.Kind, native.CharacterId, native.ClipDepth,
                native.BlendMode, native.Visible != 0, native.Alpha,
                Marshal.PtrToStringUTF8(native.Path) ?? string.Empty,
                Marshal.PtrToStringUTF8(native.Name) ?? string.Empty, matrix, colour, runs,
                (int)native.FilterCount, native.Ratio, native.CacheAsBitmap != 0)
            {
                Extra = new RuntimeEntryExtra(extra.DrawnKey, extra.DrawnRevision,
                                              CopyFilters(extra.PlacedFilters, (int)extra.PlacedFilterCount)),
            });
        }
        var soundCount = (int)RadicleNative.SnapshotSoundCount(snapshot);
        var sounds = new List<RuntimeSoundEvent>(soundCount);
        for (var index = 0; index < soundCount; ++index)
        {
            RuntimeSound sound = default;
            NativeCall.Check(RadicleNative.SnapshotSoundAt(snapshot, (nuint)index, &sound));
            sounds.Add(new RuntimeSoundEvent(sound.Kind, sound.SoundId, sound.Volume, sound.Loops));
        }
        return new RuntimeFrameSnapshot(entries, sounds, RadicleNative.SnapshotHash(snapshot));
    }

    public RuntimeArtShape? DrawnShape(int entryIndex)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        if (_snapshot == IntPtr.Zero) throw new InvalidOperationException("take a snapshot before reading its drawn shapes");
        ArtShape native = default;
        if (!NativeCall.Found(RadicleNative.RuntimeSnapshotDrawnAt(_snapshot, (nuint)entryIndex, &native))) return null;
        return ToArtShape(native);
    }

    public RuntimeArt OpenArt()
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        IntPtr art = IntPtr.Zero;
        NativeCall.Check(RadicleNative.MovieArtOpen(_handle, &art));
        try
        {
            var shapes = new RuntimeArtShape[(int)RadicleNative.ArtShapeCount(art)];
            for (var index = 0; index < shapes.Length; ++index)
            {
                ArtShape native = default;
                NativeCall.Check(RadicleNative.ArtShapeAt(art, (nuint)index, &native));
                shapes[index] = ToArtShape(native);
            }
            var bitmaps = new RuntimeArtBitmap[(int)RadicleNative.ArtBitmapCount(art)];
            for (var index = 0; index < bitmaps.Length; ++index)
            {
                ArtBitmap native = default;
                NativeCall.Check(RadicleNative.ArtBitmapAt(art, (nuint)index, &native));
                bitmaps[index] = new RuntimeArtBitmap(native.CharacterId, native.Width, native.Height,
                                                      CopyArray<byte>(native.Png, (int)native.PngSize));
            }
            var fonts = new RuntimeArtFont[(int)RadicleNative.ArtFontCount(art)];
            for (var index = 0; index < fonts.Length; ++index)
            {
                ArtFont native = default;
                NativeCall.Check(RadicleNative.ArtFontAt(art, (nuint)index, &native));
                var glyphs = new RuntimeArtShape[(int)native.GlyphCount];
                var nativeGlyphs = (ArtShape*)native.Glyphs;
                for (var glyph = 0; glyph < glyphs.Length; ++glyph)
                {
                    glyphs[glyph] = ToArtShape(nativeGlyphs[glyph]);
                }
                fonts[index] = new RuntimeArtFont(native.FontId, glyphs);
            }
            return new RuntimeArt(shapes, bitmaps, fonts);
        }
        finally
        {
            RadicleNative.ArtDestroy(art);
        }
    }

    private void ReleaseSnapshot()
    {
        if (_snapshot == IntPtr.Zero) return;
        RadicleNative.SnapshotDestroy(_snapshot);
        _snapshot = IntPtr.Zero;
    }

    private static RuntimeArtShape ToArtShape(ArtShape shape)
    {
        var fills = new RuntimeArtFill[(int)shape.FillCount];
        var nativeFills = (ArtFill*)shape.Fills;
        for (var index = 0; index < fills.Length; ++index)
        {
            var fill = nativeFills[index];
            var nativeStops = (ArtGradientStop*)fill.Stops;
            var stops = new RuntimeArtStop[(int)fill.StopCount];
            for (var stop = 0; stop < stops.Length; ++stop)
            {
                stops[stop] = new RuntimeArtStop(nativeStops[stop].Ratio, nativeStops[stop].Color);
            }
            fills[index] = new RuntimeArtFill(
                fill.Kind, fill.Color, fill.BitmapId, fill.Spread, fill.Interpolation, fill.FocalPoint,
                new[] { fill.Matrix[0], fill.Matrix[1], fill.Matrix[2], fill.Matrix[3], fill.Matrix[4], fill.Matrix[5] },
                stops, ToContours(fill.ContourCount, fill.Contours), fill.BitmapMissing != 0);
        }
        var lines = new RuntimeArtLine[(int)shape.LineCount];
        var nativeLines = (ArtLine*)shape.Lines;
        for (var index = 0; index < lines.Length; ++index)
        {
            var line = nativeLines[index];
            lines[index] = new RuntimeArtLine(line.Width, line.Color, line.Caps, line.Joins, line.MiterLimit,
                                              ToContours(line.ContourCount, line.Contours));
        }
        return new RuntimeArtShape(shape.CharacterId, fills, lines);
    }

    private static IReadOnlyList<RuntimeArtContour> ToContours(nuint count, IntPtr source)
    {
        var contours = new RuntimeArtContour[(int)count];
        var nativeContours = (ArtContour*)source;
        for (var index = 0; index < contours.Length; ++index)
        {
            var contour = nativeContours[index];
            contours[index] = new RuntimeArtContour(CopyArray<double>(contour.Points, (int)contour.PointCount * 2),
                                                    contour.Closed != 0);
        }
        return contours;
    }

    private static IReadOnlyList<RuntimeFilter> CopyFilters(IntPtr source, int count)
    {
        if (count == 0 || source == IntPtr.Zero) return Array.Empty<RuntimeFilter>();
        var nativeFilters = (SnapshotFilter*)source;
        var filters = new RuntimeFilter[count];
        for (var index = 0; index < count; ++index)
        {
            var filter = nativeFilters[index];
            filters[index] = new RuntimeFilter(
                filter.Kind, filter.Color, filter.HighlightColor, filter.BlurX, filter.BlurY, filter.Angle,
                filter.Distance, filter.Strength, filter.Inner != 0, filter.Knockout != 0,
                filter.CompositeSource != 0, filter.OnTop != 0, filter.Passes, filter.MatrixColumns,
                filter.MatrixRows, filter.Divisor, filter.Bias, filter.DefaultColor, filter.Clamp != 0,
                filter.PreserveAlpha != 0,
                CopyArray<uint>(filter.GradientColors, (int)filter.GradientCount),
                CopyArray<byte>(filter.GradientRatios, (int)filter.GradientCount),
                CopyArray<float>(filter.Matrix, (int)filter.MatrixCount));
        }
        return filters;
    }

    private static T[] CopyArray<T>(IntPtr source, int count) where T : unmanaged
    {
        if (count == 0 || source == IntPtr.Zero) return Array.Empty<T>();
        return new ReadOnlySpan<T>((void*)source, count).ToArray();
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
            _owner.CheckAfterCallbacks(RadicleNative.MovieReplay(
                _handle, inputPointer, (nuint)nativeInputs.Length, frameDtSeconds,
                (nuint)frameCount, hashPointer, (nuint)hashes.Length, &written));
        }
        if ((int)written != frameCount) Array.Resize(ref hashes, (int)written);
        return hashes;
    }

    // Requires: pass a single null argument as (object?)null; a bare null literal means no arguments.
    public string? Call(string member, params object?[]? arguments)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        var values = arguments ?? Array.Empty<object?>();
        using var strings = new NativeStrings();
        var native = new RuntimeCallArg[values.Length];
        for (var index = 0; index < values.Length; ++index) native[index] = CallArgument(values[index], strings);
        byte* text = null;
        nuint size = 0;
        Status status;
        fixed (RuntimeCallArg* nativeArguments = native)
        {
            status = RadicleNative.MovieCall(_handle, (byte*)strings.Add(member), nativeArguments,
                                             (nuint)native.Length, &text, &size);
        }
        var result = text == null ? null : NativeCall.TakeText(text, size);
        _owner.CheckAfterCallbacks(status);
        return result;
    }

    // Returns false and moves nothing when the root timeline has no such label; a failing frame script throws after the playhead has moved; Frame is 1-based.
    public bool GoToLabel(string label, bool play)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
        using var strings = new NativeStrings();
        var status = RadicleNative.MovieGoto(_handle, (byte*)strings.Add(label), play ? 1 : 0);
        _owner.RethrowFault();
        return NativeCall.Found(status);
    }

    public RuntimeRootState RootState
    {
        get
        {
            if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
            uint frame = 0;
            uint total = 0;
            var playing = 0;
            byte* label = null;
            NativeCall.Check(RadicleNative.MovieRootState(_handle, &frame, &total, &playing, &label));
            return new RuntimeRootState(frame, total, playing != 0, Utf8.Read((IntPtr)label) ?? string.Empty);
        }
    }

    public string? FrameError
    {
        get
        {
            if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(RuntimeMovie));
            byte* text = null;
            nuint size = 0;
            NativeCall.Check(RadicleNative.MovieFrameError(_handle, &text, &size));
            return text == null ? null : NativeCall.TakeText(text, size);
        }
    }

    private static RuntimeCallArg CallArgument(object? value, NativeStrings strings) => value switch
    {
        null => new RuntimeCallArg { Kind = (byte)RuntimeCallArgKind.Null },
        bool flag => new RuntimeCallArg { Kind = (byte)RuntimeCallArgKind.Boolean, Flag = flag ? 1 : 0 },
        string text => new RuntimeCallArg { Kind = (byte)RuntimeCallArgKind.String, Text = strings.Add(text) },
        float or double or int or uint or long or ulong or short or ushort or byte or sbyte => new RuntimeCallArg
        {
            Kind = (byte)RuntimeCallArgKind.Number,
            Number = Convert.ToDouble(value, CultureInfo.InvariantCulture),
        },
        _ => throw new ArgumentException("a call argument must be null, a bool, a number or a string", nameof(value)),
    };

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        ReleaseSnapshot();
        RadicleNative.MovieClose(_handle);
        _handle = IntPtr.Zero;
        _owner.Remove(this);
    }
}

// Invariant: each runtime keeps its own callback delegates in fields for its whole life, so a second runtime can never replace or free the first one's.
public sealed unsafe class Runtime : IDisposable
{
    private readonly List<RuntimeMovie> _movies = new();
    private readonly RuntimeResolveAssetCallback? _resolve;
    private readonly RuntimeReadStorageCallback? _read;
    private readonly RuntimeWriteStorageCallback? _write;
    private readonly RuntimeHostCallCallback? _hostCall;
    private GCHandle _callbackHandle;
    private IntPtr _handle;
    // Requires: one thread drives a Runtime and its movies at a time; _fault is not synchronized.
    private Exception? _fault;

    private Runtime(RuntimeCallbacks? callbacks)
    {
        if (callbacks == null) return;
        _resolve = (user, path, outBytes, outSize) =>
            Guard(() => Resolve(callbacks.ResolveAsset, path, outBytes, outSize));
        _read = (user, name, outBytes, outSize) =>
            Guard(() => Resolve(callbacks.ReadStorage, name, outBytes, outSize));
        _write = (user, name, bytes, size) =>
            Guard(() => Write(callbacks.WriteStorage, name, bytes, size));
        _hostCall = (user, method, arguments, argumentCount, outText, outSize) =>
            Guard(() => HostCall(callbacks.HostCall, method, arguments, argumentCount, outText, outSize));
    }

    public Exception? Fault => _fault;

    public static Runtime Open(RuntimeCallbacks? callbacks = null)
    {
        var runtime = new Runtime(callbacks);
        RuntimeHost host = default;
        if (callbacks != null)
        {
            runtime._callbackHandle = GCHandle.Alloc(callbacks);
            host.User = (void*)GCHandle.ToIntPtr(runtime._callbackHandle);
            host.ResolveAsset = Marshal.GetFunctionPointerForDelegate(runtime._resolve!);
            host.ReadStorage = Marshal.GetFunctionPointerForDelegate(runtime._read!);
            host.WriteStorage = Marshal.GetFunctionPointerForDelegate(runtime._write!);
            host.HostCall = Marshal.GetFunctionPointerForDelegate(runtime._hostCall!);
        }
        IntPtr runtimeHandle = IntPtr.Zero;
        var openStatus = RadicleNative.RuntimeOpen(&host, &runtimeHandle);
        if (openStatus != Status.Ok && runtime._callbackHandle.IsAllocated) runtime._callbackHandle.Free();
        Call.Check(openStatus);
        runtime._handle = runtimeHandle;
        return runtime;
    }

    // Invariant: no managed exception may cross the native boundary, so a failing handler answers NOT_FOUND, the documented "absent" status, and its first exception is kept in _fault for the calling thread's next CheckAfterCallbacks.
    private int Guard(Func<int> body)
    {
        try { return body(); }
        catch (Exception fault)
        {
            _fault ??= fault;
            return (int)Status.NotFound;
        }
    }

    internal void RethrowFault()
    {
        if (_fault is not { } fault) return;
        _fault = null;
        ExceptionDispatchInfo.Throw(fault);
    }

    internal void CheckAfterCallbacks(Status status)
    {
        RethrowFault();
        Call.Check(status);
    }

    private static int Write(Func<string, byte[], bool>? sink, byte* name, byte* bytes, nuint size)
    {
        if (sink == null) return (int)Status.NotFound;
        var key = Marshal.PtrToStringUTF8((IntPtr)name) ?? string.Empty;
        var data = new byte[(int)size];
        Marshal.Copy((IntPtr)bytes, data, 0, data.Length);
        return sink(key, data) ? (int)Status.Ok : (int)Status.NotFound;
    }

    private static int HostCall(Func<string, IReadOnlyList<string>, string?>? host, byte* method,
                                byte** arguments, nuint argumentCount, byte** outText,
                                nuint* outSize)
    {
        if (host == null) return (int)Status.NotFound;
        var name = Marshal.PtrToStringUTF8((IntPtr)method) ?? string.Empty;
        var parts = new string[(int)argumentCount];
        for (var index = 0; index < parts.Length; ++index)
            parts[index] = Marshal.PtrToStringUTF8((IntPtr)arguments[index]) ?? string.Empty;
        var answer = host(name, parts);
        if (answer == null) return (int)Status.NotFound;
        WriteBuffer(System.Text.Encoding.UTF8.GetBytes(answer), outText, outSize);
        return (int)Status.Ok;
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

    public RuntimeMovie OpenMovie(ReadOnlySpan<byte> bytes) => OpenMovie(bytes, bgsHost: false);

    public RuntimeMovie OpenMovie(ReadOnlySpan<byte> bytes, bool bgsHost)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(Runtime));
        IntPtr movie = IntPtr.Zero;
        Status status;
        fixed (byte* pointer = bytes)
        {
            status = RadicleNative.RuntimeOpenMovieWithBgs(_handle, pointer, (nuint)bytes.Length,
                                                           bgsHost ? 1 : 0, &movie);
        }
        return Wrap(status, movie);
    }

    public RuntimeMovie OpenMovie(Ba2Archive archive, string path) => OpenMovie(archive, path, bgsHost: false);

    public RuntimeMovie OpenMovie(Ba2Archive archive, string path, bool bgsHost)
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(Runtime));
        IntPtr movie = IntPtr.Zero;
        var status = RadicleNative.RuntimeOpenMovieFromBa2WithBgs(_handle, archive.NativeHandle, path,
                                                                  bgsHost ? 1 : 0, &movie);
        return Wrap(status, movie);
    }

    private RuntimeMovie Wrap(Status status, IntPtr movie)
    {
        if (status != Status.Ok) CheckAfterCallbacks(status);
        var wrapper = new RuntimeMovie(movie, this);
        _movies.Add(wrapper);
        try { RethrowFault(); }
        catch
        {
            wrapper.Dispose();
            throw;
        }
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
        GC.KeepAlive(_resolve);
        GC.KeepAlive(_read);
        GC.KeepAlive(_write);
        GC.KeepAlive(_hostCall);
    }
}
