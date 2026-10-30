using System.Collections.Concurrent;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using JsonConvert = Newtonsoft.Json.JsonConvert;

namespace FO4RecordEditor.Services;

public static class NifService
{
    // Invariant: every NIF verb reports this one message; this host holds no NIF implementation.
    private const string NoBackend =
        "NIF tools are in FO4IDE. This host has no NIF backend, so NIF work is not available here.";

    public static string Import(string objPath, string outNif, string material,
        string texDiffuse, string texNormal, bool collision, bool fromBlender)
    {
        objPath = Clean(objPath);
        outNif = Clean(outNif);
        if (string.IsNullOrWhiteSpace(objPath) || string.IsNullOrWhiteSpace(outNif))
            return "Provide 'obj_path' and 'out_nif'.";
        if (!File.Exists(objPath)) return $"OBJ not found: {objPath}";

        return NoBackend;
    }

    public static string Inspect(string nifPath)
    {
        nifPath = Clean(nifPath);
        if (string.IsNullOrWhiteSpace(nifPath)) return "Provide 'nif_path'.";
        if (!File.Exists(nifPath)) return $"NIF not found: {nifPath}";
        return NoBackend;
    }

    public static string Geo(string nifPath)
    {
        nifPath = Clean(nifPath);
        if (string.IsNullOrWhiteSpace(nifPath)) return "Provide 'nif_path'.";
        if (!File.Exists(nifPath)) return $"NIF not found: {nifPath}";
        return NoBackend;
    }

    public static int GeoBatchDone;
    public static int GeoBatchTotal;

    public static string GeoBatch(IEnumerable<string> relModelPaths)
    {
        var unique = relModelPaths
            .Where(p => !string.IsNullOrWhiteSpace(p))
            .Select(p => p.Trim())
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();

        var results = new ConcurrentDictionary<string, object>(StringComparer.OrdinalIgnoreCase);
        var degree = Math.Max(1, Math.Min(Environment.ProcessorCount, 8));

        GeoBatchDone = 0;
        GeoBatchTotal = unique.Count;

        Parallel.ForEach(unique, new ParallelOptions { MaxDegreeOfParallelism = degree }, rel =>
        {
            var resolved = TextureService.ResolveNif(rel);
            if (resolved == null)
            {
                results[rel] = new { error = "Not found (loose or in any indexed archive)." };
                Interlocked.Increment(ref GeoBatchDone);
                return;
            }

            results[rel] = new { error = NoBackend };
            Interlocked.Increment(ref GeoBatchDone);
        });

        return JsonConvert.SerializeObject(new { count = unique.Count, geometry = results });
    }

    public static string Verify(string nifPath)
    {
        nifPath = Clean(nifPath);
        if (string.IsNullOrWhiteSpace(nifPath)) return "Provide 'nif_path'.";
        if (!File.Exists(nifPath)) return $"NIF not found: {nifPath}";
        return NoBackend;
    }

    public static string Fix(string nifPath, string outNif)
    {
        nifPath = Clean(nifPath);
        outNif = Clean(outNif);
        if (string.IsNullOrWhiteSpace(nifPath) || string.IsNullOrWhiteSpace(outNif))
            return "Provide 'nif_path' and 'out_nif'.";
        if (!File.Exists(nifPath)) return $"NIF not found: {nifPath}";
        return NoBackend;
    }

    public static string Tree(string nifPath)
    {
        nifPath = Clean(nifPath);
        if (string.IsNullOrWhiteSpace(nifPath)) return "Provide 'nif_path'.";
        if (!File.Exists(nifPath)) return $"NIF not found: {nifPath}";
        return NoBackend;
    }

    public static string ApplyEdits(string nifPath, string editsJson, string outNif = "")
    {
        nifPath = Clean(nifPath);
        outNif = Clean(outNif);
        if (string.IsNullOrWhiteSpace(nifPath)) return "Provide 'nif_path'.";
        if (!File.Exists(nifPath)) return $"NIF not found: {nifPath}";
        if (string.IsNullOrWhiteSpace(editsJson)) return "No edits to apply.";

        return NoBackend;
    }

    private static string Clean(string s) => (s ?? "").Trim().Trim('"');
}
