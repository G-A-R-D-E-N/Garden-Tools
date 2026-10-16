using System;
using System.Collections.Generic;
using System.Linq;

namespace OpenCommonwealth.Services.Hkx;

public sealed record PhysicsLoadFacts
{
    public string? ContainerProblem { get; init; }

    public string? ContainerNotice { get; init; }

    public string? ParseStage { get; init; }

    public string? ParseMessage { get; init; }

    public int? FileVersion { get; init; }
    public int? PointerSize { get; init; }

    public IReadOnlyDictionary<string, int> ClassCounts { get; init; } = new Dictionary<string, int>(StringComparer.Ordinal);

    public int BodyCount { get; init; }

    public int UsableBodies { get; init; }
    public int SkippedBodies { get; init; }
    public int SkippedConstraints { get; init; }
    public int UnsupportedShapeBodies { get; init; }
    public IReadOnlyDictionary<string, int> UnsupportedShapeClasses { get; init; } = new Dictionary<string, int>(StringComparer.Ordinal);
    public int Mappings { get; init; }

    public int ShapeCount { get; init; }

    public int ConstraintCount { get; init; }

    public int CountOf(string className) =>
        ClassCounts.TryGetValue(className, out int count) ? count : 0;

    public bool Has(string className) => CountOf(className) > 0;
}

public static class PhysicsLoadClassifier
{
    public const int Fallout4FileVersion = 11;

    public const int Fallout4PointerSize = 8;

    public static PhysicsLoadReport Inspect(byte[]? hkx)
    {
        HavokPhysicsExtractor.TryExtract(hkx, out var report);
        return report;
    }

    public static string? ContainerProblem(byte[] bytes, out int? fileVersion, out int? pointerSize)
    {
        fileVersion = null;
        pointerSize = null;

        if (bytes.Length == 0) return "the file is empty";
        if (bytes.Length < PackfileImage.HeaderSize)
            return $"the file is only {bytes.Length} bytes, too small to hold a packfile header";

        if (LooksLikeXml(bytes))
            return "the bytes are XML, which is an unpacked tagfile rather than a packed Fallout 4 file";

        if (IsBinaryTagfile(bytes))
            return "the bytes are a binary tagfile, which is not a packed Fallout 4 file";

        uint magic0 = PackfileImage.U32(bytes, 0);
        uint magic1 = PackfileImage.U32(bytes, 4);
        if (magic0 != PackfileImage.Magic0 || magic1 != PackfileImage.Magic1)
            return $"the packfile magic does not match (it starts with 0x{magic0:X8} 0x{magic1:X8})";

        fileVersion = PackfileImage.I32(bytes, 0x0C);
        if (fileVersion != Fallout4FileVersion)
            return $"it is a version {fileVersion} packfile, and Fallout 4 uses version {Fallout4FileVersion}";

        pointerSize = bytes[0x10];
        if (pointerSize != Fallout4PointerSize)
            return $"it uses {pointerSize}-byte pointers, and Fallout 4 uses {Fallout4PointerSize}-byte pointers";

        int endian = bytes[0x11];
        if (endian != 1) return "it is a big-endian packfile, which only the little-endian layout is read here";

        if (bytes[0x12] != 0 || bytes[0x13] != 1)
            return $"its layout rules are {bytes[0x10]} {bytes[0x11]} {bytes[0x12]} {bytes[0x13]}, " +
                   "which are not the Fallout 4 rules 8 1 0 1";

        return null;
    }

    public static bool IsShapeClass(string className) =>
        className.EndsWith("Shape", StringComparison.Ordinal);

    public static bool IsConstraintClass(string className) =>
        className.EndsWith("ConstraintData", StringComparison.Ordinal);

    public static bool IsLegacyPhysicsClass(string className)
    {
        if (className == "hkpPhysicsSystem" || className == "hkpPhysicsData") return true;
        if (className.StartsWith("hkpRigidBody", StringComparison.Ordinal)) return true;
        return className.StartsWith("hkp", StringComparison.Ordinal) && IsShapeClass(className);
    }

