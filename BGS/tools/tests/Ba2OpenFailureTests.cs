using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using OpenCommonwealth.Services.Archive;
using Radicle.Interop;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class Ba2OpenFailureTests
{
    [Fact]
    public void MissingArchiveIsReportedAsFileNotFound()
    {
        using var root = new TempDirectory("bgs-ba2-missing");
        string archive = Path.Combine(root.Path, "gone.ba2");

        var error = Assert.Throws<FileNotFoundException>(() => Ba2.Open(archive));
        Assert.Equal("gone.ba2 was not found", error.Message);
    }

    [Fact]
    public void FileThatIsNotABa2IsInvalidData()
    {
        using var root = new TempDirectory("bgs-ba2-magic");
        string text = Path.Combine(root.Path, "text.ba2");
        File.WriteAllText(text, "NOTBA2__".PadRight(24, '_'));
        string empty = Path.Combine(root.Path, "empty.ba2");
        File.WriteAllBytes(empty, Array.Empty<byte>());

        foreach (string path in new[] { text, empty })
        {
            var error = Assert.Throws<InvalidDataException>(() => Ba2.Open(path));
            Assert.IsType<RadicleException>(error.InnerException);
        }
    }

    [Theory]
    [InlineData("/abs/x.hkx")]
    [InlineData("\\abs\\x.hkx")]
    [InlineData("C:/x.hkx")]
    public void RootedArchiveNamesAreRejected(string entryName)
    {
        using var root = new TempDirectory("bgs-ba2-rooted");
        string archive = Path.Combine(root.Path, "rooted.ba2");
        WriteBa2(archive, entryName, new byte[] { 1, 2, 3 }, 0, 3);

        var error = Assert.Throws<InvalidDataException>(() => Ba2.Open(archive));
        Assert.Contains("rooted archive path", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void GeneralArchiveWithEntriesButNoNameTableIsRefused()
    {
        using var root = new TempDirectory("bgs-ba2-nonames");
        string archive = Path.Combine(root.Path, "nofiles.ba2");
        WriteBa2(archive, "x.hkx", new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }, 0, 8, nameTable: false);

        var error = Assert.Throws<InvalidDataException>(() => Ba2.Open(archive));
        Assert.Equal("nofiles.ba2 has no name table, so its files cannot be named", error.Message);
    }

    [Fact]
    public void EmptyGeneralArchiveOpensWithNoEntries()
    {
        using var root = new TempDirectory("bgs-ba2-empty");
        string archive = Path.Combine(root.Path, "empty.ba2");
        using (var stream = File.Create(archive))
        using (var writer = new BinaryWriter(stream))
        {
            writer.Write(Encoding.ASCII.GetBytes("BTDX"));
            writer.Write((uint)1);
            writer.Write(Encoding.ASCII.GetBytes("GNRL"));
            writer.Write((uint)0);
            writer.Write(0ul);
        }

        using var ba2 = Ba2.Open(archive);
        Assert.Empty(ba2.Entries);
        Assert.Equal(1u, ba2.Version);
    }

    [Fact]
    public void ZlibPackedEntryRoundTripsThroughRead()
    {
        using var root = new TempDirectory("bgs-ba2-zlib");
        string archive = Path.Combine(root.Path, "packed.ba2");
        byte[] payload = Enumerable.Range(0, 64).Select(i => (byte)(i % 7)).ToArray();
        var packed = new MemoryStream();
        using (var zlib = new ZLibStream(packed, CompressionLevel.Fastest, leaveOpen: true))
            zlib.Write(payload);
        byte[] stored = packed.ToArray();
        WriteBa2(archive, "packed.hkx", stored, (uint)stored.Length, 64);

        using var ba2 = Ba2.Open(archive);
        var entry = Assert.Single(ba2.Entries);
        Assert.Equal(64u, entry.Unpacked);
        Assert.Equal(payload, ba2.Read(entry));
    }

    [Fact]
    public void CorruptPackedEntryIsWrappedAsInvalidData()
    {
        using var root = new TempDirectory("bgs-ba2-corrupt");
        string archive = Path.Combine(root.Path, "corrupt.ba2");
        WriteBa2(archive, "bad.hkx", Enumerable.Repeat((byte)0xFF, 12).ToArray(), 12, 8);

        using var ba2 = Ba2.Open(archive);
        var entry = Assert.Single(ba2.Entries);
        var error = Assert.Throws<InvalidDataException>(() => ba2.Read(entry));
        Assert.IsType<RadicleException>(error.InnerException);
    }

    [Fact]
    public void SkippableArchiveFailureCoversIoAndAccessErrorsOnly()
    {
        var stale = new TypeInitializationException("Ba2Archive", new RadicleAbiMismatchException("stale"));

        Assert.True(GameData.IsSkippableArchiveFailure(new IOException()));
        Assert.True(GameData.IsSkippableArchiveFailure(new UnauthorizedAccessException()));
        Assert.True(GameData.IsSkippableArchiveFailure(new InvalidDataException("x")));
        Assert.False(GameData.IsSkippableArchiveFailure(new ArgumentException()));
        Assert.False(GameData.IsSkippableArchiveFailure(new InvalidDataException("w", stale)));
    }

    private static void WriteBa2(string path, string entryName, byte[] stored, uint packedSize, uint unpackedSize,
                                 bool nameTable = true)
    {
        byte[] name = Encoding.UTF8.GetBytes(entryName);
        const int headerBytes = 24;
        const int recordBytes = 36;
        ulong recordsEnd = headerBytes + recordBytes;
        ulong dataOffset = recordsEnd + (nameTable ? 2u + (uint)name.Length : 0u);

        using var stream = File.Create(path);
        using var writer = new BinaryWriter(stream);
        writer.Write(Encoding.ASCII.GetBytes("BTDX"));
        writer.Write((uint)1);
        writer.Write(Encoding.ASCII.GetBytes("GNRL"));
        writer.Write((uint)1);
        writer.Write(nameTable ? recordsEnd : 0ul);
        writer.Write((uint)0);
        writer.Write((uint)0);
        writer.Write((uint)0);
        writer.Write((uint)0x00100100);
        writer.Write(dataOffset);
        writer.Write(packedSize);
        writer.Write(unpackedSize);
        writer.Write(0xBAADF00Du);
        if (nameTable)
        {
            writer.Write((ushort)name.Length);
            writer.Write(name);
        }
        writer.Write(stored);
    }

    private sealed class TempDirectory : IDisposable
    {
        public TempDirectory(string prefix) =>
            Path = Directory.CreateTempSubdirectory(prefix).FullName;

        public string Path { get; }

        public void Dispose()
        {
            try { Directory.Delete(Path, recursive: true); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        }
    }
}
