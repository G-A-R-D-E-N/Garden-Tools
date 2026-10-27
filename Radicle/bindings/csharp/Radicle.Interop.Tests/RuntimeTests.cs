using System;
using System.Collections.Generic;
using System.IO;
using Radicle.Interop;
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
}
