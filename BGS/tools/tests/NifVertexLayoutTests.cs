using System;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using OpenCommonwealth.Services.Nif;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class NifVertexLayoutTests
{
    private const uint FullPrecisionSkinned = 0x45b;

    [Fact]
    public void FullPrecisionSkinnedVertexReadsWeightsAfterTheSixteenBytePosition()
    {
        var shape = Assert.Single(NifGeometry.Shapes(NifFile.Parse(Mesh(FullPrecisionSkinned, 40), "fullprec.nif")));

        Assert.Equal(new Vector3(1, 2, 3), shape.Vertices[0]);
        Assert.Equal(new[] { 0.5f, 0.25f, 0.125f, 0.125f }, shape.BoneWeights.ToArray());
        Assert.Equal(new[] { 1, 2, 3, 4 }, shape.BoneIndices.ToArray());
        var location = shape.SkinSource!.Value;
        Assert.Equal(28, location.Weights);
        Assert.Equal(36, location.Indices);
    }

    [Fact]
    public void FullPrecisionSkinnedVertexWithATailSlotKeepsTheSkinBeforeTheTail()
    {
        var shape = Assert.Single(NifGeometry.Shapes(NifFile.Parse(Mesh(FullPrecisionSkinned | 0x100, 44), "tail.nif")));

        Assert.Equal(new[] { 1, 2, 3, 4 }, shape.BoneIndices.ToArray());
        var location = shape.SkinSource!.Value;
        Assert.Equal(28, location.Weights);
        Assert.Equal(36, location.Indices);
    }

    [Theory]
    [InlineData(36)]
    [InlineData(44)]
    public void SkinnedVertexWhoseStrideDiffersFromItsAttributesIsRejected(int stride)
    {
        var nif = NifFile.Parse(Mesh(FullPrecisionSkinned, stride), "fullprec.nif");

        var error = Assert.Throws<InvalidDataException>(() => NifGeometry.Shapes(nif));
        Assert.Contains("stride", error.Message);
    }

    [Fact]
    public void UnskinnedVertexKeepsItsDeclaredStride()
    {
        Assert.Single(NifGeometry.Shapes(NifFile.Parse(Mesh(0x40b, 40), "static.nif")));
    }

    private static byte[] Mesh(uint flags, int stride)
    {
        var vertex = new byte[stride];
        Array.Fill(vertex, (byte)0xEE);
        BitConverter.TryWriteBytes(vertex.AsSpan(0), 1f);
        BitConverter.TryWriteBytes(vertex.AsSpan(4), 2f);
        BitConverter.TryWriteBytes(vertex.AsSpan(8), 3f);
        if (stride >= 40)
        {
            float[] weights = { 0.5f, 0.25f, 0.125f, 0.125f };
            for (int s = 0; s < 4; s++)
            {
                BitConverter.TryWriteBytes(vertex.AsSpan(28 + s * 2), BitConverter.HalfToUInt16Bits((Half)weights[s]));
                vertex[36 + s] = (byte)(s + 1);
            }
        }

        using var block = new MemoryStream();
        using (var w = new BinaryWriter(block, Encoding.ASCII, leaveOpen: true))
        {
            w.Write(0u);
            w.Write(0u);
            w.Write(-1);
            w.Write(0u);
            for (int i = 0; i < 3; i++) w.Write(0f);
            for (int i = 0; i < 9; i++) w.Write(i % 4 == 0 ? 1f : 0f);
            w.Write(1f);
            w.Write(-1);
            w.Write(new byte[16]);
            w.Write(-1);
            w.Write(-1);
            w.Write(-1);
            w.Write(((ulong)flags << 44) | (ulong)(stride / 4));
            w.Write(0u);
            w.Write((ushort)1);
            w.Write((uint)stride);
            w.Write(vertex);
        }
        return Nif("BSTriShape", block.ToArray());
    }

    private static byte[] Nif(string type, byte[] body)
    {
        using var stream = new MemoryStream();
        using var w = new BinaryWriter(stream, Encoding.ASCII, leaveOpen: true);
        w.Write(Encoding.ASCII.GetBytes("Gamebryo File Format, Version 20.2.0.7\n"));
        w.Write(0x14020007u);
        w.Write((byte)1);
        w.Write(12u);
        w.Write(1u);
        w.Write(130u);
        for (int i = 0; i < 4; i++) w.Write((byte)0);
        w.Write((ushort)1);
        w.Write((uint)type.Length);
        w.Write(Encoding.ASCII.GetBytes(type));
        w.Write((ushort)0);
        w.Write((uint)body.Length);
        w.Write(1u);
        w.Write(5u);
        w.Write(5u);
        w.Write(Encoding.ASCII.GetBytes("shape"));
        w.Write(0u);
        w.Write(body);
        w.Write(0u);
        return stream.ToArray();
    }
}
