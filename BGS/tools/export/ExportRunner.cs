using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using OpenCommonwealth.Services.Hkx;
using Radicle.Interop;

namespace BehaviourStudio.Tools.Export;

public sealed record ExportErrorRow(string Source, string Message);

public sealed class ExportRunner
{
    private const long MaximumInputBytes = 512L * 1024 * 1024;

    private static readonly UTF8Encoding Utf8 = new(false);

    private readonly string _output;
    private readonly TextWriter _log;
    private readonly List<ExportErrorRow> _errors = new();
    private readonly List<IndexRow> _documents = new();
    private readonly HashSet<string> _targets = new(StringComparer.Ordinal);

    public ExportRunner(string output, TextWriter log)
    {
        _output = output ?? throw new ArgumentNullException(nameof(output));
        _log = log ?? throw new ArgumentNullException(nameof(log));
    }

    public IReadOnlyList<ExportErrorRow> Errors => _errors;

    public int Run(IReadOnlyList<string> inputs, bool batch)
    {
        ArgumentNullException.ThrowIfNull(inputs);
        if (!TryPrepareOutput()) return 2;

        var files = new List<(string Path, string Root)>();
        foreach (string input in inputs)
        {
            if (batch)
            {
                if (!Directory.Exists(input))
                {
                    _errors.Add(new ExportErrorRow(input, "batch input is not a directory"));
                    continue;
                }

                Collect(input, files);
                continue;
            }

            if (!File.Exists(input))
            {
                _errors.Add(new ExportErrorRow(input, "no such file"));
                continue;
            }

            files.Add((input, Path.GetDirectoryName(Path.GetFullPath(input)) ?? ""));
        }

        files.Sort((left, right) => string.CompareOrdinal(left.Path, right.Path));

        foreach (var file in files) ExportOne(file.Path, file.Root, batch);

        WriteIndex();
        WriteErrors();

        _log.WriteLine($"exported {_documents.Count} document(s) from {files.Count} input(s); " +
                       $"{_errors.Count} error(s)");
        return _errors.Count == 0 ? 0 : 2;
    }

    // Constraint: an output directory that cannot be created is a row and an exit code, not an exception out of Run.
    private bool TryPrepareOutput()
    {
        try
        {
            Directory.CreateDirectory(_output);
            return true;
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException
                                        or NotSupportedException or System.Security.SecurityException
                                        or ArgumentException)
        {
            _errors.Add(new ExportErrorRow(_output, "output directory: " + Detail(failure)));
            _log.WriteLine("cannot prepare the output directory: " + failure.Message);
            return false;
        }
    }

    public static void WriteSchema(string output)
    {
        Directory.CreateDirectory(output);
        File.WriteAllText(Path.Combine(output, ExportFormats.SchemaFile), SchemaDocument.Json(), Utf8);
    }

    // Invariant: a directory that cannot be walked becomes a row, so no input tree part vanishes without one.
    private void Collect(string input, List<(string Path, string Root)> files)
    {
        try
        {
            foreach (string found in Directory.EnumerateFiles(input, "*", SearchOption.AllDirectories))
                if (IsExportable(found)) files.Add((found, input));
        }
        catch (Exception failure) when (failure is not OperationCanceledException)
        {
            _errors.Add(new ExportErrorRow(input, "directory walk stopped: " + Detail(failure)));
        }
    }

    private void ExportOne(string path, string root, bool batch)
    {
        string source = SourceName(path, root, batch);
        string stem = Stem(source);

        try
        {
            byte[] bytes = Read(path);
            string inputSha = Convert.ToHexString(SHA256.HashData(bytes));

            if (path.EndsWith(".nif", StringComparison.OrdinalIgnoreCase))
                ExportNif(bytes, source, stem, inputSha);
            else
                ExportHkx(bytes, source, stem, inputSha);
        }
        catch (Exception failure) when (failure is not OperationCanceledException)
        {
            // Invariant: no exception escapes a single input, so one bad file cannot end a batch.
            _errors.Add(new ExportErrorRow(source, Detail(failure)));
        }
    }

