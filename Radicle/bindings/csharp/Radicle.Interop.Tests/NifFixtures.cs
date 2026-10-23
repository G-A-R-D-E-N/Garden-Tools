using System;
using System.Collections.Generic;

namespace Radicle.Interop.Tests;

internal static class NifFixtures
{
    private const uint HasUv = 1U << 1;
    private const uint HasNormal = 1U << 3;
    private const uint HasTangent = 1U << 4;
    private const uint HasColour = 1U << 5;
    private const uint HasSkin = 1U << 6;
    private const uint Full = 1U << 10;

    private static readonly float[] Identity = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

    internal sealed class Writer
    {
        private readonly List<byte> _bytes = new();

        public void U8(byte value) => _bytes.Add(value);

        public void U16(ushort value)
        {
            U8((byte)value);
            U8((byte)(value >> 8));
        }

        public void U32(uint value)
        {
            U16((ushort)value);
            U16((ushort)(value >> 16));
        }

        public void U64(ulong value)
        {
            U32((uint)value);
            U32((uint)(value >> 32));
        }

        public void I32(int value) => U32(unchecked((uint)value));

        public void F32(float value) => U32(BitConverter.SingleToUInt32Bits(value));

        public void Text(string value)
        {
            foreach (var ch in value) U8((byte)ch);
        }

        public void Sized(string value)
        {
            U32((uint)value.Length);
            Text(value);
        }

        public void ShortString(string value)
        {
            U8((byte)value.Length);
            Text(value);
        }

        public void Append(byte[] bytes) => _bytes.AddRange(bytes);

        public void Transform(float[] translation, float scale) => Transform(translation, Identity, scale);

        public void Transform(float[] translation, float[] rotation, float scale)
        {
            foreach (var value in translation) F32(value);
            foreach (var value in rotation) F32(value);
            F32(scale);
        }

        public byte[] Bytes() => _bytes.ToArray();
    }

    private sealed record Block(string Type, byte[] Data);

    private static byte[] BuildNif(IReadOnlyList<string> strings, IReadOnlyList<Block> blocks, IReadOnlyList<int> roots)
    {
        var types = new List<string>();
        var indices = new List<ushort>();
        foreach (var block in blocks)
        {
            var found = types.IndexOf(block.Type);
            if (found < 0)
            {
                types.Add(block.Type);
                found = types.Count - 1;
            }
            indices.Add((ushort)found);
        }

        var w = new Writer();
        w.Text("Gamebryo File Format, Version 20.2.0.7\n");
        w.U32(0x14020007U);
        w.U8(1);
        w.U32(12);
        w.U32((uint)blocks.Count);
        w.U32(130);
        for (var i = 0; i < 4; ++i) w.ShortString("");
        w.U16((ushort)types.Count);
        foreach (var type in types) w.Sized(type);
        foreach (var index in indices) w.U16(index);
        foreach (var block in blocks) w.U32((uint)block.Data.Length);
        w.U32((uint)strings.Count);
        w.U32(0);
        foreach (var value in strings) w.Sized(value);
        w.U32(0);
        foreach (var block in blocks) w.Append(block.Data);
        w.U32((uint)roots.Count);
        foreach (var root in roots) w.I32(root);
        return w.Bytes();
    }

    private static byte[] NodeBody(int name, float[] translation, float[] rotation, float scale, int[] children)
    {
        var w = new Writer();
        w.I32(name);
        w.U32(0);
        w.I32(-1);
        w.U32(0);
        w.Transform(translation, rotation, scale);
        w.I32(-1);
        w.U32((uint)children.Length);
        foreach (var child in children) w.I32(child);
        return w.Bytes();
    }

    private static Block NodeBlock(string type, int name, float[] translation, float scale, int[] children) =>
        new(type, NodeBody(name, translation, Identity, scale, children));

    private static byte[] ShapeBody(uint flags, int stride, int skin, int shader, int alpha, byte[] vertices,
        ushort[] triangles, float[] translation)
    {
        var w = new Writer();
        w.I32(0);
        w.U32(0);
        w.I32(-1);
        w.U32(0);
        w.Transform(translation, 1.0f);
        w.I32(-1);
        for (var i = 0; i < 4; ++i) w.F32(0);
        w.I32(skin);
        w.I32(shader);
        w.I32(alpha);
        w.U64(((ulong)flags << 44) | (uint)(stride / 4));
        w.U32((uint)(triangles.Length / 3));
        w.U16((ushort)(vertices.Length / stride));
        w.U32((uint)(vertices.Length + triangles.Length * 2));
        w.Append(vertices);
        foreach (var index in triangles) w.U16(index);
        return w.Bytes();
    }

    private static byte[] Vertex(float x, float y, float z)
    {
        var w = new Writer();
        w.F32(x);
        w.F32(y);
        w.F32(z);
        w.F32(0.5f);
        w.U16(0x3800);
        w.U16(0x3400);
        foreach (var value in new byte[] { 255, 0, 128, 0x11 }) w.U8(value);
        foreach (var value in new byte[] { 0, 255, 128, 0x22 }) w.U8(value);
        foreach (var value in new byte[] { 255, 0, 51, 102 }) w.U8(value);
        foreach (var value in new ushort[] { 0x3C00, 0x3800, 0x3400, 0 }) w.U16(value);
        foreach (var value in new byte[] { 0, 0, 0, 0 }) w.U8(value);
        return w.Bytes();
    }

