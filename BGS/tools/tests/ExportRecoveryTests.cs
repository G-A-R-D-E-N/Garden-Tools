using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using BehaviourStudio.Tools.Export;
using OpenCommonwealth.Services.Hkx;
using Radicle.Interop;
using Xunit;

namespace BehaviourStudio.Tests;

// Invariant: an output or document failure becomes a row or an exit code, never an exception out of the tool.
public sealed class ExportRecoveryTests
{
    // Invariant: an output directory that cannot be created must be recorded and reported, not thrown out of Run.
    [Fact]
    public void AnOutputDirectoryThatCannotBeCreatedIsAnErrorRowAndExitCode()
    {
        string directory = ExportTestSupport.Temp("output-failure");
        string output = Path.Combine(directory, "not-a-directory");
        File.WriteAllBytes(output, new byte[4]);
        string input = Path.Combine(directory, "absent.hkx");

        var runner = new ExportRunner(output, TextWriter.Null);
        int code = runner.Run(new[] { input }, batch: false);

        Assert.Equal(2, code);
        var row = Assert.Single(runner.Errors);
        Assert.Equal(output, row.Source);
        Assert.StartsWith("output directory:", row.Message, StringComparison.Ordinal);

        Assert.Equal(2, Program.Run(new[] { "--out", output, input }, TextWriter.Null, TextWriter.Null));
    }

    // Invariant: one document kind failing must not stop the other kinds the same file carries.
    [Fact]
    public void ANonInputFailureInOneKindStillRunsTheNextKind()
    {
        var rows = new List<ExportErrorRow>();
        bool secondRan = false;

        bool first = ExportRunner.AttemptKind("input.hkx", "skeleton",
                                              () => throw new NullReferenceException("kind failed"), rows);
        bool second = ExportRunner.AttemptKind("input.hkx", "animation", () => secondRan = true, rows);

        Assert.False(first);
        Assert.True(second);
        Assert.True(secondRan);

        var row = Assert.Single(rows);
        Assert.Equal("input.hkx", row.Source);
        Assert.StartsWith("skeleton:", row.Message, StringComparison.Ordinal);
        Assert.Contains("NullReferenceException", row.Message, StringComparison.Ordinal);
    }

    // Invariant: a NIF's good systems must export even when another system in the same file does not decode.
    [Fact]
    public void AMultiSystemNifExportsItsGoodBlobsAndReportsTheBadOne()
    {
        var good = NifCollisionFixture.Build();
        var bad = new NifCollisionBlob
        {
            Block = 3,
            Type = "bhkPhysicsSystem",
            Bytes = new byte[] { 0xDE, 0xAD, 0xBE, 0xEF },
            OwnerBlock = -1,
            Flags = 0,
            SystemRef = -1,
            BodyId = 0,
            TargetBlock = -1,
            TargetNodeIndex = -1,
        };
        var goodBlob = new NifCollisionBlob
        {
            Block = 7,
            Type = "bhkPhysicsSystem",
            Bytes = good.Bytes,
            OwnerBlock = -1,
            Flags = 0,
            SystemRef = -1,
            BodyId = 0,
            TargetBlock = -1,
            TargetNodeIndex = -1,
        };

        string output = ExportTestSupport.Temp("multi-system");
        Directory.CreateDirectory(output);
        var runner = new ExportRunner(output, TextWriter.Null);
        runner.ExportCollisionBlobs(new[] { bad, goodBlob }, "multi.nif", "multi", new string('0', 64));

        var row = Assert.Single(runner.Errors);
        Assert.Contains("collision block 3", row.Message, StringComparison.Ordinal);
        Assert.Contains("did not decode", row.Message, StringComparison.Ordinal);

        string[] documents = Directory.GetFiles(Path.Combine(output, ExportFormats.CollisionFolder));
        string document = Assert.Single(documents);
        Assert.EndsWith(".7.collision.json", document, StringComparison.Ordinal);

        var json = JsonNode.Parse(File.ReadAllText(document))!.AsObject();
        Assert.Equal(ExportFormats.Collision, json["format"]!.GetValue<string>());
        Assert.Equal("multi.nif", json["source"]!.GetValue<string>());
    }
}
