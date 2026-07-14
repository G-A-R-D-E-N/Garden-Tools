using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Archives;
using Newtonsoft.Json;

namespace FO4RecordEditor.Services;

/// <summary>
/// Resolves a NIF's game-relative texture path (e.g. "Textures\mod\thing_d.dds") to a real DDS and
/// converts it to a PNG data URL for the WebGL viewport — via Texconv (DirectXTex), which decodes
/// every BCn format FO4 uses (BC1/3/5/7) that the browser's DDSLoader cannot. The DDS is found either
/// as a loose file (searched from a user root and the NIF's Data\ ancestors) OR extracted from a
/// Fallout 4 BA2 archive (Mutagen's Ba2Reader) — most vanilla/mod textures ship packed. Converted
/// PNGs are cached by source path + write time so repeat views are instant.
/// </summary>
public static class TextureService
{
    private static string? ResolveTexconv() => ToolPaths.Texconv();

    /// <summary>Return a "data:image/png;base64,…" URL for a NIF texture slot, or "" if it can't be
    /// resolved/converted. nifPath anchors the loose-texture and archive search; textureRoot (optional)
    /// is a user-picked Data/Textures folder tried first.</summary>
    public static string GetTexturePngDataUrl(string nifPath, string relTexPath, string textureRoot = "")
    {
        try
        {
            nifPath = (nifPath ?? "").Trim().Trim('"');
            relTexPath = (relTexPath ?? "").Trim().Trim('"');
            textureRoot = (textureRoot ?? "").Trim().Trim('"');
            if (string.IsNullOrWhiteSpace(relTexPath)) return "";

            var dds = ResolveDds(nifPath, relTexPath, textureRoot);
            if (dds == null) return "";

            var png = ConvertToPng(dds);
            if (png == null || !File.Exists(png)) return "";

            var bytes = File.ReadAllBytes(png);
            return "data:image/png;base64," + Convert.ToBase64String(bytes);
        }
        catch (Exception ex) { DebugLog.Exception("Texture.GetPng", ex); return ""; }
    }

    // Find the DDS: a user-picked texture root first, then absolute path, then game-relative under any
    // ancestor of the NIF (Data\Textures\…), then alongside the NIF, and finally inside FO4 BA2 archives.
    private static string? ResolveDds(string nifPath, string rel, string textureRoot = "")
    {
        rel = rel.Replace('/', '\\').TrimStart('\\');
        if (Path.IsPathRooted(rel) && File.Exists(rel)) return rel;

        string? nifDir = null;
        try { nifDir = Path.GetDirectoryName(Path.GetFullPath(nifPath)); } catch { }

        var candidates = new List<string>();

        // 1) user-picked texture root (highest priority): handle root = Data\, root = Data\Textures\,
        //    or root = the exact folder holding the file.
        if (!string.IsNullOrWhiteSpace(textureRoot) && Directory.Exists(textureRoot))
        {
            candidates.Add(Path.Combine(textureRoot, rel));
            if (rel.StartsWith("textures\\", StringComparison.OrdinalIgnoreCase))
                candidates.Add(Path.Combine(textureRoot, rel.Substring("textures\\".Length)));
            candidates.Add(Path.Combine(textureRoot, "Textures", rel));
            candidates.Add(Path.Combine(textureRoot, Path.GetFileName(rel)));
        }

        var dir = nifDir;
        while (dir != null)
        {
            candidates.Add(Path.Combine(dir, rel));
            candidates.Add(Path.Combine(dir, "Textures", rel));  // when rel lacks a leading Textures\
            dir = Path.GetDirectoryName(dir);
        }
        if (nifDir != null) candidates.Add(Path.Combine(nifDir, Path.GetFileName(rel)));

        foreach (var c in candidates)
            if (File.Exists(c)) return c;

        // 2) not loose — try to pull it out of a BA2 (vanilla + mods ship packed textures).
        return ResolveFromArchives(rel, nifDir, textureRoot);
    }

    // ---- BA2 archive resolution ------------------------------------------------------------------
    //
    // Lazy, session-cached, tiered: scan cheap NIF-local / user-root archives first (a mod's own BA2
    // usually sits beside its meshes), and only fall back to the big MO2 mods / game Data scan on a
    // miss. Each archive's .dds file table is indexed once (innerPath -> archive path); lookups after
    // that are dictionary hits, and the matched entry is extracted to a temp .dds for Texconv.