    private static byte[] LightingShader(int name, int textureSet)
    {
        var w = new Writer();
        w.U32(1);
        w.I32(name);
        w.U32(0);
        w.I32(-1);
        w.U32(0x80400201U);
        w.U32(0x21U);
        w.F32(0);
        w.F32(0);
        w.F32(1);
        w.F32(1);
        w.I32(textureSet);
        for (var i = 0; i < 3; ++i) w.F32(0);
        w.F32(1);
        w.I32(-1);
        w.U32(3);
        w.F32(1);
        w.F32(0);
        w.F32(1);
        for (var i = 0; i < 3; ++i) w.F32(1);
        w.F32(1);
        w.F32(0);
        w.F32(BitConverter.UInt32BitsToSingle(0x7F7FFFFFU));
        w.F32(0);
        w.F32(1);
        w.F32(5);
        for (var i = 0; i < 6; ++i) w.F32(-1);
        w.F32(1);
        w.U8(0);
        w.U8(0);
        return w.Bytes();
    }

    internal static byte[] FullNif()
    {
        var shader = LightingShader(2, 5);

        var textures = new Writer();
        textures.U32(2);
        textures.Sized("textures\\body_d.dds");
        textures.Sized("textures\\body_n.dds");

        var skin = new Writer();
        skin.I32(0);
        skin.I32(3);
        skin.U32(1);
        skin.I32(6);
        skin.U32(0);

        var boneData = new Writer();
        boneData.U32(1);
        foreach (var value in new float[] { 1, 2, 3 }) boneData.F32(value);
        boneData.F32(4);
        for (var i = 1; i <= 9; ++i) boneData.F32(i);
        foreach (var value in new float[] { 5, 6, 7 }) boneData.F32(value);
        boneData.F32(2);

        var vertices = new List<byte>();
        vertices.AddRange(Vertex(1, 2, 3));
        vertices.AddRange(Vertex(4, 5, 6));

        var flags = Full | HasUv | HasNormal | HasTangent | HasColour | HasSkin;
        var blocks = new List<Block>
        {
            NodeBlock("NiNode", 0, new float[] { 10, 0, 0 }, 2.0f, new[] { 1 }),
            new("BSTriShape", ShapeBody(flags, 44, 2, 4, -1, vertices.ToArray(), new ushort[] { 0, 1, 1 },
                new float[] { 7, 8, 9 })),
            new("BSSkin::Instance", skin.Bytes()),
            new("BSSkin::BoneData", boneData.Bytes()),
            new("BSLightingShaderProperty", shader),
            new("BSShaderTextureSet", textures.Bytes()),
            NodeBlock("NiNode", 1, new float[] { 0, 0, 0 }, 1.0f, Array.Empty<int>()),
        };
        return BuildNif(new[]
        {
            "shape", "RootBone", "materials\\body.bgsm", "textures\\body_d.dds", "textures\\body_n.dds",
        }, blocks, new[] { 0 });
    }

    internal static byte[] NodeChainNif()
    {
        var shape = ShapeBody(Full, 16, -1, -1, -1, new byte[16], new ushort[] { 0, 0, 0 }, new float[] { 7, 8, 9 });
        var blocks = new List<Block>
        {
            new("BSFadeNode", NodeBody(0, new float[] { 1, 2, 3 }, new float[] { 0, -1, 0, 1, 0, 0, 0, 0, 1 }, 2.0f,
                new[] { 1 })),
            new("NiNode", NodeBody(1, new float[] { 4, 5, 6 }, Identity, 3.0f, new[] { 2 })),
            new("BSTriShape", shape),
        };
        return BuildNif(new[] { "Root", "Chest", "shape" }, blocks, new[] { 0 });
    }

    internal static byte[] DeepChainNif(int depth)
    {
        var blocks = new List<Block>(depth);
        for (int i = 0; i < depth; i++)
        {
            var children = i + 1 < depth ? new[] { i + 1 } : Array.Empty<int>();
            blocks.Add(new("NiNode", NodeBody(-1, new float[] { 1, 0, 0 }, Identity, 1.0f, children)));
        }
        return BuildNif(Array.Empty<string>(), blocks, new[] { 0 });
    }

    internal static byte[] SelfParentNif()
    {
        var blocks = new List<Block> { new("NiNode", NodeBody(-1, new float[] { 0, 0, 0 }, Identity, 1.0f, new[] { 0 })) };
        return BuildNif(Array.Empty<string>(), blocks, new[] { 0 });
    }
    internal static byte[] BadShaderNif()
    {
        var blocks = new List<Block>
        {
            NodeBlock("NiNode", 0, new float[] { 0, 0, 0 }, 1.0f, new[] { 1 }),
            new("BSTriShape", ShapeBody(Full, 16, -1, 2, -1, new byte[16], new ushort[] { 0, 0, 0 },
                new float[] { 7, 8, 9 })),
            new("BSLightingShaderProperty", LightingShader(1, 99)),
        };
        return BuildNif(new[] { "Root", "materials\\body.bgsm" }, blocks, new[] { 0 });
    }
}
