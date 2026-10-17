using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Radicle.Interop;
using Radicle.Interop.Native;

namespace OpenCommonwealth.Services.Archive;

public sealed class Ba2 : IDisposable
{
    public sealed record Entry(int Index, string Name, uint Unpacked)
    {

        public string Folder => Name.Contains('/') ? Name[..Name.LastIndexOf('/')] : "";
        public string FileName => Name.Contains('/') ? Name[(Name.LastIndexOf('/') + 1)..] : Name;

        public override string ToString() => $"{Name} ({Unpacked} bytes)";
    }

    private readonly Ba2Archive _archive;

    public string Path { get; }
    public uint Version { get; }
    public IReadOnlyList<Entry> Entries { get; }

    private Ba2(string path, uint version, Ba2Archive archive, List<Entry> entries)
    {
        Path = path;
        Version = version;
        _archive = archive;
        Entries = entries;
    }

    [ThreadStatic]
    internal static int OpenCountForTest;

    public static Ba2 Open(string archivePath)
    {
        OpenCountForTest++;
        string name = System.IO.Path.GetFileName(archivePath);
        if (!File.Exists(archivePath)) throw new FileNotFoundException($"{name} was not found", archivePath);
        Ba2Archive? archive = null;
        try
        {
            archive = Ba2Archive.Open(archivePath);
            if (archive.Kind != Ba2Kind.General)
                throw new InvalidDataException(
                    $"{name} is a DX10 archive, not GNRL. Textures are stored differently and nothing " +
                    "here reads them.");
            int count = archive.Count;
            if (count > 0 && !archive.HasNameTable)
                throw new InvalidDataException($"{name} has no name table, so its files cannot be named");

            var entries = new List<Entry>(count);
            for (int i = 0; i < count; i++)
            {
                Ba2Entry native = archive.Entry(i);
                string entryName = native.Path.Replace('\\', '/');
                if (HasRootedComponent(entryName))
                    throw new InvalidDataException($"{name} contains a rooted archive path: {entryName}");
                if (entryName.Contains(':'))
                    throw new InvalidDataException($"{name} contains an archive path with a colon: {entryName}");
                if (native.UnpackedSize > uint.MaxValue)
                    throw new InvalidDataException($"{name} entry {entryName} is larger than 4 GiB");
                entries.Add(new Entry(i, entryName, (uint)native.UnpackedSize));
            }

            return new Ba2(archivePath, archive.Version, archive, entries);
        }
        catch (RadicleException e)
        {
            archive?.Dispose();
            throw new InvalidDataException(e.Message, e);
        }
        catch (Exception e) when (IsNativeLoadFailure(e))
        {
            archive?.Dispose();
            throw NativeLoadFailure(e);
        }
        catch
        {
            archive?.Dispose();
            throw;
        }
    }

    public byte[] Read(Entry entry)
    {
        try { return _archive.Extract(entry.Index); }
        catch (RadicleException e) { throw new InvalidDataException(e.Message, e); }
        catch (Exception e) when (IsNativeLoadFailure(e)) { throw NativeLoadFailure(e); }
    }

    internal static bool IsNativeLoadFailure(Exception e) =>
        e is DllNotFoundException or BadImageFormatException or EntryPointNotFoundException or TypeInitializationException
            or RadicleAbiMismatchException || e.InnerException is RadicleAbiMismatchException;

    private static InvalidDataException NativeLoadFailure(Exception e) =>
        new InvalidDataException("the Radicle native library could not be loaded: " + e.Message, e);

    public IEnumerable<Entry> Matching(string query, string extension = "")
    {
        var words = query.Split(' ', StringSplitOptions.RemoveEmptyEntries);

        foreach (var entry in Entries)
        {
            if (extension.Length > 0 &&
                !entry.Name.EndsWith(extension, StringComparison.OrdinalIgnoreCase)) continue;

            if (words.All(w => entry.Name.Contains(w, StringComparison.OrdinalIgnoreCase)))
                yield return entry;
        }
    }

    public void Dispose() => _archive.Dispose();

    public static string FlatFileName(string name) =>
        name.Replace('/', '_').Replace('\\', '_').Replace(':', '_');

    public static int ExtractMatching(string archivePath, string substring, string outputDir,
                                      string extension, Action<string> log, bool keepFolders = false)
    {
        using var archive = Open(archivePath);
        log($"{System.IO.Path.GetFileName(archivePath)}: version {archive.Version}, " +
            $"{archive.Entries.Count} files");

        Directory.CreateDirectory(outputDir);
        int written = 0;

        foreach (var entry in archive.Matching(substring, extension))
        {

            string target;
            if (keepFolders)
            {
                string flat = entry.Name.Replace('/', System.IO.Path.DirectorySeparatorChar);
                if (System.IO.Path.IsPathRooted(flat))
                    throw new InvalidDataException($"{entry.Name} is an absolute path");

                string root = System.IO.Path.GetFullPath(outputDir);
                string full = System.IO.Path.GetFullPath(System.IO.Path.Combine(root, flat));
                string relative = System.IO.Path.GetRelativePath(root, full);
                bool outside = System.IO.Path.IsPathRooted(relative) ||
                               relative.Equals("..", StringComparison.Ordinal) ||
                               relative.StartsWith(".." + System.IO.Path.DirectorySeparatorChar,
                                                   StringComparison.Ordinal) ||
                               (System.IO.Path.AltDirectorySeparatorChar != System.IO.Path.DirectorySeparatorChar &&
                                relative.StartsWith(".." + System.IO.Path.AltDirectorySeparatorChar,
                                                    StringComparison.Ordinal));
                if (outside)
                    throw new InvalidDataException($"{entry.Name} is not inside the extraction folder");
                RefuseLinkedComponents(root, full, entry.Name);
                target = full;

                string? folder = System.IO.Path.GetDirectoryName(target);
                if (folder != null) Directory.CreateDirectory(folder);
            }
            else
            {
                target = System.IO.Path.Combine(outputDir, FlatFileName(entry.Name));
            }
            File.WriteAllBytes(target, archive.Read(entry));
            written++;
        }

        return written;
    }

    private static bool HasRootedComponent(string name)
    {
        if (name.StartsWith("/", StringComparison.Ordinal)) return true;
        foreach (string part in name.Split('/', StringSplitOptions.RemoveEmptyEntries))
            if (part.Length >= 2 && char.IsAsciiLetter(part[0]) && part[1] == ':') return true;
        return false;
    }

    private static void RefuseLinkedComponents(string root, string target, string entryName)
    {
        string relative = System.IO.Path.GetRelativePath(root, target);
        string current = root;
        string[] parts = relative.Split(System.IO.Path.DirectorySeparatorChar,
                                        StringSplitOptions.RemoveEmptyEntries);
        for (int i = 0; i < parts.Length; i++)
        {
            current = System.IO.Path.Combine(current, parts[i]);
            FileSystemInfo info = i == parts.Length - 1
                ? new FileInfo(current)
                : new DirectoryInfo(current);
            try
            {
                if (info.LinkTarget != null ||
                    (info.Exists && (info.Attributes & FileAttributes.ReparsePoint) != 0))
                    throw new InvalidDataException(
                        $"{entryName} crosses a linked path inside the extraction folder");
            }
            catch (FileNotFoundException) { }
            catch (DirectoryNotFoundException) { }
        }
    }
}
