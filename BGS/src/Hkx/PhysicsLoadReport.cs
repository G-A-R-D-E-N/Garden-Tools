using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;

namespace OpenCommonwealth.Services.Hkx;

/// <summary>
/// Why a physics load ended the way it did. The point of the taxonomy is that "no ragdoll" stops
/// being one silent outcome: a file can legitimately have no physics, or it can hold physics BGS
/// cannot use yet, and each of those needs a different sentence for the person who exported it.
/// </summary>
public enum PhysicsLoadStatus
{
    /// <summary>A readable hknp ragdoll: every body has a capsule shape and the mapper is usable.</summary>
    Ok,

    /// <summary>No hknpRagdollData at all. Legitimate for animations and props; the reason a skeleton
    /// with physics "missing" ends up here.</summary>
    SkeletonOnly,

    /// <summary>Older hkp rigid bodies/shapes/a physics system instead of hknp bodies. Constraint data
    /// alone (hkpRagdollConstraintData and friends) is normal inside a Fallout 4 ragdoll and never
    /// selects this status.</summary>
    LegacyPhysics,

    /// <summary>A ragdoll is present and bodies were read, but some bodies use a shape other than
    /// hknpCapsuleShape, or a body/constraint entry could not be read.</summary>
    UnsupportedShapes,

    /// <summary>A readable ragdoll with no usable hkaSkeletonMapper, so it cannot follow the animation.</summary>
    MapperMissing,

    /// <summary>hknpRagdollData is present but no usable rigid body could be read from it.</summary>
    NoBodies,

    /// <summary>Not a Fallout 4 packfile container: not a packfile at all (a tagfile or XML), a
    /// version other than 11, or a pointer width other than 8.</summary>
    WrongContainer,

    /// <summary>Reading failed after the container header: a section, the byte-backed XML or the model
    /// could not be produced. <see cref="PhysicsLoadReport.FailingStage"/> names the stage.</summary>
    ParseError,
}

public enum PhysicsLoadSeverity
{
    Info,
    Warning,
    Error,
}

public sealed record PhysicsLoadFinding(PhysicsLoadSeverity Severity, string Message, string? Hint = null);

/// <summary>
/// The measured outcome of one physics load attempt: the status, why it happened, and the class
/// census the decision was made from. The report explains a null <see cref="HavokRagdollModel"/> and
/// never changes what a load is willing to accept.
/// </summary>
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

    /// <summary>Set only for <see cref="PhysicsLoadStatus.ParseError"/>: the pipeline stage that failed.</summary>
    public string? FailingStage { get; init; }

    /// <summary>Body entries the ragdoll declares (readable or not).</summary>
    public int BodyCount { get; init; }

    public int UsableBodies { get; init; }
    public int SkippedBodies { get; init; }
    public int SkippedConstraints { get; init; }
    public int UnsupportedShapeBodies { get; init; }

    /// <summary>Shape classes the ragdoll's bodies use that BGS cannot preview yet, with body counts.</summary>
    public IReadOnlyDictionary<string, int> UnsupportedShapeClasses { get; init; } = EmptyCounts;

    /// <summary>Shapes the extractor read into the model (capsule shapes today).</summary>
    public int ShapeCount { get; init; }

    /// <summary>Constraints the extractor read into the model.</summary>
    public int ConstraintCount { get; init; }

    /// <summary>Animation-to-ragdoll bone mappings the mapper actually produced.</summary>
    public int Mappings { get; init; }

    private static readonly IReadOnlyDictionary<string, int> EmptyCounts =
        new Dictionary<string, int>(StringComparer.Ordinal);

    public int CountOf(string className) =>
        ClassCounts.TryGetValue(className, out int count) ? count : 0;

    public bool Has(string className) => CountOf(className) > 0;

    public bool MapperPresent => Has(MapperClass);
    public bool MaterialPresent => Has(MaterialClass);

    /// <summary>The status as a short label for logs and scan output.</summary>
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

    /// <summary>The status as a stable slug for machine-readable output.</summary>
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

    /// <summary>The first finding in plain words: the sentence that explains the outcome.</summary>
    public string Headline =>
        Findings.Count > 0 ? Findings[0].Message : $"Physics could not be loaded ({StatusLabel}).";

    /// <summary>The first available next-step sentence, if the report offers one.</summary>
    public string? Hint => Findings.Select(finding => finding.Hint).FirstOrDefault(hint => !string.IsNullOrEmpty(hint));

    /// <summary>Headline plus the next step, for one-line UI status text.</summary>
    public string Summary => Hint is { Length: > 0 } hint ? Headline + " " + hint : Headline;

    public IReadOnlyDictionary<string, int> ShapeClasses => Filtered(ClassCounts, IsShapeName);

    public IReadOnlyDictionary<string, int> ConstraintClasses => Filtered(ClassCounts, IsConstraintName);

    /// <summary>Every physics class the file carries (hknp/hkp), with its object count.</summary>
    public IReadOnlyDictionary<string, int> PhysicsClasses => PhysicsClassesOf(ClassCounts);

    /// <summary>Every hknp/hkp class in a census, with its object count.</summary>
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

    /// <summary>Class counts as "name xCount" text, most common first.</summary>
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

    /// <summary>
    /// The full plain-text dump the "Copy diagnostics" buttons put on the clipboard. The product line
    /// comes from the caller so core code does not depend on the app's build stamp.
    /// </summary>
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
