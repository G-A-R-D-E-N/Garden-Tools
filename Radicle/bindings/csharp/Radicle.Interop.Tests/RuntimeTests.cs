using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using Radicle.Interop;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class RuntimeTests
{
    private static byte[] Rect(int xMin, int xMax, int yMin, int yMax)
    {
        const int bits = 15;
        var bytes = new List<byte>();
        var bitsBuffer = new List<bool>();
        void Push(uint value, int count)
        {
            for (var index = count - 1; index >= 0; --index) bitsBuffer.Add(((value >> index) & 1u) != 0);
        }
        Push(bits, 5);
        Push((uint)xMin & 0x7FFFu, bits);
        Push((uint)xMax & 0x7FFFu, bits);
        Push((uint)yMin & 0x7FFFu, bits);
        Push((uint)yMax & 0x7FFFu, bits);
        while (bitsBuffer.Count % 8 != 0) bitsBuffer.Add(false);
        for (var index = 0; index < bitsBuffer.Count; index += 8)
        {
            var value = 0;
            for (var bit = 0; bit < 8; ++bit)
                if (bitsBuffer[index + bit]) value |= 1 << (7 - bit);
            bytes.Add((byte)value);
        }
        return bytes.ToArray();
    }

    private static byte[] SyntheticMovie()
    {
        var body = new List<byte>();
        body.AddRange(Rect(0, 400, 0, 200));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(BitConverter.GetBytes((ushort)((1 << 6) | 0)));
        body.AddRange(BitConverter.GetBytes((ushort)0));
        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    private static byte[] PlacedMovie()
    {
        var place = new List<byte> { 0x12, 0x26 };
        place.AddRange(SwfFixtures.U16(1));
        place.AddRange(SwfFixtures.U16(20));
        place.AddRange(SwfFixtures.U16(3));
        place.AddRange(new byte[] { 8, 1, 0 });
        var body = new List<byte>();
        body.AddRange(Rect(0, 400, 0, 200));
        body.AddRange(SwfFixtures.U16(0x0C00));
        body.AddRange(SwfFixtures.U16(1));
        body.AddRange(SwfFixtures.Tag(70, place.ToArray()));
        body.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        body.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));
        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    private static byte[] LabelledMovie()
    {
        var body = new List<byte>();
        body.AddRange(Rect(0, 400, 0, 200));
        body.AddRange(SwfFixtures.U16(0x0C00));
        body.AddRange(SwfFixtures.U16(3));
        for (var id = 21; id <= 23; ++id)
        {
            var sprite = new List<byte>();
            sprite.AddRange(SwfFixtures.U16(id));
            sprite.AddRange(SwfFixtures.U16(1));
            sprite.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
            sprite.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));
            body.AddRange(SwfFixtures.Tag(39, sprite.ToArray()));
        }
        var labels = new[] { "Open", "Mid", "Close" };
        var names = new[] { "A", "B", "C" };
        for (var frame = 0; frame < 3; ++frame)
        {
            var place = new List<byte> { 0x22 };
            place.AddRange(SwfFixtures.U16(1));
            place.AddRange(SwfFixtures.U16(21 + frame));
            place.AddRange(SwfFixtures.Z(names[frame]));
            body.AddRange(SwfFixtures.Tag(43, SwfFixtures.Z(labels[frame])));
            body.AddRange(SwfFixtures.Tag(26, place.ToArray()));
            body.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        }
        body.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));
        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public void SnapshotEntriesCarryAPlacementsRatioAndCacheFlag()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(PlacedMovie());
        var entries = movie.Snapshot().Entries;
        Assert.Equal(2, entries.Count);
        Assert.True(entries[0].Visible);
        Assert.Equal((ushort)0, entries[0].Ratio);
        Assert.False(entries[0].CacheAsBitmap);
        var placed = entries[1];
        Assert.Equal(20u, placed.CharacterId);
        Assert.Equal((byte)8, placed.BlendMode);
        Assert.False(placed.Visible);
        Assert.Equal((ushort)3, placed.Ratio);
        Assert.True(placed.CacheAsBitmap);
    }

    [Fact]
    public void AdvancesAndSnapshotsASyntheticMovie()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(SyntheticMovie());
        var snapshot = movie.Snapshot();
        Assert.NotEmpty(snapshot.Entries);
        Assert.NotEqual(0ul, snapshot.Hash);
        movie.Advance(1.0 / 24.0);
        Assert.NotEmpty(movie.Snapshot().Entries);
    }

    [Fact]
    public void ReplayIsDeterministic()
    {
        var inputs = new List<RuntimeInput>
        {
            new(0.0, RuntimeInputKind.MouseMove, 10.0, 20.0, 0, 0, 0),
            new(50.0, RuntimeInputKind.MouseDown, 10.0, 20.0, 0, 0, 0),
            new(100.0, RuntimeInputKind.MouseUp, 10.0, 20.0, 0, 0, 0),
        };
        IReadOnlyList<ulong> RunOnce()
        {
            using var runtime = Runtime.Open();
            using var movie = runtime.OpenMovie(SyntheticMovie());
            return movie.Replay(inputs, 1.0 / 24.0, 30);
        }
        var first = RunOnce();
        var second = RunOnce();
        Assert.Equal(30, first.Count);
        Assert.Equal(first, second);
    }

    [Fact]
    public void ReplaysAProgramMovieFromTheArchiveWhenDataIsAvailable()
    {
        var dataDir = Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(dataDir)) return;
        var archivePath = Path.Combine(dataDir, "Fallout4 - Interface.ba2");
        if (!File.Exists(archivePath)) return;
        using var archive = Ba2Archive.Open(archivePath);
        string? moviePath = null;
        foreach (var entry in archive.Entries)
        {
            if (!entry.Path.EndsWith("AtomicCommand.swf", StringComparison.OrdinalIgnoreCase)) continue;
            moviePath = entry.Path;
            break;
        }
        if (moviePath == null) return;
        var directory = moviePath[..(moviePath.LastIndexOfAny(new[] { '\\', '/' }) + 1)];
        byte[]? Resolve(string path)
        {
            foreach (var candidate in new[] { path, directory + path, Path.GetFileName(path) })
            {
                var index = archive.Find(candidate);
                if (index != null) return archive.Extract(index.Value);
            }
            return null;
        }
        var callbacks = new RuntimeCallbacks { ResolveAsset = Resolve };
        var inputs = new List<RuntimeInput>();
        for (var frame = 0; frame < 40; ++frame)
            inputs.Add(new RuntimeInput(frame * 41.6667, RuntimeInputKind.MouseMove,
                                        300.0 + frame * 4.0, 200.0 + frame * 2.0, 0, 0, 0));
        inputs.Add(new RuntimeInput(600.0, RuntimeInputKind.MouseDown, 460.0, 280.0, 0, 0, 0));
        inputs.Add(new RuntimeInput(650.0, RuntimeInputKind.MouseUp, 460.0, 280.0, 0, 0, 0));
        inputs.Add(new RuntimeInput(900.0, RuntimeInputKind.KeyDown, 0, 0, 0, 13, 0));
        inputs.Add(new RuntimeInput(950.0, RuntimeInputKind.KeyUp, 0, 0, 0, 13, 0));
        IReadOnlyList<ulong>? RunOnce()
        {
            using var runtime = Runtime.Open(callbacks);
            using var movie = runtime.OpenMovie(archive, moviePath);
            var snapshot = movie.Snapshot();
            if (snapshot.Entries.Count == 0) return null;
            return movie.Replay(inputs, 1.0 / 24.0, 60);
        }
        var first = RunOnce();
        if (first == null) return;
        var second = RunOnce();
        if (second == null) return;
        Assert.Equal(first, second);
    }

    // Detects: an art table that lists shapes, bitmaps or fonts for a movie that defines none, which would make GE upload art for nothing; the managed table must be empty.
    [Fact]
    public void ArtTableIsEmptyForAMovieWithoutDrawableDefinitions()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(SyntheticMovie());
        var art = movie.OpenArt();
        Assert.Empty(art.Shapes);
        Assert.Empty(art.Bitmaps);
        Assert.Empty(art.Fonts);
    }

    // Detects: a snapshot entry that reports a drawn key, revision or placed filter without a Graphics object or filter list, so GE would re-upload or redraw it; the extras must be empty.
    [Fact]
    public void SnapshotEntriesWithoutGraphicsOrFiltersExposeEmptyExtras()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(PlacedMovie());
        var entries = movie.Snapshot().Entries;
        Assert.Equal(2, entries.Count);
        foreach (var entry in entries)
        {
            Assert.Equal(0ul, entry.Extra.DrawnKey);
            Assert.Equal(0ul, entry.Extra.DrawnRevision);
            Assert.Empty(entry.Extra.PlacedFilters);
        }
        Assert.Null(movie.DrawnShape(0));
    }

    private static byte[] SquareShape(ushort id, int size)
    {
        var payload = new List<byte>();
        payload.AddRange(SwfFixtures.U16(id));
        payload.AddRange(Rect(0, size, 0, size));
        payload.Add(1);
        payload.AddRange(new byte[] { 0x00, 0xFF, 0x00, 0x00, 0xFF });
        payload.Add(0);
        payload.Add(0x10);
        var bits = new List<bool>();
        void Push(int value, int count)
        {
            for (var index = count - 1; index >= 0; --index) bits.Add((((uint)value >> index) & 1u) != 0);
        }
        void Edge(int dx, int dy)
        {
            Push(1, 1);
            Push(1, 1);
            Push(10, 4);
            Push(1, 1);
            Push(dx, 12);
            Push(dy, 12);
        }
        Push(0, 1);
        Push(0x03, 5);
        Push(2, 5);
        Push(0, 2);
        Push(0, 2);
        Push(1, 1);
        Edge(size, 0);
        Edge(0, size);
        Edge(-size, 0);
        Edge(0, -size);
        Push(0, 6);
        while (bits.Count % 8 != 0) bits.Add(false);
        for (var index = 0; index < bits.Count; index += 8)
        {
            var value = 0;
            for (var bit = 0; bit < 8; ++bit)
                if (bits[index + bit]) value |= 1 << (7 - bit);
            payload.Add((byte)value);
        }
        return payload.ToArray();
    }

    private static byte[] SquareMovie()
    {
        var body = new List<byte>();
        body.AddRange(Rect(0, 400, 0, 200));
        body.AddRange(SwfFixtures.U16(0x0C00));
        body.AddRange(SwfFixtures.U16(1));
        body.AddRange(SwfFixtures.Tag(32, SquareShape(7, 200)));
        body.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        body.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));
        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    private static byte[] FilteredMovie()
    {
        var place = new List<byte> { 0x02, 0x01 };
        place.AddRange(SwfFixtures.U16(1));
        place.AddRange(SwfFixtures.U16(7));
        place.Add(1);
        place.Add(0);
        place.AddRange(new byte[] { 0xFF, 0x00, 0x00, 0xFF });
        place.AddRange(new byte[] { 0x00, 0x00, 0x02, 0x00 });
        place.AddRange(new byte[] { 0x00, 0x00, 0x03, 0x00 });
        place.AddRange(new byte[] { 0x00, 0x00, 0x00, 0x00 });
        place.AddRange(new byte[] { 0x00, 0x00, 0x04, 0x00 });
        place.AddRange(SwfFixtures.U16(0x0100));
        place.Add(0x21);
        var body = new List<byte>();
        body.AddRange(Rect(0, 400, 0, 200));
        body.AddRange(SwfFixtures.U16(0x0C00));
        body.AddRange(SwfFixtures.U16(1));
        body.AddRange(SwfFixtures.Tag(70, place.ToArray()));
        body.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        body.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));
        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    // Detects: a DefineShape3 whose fill, colour or contour is dropped or misread by the managed art conversion, so GE draws nothing for the shape; the conversion must keep each of them.
    [Fact]
    public void OpenArtCarriesTheStyledOutlineOfEachDefinedShape()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(SquareMovie());
        var shape = Assert.Single(movie.OpenArt().Shapes);
        Assert.Equal((ushort)7, shape.CharacterId);
        var fill = Assert.Single(shape.Fills);
        Assert.Equal((byte)0x00, fill.Kind);
        Assert.Equal(0xFFFF0000u, fill.Color);
        var contour = Assert.Single(fill.Contours);
        Assert.True(contour.Closed);
        Assert.Equal(new double[] { 0, 0, 10, 0, 10, 10, 0, 10 }, contour.Points);
    }

    // Detects: a placed filter whose kind, colour, blur, distance, strength, passes or composite flag is misread by the managed copy, so GE draws a different shadow than the movie placed; the copy must keep each field.
    [Fact]
    public void SnapshotCarriesThePlacedFilterListFieldByField()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(FilteredMovie());
        var filter = Assert.Single(movie.Snapshot().Entries[1].Extra.PlacedFilters);
        Assert.Equal((byte)0, filter.Kind);
        Assert.Equal(0xFFFF0000u, filter.Color);
        Assert.Equal(2.0, filter.BlurX);
        Assert.Equal(3.0, filter.BlurY);
        Assert.Equal(4.0, filter.Distance);
        Assert.Equal(1.0, filter.Strength);
        Assert.True(filter.CompositeSource);
        Assert.Equal((byte)1, filter.Passes);
    }

    // Detects: a bgs-enabled open that reports success for bytes that do not parse, so the host would hold a movie it cannot run; the open must refuse them.
    [Fact]
    public void OpenMovieWithBgsRefusesBytesThatDoNotParse()
    {
        using var runtime = Runtime.Open();
        var refused = Assert.Throws<RadicleException>(() => runtime.OpenMovie(new byte[] { 1, 2, 3, 4 }, bgsHost: true));
        Assert.Equal(Status.ParseError, refused.Status);
    }

    private static byte[] MemberRootAbc() => new byte[]
    {
        0x10, 0x00, 0x2E, 0x00, 0x01, 0x01, 0x01, 0x0E, 0x0A, 0x4D, 0x65, 0x6D, 0x62, 0x65, 0x72, 0x52,
        0x6F, 0x6F, 0x74, 0x00, 0x09, 0x4D, 0x6F, 0x76, 0x69, 0x65, 0x43, 0x6C, 0x69, 0x70, 0x0D, 0x66,
        0x6C, 0x61, 0x73, 0x68, 0x2E, 0x64, 0x69, 0x73, 0x70, 0x6C, 0x61, 0x79, 0x06, 0x53, 0x74, 0x72,
        0x69, 0x6E, 0x67, 0x06, 0x4E, 0x75, 0x6D, 0x62, 0x65, 0x72, 0x05, 0x50, 0x6C, 0x61, 0x69, 0x6E,
        0x04, 0x50, 0x69, 0x6E, 0x67, 0x03, 0x41, 0x64, 0x64, 0x06, 0x66, 0x72, 0x61, 0x6D, 0x65, 0x32,
        0x04, 0x74, 0x65, 0x78, 0x74, 0x05, 0x63, 0x69, 0x6E, 0x69, 0x74, 0x04, 0x69, 0x6E, 0x69, 0x74,
        0x03, 0x16, 0x00, 0x16, 0x04, 0x02, 0x01, 0x01, 0x09, 0x07, 0x01, 0x01, 0x07, 0x02, 0x03, 0x07,
        0x01, 0x05, 0x07, 0x01, 0x06, 0x07, 0x01, 0x07, 0x07, 0x01, 0x08, 0x07, 0x01, 0x09, 0x07, 0x01,
        0x0A, 0x06, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03, 0x03, 0x08, 0x08, 0x01, 0x00, 0x00, 0x02, 0x04,
        0x04, 0x04, 0x09, 0x08, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0C,
        0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00, 0x03, 0x06, 0x01, 0x00,
        0x01, 0x07, 0x01, 0x00, 0x02, 0x08, 0x01, 0x00, 0x03, 0x04, 0x00, 0x01, 0x05, 0x01, 0x01, 0x04,
        0x00, 0x00, 0x06, 0x00, 0x10, 0x01, 0x00, 0x02, 0x08, 0xD0, 0x30, 0xD0, 0x2C, 0x0B, 0x68, 0x05,
        0x47, 0x00, 0x00, 0x01, 0x10, 0x02, 0x00, 0x02, 0x03, 0x62, 0x01, 0x48, 0x00, 0x00, 0x02, 0x10,
        0x03, 0x00, 0x02, 0x06, 0x62, 0x01, 0x62, 0x02, 0xA0, 0x48, 0x00, 0x00, 0x03, 0x10, 0x01, 0x00,
        0x02, 0x03, 0x24, 0x01, 0x03, 0x00, 0x00, 0x04, 0x10, 0x01, 0x00, 0x02, 0x01, 0x47, 0x00, 0x00,
        0x05, 0x10, 0x01, 0x00, 0x02, 0x01, 0x47, 0x00, 0x00,
    };

    private static byte[] BgsRootAbc() => new byte[]
    {
        0x10, 0x00, 0x2E, 0x00, 0x01, 0x01, 0x01, 0x0C, 0x07, 0x42, 0x67, 0x73, 0x52, 0x6F, 0x6F, 0x74,
        0x00, 0x09, 0x4D, 0x6F, 0x76, 0x69, 0x65, 0x43, 0x6C, 0x69, 0x70, 0x0D, 0x66, 0x6C, 0x61, 0x73,
        0x68, 0x2E, 0x64, 0x69, 0x73, 0x70, 0x6C, 0x61, 0x79, 0x0A, 0x42, 0x47, 0x53, 0x43, 0x6F, 0x64,
        0x65, 0x4F, 0x62, 0x6A, 0x09, 0x70, 0x6C, 0x61, 0x79, 0x53, 0x6F, 0x75, 0x6E, 0x64, 0x05, 0x61,
        0x70, 0x70, 0x6C, 0x79, 0x04, 0x50, 0x6C, 0x61, 0x79, 0x01, 0x78, 0x05, 0x63, 0x69, 0x6E, 0x69,
        0x74, 0x04, 0x69, 0x6E, 0x69, 0x74, 0x03, 0x16, 0x00, 0x16, 0x04, 0x02, 0x01, 0x01, 0x07, 0x07,
        0x01, 0x01, 0x07, 0x02, 0x03, 0x07, 0x01, 0x05, 0x07, 0x01, 0x06, 0x07, 0x01, 0x07, 0x07, 0x01,
        0x08, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
        0x0B, 0x00, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x00, 0x01, 0x02, 0x00,
        0x01, 0x03, 0x01, 0x01, 0x04, 0x00, 0x00, 0x04, 0x00, 0x10, 0x01, 0x00, 0x02, 0x08, 0xD0, 0x30,
        0xD0, 0x55, 0x00, 0x68, 0x03, 0x47, 0x00, 0x00, 0x01, 0x10, 0x01, 0x00, 0x02, 0x0E, 0xD0, 0x66,
        0x03, 0x66, 0x04, 0x20, 0x2C, 0x09, 0x56, 0x01, 0x46, 0x05, 0x02, 0x48, 0x00, 0x00, 0x02, 0x10,
        0x01, 0x00, 0x02, 0x01, 0x47, 0x00, 0x00, 0x03, 0x10, 0x01, 0x00, 0x02, 0x01, 0x47, 0x00, 0x00,
    };

    private static byte[] RootClassMovie(byte[] abc, string root, ushort frames, string? lastFrameLabel = null)
    {
        var payload = new List<byte>();
        payload.AddRange(BitConverter.GetBytes(0u));
        payload.AddRange(SwfFixtures.Z(root));
        payload.AddRange(abc);
        var symbols = new List<byte>();
        symbols.AddRange(SwfFixtures.U16(1));
        symbols.AddRange(SwfFixtures.U16(0));
        symbols.AddRange(SwfFixtures.Z(root));
        var body = new List<byte>();
        body.AddRange(Rect(0, 200, 0, 100));
        body.AddRange(SwfFixtures.U16(24 * 256));
        body.AddRange(SwfFixtures.U16(frames));
        body.AddRange(SwfFixtures.Tag(82, payload.ToArray()));
        body.AddRange(SwfFixtures.Tag(76, symbols.ToArray()));
        for (var frame = 0; frame < frames; ++frame)
        {
            if (lastFrameLabel != null && frame + 1 == frames) body.AddRange(SwfFixtures.Tag(43, SwfFixtures.Z(lastFrameLabel)));
            body.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        }
        body.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));
        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    private static byte[] MemberRootMovie(ushort frames, string? lastFrameLabel = null) => RootClassMovie(MemberRootAbc(), "MemberRoot", frames, lastFrameLabel);

    private static byte[] BgsRootMovie() => RootClassMovie(BgsRootAbc(), "BgsRoot", 1);

    private static RuntimeCallbacks RecordHostCalls(List<string> calls) => new()
    {
        HostCall = (name, args) =>
        {
            calls.Add(name + ":" + string.Join(",", args));
            return "ok";
        },
    };

    // Detects: a bgs-enabled open whose root script's bgs.* call never reaches the host, or reaches it on a plain open, so GE would run a movie with the wrong host wiring; only bgsHost: true must forward the call.
    [Fact]
    public void BgsHostCallReachesTheHostOnlyWhenTheOpenAsksForIt()
    {
        var calls = new List<string>();
        using (var off = Runtime.Open(RecordHostCalls(calls)))
        using (var plain = off.OpenMovie(BgsRootMovie(), bgsHost: false))
        {
            _ = Xunit.Record.Exception(() => plain.Call("Play"));
        }
        Assert.Empty(calls);
        using var bgs = Runtime.Open(RecordHostCalls(calls));
        using var movie = bgs.OpenMovie(BgsRootMovie(), bgsHost: true);
        movie.Call("Play");
        Assert.Equal(new[] { "bgs.playSound:x" }, calls);
    }

    // Detects: an archive open that drops bgsHost, so a bgs.* call from an archived root script never reaches the host; the archive overload must forward the call when asked.
    [Fact]
    public void ArchivedBgsOpenForwardsTheHostCallWhenAsked()
    {
        using var temp = new TempDirectory();
        var archivePath = temp.Write("Data/Interface - Main.ba2", Ba2Writer.General(("Interface\\Bgs.swf", BgsRootMovie())));
        using var archive = Ba2Archive.Open(archivePath);
        var calls = new List<string>();
        using var runtime = Runtime.Open(RecordHostCalls(calls));
        using var movie = runtime.OpenMovie(archive, "Interface\\Bgs.swf", bgsHost: true);
        movie.Call("Play");
        Assert.Equal(new[] { "bgs.playSound:x" }, calls);
    }

    // Detects: a second runtime sharing one RuntimeCallbacks object that replaces or frees the delegates the first runtime registered, so a later host call from the first runtime reaches released code; the first runtime must keep answering from its own registrations after the sibling closes and is collected.
    [Fact]
    public void TheFirstRuntimeKeepsItsCallbacksAfterASecondRuntimeCloses()
    {
        var calls = new List<string>();
        var callbacks = RecordHostCalls(calls);
        using var first = Runtime.Open(callbacks);
        using var movie = first.OpenMovie(BgsRootMovie(), bgsHost: true);
        for (var index = 0; index < 4; ++index)
        {
            var second = Runtime.Open(callbacks);
            using (var sibling = second.OpenMovie(BgsRootMovie(), bgsHost: true)) sibling.Call("Play");
            second.Dispose();
        }
        calls.Clear();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        _ = movie.Call("Play");
        Assert.Equal(new[] { "bgs.playSound:x" }, calls);
        Assert.Equal(1u, movie.RootState.Total);
    }

    // Detects: a managed exception thrown inside a host callback being swallowed so the host never learns its handler failed, or escaping the native frame and killing the process; the fault must surface from the call that ran the handler, once, and leave the movie usable.
    [Fact]
    public void AThrowingHostCallbackSurfacesOnTheCallThatRanItAndLeavesTheMovieUsable()
    {
        var attempts = 0;
        var armed = true;
        using var runtime = Runtime.Open(new RuntimeCallbacks
        {
            HostCall = (_, _) =>
            {
                attempts += 1;
                return armed ? throw new InvalidOperationException("the host refused the call") : "ok";
            },
        });
        using var movie = runtime.OpenMovie(BgsRootMovie(), bgsHost: true);
        Assert.Equal("the host refused the call", Assert.Throws<InvalidOperationException>(() => movie.Call("Play")).Message);
        Assert.Equal(1, attempts);
        Assert.Null(runtime.Fault);
        armed = false;
        _ = movie.Call("Play");
        Assert.Equal(2, attempts);
        movie.Advance(1.0 / 24.0);
        Assert.Null(movie.FrameError);
        Assert.Equal(1u, movie.RootState.Total);
    }

    // Detects: a call to an absent root member that reports success, or that leaves the movie unusable, which would stop GE's host from driving the movie after one bad call; the call must leave the movie usable.
    [Fact]
    public void CallRaisesNotFoundForAnAbsentMemberAndLeavesTheMovieUsable()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(SyntheticMovie());
        var missing = Assert.Throws<RadicleException>(() => { movie.Call("NoSuchMember"); });
        Assert.Equal(Status.NotFound, missing.Status);
        movie.Advance(1.0 / 24.0);
        Assert.NotEmpty(movie.Snapshot().Entries);
    }

    [Fact]
    public unsafe void AllocBytesBlocksAreReleasedByFreeBytes()
    {
        using var runtime = Runtime.Open();
        var block = (byte*)RadicleNative.AllocBytes(33);
        Assert.True(block != null);
        for (var index = 0; index < 33; ++index) block[index] = (byte)index;
        RadicleNative.FreeBytes(block);
        Assert.True(RadicleNative.AllocBytes(0) == IntPtr.Zero);
    }

    // Detects: a root method call whose number, string or boolean argument is marshalled to the wrong type, or whose returned text is not read back, so a host gets the wrong value from a root method; each typed call must return its text and a null result must stay null.
    [Fact]
    public void CallReturnsTheTextOfEachTypedRootResult()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(MemberRootMovie(1));
        Assert.Equal("5", movie.Call("Add", 2, 3));
        Assert.Equal("hello", movie.Call("Ping", "hello"));
        Assert.Equal("true", movie.Call("Ping", true));
        Assert.Null(movie.Call("Ping", (object?)null));
    }

    // Detects: a frame error reported while no frame script has failed, which would make a host stop a healthy movie; the error must stay null.
    [Fact]
    public void FrameErrorStaysNullWhileNoFrameScriptFails()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(SyntheticMovie());
        Assert.Null(movie.FrameError);
        movie.Advance(1.0 / 24.0);
        Assert.Null(movie.FrameError);
    }

    // Detects: a frame script that throws after Advance while FrameError still reads null, so a host keeps driving a movie that has stopped; the error must read back its text once the frame has run.
    [Fact]
    public void FrameErrorReadsBackAFrameScriptThatThrows()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(MemberRootMovie(2));
        Assert.Null(movie.FrameError);
        movie.Advance(1.0);
        Assert.Equal("uncaught exception: 1", movie.FrameError);
    }

    // Detects: a label jump that does nothing, lands on the wrong frame, ignores play, or reports success for an unknown label and moves the movie anyway; a known label must show that frame's placement and state, an unknown one must return false and change nothing.
    [Fact]
    public void GoToLabelJumpsToAFrameLabelAndRefusesAnUnknownOne()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(LabelledMovie());
        Assert.Equal(new RuntimeRootState(1, 3, true, "Open"), movie.RootState);

        Assert.True(movie.GoToLabel("Close", false));
        Assert.Equal(new RuntimeRootState(3, 3, false, "Close"), movie.RootState);
        var closed = movie.Snapshot();
        Assert.Equal("C", closed.Entries[1].Name);

        Assert.True(movie.GoToLabel("Mid", true));
        Assert.Equal(new RuntimeRootState(2, 3, true, "Mid"), movie.RootState);

        var before = movie.Snapshot().Hash;
        Assert.False(movie.GoToLabel("Nope", false));
        Assert.Equal(new RuntimeRootState(2, 3, true, "Mid"), movie.RootState);
        Assert.Equal(before, movie.Snapshot().Hash);
    }

    // Detects: a jump to a label whose frame script throws that returns false or succeeds instead of throwing, reports a status other than InternalError, or leaves the playhead off the labelled frame; the C++ test MovieGotoReportsAFailingFrameScriptAsInternalErrorAfterMovingThePlayhead is the revert guard for the NOT_FOUND mapping.
    [Fact]
    public void GoToLabelThrowsWhenTheTargetFrameScriptFailsAfterMovingThePlayhead()
    {
        using var runtime = Runtime.Open();
        using var movie = runtime.OpenMovie(MemberRootMovie(2, "Boom"));
        var error = Assert.Throws<RadicleException>(() => movie.GoToLabel("Boom", false));
        Assert.Equal(Status.InternalError, error.Status);
        Assert.Equal(new RuntimeRootState(2, 2, false, "Boom"), movie.RootState);
    }

    [Fact]
    public void OwnedLifecycleKeepsPexAndGateAliveWhenDisposedInsideGate()
    {
        var pex = PexFile.FromBytes(PexModelTests.SyntheticPex(withoutParent: true));
        PapyrusQuestLifecycle? lifecycle = null;
        var invoked = false;
        try
        {
            lifecycle = new PapyrusQuestLifecycle(new[] { pex }, _ =>
            {
                invoked = true;
                lifecycle!.Dispose();
                pex.Dispose();
                GC.Collect();
                GC.WaitForPendingFinalizers();
                return true;
            });
            var result = lifecycle.Start(new[] { new QuestStartupInput("Q1", true, new[] { "SyntheticObject" }) });
            Assert.True(invoked);
            Assert.True(result.Applied);
            Assert.Throws<ObjectDisposedException>(() => lifecycle.InstancesFor("Q1"));
        }
        finally
        {
            lifecycle?.Dispose();
            pex.Dispose();
        }
    }

    [Fact]
    public void MovieHandleClosesTheNativeMovieOnceWhenOwnerAddRefThrows()
    {
        using var runtime = Runtime.Open();
        var movie = runtime.OpenMovie(SyntheticMovie());
        var movieHandle = PrivateField<SafeHandle>(movie, "_movie");
        var closedOwner = CreateClosedOwner("RuntimeHandle");
        var movieHandleType = typeof(Runtime).Assembly.GetType("Radicle.Interop.MovieHandle", throwOnError: true)!;
        var nativeMovie = DetachHandle(movieHandle);
        var closes = 0;
        var closeAttempted = false;

        try
        {
            var error = Assert.Throws<TargetInvocationException>(() => Activator.CreateInstance(
                movieHandleType,
                BindingFlags.Instance | BindingFlags.NonPublic,
                binder: null,
                args: new object[] { nativeMovie, closedOwner, (Action<IntPtr>)(pointer =>
                {
                    closeAttempted = true;
                    RadicleNative.MovieClose(pointer);
                    closes += 1;
                }) },
                culture: null));

            Assert.IsType<ObjectDisposedException>(error.InnerException);
            Assert.Equal(1, closes);
        }
        finally
        {
            if (!closeAttempted) RadicleNative.MovieClose(nativeMovie);
            closedOwner.Dispose();
            movie.Dispose();
        }
    }

    [Fact]
    public void PapyrusLifecycleHandleDestroysTheNativeLifecycleOnceWhenOwnerAddRefThrows()
    {
        using var runtime = new PapyrusRuntime();
        var lifecycle = new PapyrusQuestLifecycle(runtime);
        var lifecycleHandle = PrivateField<SafeHandle>(lifecycle, "_lifecycle");
        var closedOwner = CreateClosedOwner("PapyrusRuntimeHandle");
        var handleType = typeof(Runtime).Assembly.GetType("Radicle.Interop.PapyrusLifecycleHandle", throwOnError: true)!;
        var nativeLifecycle = DetachHandle(lifecycleHandle);
        var destroys = 0;
        var destroyAttempted = false;

        try
        {
            var error = Assert.Throws<TargetInvocationException>(() => Activator.CreateInstance(
                handleType,
                BindingFlags.Instance | BindingFlags.NonPublic,
                binder: null,
                args: new object[] { nativeLifecycle, closedOwner, (Action<IntPtr>)(pointer =>
                {
                    destroyAttempted = true;
                    RadicleNative.PapyrusLifecycleDestroy(pointer);
                    destroys += 1;
                }) },
                culture: null));

            Assert.IsType<ObjectDisposedException>(error.InnerException);
            Assert.Equal(1, destroys);
        }
        finally
        {
            if (!destroyAttempted) RadicleNative.PapyrusLifecycleDestroy(nativeLifecycle);
            closedOwner.Dispose();
            lifecycle.Dispose();
        }
    }

    private static T PrivateField<T>(object instance, string name) where T : class =>
        (T)instance.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(instance)!;

    private static IntPtr DetachHandle(SafeHandle handle)
    {
        var nativeHandle = handle.DangerousGetHandle();
        var owner = PrivateField<SafeHandle>(handle, "_owner");
        handle.SetHandleAsInvalid();
        owner.DangerousRelease();
        return nativeHandle;
    }

    private static SafeHandle CreateClosedOwner(string typeName)
    {
        var ownerType = typeof(Runtime).Assembly.GetType($"Radicle.Interop.{typeName}", throwOnError: true)!;
        var owner = (SafeHandle)Activator.CreateInstance(
            ownerType,
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
            binder: null,
            args: new object[] { IntPtr.Zero },
            culture: null)!;
        owner.Dispose();
        return owner;
    }

    // Detects: a hand-written RuntimeCallArg whose size or text offset drifts from the C struct, which the generated layout probe cannot check. source:Radicle/include/fo4/c_api_runtime.h:radicle_call_arg
    [Fact]
    public void RuntimeCallArgMatchesTheCCallArgumentLayout()
    {
        Assert.Equal(24, Marshal.SizeOf(typeof(RuntimeCallArg)));
        Assert.Equal(16L, (long)Marshal.OffsetOf(typeof(RuntimeCallArg), nameof(RuntimeCallArg.Text)));
    }
}
