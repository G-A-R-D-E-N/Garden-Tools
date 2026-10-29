using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools;

public static partial class Program
{
    private static int Ragdoll(string[] argv)
    {
        string? target = null;
        bool force = false;
        bool allowSharedMaterial = false;
        bool allowSharedShape = false;
        string? outPath = null;
        string? dataFolder = null;
        var edits = new List<string>();

        for (int i = 1; i < argv.Length; i++)
        {
            string flag = argv[i];

            if (!flag.StartsWith("-", StringComparison.Ordinal))
            {
                if (target != null)
                    return RagdollUsage($"this command reads one file, and '{flag}' is a second");

                if (FullPath(flag) is not string file) return RagdollUsage($"'{flag}' is not a usable path");
                target = file;
                continue;
            }

            switch (flag)
            {
                case "--force":
                    force = true;
                    continue;

                case "--allow-shared-material":
                    allowSharedMaterial = true;
                    continue;

                case "--allow-shared-shape":
                    allowSharedShape = true;
                    continue;

                case "--set":
                    if (i + 1 >= argv.Length)
                        return RagdollUsage("--set needs an edit such as mass:0=5");
                    edits.Add(argv[++i]);
                    continue;

                case "--out":
                    if (i + 1 >= argv.Length) return RagdollUsage("--out needs the file to write");
                    if (FullPath(argv[++i]) is not string outFile) return RagdollUsage("--out is not a usable path");
                    outPath = outFile;
                    continue;

                case "--data":
                    if (i + 1 >= argv.Length) return RagdollUsage("--data needs the Data folder to sweep");
                    if (FullPath(argv[++i]) is not string folder) return RagdollUsage("--data is not a usable path");
                    dataFolder = folder;
                    continue;

                default:
                    return RagdollUsage($"'{flag}' is not a ragdoll switch");
            }
        }

        if (dataFolder != null) return RagdollSweep(dataFolder, outPath);

        if (target == null) return RagdollUsage("ragdoll needs a file to read, or --data with a Data folder");

        byte[] source;
        try { source = InputFilePolicy.ReadHkx(target); }
        catch (Exception error)
        {
            Console.Error.WriteLine($"cannot read {target}: {error.Message}");
            return 1;
        }

        return RagdollOne(target, source, edits, allowSharedMaterial, allowSharedShape, outPath, force);
    }

    private static int RagdollUsage(string problem)
    {
        Console.Error.WriteLine(problem);
        Console.Error.WriteLine();
        Console.Error.WriteLine(SymrmHelp.TryCommand("ragdoll", out string help)
            ? help
            : "symrm ragdoll <skeleton.hkx> [--set <edit>] [--set ragdoll=<i>] [--out <file>] [--force] " +
              "[--data <Data folder>]");
        return 1;
    }

