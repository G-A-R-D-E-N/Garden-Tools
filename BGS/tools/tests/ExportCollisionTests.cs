using System;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using BehaviourStudio.Tools.Export;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

// Invariant: a collision system decodes into a versioned document, and unreadable input becomes a row, never a crash.
public sealed class ExportCollisionTests
{
    // Invariant: two writes of one collision system must match, so hashes, float text and ordering cannot drift.
    [Fact]
    public void SyntheticCollisionSystemExportsAsVersionedDocument()
    {
        var sample = NifCollisionFixture.Build();
        Assert.True(NifCollisionDecoder.TryDecode(sample.Bytes, out var decoded, out string error), error);
        Assert.NotNull(decoded);
        var model = decoded!;

        string first = CollisionExport.Json(model, "synthetic.nif", 7, "bhkPhysicsSystem");
        string second = CollisionExport.Json(model, "synthetic.nif", 7, "bhkPhysicsSystem");
        Assert.Equal(first, second);

        var document = JsonNode.Parse(first)!.AsObject();
        Assert.Equal(ExportFormats.Collision, document["format"]!.GetValue<string>());
        Assert.Equal(ExportFormats.Version, document["version"]!.GetValue<int>());
        Assert.Equal("synthetic.nif", document["source"]!.GetValue<string>());
        Assert.Equal(7, document["block"]!.GetValue<int>());
        Assert.Equal("bhkPhysicsSystem", document["type"]!.GetValue<string>());

        Assert.Equal(model.Sha256Hex(), document["sha256"]!.GetValue<string>());
        Assert.Equal(model.PointerSize, document["pointerSize"]!.GetValue<int>());
        Assert.Equal(model.LengthPrefixed, document["lengthPrefixed"]!.GetValue<bool>());
        Assert.Equal(model.SystemClass, document["systemClass"]!.GetValue<string>());

        var bodies = document["bodies"]!.AsArray();
        var shapes = document["shapes"]!.AsArray();
        Assert.Equal(model.Bodies.Count, bodies.Count);
        Assert.Equal(model.Shapes.Count, shapes.Count);

        Assert.Equal(model.Bodies[0].MaterialHash, bodies[0]!["materialHash"]!.GetValue<uint>());
        Assert.Equal(model.Bodies[0].Shape, bodies[0]!["shape"]!.GetValue<int>());
        Assert.Equal(HkxNumber.Text(model.Bodies[0].Response.DynamicFriction),
                     bodies[0]!["response"]!["dynamicFriction"]!.ToJsonString());

        Assert.Equal(NifCollisionFixture.SphereRadius,
                     shapes[0]!["convexRadius"]!.GetValue<float>());
        Assert.Equal("sphere", shapes[0]!["kind"]!.GetValue<string>());
        Assert.Equal(model.Shapes[1].Instances.Count, shapes[1]!["instances"]!.AsArray().Count);
        Assert.Equal(model.Shapes[1].VertexCount, shapes[1]!["vertexCount"]!.GetValue<int>());
        Assert.Equal(model.Shapes[1].Triangles.Count, shapes[1]!["triangles"]!.AsArray().Count);

        Assert.Equal("convexPolytope", shapes[2]!["kind"]!.GetValue<string>());
        Assert.Equal(model.Shapes[2].VertexCount, shapes[2]!["vertexCount"]!.GetValue<int>());
        Assert.Equal(model.Shapes[2].Triangles.Count, shapes[2]!["triangles"]!.AsArray().Count);
        Assert.Equal(HkxNumber.Text(model.Shapes[2].Vertices[0]),
                     shapes[2]!["vertices"]![0]!.ToJsonString());
        Assert.Equal(model.Shapes[2].Instances.Count, shapes[2]!["instances"]!.AsArray().Count);
        Assert.Equal(model.Notes.Count, document["notes"]!.AsArray().Count);
    }

