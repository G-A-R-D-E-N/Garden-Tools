using System;
using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;
using Xunit.Abstractions;

namespace FO4RecordEditor.Tests;

// Regression test for the Next-Gen (BA2 version >= 7) decompression bug fixed in
// Mutagen/Mutagen.Bethesda.Core/Archives/Ba2/Ba2Reader.cs (BA2FileEntry). Before the fix,
// `Compressed = _realSize != 0xBAADF00D` mis-evaluated to false for every real Next-Gen entry
// (the field always reads back as that exact sentinel for GNRL entries in this format), so
// GetBytes() returned the raw still-zlib-compressed bytes unmarked as compressed.
//
// Root-caused empirically against this exact archive, not guessed: reading `size` bytes from
// `offset` and inflating them (unbounded, matching AsStream()'s InflaterInputStream) reproduces
// the real file content every time, with decompressed length == `size` -- so `size != 0` (the
// same rule v1 already uses) is the correct compressed flag; the old sentinel check never worked.
//
// Skips loudly when the fixture archive isn't present, rather than passing.
public class Ba2NextGenDecompressionTests
{
    private const string Archive = @"E:\Modlists\Fallen World Alpha 2\Stock Folder\Data\Fallout4 - Meshes.ba2";
    private static bool Available => File.Exists(Archive);

    private readonly ITestOutputHelper _out;
    public Ba2NextGenDecompressionTests(ITestOutputHelper o) => _out = o;

    [Fact]
    public void ExtractFile_FromARealNextGenArchive_ProducesARealNif_NotRawZlibBytes()
    {
        if (!Available) { _out.WriteLine($"Skipped -- fixture archive not present: {Archive}"); return; }

        var outPath = Path.Combine(Path.GetTempPath(),
            "FO4RE_Ba2NextGen_" + Guid.NewGuid().ToString("N")[..8] + ".nif");
        try
        {
            var result = ArchiveService.ExtractFile(Archive, "Meshes/Furniture/ParkBench01.nif", outPath);
            _out.WriteLine(result);
            File.Exists(outPath).Should().BeTrue();

            var bytes = File.ReadAllBytes(outPath);
            // Before the fix this was the raw zlib stream: 0x78 0x9C (a zlib header), not a NIF.
            bytes.Length.Should().Be(69920, "the archive's own 'size' field for this entry");
            var header = System.Text.Encoding.ASCII.GetString(bytes, 0, 20);
            header.Should().StartWith("Gamebryo File Format",
                "a fixed reader must return the real, decompressed NIF -- not the raw zlib bytes " +
                "(which would start with 0x78 0x9C instead)");
        }
        finally { try { File.Delete(outPath); } catch { } }
    }
}