    private static int RagdollOne(string file, byte[] source, IReadOnlyList<string> edits,
                                  bool allowSharedMaterial, bool allowSharedShape, string? outPath, bool force)
    {
        int selected = -1;
        RagdollDocument document;
        try { document = new RagdollDocument(source); }
        catch (Exception error)
        {
            Console.Error.WriteLine($"{Path.GetFileName(file)}: {error.Message}");
            return 1;
        }

        Console.WriteLine($"{Path.GetFileName(file)}  {source.Length} bytes, {document.Ragdolls.Count} ragdoll(s), " +
                          $"{document.Shapes.Count} capsule shape(s)");
        DescribeRagdolls(document);

        foreach (string edit in edits)
        {
            try
            {
                string? note = ApplyRagdollEdit(document, edit, allowSharedMaterial, allowSharedShape, ref selected);
                Console.WriteLine(note == null ? $"  set {edit}" : $"  set {edit} -> {note}");
            }
            catch (Exception error)
            {
                Console.Error.WriteLine($"  refused {edit}: {error.Message}");
                return 1;
            }
        }

        byte[] produced;
        try { produced = document.Dirty ? document.Serialize() : document.ForceRebuild(); }
        catch (Exception error)
        {
            Console.Error.WriteLine($"  refused: {error.Message}");
            return 1;
        }

        int firstDifference = FirstDifference(source, produced);
        Console.WriteLine(firstDifference < 0
            ? $"  round trip: byte-identical ({source.Length} bytes)"
            : $"  round trip: {source.Length} bytes in, {produced.Length} out, " +
              $"first difference at 0x{firstDifference:x}");

        if (document.Dirty)
        {
            try { VerifyRagdollEdits(produced, document, edits); }
            catch (Exception error)
            {
                Console.Error.WriteLine($"  refused: {error.Message}");
                return 1;
            }
            Console.WriteLine("  the edits came back as written");
        }
        else if (edits.Count == 0)
        {
            Console.WriteLine("  the forced no-op rebuild reproduced the file byte for byte");
        }

        if (outPath == null)
        {
            Console.WriteLine("  nothing was written: pass --out <file> to keep the rebuilt file");
            return 0;
        }

        string sourcePath = Path.GetFullPath(file);
        bool replacesSource = string.Equals(outPath, sourcePath, StringComparison.OrdinalIgnoreCase);

        if (!force)
        {
            if (replacesSource)
            {
                Console.Error.WriteLine($"  refused: {outPath} is the file being read; pass --force to overwrite it");
                return 1;
            }

            if (File.Exists(outPath))
            {
                Console.Error.WriteLine($"  refused: {outPath} already exists; pass --force to overwrite it");
                return 1;
            }
        }

        try
        {
            if (replacesSource) FileSafety.ReplaceChecked(outPath, produced, DocumentSourceStamp.Capture(source));
            else FileSafety.Replace(outPath, produced);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"  cannot write {outPath}: {error.Message}");
            return 1;
        }