    private static readonly object _lock = new();
    private static readonly Dictionary<string, string> _index = new(StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> _scannedArchives = new(StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> _scannedRoots = new(StringComparer.OrdinalIgnoreCase);
    private static string[]? _globalRoots;   // settings-derived (DataFolder, MO2 mods) — computed once

    private static string Norm(string p) => p.Replace('/', '\\').TrimStart('\\');

    private static string? ResolveFromArchives(string rel, string? nifDir, string textureRoot)
    {
        try
        {
            var keys = LookupKeys(rel);

            // Tier 1 — cheap, NIF-local / user-root archives.
            var cheap = new List<string>();
            AddDataRoots(cheap, nifDir);
            if (!string.IsNullOrWhiteSpace(textureRoot)) AddDataRoots(cheap, textureRoot);
            var hit = ScanAndLookup(cheap, keys);
            if (hit != null) return hit;

            // Tier 2 — the big scan: configured Data folder + MO2 mods tree.
            var hit2 = ScanAndLookup(GlobalRoots(), keys);
            if (hit2 != null) return hit2;
        }
        catch (Exception ex) { DebugLog.Exception("Texture.Archive", ex); }
        return null;
    }

    // A slot path is normally "Textures\...". Match that, and also a bare form (some NIFs omit the
    // leading Textures\), against the archives' Data-relative entries.
    private static List<string> LookupKeys(string rel)
    {
        var norm = Norm(rel);
        var keys = new List<string> { norm };
        if (!norm.StartsWith("textures\\", StringComparison.OrdinalIgnoreCase))
            keys.Add("textures\\" + norm);
        return keys;
    }

    private static string? ScanAndLookup(IEnumerable<string> roots, List<string> keys)
    {
        lock (_lock)
        {
            foreach (var root in roots) EnsureRootScanned(root);
            foreach (var k in keys)
                if (_index.TryGetValue(k, out var archive)) return ExtractFromArchive(archive, k);
        }
        return null;
    }

    // Add a folder and any "Data" ancestor of it as BA2 search roots (BA2s live in a Data folder, but
    // a mod's loose meshes may also sit beside its BA2).
    private static void AddDataRoots(List<string> roots, string? path)
    {
        var dir = path;
        for (int guard = 0; dir != null && guard < 24; guard++)
        {
            roots.Add(dir);
            if (string.Equals(Path.GetFileName(dir), "Data", StringComparison.OrdinalIgnoreCase)) break;
            dir = Path.GetDirectoryName(dir);
        }
    }

    private static string[] GlobalRoots()
    {
        if (_globalRoots != null) return _globalRoots;
        var roots = new List<string>();
        try
        {
            var file = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "FO4RecordEditor", "settings.json");
            if (File.Exists(file))
            {
                var s = JsonConvert.DeserializeObject<AppSettings>(File.ReadAllText(file));
                if (s != null)
                {
                    if (!string.IsNullOrWhiteSpace(s.DataFolder)) roots.Add(s.DataFolder);
                    if (!string.IsNullOrWhiteSpace(s.Mo2InstancePath))
                        roots.Add(Path.Combine(s.Mo2InstancePath, "mods"));
                }
            }
        }
        catch (Exception ex) { DebugLog.Exception("Texture.GlobalRoots", ex); }
        return _globalRoots = roots.ToArray();
    }

    // Index every .dds entry of every BA2 under a root (recursive). One-time per root/archive.
    private static void EnsureRootScanned(string root)
    {
        root = (root ?? "").Trim();
        if (string.IsNullOrWhiteSpace(root) || !_scannedRoots.Add(root) || !Directory.Exists(root)) return;

        IEnumerable<string> archives;
        try { archives = Directory.EnumerateFiles(root, "*.ba2", SearchOption.AllDirectories); }
        catch (Exception ex) { DebugLog.Exception("Texture.EnumBa2", ex); return; }

        int added = 0;
        var sw = Stopwatch.StartNew();
        foreach (var archive in archives)
        {
            if (!_scannedArchives.Add(archive)) continue;
            try
            {
                var reader = Archive.CreateReader(GameRelease.Fallout4, new Noggog.FilePath(archive));
                foreach (var f in reader.Files)
                {
                    var p = f.Path?.ToString();
                    if (string.IsNullOrEmpty(p) || !p.EndsWith(".dds", StringComparison.OrdinalIgnoreCase)) continue;
                    var key = Norm(p);
                    if (!_index.ContainsKey(key)) { _index[key] = archive; added++; }  // first (most-local) wins
                }
            }
            catch (Exception ex) { DebugLog.Exception("Texture.ReadBa2:" + Path.GetFileName(archive), ex); }
        }
        if (added > 0)
            DebugLog.Info("Texture.Index", $"Indexed {added} DDS entries from BA2s under {root} in {sw.ElapsedMilliseconds}ms");
    }

    // Pull one entry out of a BA2 to a cached temp .dds (keyed by archive path+mtime+inner path).
    private static string? ExtractFromArchive(string archivePath, string innerKey)
    {
        try
        {
            long stamp = 0;
            try { stamp = File.GetLastWriteTimeUtc(archivePath).Ticks; } catch { }
            var key = Hash(archivePath.ToLowerInvariant() + "|" + stamp + "|" + innerKey);
            var cacheDir = Path.Combine(Path.GetTempPath(), "FO4RE_Tex");
            Directory.CreateDirectory(cacheDir);
            var outDds = Path.Combine(cacheDir, "ba2_" + key + ".dds");
            if (File.Exists(outDds)) return outDds;

            var reader = Archive.CreateReader(GameRelease.Fallout4, new Noggog.FilePath(archivePath));
            foreach (var f in reader.Files)
            {
                if (!string.Equals(Norm(f.Path?.ToString() ?? ""), innerKey, StringComparison.OrdinalIgnoreCase))
                    continue;
                File.WriteAllBytes(outDds, f.GetBytes());
                return outDds;
            }
        }
        catch (Exception ex) { DebugLog.Exception("Texture.Extract", ex); }
        return null;
    }

    private static string? ConvertToPng(string ddsPath)
    {
        var texconv = ResolveTexconv();
        if (texconv == null) return null;

        var stamp = File.GetLastWriteTimeUtc(ddsPath).Ticks;
        // Salt the cache key so PNGs from before Z-reconstruction was added aren't reused.
        var key = Hash(ddsPath.ToLowerInvariant() + "|" + stamp + "|v2");
        var cacheDir = Path.Combine(Path.GetTempPath(), "FO4RE_Tex");
        Directory.CreateDirectory(cacheDir);
        var pngPath = Path.Combine(cacheDir, key + ".png");
        if (File.Exists(pngPath)) return pngPath;

        var work = Path.Combine(cacheDir, "w_" + key);
        Directory.CreateDirectory(work);
        try
        {
            // BC5 stores only R,G (a tangent-space normal map); decoded straight to RGBA its blue is 0
            // and three.js lights it wrong. -reconstructz rebuilds Z = sqrt(1-x²-y²). Gate it to BC5
            // so diffuse/other maps are untouched, and fall back if an older texconv lacks the flag.
            var produced = RunTexconv(texconv, ddsPath, work, IsBc5(ddsPath));
            if (produced == null) return null;
            File.Copy(produced, pngPath, true);
            return pngPath;
        }
        finally { try { Directory.Delete(work, true); } catch { } }
    }

    // Run texconv into an isolated dir; returns the produced PNG path, or null. When reconstructZ is
    // requested but yields nothing (old texconv rejects the flag), retries once without it.
    private static string? RunTexconv(string texconv, string ddsPath, string work, bool reconstructZ)
    {
        var produced = Path.Combine(work, Path.GetFileNameWithoutExtension(ddsPath) + ".png");

        var args = new List<string> { "-nologo", "-y", "-ft", "png", "-f", "R8G8B8A8_UNORM" };
        if (reconstructZ) args.Add("-reconstructz");
        args.AddRange(new[] { "-o", work, ddsPath });

        var psi = new ProcessStartInfo
        {
            FileName = texconv,
            WorkingDirectory = work,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        foreach (var a in args) psi.ArgumentList.Add(a);

        using (var p = Process.Start(psi))
        {
            if (p == null) return null;
            p.StandardOutput.ReadToEnd();
            p.StandardError.ReadToEnd();
            if (!p.WaitForExit(30_000)) { try { p.Kill(true); } catch { } return null; }
        }

        if (File.Exists(produced)) return produced;
        if (reconstructZ) { try { File.Delete(produced); } catch { } return RunTexconv(texconv, ddsPath, work, false); }
        return null;
    }

    // Read a DDS header far enough to tell if it is BC5 (ATI2 FourCC, or a DX10 header with
    // DXGI_FORMAT_BC5_UNORM/SNORM = 83/84). BC5 is FO4's normal-map format.
    private static bool IsBc5(string ddsPath)
    {
        try
        {
            using var fs = File.OpenRead(ddsPath);
            Span<byte> h = stackalloc byte[148];
            int n = fs.Read(h);
            if (n < 88 || BitConverter.ToUInt32(h.Slice(0, 4)) != 0x20534444u) return false;  // "DDS "
            uint fourCC = BitConverter.ToUInt32(h.Slice(84, 4));                                // ddspf.dwFourCC
            if (fourCC == 0x32495441u) return true;                                             // "ATI2" = BC5
            if (fourCC == 0x30315844u && n >= 132)                                              // "DX10" ext header
            {
                uint dxgi = BitConverter.ToUInt32(h.Slice(128, 4));
                return dxgi == 83 || dxgi == 84;                                                // BC5_UNORM / BC5_SNORM
            }
        }
        catch (Exception ex) { DebugLog.Exception("Texture.IsBc5", ex); }
        return false;
    }

    private static string Hash(string s)
    {
        var bytes = SHA1.HashData(Encoding.UTF8.GetBytes(s));
        var sb = new StringBuilder();
        foreach (var b in bytes) sb.Append(b.ToString("x2"));
        return sb.ToString().Substring(0, 16);
    }
}
