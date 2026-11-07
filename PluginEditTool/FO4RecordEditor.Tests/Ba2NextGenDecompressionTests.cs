using System;
using System.IO;
using System.Linq;
using System.Text;
using FluentAssertions;
using FO4RecordEditor.Services;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Archives;
using Xunit;
using Xunit.Abstractions;

namespace FO4RecordEditor.Tests;



















public class Ba2NextGenDecompressionTests
{
    private readonly ITestOutputHelper _out;
    public Ba2NextGenDecompressionTests(ITestOutputHelper o) => _out = o;


    private static readonly string[] V7Candidates =
    {
        @"E:\Modlists\Fallen World Alpha 2\Stock Folder\Data\DLCRobot - Voices_en.ba2",
        @"E:\SteamLibrary\steamapps\common\Fallout 4\Data\DLCRobot - Voices_en.ba2",
    };
    private static string? V7Archive => V7Candidates.FirstOrDefault(File.Exists);

    private static readonly string[] V7TexCandidates =
    {
        @"E:\Modlists\Fallen World Alpha 2\Stock Folder\Data\Fallout4 - Textures1.ba2",
        @"E:\SteamLibrary\steamapps\common\Fallout 4\Data\Fallout4 - Textures1.ba2",
    };
    private static string? V7TexArchive => V7TexCandidates.FirstOrDefault(File.Exists);

    private bool TryFixture(string fileName, out string path)
    {
        var found = TestDataRoots.Archive(fileName);
        path = found ?? "";
        if (found != null) return true;
        var msg = $"Fixture archive not present: {fileName} (searched FO4RE_TEST_DATA and the known Data folders)";
        if (TestDataRoots.FixturesRequired) Assert.Fail(msg);
        _out.WriteLine("Skipped -- " + msg);
        return false;
    }

    private static byte[] ReadEntry(string archivePath, Func<IArchiveFile, bool> match)
    {
        var reader = Archive.CreateReader(GameRelease.Fallout4, archivePath);
        var file = reader.Files.FirstOrDefault(match)
                   ?? throw new FileNotFoundException($"No matching entry in {Path.GetFileName(archivePath)}");
        return file.GetBytes();
    }

    [Fact]
    public void V8CompressedEntry_ExtractsARealNif_NotRawZlibBytes()
    {
        if (!TryFixture("Fallout4 - Meshes.ba2", out var archive)) return;

        var outPath = Path.Combine(Path.GetTempPath(),
            "FO4RE_Ba2NextGen_" + Guid.NewGuid().ToString("N")[..8] + ".nif");
        try
        {
            var result = ArchiveService.ExtractFile(archive, "Meshes/Furniture/ParkBench01.nif", outPath);
            _out.WriteLine(result);
            File.Exists(outPath).Should().BeTrue();

            var bytes = File.ReadAllBytes(outPath);
            bytes.Length.Should().Be(69920, "the archive's own uncompressed 'size' field for this entry");
            Encoding.ASCII.GetString(bytes, 0, 20).Should().StartWith("Gamebryo File Format",
                "a fixed reader returns the decompressed NIF -- raw zlib would start 0x78 0x9C");
        }
        finally { try { File.Delete(outPath); } catch { } }
    }

    [Fact]
    public void V8StoredEntry_ReadsRaw_InsteadOfBeingInflated()
    {
        if (!TryFixture("Fallout4 - Interface.ba2", out var archive)) return;



        var act = () => ReadEntry(archive, f => f.Path.EndsWith("Fallout4_en.STRINGS", StringComparison.OrdinalIgnoreCase));
        var bytes = act.Should().NotThrow("a stored entry must be read raw, not inflated").Subject;
        bytes.Length.Should().BeGreaterThan(0);
        _out.WriteLine($"STRINGS\\Fallout4_en.STRINGS -> {bytes.Length} bytes");
    }

    [Fact]
    public void V7GnrlArchive_EnumeratesEveryEntry_WithoutDrift()
    {
        if (!TryFixture("DLCRobot - Voices_en.ba2", out var archive)) return;

        var files = Archive.CreateReader(GameRelease.Fallout4, archive).Files.ToList();
        _out.WriteLine($"{files.Count} entries");

        files.Should().NotBeEmpty();


        files.Should().OnlyContain(f => f.Path.EndsWith(".fuz", StringComparison.OrdinalIgnoreCase),
            "this archive is all voice .fuz files -- any other extension means the entry stride drifted");
        files.First().GetBytes().Length.Should().BeGreaterThan(0);
    }

    [Fact]
    public void V7Dx10TextureArchive_EnumeratesDdsEntries()
    {
        if (!TryFixture("Fallout4 - Textures1.ba2", out var archive)) return;

        var files = Archive.CreateReader(GameRelease.Fallout4, archive).Files.Take(25).ToList();
        files.Should().NotBeEmpty();
        files.Should().OnlyContain(f => f.Path.EndsWith(".dds", StringComparison.OrdinalIgnoreCase),
            "a mis-strided DX10 entry table produces garbage paths and 'Unsupported DDS header format'");
    }

    [Fact]
    public void ExtractFile_FromARealVersion7Archive_ProducesARealFuz_NotDriftedGarbage()
    {
        var archive = V7Archive;
        if (archive == null) { _out.WriteLine("Skipped -- v7 fixture archive not present in any known location."); return; }

        var outPath = Path.Combine(Path.GetTempPath(),
            "FO4RE_Ba2V7_" + Guid.NewGuid().ToString("N")[..8] + ".fuz");
        try
        {
            var result = ArchiveService.ExtractFile(
                archive, @"Sound\Voice\DLCRobot.esm\DLC01MechanistEyebot\00007072_1.fuz", outPath);
            _out.WriteLine(result);
            File.Exists(outPath).Should().BeTrue();

            var bytes = File.ReadAllBytes(outPath);
            var magic = System.Text.Encoding.ASCII.GetString(bytes, 0, 4);
            magic.Should().Be("FUZE",
                "a correctly-strided v7 entry table resolves the name to real FUZ data; a drifted " +
                "table would hand back an unrelated blob");
        }
        finally { try { File.Delete(outPath); } catch { } }
    }

    [Fact]
    public void ExtractFile_FromARealVersion7Dx10TextureArchive_ProducesAValidDds()
    {
        var archive = V7TexArchive;
        if (archive == null) { _out.WriteLine("Skipped -- v7 DX10 texture fixture not present in any known location."); return; }

        var outPath = Path.Combine(Path.GetTempPath(),
            "FO4RE_Ba2V7Tex_" + Guid.NewGuid().ToString("N")[..8] + ".dds");
        try
        {
            var result = ArchiveService.ExtractFile(archive, @"Textures\Props\Cigar\AshTray_d.DDS", outPath);
            _out.WriteLine(result);
            File.Exists(outPath).Should().BeTrue();

            var bytes = File.ReadAllBytes(outPath);
            var magic = System.Text.Encoding.ASCII.GetString(bytes, 0, 4);
            magic.Should().Be("DDS ",
                "the DX10 entry+chunk layout must be read at the right stride to reconstruct a valid " +
                "DDS header; a mis-strided entry threw 'Unsupported DDS header format' or overflowed");

            bytes.Length.Should().BeGreaterThan(128);
        }
        finally { try { File.Delete(outPath); } catch { } }
    }
}
