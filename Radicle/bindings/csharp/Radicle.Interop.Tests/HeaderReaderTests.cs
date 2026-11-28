using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Radicle.Interop;
using Xunit;

namespace Radicle.Interop.Tests;

public class HeaderReaderTests
{
    private static void Sized(List<byte> bytes, string text, int prefix)
    {
        if (prefix == 1) bytes.Add((byte)text.Length);
        else bytes.AddRange(BitConverter.GetBytes((uint)text.Length));
        bytes.AddRange(Encoding.Latin1.GetBytes(text));
    }

    private static byte[] Fo4Header(params string[] types)
    {
        var bytes = new List<byte>(Encoding.ASCII.GetBytes("Gamebryo File Format, Version 20.2.0.7\n"));
        bytes.AddRange(BitConverter.GetBytes(0x14020007U));
        bytes.Add(1);
        bytes.AddRange(BitConverter.GetBytes(12U));
        bytes.AddRange(BitConverter.GetBytes((uint)types.Length));
        bytes.AddRange(BitConverter.GetBytes(130U));
        Sized(bytes, "a", 1);
        Sized(bytes, "b", 1);
        Sized(bytes, "c", 1);
        Sized(bytes, "d", 1);
        bytes.AddRange(BitConverter.GetBytes((ushort)types.Length));
        foreach (var type in types) Sized(bytes, type, 4);
        return bytes.ToArray();
    }

    [Fact]
    public void NifHeaderListsBlockTypesWithoutParsingBlocks()
    {
        var header = NifHeader.Read(Fo4Header("NiNode", "BSSkin::Instance"));
        Assert.True(header.Readable);
        Assert.Equal(0x14020007U, header.FileVersion);
        Assert.Equal(12U, header.UserVersion);
        Assert.Equal(2U, header.BlockCount);
        Assert.Equal(new[] { "NiNode", "BSSkin::Instance" }, header.BlockTypeNames);
    }

    [Fact]
    public void NifHeaderKeepsNamesReadBeforeATruncation()
    {
        var full = Fo4Header("NiNode", "NiSkinInstance");
        var header = NifHeader.Read(full.AsSpan(0, full.Length - 3));
        Assert.False(header.Readable);
        Assert.Equal(new[] { "NiNode" }, header.BlockTypeNames);
    }

    [Fact]
    public void NifHeaderOfNonNifIsUnreadable()
    {
        Assert.False(NifHeader.Read(Encoding.ASCII.GetBytes("not a nif\n")).Readable);
        Assert.False(NifHeader.Read(ReadOnlySpan<byte>.Empty).Readable);
    }

