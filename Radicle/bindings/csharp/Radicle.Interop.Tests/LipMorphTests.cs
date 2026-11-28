using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Radicle.Interop;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class LipMorphTests
{
    private static byte[] BuildTri(int vertices, float[] positions, (string Name, float Scale, short[] Raw)[] morphs)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.ASCII);
        writer.Write(Encoding.ASCII.GetBytes("FRTRI003"));
        writer.Write(vertices);
        writer.Write(1);
        writer.Write(0);
        writer.Write(0);
        writer.Write(0);
        writer.Write(vertices);
        writer.Write(1);
        writer.Write(morphs.Length);
        writer.Write(0);
        writer.Write(0);
        for (var i = 0; i < 4; i++) writer.Write(0);
        foreach (var value in positions) writer.Write(value);
        for (var i = 0; i < 3; i++) writer.Write(0);
        for (var i = 0; i < vertices * 2; i++) writer.Write(0.5f);
        for (var i = 0; i < 3; i++) writer.Write(0);
        foreach (var morph in morphs)
        {
            writer.Write(morph.Name.Length + 1);
            writer.Write(Encoding.ASCII.GetBytes(morph.Name));
            writer.Write((byte)0);
            writer.Write(morph.Scale);
            foreach (var raw in morph.Raw) writer.Write(raw);
        }
        return stream.ToArray();
    }

    private static byte[] SampleTri() => BuildTri(
        2,
        new float[] { 0, 0, 0, 1, 2, 3 },
        new[]
        {
            ("JawOpen", 0.001f, new short[] { 0, -1000, -2000, 0, 500, 1000 }),
            ("Pucker", 0.5f, new short[] { 1, 2, 3, 4, 5, 6 }),
        });

    [Fact]
    public void ReadsCountsAndMorphNames()
    {
        using var tri = TriMorphFile.Read(SampleTri());
        Assert.Equal(2, tri.VertexCount);
        Assert.Equal(1, tri.TriangleCount);
        Assert.Equal(new[] { "JawOpen", "Pucker" }, tri.MorphNames);
        Assert.Equal(1, tri.IndexOf("Pucker"));
        Assert.Equal(-1, tri.IndexOf("Nope"));
    }

    [Fact]
    public void ReturnsPositionsAndDeltasInTheEngineFrame()
    {
        using var tri = TriMorphFile.Read(SampleTri());
        Assert.Equal(new float[] { 0, 0, 0, 1, 3, -2 }, tri.Positions(1f));
        var deltas = tri.Deltas(0, 1f);
        Assert.Equal(6, deltas.Length);
        Assert.Equal(1f, deltas[2], 4);
        Assert.Equal(1f, deltas[4], 4);
        Assert.Equal(-0.5f, deltas[5], 4);
        Assert.Equal(1f / 69.99125f, tri.Positions()[3], 6);
    }

    [Fact]
    public void ReadsTriBytesThatCameOutOfAnArchiveRead()
    {
        using var temp = new TempDirectory();
        var archive = temp.Write("Data/Base - Meshes.ba2", Ba2Writer.General((@"Meshes\Actors\Character\CharacterAssets\BaseMaleHead.tri", SampleTri())));
        using var assets = AssetResolver.Open(null, new[] { archive });
        var bytes = assets.Read(@"Meshes\Actors\Character\CharacterAssets\BaseMaleHead.tri");
        Assert.NotNull(bytes);
        using var tri = TriMorphFile.Read(bytes);
        Assert.Equal(new[] { "JawOpen", "Pucker" }, tri.MorphNames);
        Assert.Equal(new float[] { 0, 0, 0, 1, 3, -2 }, tri.Positions(1f));
    }

    [Fact]
    public void RejectsBadInputAndBadIndices()
    {
        Assert.Throws<ArgumentException>(() => TriMorphFile.Read(ReadOnlySpan<byte>.Empty));
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => TriMorphFile.Read(new byte[16])).Status);
        using var tri = TriMorphFile.Read(SampleTri());
        Assert.Throws<ArgumentOutOfRangeException>(() => tri.Deltas(2));
        Assert.Throws<ArgumentOutOfRangeException>(() => tri.Deltas(-1));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => tri.Positions(-1f)).Status);
    }

    [Fact]
    public void UseAfterDisposeThrows()
    {
        var tri = TriMorphFile.Read(SampleTri());
        tri.Dispose();
        Assert.Throws<ObjectDisposedException>(() => tri.Positions());
        tri.Dispose();
    }

    [Fact]
    public void PhonemeNamesCoverTheFortyThreeSlots()
    {
        Assert.Equal(43, LipPhonemes.Count);
        Assert.Equal("IY", LipPhonemes.Name(0));
        Assert.Equal("SIL", LipPhonemes.Name(40));
        var names = new HashSet<string>();
        for (var slot = 0; slot < LipPhonemes.Count; slot++) names.Add(LipPhonemes.Name(slot));
        Assert.Equal(43, names.Count);
        Assert.Throws<ArgumentOutOfRangeException>(() => LipPhonemes.Name(43));
        Assert.Throws<ArgumentOutOfRangeException>(() => LipPhonemes.Name(-1));
    }

    [Fact]
    public void SlotMorphsMatchTheNativeTable()
    {
        var bilabial = LipPhonemes.MorphsFor(24);
        Assert.Single(bilabial);
        Assert.Equal("StickyLips", bilabial[0].Morph);
        Assert.Equal(1f, bilabial[0].Weight, 5);
        Assert.Empty(LipPhonemes.MorphsFor(40));
        var driven = 0;
        for (var slot = 0; slot < LipPhonemes.Count; slot++)
        {
            var morphs = LipPhonemes.MorphsFor(slot);
            if (morphs.Length != 0) driven++;
            foreach (var morph in morphs) Assert.InRange(morph.Weight, 0f, 1f);
        }
        Assert.Equal(40, driven);
        Assert.Throws<ArgumentOutOfRangeException>(() => LipPhonemes.MorphsFor(43));
        Assert.Throws<ArgumentOutOfRangeException>(() => LipPhonemes.MorphsFor(-1));
    }
}
