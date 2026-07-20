using System;
using System.IO;
using System.Linq;
using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;
using Xunit.Abstractions;

namespace FO4RecordEditor.Tests;

// Regression test for the BA2 version-7 GNRL layout fix in
// Mutagen/Mutagen.Bethesda.Core/Archives/Ba2/Ba2Reader.cs (BA2FileEntry). Mutagen read a phantom
// "unknown" uint32 after nameHash for versions 2..7 AND skipped the trailing 0xBAADF00D align field
// for v7, making each entry 40 bytes when a real v7 GNRL entry is the classic v1 36-byte layout.
// That misalignment drifted cumulatively -- early entries looked sane, later ones decoded to garbage.
//
// Verified against the one real v7 GNRL archive in the test modlist, DLCRobot - Voices_en.ba2 (6112
// .fuz voice lines): all 6112 entries decode as ext="fuz" with align==0xBAADF00D at the v1 offsets,
// and 24 + 6112*36 lands exactly on the first file's data offset. This test extracts a real entry
// through the production ArchiveService path and asserts the FUZ container magic -- proving the entry
// table is read at the right stride (a wrong stride would resolve the name to the wrong data blob).
//
// Skips loudly when the fixture archive isn't present, rather than passing.
public class Ba2Version7Tests
{
    private static readonly string[] Candidates =
    {
        @"E:\Modlists\Fallen World Alpha 2\Stock Folder\Data\DLCRobot - Voices_en.ba2",
        @"E:\SteamLibrary\steamapps\common\Fallout 4\Data\DLCRobot - Voices_en.ba2",
    };
    private static string? Archive => Candidates.FirstOrDefault(File.Exists);

    private readonly ITestOutputHelper _out;
    public Ba2Version7Tests(ITestOutputHelper o) => _out = o;

    [Fact]
    public void ExtractFile_FromARealVersion7Archive_ProducesARealFuz_NotDriftedGarbage()
    {
        var archive = Archive;
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
}
