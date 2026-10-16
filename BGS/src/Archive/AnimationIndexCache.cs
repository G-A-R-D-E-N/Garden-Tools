using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OpenCommonwealth.Services.Archive;

public sealed record AnimationIndexCacheSource(
    string Kind,
    string Path,
    string Source,
    string Stamp,
    List<string> Paths,
    List<long> Sizes);

public sealed record AnimationIndexCacheData(
    int Version,
    string DataFolder,
    string? Profile,
    string? ModlistPath,
    string? ModlistStamp,
    List<AnimationIndexCacheSource> Sources);

public sealed class AnimationIndexCache
{
    public const int CurrentVersion = 1;

    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        WriteIndented = false,
    };

    public AnimationIndexCache(string path) => Path = path;

    public string Path { get; }

    public AnimationIndexCacheData? Load()
    {
        if (!File.Exists(Path)) return null;
        try
        {
            var data = JsonSerializer.Deserialize<AnimationIndexCacheData>(File.ReadAllText(Path), Options);
            if (data == null || data.Version != CurrentVersion) return null;
            if (string.IsNullOrEmpty(data.DataFolder) || data.Sources == null) return null;

            foreach (var source in data.Sources)
            {
                if (source == null || source.Paths == null || source.Sizes == null) return null;
                if (source.Kind == null || source.Path == null || source.Stamp == null) return null;
            }
            return data;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException
                                       or NotSupportedException or ArgumentException)
        {
            return null;
        }
    }

    public bool Save(AnimationIndexCacheData data, out string failure)
    {
        failure = "";
        string temp = Path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            string? folder = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(Path));
            if (!string.IsNullOrEmpty(folder)) Directory.CreateDirectory(folder);

            File.WriteAllText(temp, JsonSerializer.Serialize(data, Options));
            File.Move(temp, Path, overwrite: true);
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or NotSupportedException
                                       or ArgumentException)
        {
            failure = (e.Message ?? "").Split('\n')[0].Trim();
            return false;
        }
        finally
        {
            try
            {
                if (File.Exists(temp)) File.Delete(temp);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
            }
        }
    }

    public static bool MatchesKey(AnimationIndexCacheData data, AnimationIndexRequest request) =>
        Same(data.DataFolder, request.DataFolder) &&
        string.Equals(data.Profile ?? "", request.Profile ?? "", StringComparison.Ordinal) &&
        Same(data.ModlistPath ?? "", request.ModlistPath ?? "");

    private static bool Same(string left, string right) =>
        string.Equals(left.TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar),
                      right.TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar),
                      StringComparison.OrdinalIgnoreCase);
}
