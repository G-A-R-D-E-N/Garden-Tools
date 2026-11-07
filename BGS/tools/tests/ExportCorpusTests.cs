using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using ExportProgram = BehaviourStudio.Tools.Export.Program;

namespace BehaviourStudio.Tests;

// Invariant: the export tool writes the same bytes twice and reports every manifest fixture it cannot export.
public sealed class ExportCorpusTests
{
    private static int Export(string output, bool batch, params string[] inputs)
    {
        var args = new List<string> { "--out", output };
        if (batch) args.Add("--batch");
        args.AddRange(inputs);
        return ExportProgram.Run(args.ToArray(), TextWriter.Null, TextWriter.Null);
    }

    private static (string Source, string Message)[] Errors(string output)
    {
        string path = Path.Combine(output, "errors.jsonl");
        if (!File.Exists(path)) return Array.Empty<(string, string)>();

        return File.ReadAllLines(path)
            .Where(line => line.Length > 0)
            .Select(line => JsonNode.Parse(line)!.AsObject())
            .Select(row => (row["source"]!.GetValue<string>(), row["message"]!.GetValue<string>()))
            .ToArray();
    }

    // Invariant: a second run writes the same bytes, so no output may depend on time, paths or enumeration order.
    [Fact]
    public void EveryManifestFileIsAttemptedAndTwoRunsAreByteIdentical()
    {
        var manifest = ExportTestSupport.Manifest();
        string first = ExportTestSupport.Temp("corpus-first");
        string second = ExportTestSupport.Temp("corpus-second");

        int firstCode = Export(first, batch: true, ExportTestSupport.VanillaRoot);
        int secondCode = Export(second, batch: true, ExportTestSupport.VanillaRoot);

        Assert.Equal(firstCode, secondCode);
        Assert.Contains(firstCode, new[] { 0, 2 });

        var left = ExportTestSupport.Files(first);
        var right = ExportTestSupport.Files(second);
        Assert.Equal(left, right);
        foreach (string relative in left)
            Assert.Equal(File.ReadAllBytes(Path.Combine(first, relative)),
                         File.ReadAllBytes(Path.Combine(second, relative)));

        var documents = JsonNode.Parse(File.ReadAllText(Path.Combine(first, "index.json")))!
            .AsObject()["documents"]!.AsArray()
            .Select(node => node!.AsObject())
            .ToArray();
        var sources = documents.Select(document => document["source"]!.GetValue<string>())
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        var errors = Errors(first);

        foreach (var (path, sha256, fidelity) in manifest)
        {
            bool exported = sources.Contains(path, StringComparer.Ordinal);
            bool failed = errors.Any(row => row.Source == path);

            Assert.True(exported || failed, $"{path} was neither exported nor reported");

            if (fidelity != "byte") continue;

            // Constraint: only a kind the tool reads must export; the rest are reported rows.
            if (!ExportableByContent(path)) continue;

            Assert.True(exported, $"{path} did not export: {string.Join("; ", errors.Select(row => row.Message))}");
        }

        foreach (var document in documents)
        {
            string source = document["source"]!.GetValue<string>();
            var entry = manifest.Single(row => row.Path == source);
            Assert.Equal(entry.Sha256, document["inputSha256"]!.GetValue<string>());
        }
    }

    // Mirrors ExportRunner.ExportHkx: the pin requires a document only for a kind the tool reads.
    private static bool ExportableByContent(string relative)
    {
        byte[] bytes = File.ReadAllBytes(Path.Combine(ExportTestSupport.VanillaRoot, relative));
        var classes = new HashSet<string>(
            new PackfileObjects(PackfileImage.Read(bytes)).Instances.Select(instance => instance.ClassName),
            StringComparer.Ordinal);
        return classes.Contains("hkaSkeleton")
            || classes.Any(name => Array.IndexOf(HkxAnimationData.ExportableAnimationClasses, name) >= 0)
            || classes.Contains("hkbBehaviorGraph")
            || classes.Contains("hkbStateMachine");
    }

    // Invariant: the exported bones must match the decoder's, so a writer slip would fail here and not in a viewer.
    [Fact]
    public void CharacterSkeletonExportsExactlyNinetyFiveBones()
    {
        string source = TestFixtures.Path("Meshes/Actors/Character/CharacterAssets/skeleton.hkx");
        string output = ExportTestSupport.Temp("skeleton");

        Assert.Contains(Export(output, batch: false, source), new[] { 0, 2 });

        var document = JsonNode.Parse(
            File.ReadAllText(Path.Combine(output, "skeletons", "skeleton.skeleton.json")))!.AsObject();
        var skeleton = new HkxBinaryReader().ReadSkeleton(File.ReadAllBytes(source));

        // Constraint: 95 is only corroborated by SkeletonMapperAuthorTests; this suite did not measure it.
        Assert.Equal(95, skeleton.BoneNames.Count);
        Assert.Equal(95, document["boneCount"]!.GetValue<int>());
        Assert.Equal(95, document["bones"]!.AsArray().Count);
        Assert.Equal(skeleton.Name, document["name"]!.GetValue<string>());

        var bones = document["bones"]!.AsArray();
        for (int i = 0; i < bones.Count; i++)
        {
            Assert.Equal(skeleton.BoneNames[i], bones[i]!["name"]!.GetValue<string>());
            Assert.Equal(skeleton.ParentIndices[i], bones[i]!["parent"]!.GetValue<int>());
        }
    }

