using System;
using System.Collections.Generic;
using System.IO;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class SeqModelTests
{
    internal static byte[] SyntheticSeq(params uint[] values)
    {
        var bytes = new List<byte>();
        foreach (var value in values)
        {
            bytes.Add((byte)(value & 0xFF));
            bytes.Add((byte)((value >> 8) & 0xFF));
            bytes.Add((byte)((value >> 16) & 0xFF));
            bytes.Add((byte)((value >> 24) & 0xFF));
        }
        return bytes.ToArray();
    }

    [Fact]
    public void ParsesEntriesInFileOrder()
    {
        using var seq = SeqFile.FromBytes(SyntheticSeq(0x07000800, 0x07000802, 0x01001733));

        Assert.Equal(3, seq.Entries.Count);
        Assert.Equal(0x07000800u, seq.Entries[0]);
        Assert.Equal(0x07000802u, seq.Entries[1]);
        Assert.Equal(0x01001733u, seq.Entries[2]);
    }

    [Fact]
    public void KeepsRawFormIdsWithoutResolvingThem()
    {
        using var seq = SeqFile.FromBytes(SyntheticSeq(0xFE000123, 0xFF000456, 0xFFFFFFFF));

        Assert.Equal(0xFE000123u, seq.Entries[0]);
        Assert.Equal(0xFF000456u, seq.Entries[1]);
        Assert.Equal(0xFFFFFFFFu, seq.Entries[2]);
    }

    [Fact]
    public void RoundTripsThroughToBytes()
    {
        var bytes = SyntheticSeq(0x00000010, 0x0000000F, 0x00000001);
        using var seq = SeqFile.FromBytes(bytes);

        Assert.Equal(bytes, seq.ToBytes());
    }

    [Fact]
    public void EmptyInputIsAnEmptyList()
    {
        using var seq = SeqFile.FromBytes(Array.Empty<byte>());

        Assert.Empty(seq.Entries);
        Assert.Empty(seq.ToBytes());
    }

    [Fact]
    public void ReadsFromAFile()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("fixture.seq", SyntheticSeq(0x07000800));
        using var seq = SeqFile.FromFile(path);
        Assert.Single(seq.Entries);
        Assert.Equal(0x07000800u, seq.Entries[0]);
        Assert.Equal(SyntheticSeq(0x07000800), seq.ToBytes());
    }

    [Fact]
    public void TheSnapshotSurvivesDispose()
    {
        var seq = SeqFile.FromBytes(SyntheticSeq(0x07000800));
        var entries = seq.Entries;
        seq.Dispose();

        Assert.Equal(0x07000800u, entries[0]);
        Assert.Throws<ObjectDisposedException>(() => seq.ToBytes());
    }

    [Fact]
    public void PartialEntryThrowsAParseError()
    {
        var error = Assert.Throws<RadicleException>(() => SeqFile.FromBytes(new byte[] { 0x41, 0x41, 0x41, 0x41, 0x41, 0x41 }));
        Assert.Equal(Status.ParseError, error.Status);
        Assert.False(string.IsNullOrEmpty(error.Message));
    }

    [Fact]
    public void MissingFileThrowsAnIoError()
    {
        var error = Assert.Throws<RadicleException>(() =>
            SeqFile.FromFile(Path.Combine(Path.GetTempPath(), "radicle_no_such_directory", "no_such_file.seq")));
        Assert.Equal(Status.IoError, error.Status);
    }
}
