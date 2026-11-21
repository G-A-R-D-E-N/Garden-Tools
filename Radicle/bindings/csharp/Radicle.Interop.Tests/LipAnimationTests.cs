using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using Radicle.Interop;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class LipAnimationTests
{
    private const uint FlagsWithModifiers = 13;
    private const uint FlagsWithoutModifiers = 9;

    private static int ModifiersFor(uint flags) => (flags & 4) != 0 ? 17 : 0;

    private static float[] SampleGrid(int frames, int phonemes, int modifiers)
    {
        var slots = phonemes + modifiers;
        var grid = new float[frames * slots];
        grid[1 * slots + 2] = 0.5f;
        grid[2 * slots + 2] = 0.75f;
        grid[2 * slots + slots - 1] = -0.25f;
        grid[3 * slots + 0] = 1.0f;
        return grid;
    }

    private static byte[] EncodeLip(float[] grid, int frames, int preroll, uint flags, int phonemes)
    {
        var leading = 0;
        while (leading < grid.Length && grid[leading] == 0f) ++leading;

        var payload = new List<byte>();
        var zeros = 0;
        void Flush()
        {
            while (zeros != 0)
            {
                var run = Math.Min(zeros, 65535);
                payload.Add(0);
                payload.Add((byte)(run & 0xFF));
                payload.Add((byte)(run >> 8));
                zeros -= run;
            }
        }
        for (var cell = leading; cell < grid.Length; ++cell)
        {
            foreach (var value in BitConverter.GetBytes(grid[cell]))
            {
                if (value == 0)
                {
                    ++zeros;
                }
                else
                {
                    Flush();
                    payload.Add(value);
                }
            }
        }
        Flush();

        var header = new byte[24];
        BinaryPrimitives.WriteUInt32LittleEndian(header, 1);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(4), (uint)(grid.Length * 4 + 28));
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(8), flags);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(12), (ushort)frames);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(14), 3);
        BinaryPrimitives.WriteInt32LittleEndian(header.AsSpan(16), preroll);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(20), (ushort)phonemes);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(22), (ushort)((leading << 2) | 3));
        var lip = new byte[header.Length + payload.Count];
        header.CopyTo(lip, 0);
        payload.CopyTo(lip, header.Length);
        return lip;
    }

    private static byte[] WrapFuz(byte[] lip)
    {
        var fuz = new byte[12 + lip.Length + 16];
        "FUZE"u8.CopyTo(fuz);
        BinaryPrimitives.WriteUInt32LittleEndian(fuz.AsSpan(4), 1);
        BinaryPrimitives.WriteUInt32LittleEndian(fuz.AsSpan(8), (uint)lip.Length);
        lip.CopyTo(fuz, 12);
        return fuz;
    }

    [Fact]
    public void DecodesABareLipBlockWithModifiers()
    {
        var grid = SampleGrid(5, 43, 17);
        var lip = LipAnimation.Decode(EncodeLip(grid, 5, -2, FlagsWithModifiers, 43));
        Assert.Equal(5, lip.FrameCount);
        Assert.Equal(-2, lip.PrerollFrames);
        Assert.Equal(43, lip.PhonemeCount);
        Assert.Equal(17, lip.ModifierCount);
        Assert.Equal(60, lip.SlotCount);
        Assert.Equal(grid, lip.Weights);
        Assert.Equal(0.75f, lip.Weight(2, 2));
        Assert.Equal(-0.25f, lip.Weight(2, 59));
        Assert.Equal(5.0 / 30.0, lip.DurationSeconds, 9);
    }

    [Fact]
    public void DecodesALipBlockWithoutModifiersWhenTheGestureFlagIsClear()
    {
        var grid = SampleGrid(5, 16, 0);
        var lip = LipAnimation.Decode(EncodeLip(grid, 5, -2, FlagsWithoutModifiers, 16));
        Assert.Equal(16, lip.PhonemeCount);
        Assert.Equal(0, lip.ModifierCount);
        Assert.Equal(16, lip.SlotCount);
        Assert.Equal(grid, lip.Weights);
        Assert.Equal(-0.25f, lip.Weight(2, 15));
    }

    [Theory]
    [InlineData(FlagsWithModifiers, 43)]
    [InlineData(FlagsWithoutModifiers, 16)]
    public void DecodesTheLipBlockOfAFuzContainer(uint flags, int phonemes)
    {
        var grid = SampleGrid(5, phonemes, ModifiersFor(flags));
        var lip = LipAnimation.Decode(WrapFuz(EncodeLip(grid, 5, -2, flags, phonemes)));
        Assert.Equal(ModifiersFor(flags), lip.ModifierCount);
        Assert.Equal(grid, lip.Weights);
    }

    // Detects: a FUZE container whose declared lip size overruns the file must be refused, not decoded from the bytes that happen to follow.
    [Fact]
    public void DecodeRefusesAFuzWhoseDeclaredLipSizeOverrunsTheContainer()
    {
        var lip = EncodeLip(SampleGrid(5, 16, 0), 5, 0, FlagsWithoutModifiers, 16);
        var lying = WrapFuz(lip);
        BinaryPrimitives.WriteUInt32LittleEndian(lying.AsSpan(8), (uint)lip.Length + 17u);
        Assert.Equal(Status.ParseError,
                     Assert.Throws<RadicleException>(() => LipAnimation.Decode(lying)).Status);
    }

    [Fact]
    public void WeightRejectsAnOutOfRangeCell()
    {
        var lip = LipAnimation.Decode(EncodeLip(SampleGrid(5, 3, 17), 5, 0, FlagsWithModifiers, 3));
        Assert.Throws<ArgumentOutOfRangeException>(() => lip.Weight(5, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => lip.Weight(0, 20));
        Assert.Throws<ArgumentOutOfRangeException>(() => lip.Weight(-1, 0));
    }

    [Fact]
    public void RejectsAnEmptyBuffer()
    {
        Assert.Throws<ArgumentException>(() => LipAnimation.Decode(ReadOnlySpan<byte>.Empty));
    }

    [Fact]
    public void RejectsBytesThatAreNotALip()
    {
        var error = Assert.Throws<RadicleException>(() => LipAnimation.Decode(new byte[32]));
        Assert.Equal(Status.ParseError, error.Status);
        Assert.Contains("lip version is not 1", error.Message);
    }

    [Fact]
    public void RejectsUncompressedLipFilesByName()
    {
        var block = EncodeLip(SampleGrid(5, 16, 0), 5, 0, FlagsWithoutModifiers, 16);
        block[8] = 8;
        var error = Assert.Throws<RadicleException>(() => LipAnimation.Decode(block));
        Assert.Equal(Status.ParseError, error.Status);
        Assert.Contains("uncompressed lip files are not supported", error.Message);
    }

    [Fact]
    public void RejectsTom3LipFilesByName()
    {
        var block = EncodeLip(SampleGrid(5, 16, 0), 5, 0, FlagsWithoutModifiers, 16);
        "TOM3"u8.CopyTo(block);
        var error = Assert.Throws<RadicleException>(() => LipAnimation.Decode(block));
        Assert.Equal(Status.ParseError, error.Status);
        Assert.Contains("TOM3", error.Message);
    }

    [Fact]
    public void RejectsAPayloadThatMissesItsDeclaredGrid()
    {
        var block = EncodeLip(SampleGrid(5, 16, 0), 2, 0, FlagsWithoutModifiers, 16);
        var error = Assert.Throws<RadicleException>(() => LipAnimation.Decode(block));
        Assert.Equal(Status.ParseError, error.Status);
        Assert.Contains("longer than its declared frame grid", error.Message);
    }

    [Fact]
    public void FormatsVoicePathsWithDefaultsAndOverrides()
    {
        Assert.Equal("Sound\\Voice\\Fallout4.esm\\MaleEvenToned\\001649A2_1.fuz",
                     SoundPaths.FormatVoicePath("Fallout4.esm", "MaleEvenToned", 0x1649A2, 1));
        Assert.Equal("Sound\\Voice\\DLCCoast.esm\\DLC03MaleDiMA\\English\\0002DFDD_2.hkx",
                     SoundPaths.FormatVoicePath("DLCCoast.esm", "DLC03MaleDiMA", 0x2DFDD, 2, "English", "", "hkx"));
    }

    [Fact]
    public void FormatVoicePathRejectsBadArguments()
    {
        Assert.Throws<ArgumentNullException>(() => SoundPaths.FormatVoicePath(null!, "Voice", 1, 1));
        Assert.Equal(Status.InvalidArgument,
                     Assert.Throws<RadicleException>(() => SoundPaths.FormatVoicePath("Fallout4.esm", "A\\B", 1, 1)).Status);
        Assert.Equal(Status.InvalidArgument,
                     Assert.Throws<RadicleException>(() => SoundPaths.FormatVoicePath("Fallout4.esm", "Voice", 1, 0)).Status);
        Assert.Equal(Status.InvalidArgument,
                     Assert.Throws<RadicleException>(() => SoundPaths.FormatVoicePath("Fallout4.esm", "Voice", 1, 1, extension: "wav")).Status);
    }
}
