using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;

namespace OpenCommonwealth.Services.Hkx;

public enum PhysicsLoadStatus
{
    Ok,

    SkeletonOnly,

    LegacyPhysics,

    UnsupportedShapes,

    MapperMissing,

    NoBodies,

    WrongContainer,

    ParseError,
}

public enum PhysicsLoadSeverity
{
    Info,
    Warning,
    Error,
}

public sealed record PhysicsLoadFinding(PhysicsLoadSeverity Severity, string Message, string? Hint = null);

public sealed class PhysicsLoadReport
{
    public const string RagdollClass = "hknpRagdollData";
    public const string SkeletonClass = "hkaSkeleton";
    public const string MapperClass = "hkaSkeletonMapper";
    public const string MaterialClass = "hknpBSMaterial";

    public PhysicsLoadStatus Status { get; init; }
    public IReadOnlyList<PhysicsLoadFinding> Findings { get; init; } = Array.Empty<PhysicsLoadFinding>();
    public IReadOnlyDictionary<string, int> ClassCounts { get; init; } = EmptyCounts;
    public int? FileVersion { get; init; }
    public int? PointerSize { get; init; }

    public string? FailingStage { get; init; }

    public int BodyCount { get; init; }

    public int UsableBodies { get; init; }
    public int SkippedBodies { get; init; }
    public int SkippedConstraints { get; init; }
    public int UnsupportedShapeBodies { get; init; }

    public IReadOnlyDictionary<string, int> UnsupportedShapeClasses { get; init; } = EmptyCounts;

    public int ShapeCount { get; init; }

    public int ConstraintCount { get; init; }

    public int Mappings { get; init; }

    private static readonly IReadOnlyDictionary<string, int> EmptyCounts =
        new Dictionary<string, int>(StringComparer.Ordinal);

    public int CountOf(string className) =>
        ClassCounts.TryGetValue(className, out int count) ? count : 0;

    public bool Has(string className) => CountOf(className) > 0;

    public bool MapperPresent => Has(MapperClass);
    public bool MaterialPresent => Has(MaterialClass);

    public string StatusLabel => Status switch
    {
        PhysicsLoadStatus.Ok => "OK",
        PhysicsLoadStatus.SkeletonOnly => "SKELETON ONLY",
        PhysicsLoadStatus.LegacyPhysics => "LEGACY PHYSICS",
        PhysicsLoadStatus.UnsupportedShapes => "UNSUPPORTED SHAPES",
        PhysicsLoadStatus.MapperMissing => "MAPPER MISSING",
        PhysicsLoadStatus.NoBodies => "NO BODIES",
        PhysicsLoadStatus.WrongContainer => "WRONG CONTAINER",
        PhysicsLoadStatus.ParseError => "PARSE ERROR",
        _ => "UNKNOWN",
    };

    public string StatusSlug => Status switch
    {
        PhysicsLoadStatus.Ok => "ok",
        PhysicsLoadStatus.SkeletonOnly => "skeleton-only",
        PhysicsLoadStatus.LegacyPhysics => "legacy-physics",
        PhysicsLoadStatus.UnsupportedShapes => "unsupported-shapes",
        PhysicsLoadStatus.MapperMissing => "mapper-missing",
        PhysicsLoadStatus.NoBodies => "no-bodies",
        PhysicsLoadStatus.WrongContainer => "wrong-container",
        PhysicsLoadStatus.ParseError => "parse-error",
        _ => "unknown",
    };

    public PhysicsLoadSeverity Severity =>
        Findings.Count == 0 ? PhysicsLoadSeverity.Error : Findings[0].Severity;

    public string Headline =>
        Findings.Count > 0 ? Findings[0].Message : $"Physics could not be loaded ({StatusLabel}).";

    public string? Hint => Findings.Select(finding => finding.Hint).FirstOrDefault(hint => !string.IsNullOrEmpty(hint));

    public string Summary => Hint is { Length: > 0 } hint ? Headline + " " + hint : Headline;

    public IReadOnlyDictionary<string, int> ShapeClasses => Filtered(ClassCounts, IsShapeName);

