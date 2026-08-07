using System.IO;

namespace FO4RecordEditor.Services.Archives;

/// <summary>
/// Build a BA2 from loose folders, in process. This is what removes the Creation Kit's
/// Archive2.exe from the packing path: that binary is not redistributable, so it could never be
/// bundled, and a user without the CK installed simply could not pack.
///
/// General (GNRL) only. A DirectX (DX10) archive stores a per-file texture header and one chunk per
/// mip range, which means parsing every DDS to fill in height/width/mip count/DXGI format -- real
/// work that belongs with the in-process DDS decoding in #76, not here. Textures pack fine into a
/// General archive; the DX10 form exists so the engine can stream individual mips.
/// </summary>
public static class Ba2Packer
{
    public sealed record Result(int FileCount, long TotalBytes, long ArchiveBytes, int CompressedCount);

    /// <summary>
    /// Pack every file under <paramref name="sourceDirs"/> into <paramref name="outputPath"/>. Each
    /// entry's in-archive name is its path relative to its own source folder, so a source folder
    /// should be the Data-equivalent root ("...\MyMod"), the same contract Archive2 has.
    /// </summary>
    public static Result Pack(IEnumerable<string> sourceDirs, string outputPath, uint version = 1, bool compress = true)
    {
        var entries = new List<Ba2Entry>();
        long totalBytes = 0;
        int compressedCount = 0;
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var dirRaw in sourceDirs)
        {
            var dir = Path.GetFullPath(dirRaw);
            if (!Directory.Exists(dir)) throw new DirectoryNotFoundException($"Source folder not found: {dir}");

            foreach (var file in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
            {
                var rel = Path.GetRelativePath(dir, file).Replace('/', '\\');
                if (!seen.Add(rel))
                    throw new InvalidDataException($"'{rel}' appears in more than one source folder; an archive cannot hold the same path twice.");

                var bytes = File.ReadAllBytes(file);
                totalBytes += bytes.Length;

                var chunk = compress ? Ba2Codec.Compress(bytes) : Ba2Chunk.Stored(bytes);
                if (chunk.Compressed) compressedCount++;

                var (name, ext, parent) = Ba2Hash.Compute(rel);
                entries.Add(new Ba2Entry
                {
                    Path = rel,
                    Chunks = new List<Ba2Chunk> { chunk },
                    NameHash = name,
                    ExtensionHash = ext,
                    DirectoryHash = parent,
                });
            }
        }

        if (entries.Count == 0) throw new InvalidDataException("Nothing to pack: no files found under the source folder(s).");

        var archive = new Ba2Archive { Version = version, Format = Ba2Format.General, Entries = entries };

        var full = Path.GetFullPath(outputPath);
        Directory.CreateDirectory(Path.GetDirectoryName(full)!);
        Ba2Codec.Write(archive, full);

        return new Result(entries.Count, totalBytes, new FileInfo(full).Length, compressedCount);
    }
}
