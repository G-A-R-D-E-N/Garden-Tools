using System.Buffers.Binary;
using System.Text;
using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Core.Tests;

public class DuplicateFormIdScannerAuditTests
{
    [Fact]
    public void Cache_DoesNotAliasCaseDistinctPaths_OnLinux()
    {
        if (!OperatingSystem.IsLinux()) return;

        var root = Path.Combine(Path.GetTempPath(), $"CaseCache_{Guid.NewGuid():N}");
        var upperDirectory = Path.Combine(root, "Mods");
        var lowerDirectory = Path.Combine(root, "mods");
        Directory.CreateDirectory(upperDirectory);
        Directory.CreateDirectory(lowerDirectory);
        var upper = Path.Combine(upperDirectory, "Same.esp");
        var lower = Path.Combine(lowerDirectory, "Same.esp");

        var duplicate = new byte[48];
        Encoding.ASCII.GetBytes("KYWD").CopyTo(duplicate, 0);
        Encoding.ASCII.GetBytes("KYWD").CopyTo(duplicate, 24);
        BinaryPrimitives.WriteUInt32LittleEndian(duplicate.AsSpan(12, 4), 0x00000800u);
        BinaryPrimitives.WriteUInt32LittleEndian(duplicate.AsSpan(36, 4), 0x00000800u);
        var clean = duplicate.ToArray();
        BinaryPrimitives.WriteUInt32LittleEndian(clean.AsSpan(36, 4), 0x00000801u);

        File.WriteAllBytes(upper, duplicate);
        File.WriteAllBytes(lower, clean);
        var timestamp = DateTime.UtcNow.AddMinutes(-1);
        File.SetLastWriteTimeUtc(upper, timestamp);
        File.SetLastWriteTimeUtc(lower, timestamp);

        try
        {
            DuplicateFormIdScanner.Scan(upper).Duplicates.Should().ContainSingle();
            DuplicateFormIdScanner.Scan(lower).Duplicates.Should().BeEmpty(
                "case-distinct files on a case-sensitive filesystem must not share a cache entry");
        }
        finally
        {
            DuplicateFormIdScanner.Invalidate(upper);
            DuplicateFormIdScanner.Invalidate(lower);
            try { Directory.Delete(root, recursive: true); } catch { }
        }
    }
}