    public IReadOnlyDictionary<string, int> ConstraintClasses => Filtered(ClassCounts, IsConstraintName);

    public IReadOnlyDictionary<string, int> PhysicsClasses => PhysicsClassesOf(ClassCounts);

    public static IReadOnlyDictionary<string, int> PhysicsClassesOf(IReadOnlyDictionary<string, int> counts) =>
        Filtered(counts, name => name.StartsWith("hknp", StringComparison.Ordinal) ||
                                 name.StartsWith("hkp", StringComparison.Ordinal));

    private static bool IsShapeName(string name) => name.EndsWith("Shape", StringComparison.Ordinal);

    private static bool IsConstraintName(string name) => name.EndsWith("ConstraintData", StringComparison.Ordinal);

    private static IReadOnlyDictionary<string, int> Filtered(IReadOnlyDictionary<string, int> counts,
                                                             Func<string, bool> keep)
    {
        var kept = new SortedDictionary<string, int>(StringComparer.Ordinal);
        foreach (var pair in counts)
            if (keep(pair.Key)) kept[pair.Key] = pair.Value;
        return kept;
    }

    public static string DescribeClasses(IReadOnlyDictionary<string, int> counts) =>
        counts.Count == 0
            ? "none"
            : string.Join(", ", counts.OrderByDescending(pair => pair.Value)
                                      .ThenBy(pair => pair.Key, StringComparer.Ordinal)
                                      .Select(pair => $"{pair.Key} x{pair.Value}"));

    public string DescribeBodies() => $"{UsableBodies} usable, {SkippedBodies} skipped of {BodyCount}";

    public string DescribeMapper() => MapperPresent
        ? Mappings > 0 ? $"present ({Mappings} mappings)" : "present, no usable mappings"
        : "absent";

    public string Diagnostics(string path, long? fileSize = null, string? product = null)
    {
        var text = new StringBuilder();
        if (product is { Length: > 0 }) text.AppendLine(product);
        text.AppendLine("file: " + path);
        text.AppendLine("size: " + (fileSize is long bytes
            ? bytes.ToString(CultureInfo.InvariantCulture) + " bytes"
            : "unknown"));
        text.AppendLine("file version: " + Format(FileVersion));
        text.AppendLine("pointer size: " + Format(PointerSize));
        text.AppendLine("status: " + StatusLabel + " (" + StatusSlug + ")");
        if (FailingStage is { Length: > 0 }) text.AppendLine("failing stage: " + FailingStage);
        text.AppendLine("bodies: " + DescribeBodies());
        text.AppendLine("shapes: " + ShapeCount.ToString(CultureInfo.InvariantCulture) + " read");
        text.AppendLine("constraints: " + ConstraintCount.ToString(CultureInfo.InvariantCulture) + " read, " +
                        SkippedConstraints.ToString(CultureInfo.InvariantCulture) + " skipped");
        text.AppendLine("ragdoll class: " + RagdollClass + " x" + CountOf(RagdollClass).ToString(CultureInfo.InvariantCulture));
        text.AppendLine("mapper: " + DescribeMapper());
        text.AppendLine(MaterialClass + ": " + (MaterialPresent ? "present" : "absent"));
        text.AppendLine("findings:");
        if (Findings.Count == 0) text.AppendLine("  (none)");
        foreach (var finding in Findings)
        {
            text.AppendLine($"  [{finding.Severity.ToString().ToLowerInvariant()}] {finding.Message}");
            if (finding.Hint is { Length: > 0 } hint) text.AppendLine("    next: " + hint);
        }
        text.AppendLine("shape classes: " + DescribeClasses(ShapeClasses));
        text.AppendLine("constraint classes: " + DescribeClasses(ConstraintClasses));
        text.AppendLine("class counts:");
        foreach (var pair in ClassCounts.OrderBy(pair => pair.Key, StringComparer.Ordinal))
            text.AppendLine($"  {pair.Key} x{pair.Value}");
        return text.ToString().TrimEnd('\r', '\n');
    }

    private static string Format(int? value) =>
        value is int measured ? measured.ToString(CultureInfo.InvariantCulture) : "unknown";
}