    private void ExportHkx(byte[] bytes, string source, string stem, string inputSha)
    {
        var image = PackfileImage.Read(bytes);
        var objects = new PackfileObjects(image);
        var classes = new HashSet<string>(objects.Instances.Select(instance => instance.ClassName),
                                          StringComparer.Ordinal);
        var reader = new HkxBinaryReader();
        bool exported = false;

        if (classes.Contains("hkaSkeleton"))
            exported |= Attempt(source, "skeleton", () =>
                Write(ExportFormats.SkeletonFolder, stem, ".skeleton.json",
                      SkeletonExport.Json(reader.ReadSkeleton(bytes), source), source, inputSha));

        if (classes.Any(name => Array.IndexOf(HkxAnimationData.DecodedAnimationClasses, name) >= 0))
            exported |= Attempt(source, "animation", () =>
            {
                if (!reader.TryReadAnimation(bytes, out var animation))
                    throw new InvalidDataException(
                        $"this file holds {Describe(animation)}, which is not decoded");
                Write(ExportFormats.AnimationFolder, stem, ".animation.json",
                      AnimationExport.Json(animation, source), source, inputSha);
            });

        if (classes.Contains("hkbBehaviorGraph") || classes.Contains("hkbStateMachine"))
            exported |= Attempt(source, "behaviour graph", () =>
            {
                string xml = NativeXml.From(objects, image);
                var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));
                Write(ExportFormats.GraphFolder, stem, ".behaviour-graph.json",
                      GraphExport.Json(plan, source), source, inputSha);
            });

        if (!exported)
            throw new InvalidDataException(
                "no skeleton, decodable animation or behaviour graph in this file");
    }

    private void ExportNif(byte[] bytes, string source, string stem, string inputSha)
    {
        // Constraint: one NIF parse, so the object naming a system comes from the same read as the payload it names.
        var nif = NifFile.FromBytes(bytes);
        if (nif.Collisions.Count == 0)
            throw new InvalidDataException("this NIF holds no bhkPhysicsSystem or bhkRagdollSystem");

        ExportCollisionBlobs(nif.Collisions, source, stem, inputSha);
    }

    // Constraint: a bad blob is one row, so a NIF's other collision systems still export.
    internal void ExportCollisionBlobs(IReadOnlyList<NifCollisionBlob> blobs, string source, string stem,
                                       string inputSha)
    {
        bool single = blobs.Count == 1;
        foreach (var blob in blobs)
        {
            try
            {
                var attribution = blob.OwnerBlock >= 0 ? Attributed(blob) : null;
                if (!NifCollisionDecoder.TryDecode(blob.Bytes, attribution, out var model, out string error))
                    throw new InvalidDataException(
                        $"collision block {blob.Block} ({blob.Type}) did not decode: {error}");

                string suffix = single ? ".collision.json" : $".{blob.Block}.collision.json";
                Write(ExportFormats.CollisionFolder, stem, suffix,
                      CollisionExport.Json(model!, source, blob.Block, blob.Type), source, inputSha);
            }
            catch (Exception failure) when (failure is not OperationCanceledException)
            {
                _errors.Add(new ExportErrorRow(source, Detail(failure)));
            }
        }
    }

    private static NifCollisionAttribution Attributed(NifCollisionBlob blob) => new()
    {
        OwnerBlock = blob.OwnerBlock,
        Flags = blob.Flags,
        SystemRef = blob.SystemRef,
        BodyId = blob.BodyId,
        TargetBlock = blob.TargetBlock,
        TargetNode = blob.TargetNodeIndex,
        TargetNodeName = blob.TargetNode?.Name ?? "",
    };

    private bool Attempt(string source, string kind, Action action) =>
        AttemptKind(source, kind, action, _errors);

    // Constraint: one kind failing must not stop the file's other kinds, so every failure becomes a row.
    internal static bool AttemptKind(string source, string kind, Action action,
                                     ICollection<ExportErrorRow> errors)
    {
        try
        {
            action();
            return true;
        }
        catch (Exception failure) when (failure is not OperationCanceledException)
        {
            errors.Add(new ExportErrorRow(source, kind + ": " + Detail(failure)));
            return false;
        }
    }

    private void Write(string folder, string stem, string suffix, string json, string source,
                       string inputSha)
    {
        string target = folder + "/" + stem + suffix;
        if (!_targets.Add(target))
            throw new InvalidDataException($"'{target}' was already written by an earlier input");

        string path = Path.Combine(_output, target.Replace('/', Path.DirectorySeparatorChar));
        string? directory = Path.GetDirectoryName(path);
        if (directory is { Length: > 0 }) Directory.CreateDirectory(directory);

        byte[] content = Utf8.GetBytes(json);
        File.WriteAllBytes(path, content);

        _documents.Add(new IndexRow(folder, source, target, inputSha,
                                    Convert.ToHexString(SHA256.HashData(content)), content.LongLength));
    }

    private void WriteIndex()
    {
        var json = new ExportJson().Object()
            .Name("format").Text(ExportFormats.Index)
            .Name("version").Number(ExportFormats.Version)
            .Name("documents").Array();

        foreach (var document in _documents.OrderBy(row => row.Target, StringComparer.Ordinal))
        {
            json.Object()
                .Name("kind").Text(document.Kind)
                .Name("source").Text(document.Source)
                .Name("target").Text(document.Target)
                .Name("inputSha256").Text(document.InputSha256)
                .Name("sha256").Text(document.Sha256)
                .Name("bytes").Number(document.Bytes)
                .EndObject();
        }

        json.EndArray();
        File.WriteAllText(Path.Combine(_output, ExportFormats.IndexFile), json.EndObject().Finish(), Utf8);
    }

    private void WriteErrors()
    {
        string path = Path.Combine(_output, ExportFormats.ErrorFile);
        if (_errors.Count == 0)
        {
            if (File.Exists(path)) File.Delete(path);
            return;
        }

        // Constraint: errors.jsonl rows go through the same writer as every document, so escaping cannot drift.
        var lines = _errors
            .OrderBy(row => row.Source, StringComparer.Ordinal)
            .ThenBy(row => row.Message, StringComparer.Ordinal)
            .Select(row => new ExportJson(singleLine: true)
                .Object()
                .Name("source").Text(row.Source)
                .Name("message").Text(row.Message)
                .EndObject()
                .Finish());

        File.WriteAllText(path, string.Concat(lines), Utf8);
    }

    private static byte[] Read(string path)
    {
        long length = new FileInfo(path).Length;
        if (length > MaximumInputBytes)
            throw new InvalidDataException(
                $"input is {length} bytes; the supported maximum is {MaximumInputBytes}");

        return File.ReadAllBytes(path);
    }

    private static string SourceName(string path, string root, bool batch)
    {
        if (!batch) return Path.GetFileName(path);
        return Path.GetRelativePath(root, path).Replace('\\', '/');
    }

    private static string Stem(string source) =>
        source[..^Path.GetExtension(source).Length];

    private static string Describe(HkxAnimationData animation) =>
        animation.AnimationClass.Length > 0 ? animation.AnimationClass : "no animation class";

    private static bool IsExportable(string path) =>
        path.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(".nif", StringComparison.OrdinalIgnoreCase);

    private static string Detail(Exception failure) =>
        IsInputFailure(failure) ? failure.Message : failure.GetType().Name + ": " + failure.Message;

    private static bool IsInputFailure(Exception failure) =>
        failure is IOException or InvalidDataException or InvalidOperationException or ArgumentException
            or UnauthorizedAccessException or NotSupportedException or FormatException
            or OverflowException or IndexOutOfRangeException or RadicleException;

    private sealed record IndexRow(string Kind, string Source, string Target, string InputSha256,
                                   string Sha256, long Bytes);
}