    // Invariant: a NIF without a collision system must be reported, not skipped and not crashed on.
    [Fact]
    public void ANifWithoutCollisionIsAnErrorRowAndWritesNoDocument()
    {
        string nif = Path.Combine(AppContext.BaseDirectory, "samples", "TurretMounted.nif");
        Assert.True(File.Exists(nif), "the turret NIF sample is not in the test output");

        string output = ExportTestSupport.Temp("nif");
        int code = Program.Run(new[] { "--out", output, nif }, TextWriter.Null, TextWriter.Null);

        Assert.Equal(2, code);

        string errors = Path.Combine(output, ExportFormats.ErrorFile);
        Assert.True(File.Exists(errors));
        var row = JsonNode.Parse(Assert.Single(File.ReadAllLines(errors)))!.AsObject();
        Assert.Equal("TurretMounted.nif", row["source"]!.GetValue<string>());
        Assert.NotEmpty(row["message"]!.GetValue<string>());

        Assert.Empty(Directory.GetFiles(output, "*.json", SearchOption.AllDirectories)
            .Where(path => !path.EndsWith(ExportFormats.IndexFile, StringComparison.Ordinal)));
    }

    // Invariant: unreadable bytes must become a row and leave no document behind.
    [Fact]
    public void UnreadableHkxIsAnErrorRowAndWritesNoDocument()
    {
        string directory = ExportTestSupport.Temp("bad-input");
        string input = Path.Combine(directory, "broken.hkx");
        File.WriteAllBytes(input, new byte[64]);

        string output = Path.Combine(directory, "out");
        int code = Program.Run(new[] { "--out", output, input }, TextWriter.Null, TextWriter.Null);

        Assert.Equal(2, code);

        var row = JsonNode.Parse(Assert.Single(File.ReadAllLines(
            Path.Combine(output, ExportFormats.ErrorFile))))!.AsObject();
        Assert.Equal("broken.hkx", row["source"]!.GetValue<string>());
        Assert.NotEmpty(row["message"]!.GetValue<string>());

        Assert.Empty(Directory.GetFiles(output, "*.json", SearchOption.AllDirectories)
            .Where(path => !path.EndsWith(ExportFormats.IndexFile, StringComparison.Ordinal)));
    }

    // Invariant: a batch argument that is not a directory becomes a row, so no input disappears silently.
    [Fact]
    public void ABatchArgumentThatIsNotADirectoryIsARow()
    {
        string directory = ExportTestSupport.Temp("batch-file");
        string input = Path.Combine(directory, "not-a-directory.hkx");
        File.WriteAllBytes(input, new byte[8]);

        string output = Path.Combine(directory, "out");
        int code = Program.Run(new[] { "--out", output, "--batch", input }, TextWriter.Null, TextWriter.Null);

        Assert.Equal(2, code);
        var row = JsonNode.Parse(Assert.Single(File.ReadAllLines(
            Path.Combine(output, ExportFormats.ErrorFile))))!.AsObject();
        Assert.Equal(input, row["source"]!.GetValue<string>());
        Assert.Equal("batch input is not a directory", row["message"]!.GetValue<string>());
    }

    // Invariant: a missing path must be an error row, so one bad argument cannot end a batch.
    [Fact]
    public void AMissingInputIsAnErrorRowWithoutThrowing()
    {
        string output = ExportTestSupport.Temp("missing");

        int code = Program.Run(new[] { "--out", output, Path.Combine(output, "absent.hkx") },
                               TextWriter.Null, TextWriter.Null);

        Assert.Equal(2, code);
        var row = JsonNode.Parse(Assert.Single(File.ReadAllLines(
            Path.Combine(output, ExportFormats.ErrorFile))))!.AsObject();
        Assert.Contains("absent.hkx", row["source"]!.GetValue<string>(), StringComparison.Ordinal);
        Assert.NotEmpty(row["message"]!.GetValue<string>());
    }
}
