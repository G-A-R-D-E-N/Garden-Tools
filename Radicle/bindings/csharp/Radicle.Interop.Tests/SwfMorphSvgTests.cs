using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class SwfMorphSvgTests
{
    private const ushort MorphId = 3;
    private const ushort SpriteId = 4;

    private static byte[] Rect(int xMax, int yMax)
    {
        var bits = new SwfBitWriter();
        bits.Rect(0, xMax, 0, yMax, 9);
        return bits.ToArray();
    }

    private static byte[] Edges(int moveX, int moveY, int dx, int dy, bool fill = false)
    {
        var bits = new SwfBitWriter();
        bits.Push(0, 1);
        bits.Push(fill ? 0x05u : 0x01u, 5);
        bits.Push(8, 5);
        bits.Push((uint)moveX & 0xFF, 8);
        bits.Push((uint)moveY & 0xFF, 8);
        if (fill) bits.Push(1, 1);
        bits.Push(1, 1);
        bits.Push(1, 1);
        bits.Push(2, 4);
        bits.Push(1, 1);
        bits.Push((uint)dx & 0xF, 4);
        bits.Push((uint)dy & 0xF, 4);
        bits.Push(0, 6);
        return bits.ToArray();
    }

    // Constraint: one solid fill from red to blue and no line styles, so the morph interpolates; the start edges set fill 1 and the end edges only move.
    private static byte[] MorphPayload()
    {
        var start = new List<byte> { 1, 0x00, 255, 0, 0, 255, 0, 0, 255, 255, 0, 0x10 };
        start.AddRange(Edges(1, 2, 4, 6, true));
        var payload = new List<byte>();
        payload.AddRange(SwfFixtures.U16(MorphId));
        payload.AddRange(Rect(100, 80));
        payload.AddRange(Rect(160, 120));
        payload.AddRange(BitConverter.GetBytes((uint)start.Count));
        payload.AddRange(start);
        payload.Add(0);
        payload.AddRange(Edges(-1, -2, -2, -2));
        return payload.ToArray();
    }

    // Constraint: PlaceObject2 flags are 0x01 Move, 0x02 HasCharacter, 0x10 HasRatio, and the ratio follows the depth and character id.
    private static byte[] Place(bool move, ushort? character, ushort? ratio)
    {
        var bytes = new List<byte> { (byte)((move ? 0x01 : 0) | (character.HasValue ? 0x02 : 0) | (ratio.HasValue ? 0x10 : 0)) };
        bytes.AddRange(SwfFixtures.U16(1));
        if (character.HasValue) bytes.AddRange(SwfFixtures.U16(character.Value));
        if (ratio.HasValue) bytes.AddRange(SwfFixtures.U16(ratio.Value));
        return bytes.ToArray();
    }

    private static byte[] MorphSpriteMovie()
    {
        var sprite = new List<byte>();
        sprite.AddRange(SwfFixtures.U16(SpriteId));
        sprite.AddRange(SwfFixtures.U16(3));
        sprite.AddRange(SwfFixtures.Tag(26, Place(false, MorphId, 0)));
        sprite.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        sprite.AddRange(SwfFixtures.Tag(26, Place(true, null, 32768)));
        sprite.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        sprite.AddRange(SwfFixtures.Tag(26, Place(true, null, 65535)));
        sprite.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        sprite.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));

        var body = new List<byte>();
        body.AddRange(Rect(100, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(SwfFixtures.Tag(46, MorphPayload()));
        body.AddRange(SwfFixtures.Tag(39, sprite.ToArray()));
        body.AddRange(SwfFixtures.Tag(26, Place(false, SpriteId, null)));
        body.AddRange(SwfFixtures.Tag(1, Array.Empty<byte>()));
        body.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public void ExportsAMorphAtThreeRatiosAsDistinctSvg()
    {
        using var swf = SwfFile.FromBytes(MorphSpriteMovie());

        var start = swf.MorphShapeSvg(MorphId, 0);
        var middle = swf.MorphShapeSvg(MorphId, 32768);
        var end = swf.MorphShapeSvg(MorphId, 65535);
        Assert.StartsWith("<svg", start);
        Assert.Contains("width=\"5\"", start);
        Assert.Contains("width=\"8\"", end);
        Assert.NotEqual(start, middle);
        Assert.NotEqual(middle, end);
        Assert.Contains("fill=\"#ff0000\"", start);
        Assert.Contains("fill=\"#0000ff\"", end);
        Assert.Contains("fill=\"#7f0080\"", middle);
        Assert.Contains("d=\"M0.05 0.1L0 0\"", middle);
        Assert.Equal(start, swf.MorphShapeSvg(MorphId, 0));
    }

    [Fact]
    public void RefusesANonMorphAndAnUnknownCharacter()
    {
        using var swf = SwfFile.FromBytes(MorphSpriteMovie());

        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => swf.MorphShapeSvg(SpriteId, 0)).Status);
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => swf.MorphShapeSvg(99, 0)).Status);
    }

    [Fact]
    public void ReportsThePlaceObjectRatioOnTheFrameInstances()
    {
        using var swf = SwfFile.FromBytes(MorphSpriteMovie());
        using var frames = swf.OpenFrames();

        var timeline = Enumerable.Range(0, frames.TimelineCount).Single(index => frames.TimelineAt(index).CharacterId == SpriteId);
        Assert.Equal(3, frames.TimelineAt(timeline).FrameCount);
        Assert.Equal(new ushort?[] { 0, 32768, 65535 },
                     Enumerable.Range(0, 3).Select(frame => Assert.Single(frames.Instances(timeline, frame)).Ratio));
        Assert.Null(frames.Instances(0, 0).First(instance => instance.CharacterId == SpriteId).Ratio);
    }

    private static readonly string[] SpinnerMovies = { "interface/loadingmenu.swf", "interface/fadermenu.swf" };

    [Fact]
    public void VanillaSpinnerMorphsExportAtBothEndsAndCarryRatios()
    {
        var dataDir = Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(dataDir)) return;
        var archivePath = Path.Combine(dataDir, "Fallout4 - Interface.ba2");
        if (!File.Exists(archivePath)) return;
        using var archive = Ba2Archive.Open(archivePath);
        foreach (var movie in SpinnerMovies)
        {
            var entry = archive.Entries.First(item => item.Path.Replace('\\', '/').Equals(movie, StringComparison.OrdinalIgnoreCase));
            using var swf = SwfFile.FromArchive(archive, entry.Path);
            var morphs = swf.Characters().Where(item => item.Kind == SwfCharacterKind.Morph).Select(item => item.CharacterId).ToList();
            Assert.NotEmpty(morphs);
            foreach (var id in morphs)
            {
                var start = swf.MorphShapeSvg(id, 0);
                var end = swf.MorphShapeSvg(id, 65535);
                Assert.StartsWith("<svg", start);
                Assert.NotEqual(start, end);
            }

            using var frames = swf.OpenFrames();
            var ratios = new Dictionary<ushort, HashSet<ushort>>();
            for (var timeline = 0; timeline < frames.TimelineCount; ++timeline)
            {
                var info = frames.TimelineAt(timeline);
                for (var frame = 0; frame < info.FrameCount; ++frame)
                {
                    foreach (var instance in frames.Instances(timeline, frame))
                    {
                        if (!morphs.Contains(instance.CharacterId)) continue;
                        if (!ratios.TryGetValue(instance.CharacterId, out var set)) ratios[instance.CharacterId] = set = new HashSet<ushort>();
                        set.Add(instance.Ratio ?? 0);
                    }
                }
            }
            Assert.NotEmpty(ratios);
            Assert.Contains(ratios.Values, set => set.Any(ratio => ratio > 0));
        }
    }
}
