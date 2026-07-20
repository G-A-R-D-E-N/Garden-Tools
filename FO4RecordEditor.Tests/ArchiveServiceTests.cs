using System.IO;
using System.Text;
using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;

// Hand-builds a minimal, real, valid BTDX v1 GNRL archive (the exact byte layout
// Mutagen.Bethesda.Archives.Ba2.Ba2Reader parses) rather than mocking IArchiveReader, so these
// tests exercise the real binary format ArchiveService will see in the wild.
public class ArchiveServiceTests
{
    private static string BuildTestBa2(params (string path, byte[] content)[] entries)
    {
        var ms = new MemoryStream();
        using (var bw = new BinaryWriter(ms, Encoding.ASCII, leaveOpen: true))
        {
            const int headerSize = 24;   // magic(4)+version(4)+fourcc(4)+numFiles(4)+nameTableOffset(8)
            const int entrySize = 36;    // nameHash(4)+ext(4)+dirHash(4)+flags(4)+offset(8)+size(4)+realSize(4)+align(4)

            uint dataStart = headerSize + (uint)(entrySize * entries.Length);
            var offsets = new uint[entries.Length];
            uint cursor = dataStart;
            for (int i = 0; i < entries.Length; i++) { offsets[i] = cursor; cursor += (uint)entries[i].content.Length; }
            uint nameTableOffset = cursor;

            bw.Write(Encoding.ASCII.GetBytes("BTDX"));
            bw.Write((uint)1);                              // version
            bw.Write(Encoding.ASCII.GetBytes("GNRL"));       // entry type
            bw.Write((uint)entries.Length);
            bw.Write((ulong)nameTableOffset);

            // BA2FileEntry, version==1 layout, one per entry
            for (int i = 0; i < entries.Length; i++)
            {
                bw.Write((uint)0);                                 // nameHash (unused; Path comes from name table)
                bw.Write(Encoding.ASCII.GetBytes("txt\0")[..4]);    // extension
                bw.Write((uint)0);                                 // dirHash
                bw.Write((uint)0);                                 // flags
                bw.Write((ulong)offsets[i]);                        // offset
                bw.Write((uint)0);                                  // size==0 -> uncompressed (Compressed = size != 0 for v<7)
                bw.Write((uint)entries[i].content.Length);          // realSize
                bw.Write((uint)0);                                  // align (version<=1 field)
            }

            foreach (var (_, content) in entries) bw.Write(content);

            foreach (var (path, _) in entries)
            {
                var pathBytes = Encoding.UTF8.GetBytes(path);
                bw.Write((short)pathBytes.Length);
                bw.Write(pathBytes);
            }
        }

        var path2 = Path.Combine(Path.GetTempPath(), $"ArchiveTest_{Guid.NewGuid():N}.ba2");
        File.WriteAllBytes(path2, ms.ToArray());
        return path2;
    }

    [Fact]
    public void ListArchive_ReportsPathAndSize()
    {
        var content = Encoding.ASCII.GetBytes("hello archive");
        var archive = BuildTestBa2((@"Meshes\Test\thing.txt", content));
        try
        {
            var result = ArchiveService.ListArchive(archive, filter: null, limit: 100);
            result.Should().Contain(@"Meshes\Test\thing.txt").And.Contain($"{content.Length}");
        }
        finally { try { File.Delete(archive); } catch { } }
    }

    [Fact]
    public void ListArchive_FilterNarrowsResults()
    {
        var archive = BuildTestBa2((@"Meshes\Test\thing.txt", Encoding.ASCII.GetBytes("x")));
        try
        {
            ArchiveService.ListArchive(archive, filter: "DoesNotExist", limit: 100)
                .Should().Contain("No entries").And.Contain("DoesNotExist");
        }
        finally { try { File.Delete(archive); } catch { } }
    }

    [Fact]
    public void ExtractFile_WritesExactBytes()
    {
        var content = Encoding.ASCII.GetBytes("the exact payload");
        var archive = BuildTestBa2((@"Scripts\Source\Test.psc", content));
        var outPath = Path.Combine(Path.GetTempPath(), $"ArchiveExtractOut_{Guid.NewGuid():N}.psc");
        try
        {
            var result = ArchiveService.ExtractFile(archive, @"Scripts\Source\Test.psc", outPath);
            result.Should().Contain("Extracted");
            File.Exists(outPath).Should().BeTrue();
            File.ReadAllBytes(outPath).Should().Equal(content);
        }
        finally
        {
            try { File.Delete(archive); } catch { }
            try { File.Delete(outPath); } catch { }
        }
    }

    [Fact]
    public void ExtractFile_UnknownInnerPath_ReturnsError()
    {
        var archive = BuildTestBa2((@"Meshes\Test\thing.txt", Encoding.ASCII.GetBytes("x")));
        try
        {
            ArchiveService.ExtractFile(archive, @"Meshes\NotHere.txt", Path.GetTempFileName())
                .Should().Contain("is not in");
        }
        finally { try { File.Delete(archive); } catch { } }
    }

    [Fact]
    public void ExtractAll_PreservesFolderStructure()
    {
        var content = Encoding.ASCII.GetBytes("payload");
        var archive = BuildTestBa2((@"Meshes\Sub\Dir\thing.txt", content));
        var outDir = Path.Combine(Path.GetTempPath(), $"ArchiveExtractAll_{Guid.NewGuid():N}");
        try
        {
            var result = ArchiveService.ExtractAll(archive, outDir, filter: null, limit: 100);
            result.Should().Contain("Extracted 1 of 1");
            var expected = Path.Combine(outDir, "Meshes", "Sub", "Dir", "thing.txt");
            File.Exists(expected).Should().BeTrue();
            File.ReadAllBytes(expected).Should().Equal(content);
        }
        finally
        {
            try { File.Delete(archive); } catch { }
            try { Directory.Delete(outDir, true); } catch { }
        }
    }

    [Fact]
    public void ExtractAll_OverLimit_RefusesWithoutWritingAnything()
    {
        var archive = BuildTestBa2(
            (@"Meshes\A.txt", Encoding.ASCII.GetBytes("a")),
            (@"Meshes\B.txt", Encoding.ASCII.GetBytes("b")));
        var outDir = Path.Combine(Path.GetTempPath(), $"ArchiveExtractLimit_{Guid.NewGuid():N}");
        try
        {
            var result = ArchiveService.ExtractAll(archive, outDir, filter: null, limit: 1);
            result.Should().Contain("2 matching entries").And.Contain("over the 1 limit");
            Directory.Exists(outDir).Should().BeFalse("a refusal must not create the output directory");
        }
        finally { try { Directory.Delete(outDir, true); } catch { } }
    }

    [Fact]
    public void ArchiveNotFound_ReturnsFriendlyError()
    {
        ArchiveService.ListArchive(@"C:\does\not\exist.ba2", null, 100)
            .Should().Contain("not found");
    }
}