    private static byte[] Lod(short x, short y, int stride, int min, int max)
    {
        var bytes = new byte[16];
        BinaryPrimitives.WriteInt16LittleEndian(bytes.AsSpan(0), x);
        BinaryPrimitives.WriteInt16LittleEndian(bytes.AsSpan(2), y);
        BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(4), stride);
        BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(8), min);
        BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(12), max);
        return bytes;
    }

    [Fact]
    public void LodSettingsReadsTheSixteenByteRecord()
    {
        Assert.Equal(new LodSettings(-64, -32, 4, 4, 32), LodSettings.Read(Lod(-64, -32, 4, 4, 32)));
    }

    [Fact]
    public void LodSettingsRejectsWrongSizeAndBadRange()
    {
        Assert.False(LodSettings.TryRead(new byte[15], out _));
        Assert.False(LodSettings.TryRead(Lod(0, 0, 4, 8, 4), out _));
        Assert.Throws<RadicleException>(() => LodSettings.Read(Lod(0, 0, 0, 4, 32)));
    }

    [Fact]
    public void NifSkinScanClassifiesBytes()
    {
        Assert.Equal(NifSkinScan.Skin, NifHeader.ScanSkin(Fo4Header("BSFadeNode", "BSTriShape", "BSSkin::Instance")));
        Assert.Equal(NifSkinScan.Skin, NifHeader.ScanSkin(Fo4Header("NiSkinInstance")));
        Assert.Equal(NifSkinScan.Skin, NifHeader.ScanSkin(Fo4Header("BSDismemberSkinInstance")));
        Assert.Equal(NifSkinScan.NoSkin, NifHeader.ScanSkin(Fo4Header("BSFadeNode", "BSSkin::BoneData", "BSSkin::Instance2")));
        Assert.Equal(NifSkinScan.NoSkin, NifHeader.ScanSkin(Fo4Header("NiNode", "\0BSSkin::Instance")));
        Assert.Equal(NifSkinScan.Skin, NifHeader.ScanSkin(Fo4Header("NiNode", "BSSkin::Instance\0junk")));
    }

    [Fact]
    public void NifSkinScanOfEmptyOrBrokenInputIsUnknown()
    {
        Assert.Equal(NifSkinScan.Unknown, NifHeader.ScanSkin(ReadOnlySpan<byte>.Empty));
        Assert.Equal(NifSkinScan.Unknown, NifHeader.ScanSkin(new byte[] { 0, 1, 2 }));
        Assert.Equal(NifSkinScan.Unknown, NifHeader.ScanSkin(Encoding.ASCII.GetBytes("not a nif\n")));
        var plain = Fo4Header("NiNode", "BSTriShape");
        Assert.Equal(NifSkinScan.NoSkin, NifHeader.ScanSkin(plain));
        for (var length = 0; length < plain.Length; length++)
            Assert.Equal(NifSkinScan.Unknown, NifHeader.ScanSkin(plain.AsSpan(0, length)));
        var skin = Fo4Header("BSSkin::Instance", "BSTriShape");
        Assert.Equal(NifSkinScan.Skin, NifHeader.ScanSkin(skin.AsSpan(0, skin.Length - 8)));
    }

    [Fact]
    public void NifSkinScanReadsAFileAndAMissingFileIsUnknown()
    {
        var root = Path.Combine(Path.GetTempPath(), "radicle-skinscan-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            Assert.Equal(NifSkinScan.Unknown, NifHeader.ScanSkin(Path.Combine(root, "missing.nif")));
            var skin = Path.Combine(root, "skin.nif");
            var plain = Path.Combine(root, "plain.nif");
            File.WriteAllBytes(skin, Fo4Header("NiNode", "BSSkin::Instance"));
            File.WriteAllBytes(plain, Fo4Header("NiNode", "BSTriShape"));
            Assert.Equal(NifSkinScan.Skin, NifHeader.ScanSkin(skin));
            Assert.Equal(NifSkinScan.NoSkin, NifHeader.ScanSkin(plain));
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void NifSkinScanReadsOnlyTheHeaderOfALargeStream()
    {
        var big = new byte[1 << 20];
        Fo4Header("NiNode", "BSSkin::Instance").CopyTo(big, 0);
        Assert.Equal(NifSkinScan.Skin, NifHeader.ScanSkin(new MemoryStream(big)));
        Assert.Equal(NifSkinScan.Unknown, NifHeader.ScanSkin(new MemoryStream()));
    }

    [Fact]
    public void NifSkinScanRetriesTheWholeStreamWhenTheHeaderOutgrowsTheWindow()
    {
        var types = new List<string>();
        for (var i = 0; i < 300; i++) types.Add(new string('A', 1000));
        types.Add("NiSkinInstance");
        var bytes = Fo4Header(types.ToArray());
        Assert.True(bytes.Length > 256 * 1024);
        Assert.Equal(NifSkinScan.Skin, NifHeader.ScanSkin(new MemoryStream(bytes)));
        types[^1] = "NiNode";
        Assert.Equal(NifSkinScan.NoSkin, NifHeader.ScanSkin(new MemoryStream(Fo4Header(types.ToArray()))));
    }
}