        Console.WriteLine(replacesSource
            ? $"  written to {outPath}; the version that was read is kept as {Path.GetFileName(outPath)}.bak"
            : $"  written to {outPath}");
        return 0;
    }

    private static void DescribeRagdolls(RagdollDocument document)
    {
        foreach (var node in document.Ragdolls)
            Console.WriteLine($"  ragdoll #{node.ObjectId}: '{node.Skeleton?.Name ?? "(no skeleton)"}' " +
                              $"({node.Skeleton?.BoneNames.Count ?? 0} bones), {node.Bodies.Count} bodies, " +
                              $"{node.Motions.Count} motions, {node.Materials.Count} materials, " +
                              $"{node.Constraints.Count} constraints");

        foreach (var material in document.Ragdolls.SelectMany(node => node.Materials))
            Console.WriteLine($"    material {material.Index}: friction {material.DynamicFriction:0.#####} " +
                              $"dynamic / {material.StaticFriction:0.#####} static, restitution " +
                              $"{material.Restitution:0.#####}, shared by {material.ReferencedByBodies} body(ies)" +
                              (material.FileSaysShared ? ", flagged shared in the file" : ""));

        foreach (var constraint in document.Ragdolls.SelectMany(node => node.Constraints).Take(20))
            Console.WriteLine($"    constraint {constraint.Index}: {constraint.ClassName}, " +
                              $"limit {constraint.MinAngle:0.#####}..{constraint.MaxAngle:0.#####} rad, " +
                              $"cone {constraint.ConeMaxAngle:0.#####} (mode {constraint.ConeAngleMeasurementMode}), " +
                              $"planes (mode {constraint.PlaneAngleMeasurementMode}), " +
                              $"friction {constraint.Friction:0.#####}");

        foreach (var shape in document.Shapes.Take(20))
            Console.WriteLine($"    shape {shape.InstanceIndex}: {shape.ClassName} #{shape.ObjectId}, " +
                              $"radius {shape.Radius:0.#####}, a ({shape.A.X:0.###} {shape.A.Y:0.###} {shape.A.Z:0.###}), " +
                              $"b ({shape.B.X:0.###} {shape.B.Y:0.###} {shape.B.Z:0.###}), " +
                              $"polytope {shape.PolytopeCounts}");
    }

    private static string? ApplyRagdollEdit(RagdollDocument document, string edit, bool allowSharedMaterial,
                                            bool allowSharedShape, ref int selected)
    {
        if (edit.StartsWith("ragdoll=", StringComparison.Ordinal))
        {
            if (!int.TryParse(edit["ragdoll=".Length..], NumberStyles.Integer, CultureInfo.InvariantCulture,
                              out int wanted))
                throw new InvalidOperationException($"'{edit}' needs a ragdoll index such as ragdoll=0");
            if (wanted < 0 || wanted >= document.Ragdolls.Count)
                throw new InvalidOperationException(
                    $"this file holds {document.Ragdolls.Count} ragdoll(s), so there is no ragdoll {wanted}");

            selected = wanted;
            return $"ragdoll {wanted} selected";
        }

        int colon = edit.IndexOf(':');
        int equals = edit.IndexOf('=');
        if (colon <= 0 || equals <= colon || equals == edit.Length - 1)
            throw new InvalidOperationException($"'{edit}' is not <field>:<index>=<values>");

        string field = edit[..colon];
        int index = int.Parse(edit[(colon + 1)..equals], NumberStyles.Integer, CultureInfo.InvariantCulture);
        float[] values = edit[(equals + 1)..]
            .Split(',', StringSplitOptions.RemoveEmptyEntries)
            .Select(value => float.Parse(value, NumberStyles.Float, CultureInfo.InvariantCulture))
            .ToArray();

        switch (field)
        {
            case "mass":
                RequireValues(values, 1, edit);
                document.SetBodyMass(RequireRagdoll(document, ref selected), index, values[0]);
                return null;

            case "com":
                RequireValues(values, 3, edit);
                document.SetBodyCenterOfMass(RequireRagdoll(document, ref selected), index,
                                             new Vector3(values[0], values[1], values[2]));
                return null;

            case "capsule":
                RequireValues(values, 6, edit);
                document.SetCapsuleEndpoints(index, new Vector3(values[0], values[1], values[2]),
                                             new Vector3(values[3], values[4], values[5]), allowSharedShape);
                return null;

            case "radius":
                RequireValues(values, 1, edit);
                document.SetShapeRadius(index, values[0], allowSharedShape);
                return null;

            case "material":
            {
                if (values.Length is < 1 or > 2)
                    throw new InvalidOperationException($"'{edit}' needs <friction> or <dynamic>,<static>");
                int ragdoll = RequireRagdoll(document, ref selected);
                document.SetMaterialFriction(ragdoll, index, values[0],
                                             values.Length > 1 ? values[1] : values[0], allowSharedMaterial);
                var stored = document.Ragdolls[ragdoll].Materials[index];
                return $"stored dynamic {Rounded(stored.DynamicFriction)} static {Rounded(stored.StaticFriction)}";
            }

            case "restitution":
            {
                RequireValues(values, 1, edit);
                int ragdoll = RequireRagdoll(document, ref selected);
                document.SetMaterialRestitution(ragdoll, index, values[0], allowSharedMaterial);
                return $"stored restitution {Rounded(document.Ragdolls[ragdoll].Materials[index].Restitution)}";
            }

            case "limit":
            {
                RequireValues(values, 2, edit);
                int ragdoll = RequireRagdoll(document, ref selected);
                document.SetConstraintLimits(ragdoll, index, values[0], values[1]);
                var stored = document.Ragdolls[ragdoll].Constraints[index];
                return $"stored twist {Rounded(stored.MinAngle)}..{Rounded(stored.MaxAngle)}";
            }

            case "cone":
            {
                RequireValues(values, 1, edit);
                int ragdoll = RequireRagdoll(document, ref selected);
                document.SetConstraintConeLimit(ragdoll, index, values[0]);
                return $"stored cone {Rounded(document.Ragdolls[ragdoll].Constraints[index].ConeMaxAngle)}";
            }

            case "plane":
            {
                RequireValues(values, 2, edit);
                int ragdoll = RequireRagdoll(document, ref selected);
                document.SetConstraintPlaneLimits(ragdoll, index, values[0], values[1]);
                var stored = document.Ragdolls[ragdoll].Constraints[index];
                return $"stored planes {Rounded(stored.PlaneMinAngle)}..{Rounded(stored.PlaneMaxAngle)}";
            }

            case "friction":
            {
                RequireValues(values, 1, edit);
                int ragdoll = RequireRagdoll(document, ref selected);
                document.SetConstraintFriction(ragdoll, index, values[0]);
                return $"stored friction {Rounded(document.Ragdolls[ragdoll].Constraints[index].Friction)}";
            }

            default:
                throw new InvalidOperationException($"'{field}' is not a ragdoll field this command sets");
        }
    }

    private static int RequireRagdoll(RagdollDocument document, ref int selected)
    {
        if (selected >= 0) return selected;
        if (document.Ragdolls.Count == 1)
        {
            selected = 0;
            return 0;
        }

        throw new InvalidOperationException(
            $"this file holds {document.Ragdolls.Count} ragdoll(s); pass --set ragdoll=<i> before an edit that " +
            "names one");
    }

    private static string Rounded(float value) => value.ToString("0.#####", CultureInfo.InvariantCulture);

    private static void RequireValues(float[] values, int count, string edit)
    {
        if (values.Length != count)
            throw new InvalidOperationException($"'{edit}' needs {count} number(s)");
    }

    private static void VerifyRagdollEdits(byte[] produced, RagdollDocument document,
                                           IReadOnlyList<string> edits)
    {
        var reread = new RagdollDocument(produced);
        if (reread.Ragdolls.Count != document.Ragdolls.Count)
            throw new InvalidDataException(
                $"the written file holds {reread.Ragdolls.Count} ragdoll(s) where {document.Ragdolls.Count} " +
                $"were expected for: {string.Join(", ", edits)}");

        for (int i = 0; i < document.Ragdolls.Count; i++)
        {
            if (document.Ragdolls[i].SameAs(reread.Ragdolls[i])) continue;

            throw new InvalidDataException(
                $"ragdoll #{reread.Ragdolls[i].ObjectId} did not read back as the value that was set for: " +
                string.Join(", ", edits));
        }

        if (reread.Shapes.Count != document.Shapes.Count)
            throw new InvalidDataException(
                $"the written file holds {reread.Shapes.Count} capsule shape(s) where {document.Shapes.Count} " +
                $"were expected for: {string.Join(", ", edits)}");

        for (int i = 0; i < document.Shapes.Count; i++)
        {
            if (document.Shapes[i].SameAs(reread.Shapes[i])) continue;

            throw new InvalidDataException(
                $"shape {i} did not read back as the value that was set for: {string.Join(", ", edits)}");
        }
    }

    private static int RagdollSweep(string dataFolder, string? outPath)
    {
        if (outPath != null)
        {
            Console.Error.WriteLine($"--out is refused in sweep mode ({outPath}): " +
                                    "a sweep reads a whole Data folder and writes nothing");
            return 1;
        }

        if (!Directory.Exists(dataFolder))
        {
            Console.Error.WriteLine($"--data folder not found: {dataFolder}");
            return 1;
        }

        using var data = Mo2ScanData.OpenScanData(dataFolder);

        var candidates = data.EnumerateWinners("", IsRagdollCandidate)
                             .OrderBy(asset => asset.ArchivePath, StringComparer.Ordinal)
                             .ThenBy(asset => asset.VirtualPath, StringComparer.Ordinal)
                             .ToList();

        int files = 0, shapes = 0, bodies = 0, constraints = 0, unreadable = 0, noRagdoll = 0, usable = 0;
        int byteIdentical = 0, massOk = 0, massSkipped = 0, limitOk = 0, limitSkipped = 0, failed = 0;
        int capsuleResized = 0, capsuleShared = 0, capsuleRefused = 0, capsuleHeld = 0, capsuleIdentical = 0;
        int capsuleFiles = 0;
        float capsuleWorst = 0f;
        string? capsuleFirstRefusal = null;
        var failures = new List<string>();
        var archiveSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var asset in candidates)
        {
            if (asset.ArchivePath != null) archiveSet.Add(asset.ArchivePath);
            foreach (var loser in asset.Shadowed)
                if (loser.ArchivePath != null) archiveSet.Add(loser.ArchivePath);
        }

        foreach (var asset in candidates)
        {
            if (!data.TryRead(asset, out byte[] bytes, out string? warning))
            {
                unreadable++;
                failures.Add($"{asset.VirtualPath}: {warning ?? "the winning copy could not be read"}");
                continue;
            }

            bool carries;
            try { carries = CarriesRagdoll(bytes); }
            catch (Exception) { unreadable++; continue; }
            if (!carries) { noRagdoll++; continue; }

            files++;

            try
            {
                var document = new RagdollDocument(bytes);
                shapes += document.Shapes.Count;
                bodies += document.Ragdolls.Sum(node => node.Bodies.Count);
                constraints += document.Ragdolls.SelectMany(node => node.Constraints).Count(row => row.LimitsEditable);
                if (document.Ragdolls.Any(node => node.Bodies.Count > 0)) usable++;

                byte[] rebuilt = document.ForceRebuild();
                if (!rebuilt.AsSpan().SequenceEqual(bytes))
                {
                    int at = FirstDifference(bytes, rebuilt);
                    failures.Add($"{asset.VirtualPath}: rebuilding the unedited file was not byte-identical " +
                                 $"({bytes.Length} bytes in, {rebuilt.Length} out, first difference at 0x{at:x})");
                }
                else
                {
                    byteIdentical++;
                }

                string? massNote = SweepMassEdit(document, asset.VirtualPath, failures);
                if (massNote == null) massOk++;
                else if (massNote.Length == 0) massSkipped++;
                else failures.Add(massNote);

                string? limitNote = SweepLimitEdit(document, asset.VirtualPath, failures);
                if (limitNote == null) limitOk++;
                else if (limitNote.Length == 0) limitSkipped++;
                else failures.Add(limitNote);

                RagdollCapsuleSweep capsuleSweep = SweepCapsuleResize(bytes, asset.VirtualPath);
                capsuleResized += capsuleSweep.Resized;
                capsuleShared += capsuleSweep.Shared;
                capsuleRefused += capsuleSweep.Refused;
                capsuleHeld += capsuleSweep.Held;
                capsuleIdentical += capsuleSweep.Identical;
                if (capsuleSweep.ByteIdentical) capsuleFiles++;
                if (capsuleSweep.Worst > capsuleWorst) capsuleWorst = capsuleSweep.Worst;
                if (capsuleFirstRefusal == null && capsuleSweep.Refusal != null)
                    capsuleFirstRefusal = capsuleSweep.Refusal;
                if (capsuleSweep.Failure != null) failures.Add(capsuleSweep.Failure);
            }
            catch (Exception error)
            {
                failed++;
                failures.Add($"{asset.VirtualPath}: {error.Message}");
            }
        }

        foreach (string failure in failures.Take(20)) Console.WriteLine("  " + failure);
        if (failures.Count > 20) Console.WriteLine($"  ... and {failures.Count - 20} more");

        Console.WriteLine($"\n{archiveSet.Count} archive(s): {files} file(s) with an hknpRagdollData " +
                          $"({usable} of them with at least one body), {bodies} bodies, {constraints} " +
                          $"limit-bearing constraints, {shapes} capsule shapes, {unreadable} unreadable, " +
                          $"{noRagdoll} skeleton-named file(s) without a ragdoll");
        Console.WriteLine($"unedited forced rebuild: {byteIdentical} byte-identical, " +
                          $"{files - byteIdentical} did not reproduce their bytes");
        Console.WriteLine($"mass edit round trip: {massOk} read back as edited, {massSkipped} skipped " +
                          "(no scalable body)");
        Console.WriteLine($"limit edit round trip: {limitOk} read back as edited, {limitSkipped} skipped " +
                          $"(no editable constraint), {failed} file(s) failed to measure");
        Console.WriteLine($"capsule resize round trip: {capsuleResized} capsule(s) scaled by 1.1 and back " +
                          $"({capsuleShared} shared by bodies or mass properties), {capsuleRefused} refused" +
                          (capsuleFirstRefusal == null ? "" : $" (first: {capsuleFirstRefusal})") + ", " +
                          $"{capsuleHeld} read back within 2e-3 of radius/99 ({capsuleIdentical} of them " +
                          $"byte-identical), {capsuleFiles} file(s) byte-identical, worst vertex difference " +
                          $"{capsuleWorst:0.#########} of radius/99");

        return failures.Count == 0 ? 0 : 1;
    }

    private static bool CarriesRagdoll(byte[] bytes) =>
        new PackfileObjects(PackfileImage.Read(bytes))
            .Instances.Any(instance => instance.ClassName == "hknpRagdollData");

    private static string? SweepMassEdit(RagdollDocument document, string virtualPath, List<string> failures)
    {
        int ragdoll = -1, index = -1;
        for (int r = 0; r < document.Ragdolls.Count && index < 0; r++)
        {
            var node = document.Ragdolls[r];
            foreach (var candidate in node.Bodies)
            {
                if (candidate.MotionId < 0 || candidate.MotionId >= node.Motions.Count) continue;
                float stored = node.Motions[candidate.MotionId].Mass;
                if (stored <= 0f || !float.IsFinite(stored)) continue;

                ragdoll = r;
                index = candidate.Index;
                break;
            }
        }

        if (index < 0) return "";

        var picked = document.Ragdolls[ragdoll];
        float mass = picked.Motions[picked.Bodies[index].MotionId].Mass;
        float wanted = mass * 2f;
        try
        {
            document.SetBodyMass(ragdoll, index, wanted);
            var reread = new RagdollDocument(document.Serialize());
            float read = reread.Ragdolls[ragdoll].Motions[reread.Ragdolls[ragdoll].Bodies[index].MotionId].Mass;
            if (read != wanted)
                return $"{virtualPath}: the mass edit read back as {read} where {wanted} was set";
            return null;
        }
        catch (Exception error)
        {
            failures.Add($"{virtualPath}: the mass edit failed: {error.Message}");
            return "";
        }
    }

    private static string? SweepLimitEdit(RagdollDocument document, string virtualPath, List<string> failures)
    {
        int ragdoll = -1, index = -1;
        for (int r = 0; r < document.Ragdolls.Count && index < 0; r++)
        {
            var node = document.Ragdolls[r];
            for (int i = 0; i < node.Constraints.Count; i++)
            {
                if (!node.Constraints[i].LimitsEditable) continue;

                ragdoll = r;
                index = i;
                break;
            }
        }

        if (index < 0) return "";

        try
        {
            document.SetConstraintLimits(ragdoll, index, -0.25f, 0.25f);
            var reread = new RagdollDocument(document.Serialize());
            var read = reread.Ragdolls[ragdoll].Constraints[index];
            if (read.MinAngle != -0.25f || read.MaxAngle != 0.25f)
                return $"{virtualPath}: the limit edit read back as {read.MinAngle}..{read.MaxAngle} " +
                       "where -0.25..0.25 was set";
            return null;
        }
        catch (Exception error)
        {
            failures.Add($"{virtualPath}: the limit edit failed: {error.Message}");
            return "";
        }
    }

    private static bool IsRagdollCandidate(string virtualPath) =>
        GameData.IsSkeletonName(virtualPath.Replace('\\', '/'));

    private sealed record RagdollCapsuleSweep(int Resized, int Shared, int Refused, int Held, int Identical,
                                              bool ByteIdentical, float Worst, string? Refusal, string? Failure);

    private static RagdollCapsuleSweep SweepCapsuleResize(byte[] bytes, string virtualPath)
    {
        var origin = new RagdollDocument(bytes);
        var first = new RagdollDocument(bytes);
        var stretched = new List<int>();
        int shared = 0, refused = 0;
        string? firstRefusal = null;

        for (int i = 0; i < first.Shapes.Count; i++)
        {
            var shape = first.Shapes[i];
            if (!shape.CarriesPolytopeData) continue;
            if (shape.ReferencedByBodies > 1 || shape.ShapesSharingProperties > 1 ||
                shape.ShapesSharingMassProperties > 1) shared++;

            try
            {
                first.SetCapsuleEndpoints(i, shape.A, shape.A + (shape.B - shape.A) * 1.1f, true);
                stretched.Add(i);
            }
            catch (Exception error) when (error is InvalidOperationException or InvalidDataException)
            {
                refused++;
                firstRefusal ??= $"capsule {i}: {error.Message}";
            }
        }

        if (stretched.Count == 0)
            return new RagdollCapsuleSweep(0, shared, refused, 0, 0, false, 0f, firstRefusal, null);

        var back = new RagdollDocument(first.Serialize());
        foreach (int i in stretched)
        {
            var shape = back.Shapes[i];
            try
            {
                back.SetCapsuleEndpoints(i, shape.A, shape.A + (shape.B - shape.A) / 1.1f, true);
            }
            catch (Exception error)
            {
                return new RagdollCapsuleSweep(stretched.Count, shared, refused, 0, 0, false, 0f, firstRefusal,
                                               $"{virtualPath}: capsule {i} was not resized back: {error.Message}");
            }
        }

        byte[] restored;
        try
        {
            restored = back.Serialize();
        }
        catch (Exception error)
        {
            return new RagdollCapsuleSweep(stretched.Count, shared, refused, 0, 0, false, 0f, firstRefusal,
                                           $"{virtualPath}: the resized file was not written: {error.Message}");
        }

        var again = new RagdollDocument(restored);
        int held = 0, identical = 0;
        float worst = 0f;
        string? failure = null;
        foreach (int i in stretched)
        {
            if (origin.Shapes[i].Capsule is not { } was || again.Shapes[i].Capsule is not { } now) continue;

            if (RagdollCapsule.SameBytes(was.Vertices, now.Vertices) &&
                RagdollCapsule.SameBytes(was.Planes, now.Planes))
                identical++;

            float scale = origin.Shapes[i].Radius / 99f;
            double difference = 0.0;
            if (scale > 0f)
                for (int vertex = 0; vertex < RagdollCapsule.VertexCount; vertex++)
                    for (int lane = 0; lane < 3; lane++)
                    {
                        int at = vertex * RagdollCapsule.VertexWidth + lane * 4;
                        float before = BitConverter.ToSingle(was.Vertices, at);
                        float after = BitConverter.ToSingle(now.Vertices, at);
                        double ratio = Math.Abs(before - after) / scale;
                        if (ratio > difference) difference = ratio;
                    }

            if (difference > 2e-3)
                failure ??= $"{virtualPath}: capsule {i} came back {difference:0.#########} of radius/99 away";
            else
                held++;
            if (difference > worst) worst = (float)difference;
        }

        bool byteIdentical = restored.AsSpan().SequenceEqual(bytes);
        return new RagdollCapsuleSweep(stretched.Count, shared, refused, held, identical, byteIdentical, worst,
                                       firstRefusal, failure);
    }
}