    public static PhysicsLoadReport Classify(PhysicsLoadFacts facts)
    {
        ArgumentNullException.ThrowIfNull(facts);

        var findings = new List<PhysicsLoadFinding>();

        if (facts.ContainerNotice is { Length: > 0 } notice)
            findings.Add(new PhysicsLoadFinding(
                PhysicsLoadSeverity.Info,
                $"This file's container is unusual for Fallout 4 but still reads: {notice}.",
                "Fallout 4 ships packfile version 11 with 8-byte pointers; a file that reads anyway is " +
                "still measured here."));

        if (facts.ContainerProblem is { Length: > 0 } problem)
            return Build(PhysicsLoadStatus.WrongContainer, facts, findings, new PhysicsLoadFinding(
                PhysicsLoadSeverity.Error,
                $"This file is not a Fallout 4 physics packfile: {problem}.",
                "BGS reads the packed format Fallout 4 ships (packfile version 11 with 8-byte pointers). " +
                "Re-export the file with your exporter's Fallout 4 settings."));

        if (facts.ParseStage is { Length: > 0 } stage)
            return Build(PhysicsLoadStatus.ParseError, facts, findings, new PhysicsLoadFinding(
                PhysicsLoadSeverity.Error,
                $"Reading this file failed at the {stage} stage: {Trim(facts.ParseMessage)}",
                "The file may be truncated or written in a layout BGS does not describe; re-export it " +
                "and try again."));

        var legacy = LegacyClasses(facts);
        bool hasRagdoll = facts.Has(PhysicsLoadReport.RagdollClass);

        if (legacy.Count > 0 && !hasRagdoll)
            return Build(PhysicsLoadStatus.LegacyPhysics, facts, findings, new PhysicsLoadFinding(
                PhysicsLoadSeverity.Error,
                "This file holds physics in the older hkp format (" + PhysicsLoadReport.DescribeClasses(legacy) + "), " +
                "not the Fallout 4 format, so the ragdoll in it is ignored. Fallout 4 loads a ragdoll from " +
                $"{PhysicsLoadReport.RagdollClass} with hknp bodies.",
                "Fallout 4 needs hknpRagdollData and hknp shapes; re-export with physics in the Fallout 4 " +
                "format rather than the legacy one."));

        if (!hasRagdoll)
        {
            var otherPhysics = PhysicsLoadReport.PhysicsClassesOf(facts.ClassCounts);
            bool hasSkeleton = facts.Has(PhysicsLoadReport.SkeletonClass);
            string hint = otherPhysics.Count > 0
                ? "The file does carry other physics classes (" + PhysicsLoadReport.DescribeClasses(otherPhysics) +
                  "), but a ragdoll still needs hknpRagdollData."
                : "Fallout 4 keeps a ragdoll in the skeleton file itself, so check the sibling skeleton " +
                  "the game loads with this animation.";
            return Build(PhysicsLoadStatus.SkeletonOnly, facts, findings, new PhysicsLoadFinding(
                hasSkeleton ? PhysicsLoadSeverity.Warning : PhysicsLoadSeverity.Info,
                hasSkeleton
                    ? "This file has a skeleton but no ragdoll. Fallout 4 ragdolls are stored as " +
                      $"{PhysicsLoadReport.RagdollClass}; re-export with physics included."
                    : "This file holds no skeleton and therefore no ragdoll; Fallout 4 ragdolls are stored " +
                      $"as {PhysicsLoadReport.RagdollClass} inside a skeleton file.",
                hint));
        }

        if (facts.UsableBodies == 0)
        {
            AddExtraFindings(findings, facts, PhysicsLoadStatus.NoBodies, legacy);
            return Build(PhysicsLoadStatus.NoBodies, facts, findings, new PhysicsLoadFinding(
                PhysicsLoadSeverity.Error,
                facts.BodyCount == 0
                    ? $"This file has a {PhysicsLoadReport.RagdollClass} ragdoll that declares no rigid bodies at all, " +
                      "so there is nothing to preview."
                    : $"This file has a {PhysicsLoadReport.RagdollClass} ragdoll, but {facts.SkippedBodies} of its " +
                      $"{facts.BodyCount} bodies could not be read, so no rigid body is left to preview.",
                "Each body needs a readable position and orientation in its hknpBodyCinfo; re-export the " +
                "ragdoll so its body frames are written."));
        }

        if (facts.Mappings == 0)
        {
            bool mapperPresent = facts.Has(PhysicsLoadReport.MapperClass);
            AddExtraFindings(findings, facts, PhysicsLoadStatus.MapperMissing, legacy);
            return Build(PhysicsLoadStatus.MapperMissing, facts, findings, new PhysicsLoadFinding(
                PhysicsLoadSeverity.Warning,
                mapperPresent
                    ? $"An {PhysicsLoadReport.MapperClass} is present but none of its mappings link this ragdoll " +
                      "to the animation skeleton, so the ragdoll cannot follow the animation."
                    : "This file has a readable ragdoll, but no " + PhysicsLoadReport.MapperClass +
                      " links the animation skeleton to it, so the ragdoll cannot follow the animation.",
                "Re-export the skeleton with the ragdoll mapper included (hkaSkeletonMapper)."));
        }

        var incomplete = IncompleteReasons(facts);
        if (incomplete.Count > 0)
        {
            AddExtraFindings(findings, facts, PhysicsLoadStatus.UnsupportedShapes, legacy);
            return Build(PhysicsLoadStatus.UnsupportedShapes, facts, findings, new PhysicsLoadFinding(
                PhysicsLoadSeverity.Warning,
                "The ragdoll is readable but not complete: " + string.Join("; ", incomplete) + ".",
                "BGS previews capsule shapes (hknpCapsuleShape) only; the other bodies are still listed " +
                "with their frames, but they are drawn with no collision hull."));
        }

        AddExtraFindings(findings, facts, PhysicsLoadStatus.Ok, legacy);

        var ok = new PhysicsLoadFinding(
            PhysicsLoadSeverity.Info,
            $"This file carries a readable ragdoll: {facts.UsableBodies} rigid bodies, " +
            $"{facts.ShapeCount} shapes and {facts.ConstraintCount} constraints.");

        return Build(PhysicsLoadStatus.Ok, facts, findings, ok);
    }