    // Invariant: exported frames must equal the decoded ones, so a lost track or a spline slip cannot pass.
    [Fact]
    public void SplineAnimationExportsDecodedFramesEventsAndBinding()
    {
        string relative = "Meshes/Actors/RadRoach/Animations/TurnLeft90.hkx";
        string source = TestFixtures.Path(relative);
        string output = ExportTestSupport.Temp("animation");

        Assert.Contains(Export(output, batch: false, source), new[] { 0, 2 });

        var document = JsonNode.Parse(
            File.ReadAllText(Path.Combine(output, "animations", "TurnLeft90.animation.json")))!.AsObject();
        var decoded = new HkxBinaryReader().ReadAnimation(File.ReadAllBytes(source));

        Assert.Equal("hkaSplineCompressedAnimation", decoded.AnimationClass);
        Assert.True(decoded.NumFrames > 1);
        Assert.True(decoded.NumTracks > 0);
        Assert.Equal(decoded.AnimationClass, document["class"]!.GetValue<string>());
        Assert.Equal(decoded.NumFrames, document["numFrames"]!.GetValue<int>());
        Assert.Equal(decoded.NumTracks, document["tracks"]!.AsArray().Count);
        Assert.Equal(HkxNumber.Text(decoded.Duration), document["duration"]!.ToJsonString());
        Assert.Equal(decoded.OriginalSkeletonName, document["skeletonName"]!.GetValue<string>());
        Assert.Equal(decoded.TrackToBoneIndices.Count, document["trackToBone"]!.AsArray().Count);
        Assert.Equal(decoded.Annotations.Count, document["events"]!.AsArray().Count);

        var track = document["tracks"]![0]!.AsObject();
        var frames = track["frames"]!.AsArray();
        Assert.Equal(decoded.NumFrames, frames.Count);

        var firstTrack = decoded.Tracks[0];
        Assert.Equal(HkxNumber.Text(firstTrack.Translations[0].X), frames[0]!["translation"]![0]!.ToJsonString());
        Assert.Equal(HkxNumber.Text(firstTrack.Rotations[0].W), frames[0]!["rotation"]![3]!.ToJsonString());

        var last = frames[decoded.NumFrames - 1]!;
        Assert.Equal(HkxNumber.Text(firstTrack.Scales[^1].Z), last["scale"]![2]!.ToJsonString());

        if (decoded.Annotations.Count > 0)
        {
            var annotation = decoded.Annotations[0];
            var exported = document["events"]![0]!.AsObject();
            Assert.Equal(HkxNumber.Text(annotation.Time), exported["time"]!.ToJsonString());
            Assert.Equal(annotation.Text, exported["text"]!.GetValue<string>());
        }
    }

    // Invariant: the stats must agree with the machines written, so a lossy plan walk shows on a vanilla graph.
    [Fact]
    public void VanillaBehaviourGraphNamesTheGraphItCameFrom()
    {
        string source = TestFixtures.Path("Meshes/Actors/Character/Behaviors/RaiderRootBehavior.hkx");
        string output = ExportTestSupport.Temp("graph");

        Assert.Contains(Export(output, batch: false, source), new[] { 0, 2 });

        var document = JsonNode.Parse(
            File.ReadAllText(Path.Combine(output, "graphs", "RaiderRootBehavior.behaviour-graph.json")))!.AsObject();
        var image = PackfileImage.Read(File.ReadAllBytes(source));
        var model = BehaviourGraphModel.Parse(NativeXml.From(new PackfileObjects(image), image));
        var graph = model.Objects.First(item => item.Class == "hkbBehaviorGraph");

        Assert.Equal(graph.Str("name"), document["graphName"]!.GetValue<string>());
        Assert.NotEmpty(document["machines"]!.AsArray());
        Assert.Equal(document["stats"]!["states"]!.GetValue<int>(),
                     document["machines"]!.AsArray().Sum(machine => machine!["states"]!.AsArray().Count));
        Assert.Equal(0, document["rootMachine"]!.GetValue<int>());
    }

    // Invariant: every written top-level field must be in the schema, so the contract cannot drift from the documents.
    [Fact]
    public void SchemaDocumentsEveryTopLevelFieldOfEveryWrittenDocument()
    {
        string schemaDirectory = ExportTestSupport.Temp("schema");
        Assert.Equal(0, ExportProgram.Run(new[] { "--out", schemaDirectory, "--schema-only" },
                                          TextWriter.Null, TextWriter.Null));

        var schema = JsonNode.Parse(
            File.ReadAllText(Path.Combine(schemaDirectory, "schema.json")))!.AsObject();
        var documented = schema["formats"]!.AsArray()
            .Select(format => format!.AsObject())
            .ToDictionary(
                format => format["name"]!.GetValue<string>(),
                format => format["fields"]!.AsArray()
                    .Select(field => field!["path"]!.GetValue<string>().Split('.', '[')[0])
                    .ToHashSet(StringComparer.Ordinal),
                StringComparer.Ordinal);

        string output = ExportTestSupport.Temp("schema-documents");
        Export(output, batch: true, ExportTestSupport.VanillaRoot);

        var documents = ExportTestSupport.Files(output)
            .Where(relative => relative.EndsWith(".json", StringComparison.Ordinal) &&
                               !relative.EndsWith("index.json", StringComparison.Ordinal) &&
                               relative != "schema.json")
            .ToArray();
        Assert.NotEmpty(documents);

        foreach (string relative in documents)
        {
            var document = JsonNode.Parse(File.ReadAllText(Path.Combine(output, relative)))!.AsObject();
            string format = document["format"]!.GetValue<string>();

            Assert.True(documented.TryGetValue(format, out var fields), $"{format} has no schema section");
            foreach (var (property, _) in document)
                Assert.True(fields!.Contains(property), $"{format}.{property} is missing from schema.json");
        }
    }
}