    private static void AddExtraFindings(List<PhysicsLoadFinding> findings, PhysicsLoadFacts facts,
                                         PhysicsLoadStatus status, Dictionary<string, int> legacy)
    {
        if (facts.UnsupportedShapeBodies > 0 && status != PhysicsLoadStatus.UnsupportedShapes)
            findings.Add(new PhysicsLoadFinding(
                PhysicsLoadSeverity.Info,
                $"{facts.UnsupportedShapeBodies} of {facts.BodyCount} bodies use shapes BGS cannot preview yet (" +
                PhysicsLoadReport.DescribeClasses(facts.UnsupportedShapeClasses) + ").",
                "Those bodies are still listed with their frames, but they are drawn with no collision hull."));

        if (facts.SkippedBodies > 0 && status != PhysicsLoadStatus.NoBodies)
            findings.Add(new PhysicsLoadFinding(
                PhysicsLoadSeverity.Info,
                Entry(facts.SkippedBodies, "body", "bodies") + " could not be read, so those bodies are missing " +
                "from the preview."));

        if (facts.SkippedConstraints > 0 && status != PhysicsLoadStatus.UnsupportedShapes)
            findings.Add(new PhysicsLoadFinding(
                PhysicsLoadSeverity.Info,
                Entry(facts.SkippedConstraints, "constraint", "constraints") + " could not be read, so those " +
                "joints are missing from the preview."));

        if (legacy.Count > 0 && (status == PhysicsLoadStatus.Ok || status == PhysicsLoadStatus.NoBodies))
            findings.Add(new PhysicsLoadFinding(
                PhysicsLoadSeverity.Info,
                "This file also carries older hkp objects (" + PhysicsLoadReport.DescribeClasses(legacy) +
                "). Fallout 4 loads the hknp ragdoll, so they are ignored."));
    }

    private static List<string> IncompleteReasons(PhysicsLoadFacts facts)
    {
        var reasons = new List<string>();
        if (facts.UnsupportedShapeBodies > 0)
            reasons.Add($"{facts.UnsupportedShapeBodies} of {facts.BodyCount} bodies use shapes BGS cannot " +
                        "preview yet (" + PhysicsLoadReport.DescribeClasses(facts.UnsupportedShapeClasses) + ")");
        if (facts.SkippedBodies > 0)
            reasons.Add(Entry(facts.SkippedBodies, "body", "bodies") + " could not be read");
        if (facts.SkippedConstraints > 0)
            reasons.Add(Entry(facts.SkippedConstraints, "constraint", "constraints") + " could not be read");
        return reasons;
    }

    private static string Entry(int count, string one, string many) =>
        count == 1 ? $"1 {one} entry" : $"{count} {many} entries";

    private static Dictionary<string, int> LegacyClasses(PhysicsLoadFacts facts)
    {
        var legacy = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var pair in facts.ClassCounts)
            if (IsLegacyPhysicsClass(pair.Key)) legacy[pair.Key] = pair.Value;
        return legacy;
    }

    private static PhysicsLoadReport Build(PhysicsLoadStatus status, PhysicsLoadFacts facts,
                                           List<PhysicsLoadFinding> findings, PhysicsLoadFinding primary)
    {
        findings.Insert(0, primary);
        return new PhysicsLoadReport
        {
            Status = status,
            Findings = findings,
            ClassCounts = facts.ClassCounts,
            FileVersion = facts.FileVersion,
            PointerSize = facts.PointerSize,
            FailingStage = status == PhysicsLoadStatus.ParseError ? facts.ParseStage : null,
            BodyCount = facts.BodyCount,
            UsableBodies = facts.UsableBodies,
            SkippedBodies = facts.SkippedBodies,
            SkippedConstraints = facts.SkippedConstraints,
            UnsupportedShapeBodies = facts.UnsupportedShapeBodies,
            UnsupportedShapeClasses = facts.UnsupportedShapeClasses,
            Mappings = facts.Mappings,
            ShapeCount = facts.ShapeCount,
            ConstraintCount = facts.ConstraintCount,
        };
    }

    private static bool LooksLikeXml(byte[] bytes)
    {
        int limit = Math.Min(bytes.Length, 64);
        for (int at = 0; at < limit; at++)
            if (bytes[at] != 0x20 && bytes[at] != 0x09 && bytes[at] != 0x0A && bytes[at] != 0x0D && bytes[at] != 0xEF &&
                bytes[at] != 0xBB && bytes[at] != 0xBF)
                return bytes[at] == (byte)'<';
        return false;
    }

    private static bool IsBinaryTagfile(byte[] bytes) =>
        bytes.Length >= 8 && bytes[4] == (byte)'T' && bytes[5] == (byte)'A' &&
        bytes[6] == (byte)'G' && bytes[7] == (byte)'0';

    private static string Trim(string? message)
    {
        if (string.IsNullOrWhiteSpace(message)) return "no detail was reported";
        string first = message.Split('\n', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault()?.Trim() ?? "";
        return first.Length == 0 ? "no detail was reported" : first;
    }
}
