using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using FluentAssertions;
using FO4RecordEditor.Services.Graph;
using FO4RecordEditor.Services.Papyrus;
using Xunit.Abstractions;

namespace FO4RecordEditor.Core.Tests;
















public class GraphLiftRoundTripTests
{
    private readonly ITestOutputHelper _output;

    public GraphLiftRoundTripTests(ITestOutputHelper output) => _output = output;

    public static IEnumerable<object[]> Fixtures =>
        GraphFixtures.All.Select(f => new object[] { f.Name });

    private static PapyrusScriptIndex Index(string extraRoot) =>
        PapyrusCompiler.IndexFor(new[] { extraRoot, TestRoots.BaseStubs, TestRoots.GraphScripts });


    private static (PexFile? Pex, string Diagnostics) CompileText(string text)
    {
        var root = Directory.CreateTempSubdirectory("fo4re-lift-");
        try
        {
            File.WriteAllText(Path.Combine(root.FullName, "Fixture.psc"), text);
            var parsed = PapyrusParser.Parse(text, "Fixture.psc");
            var compiled = new PapyrusCompiler(Index(root.FullName))
                .Compile(parsed, sourceFileName: "Fixture.psc");

            return (compiled.Pex,
                string.Join(" | ", compiled.Diagnostics.Select(d => $"{d.Code} {d.Message}")));
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }


    private (PexFile? Pex, string Report) ThroughGraph(string text)
    {
        var root = Directory.CreateTempSubdirectory("fo4re-lift-src-");
        try
        {
            File.WriteAllText(Path.Combine(root.FullName, "Fixture.psc"), text);
            var index = Index(root.FullName);

            var parsed = PapyrusParser.Parse(text, "Fixture.psc");
            var lifted = new GraphLifter(index).Lift(parsed);

            if (!lifted.Success)
            {
                return (null, "LIFT REFUSED: " + string.Join(" | ",
                    lifted.Diagnostics.Select(d => d.Code + " " + d.Message)));
            }

            var compiled = new GraphCompiler(index).Compile(lifted.Document!, new GraphCompileOptions());
            if (!compiled.Success)
            {
                return (null, "REGENERATED SOURCE DID NOT COMPILE: " + string.Join(" | ",
                    compiled.Diagnostics.Select(d => d.Code + " " + d.Message))
                    + "\n---- generated ----\n" + compiled.Source);
            }

            return (compiled.Pex, compiled.Source ?? "");
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    [Theory]
    [MemberData(nameof(Fixtures))]
    public void A_fixture_source_survives_a_trip_through_the_graph(string name)
    {
        var fixture = GraphFixtures.All.First(f => f.Name == name);

        var (original, originalDiagnostics) = CompileText(fixture.Reference);
        original.Should().NotBeNull($"the reference itself should compile; {originalDiagnostics}");

        var (rebuilt, report) = ThroughGraph(fixture.Reference);
        rebuilt.Should().NotBeNull(report);

        var difference = PexComparer.FirstDifference(original!, rebuilt!);
        if (difference != null) _output.WriteLine("REGENERATED SOURCE:\n" + report);

        difference.Should().BeNull(
            $"{name}: the graph should mean exactly what the source meant");
    }

    [Fact]
    public void A_source_the_lifter_cannot_express_is_refused_rather_than_mangled()
    {



        var text = """
            Scriptname Fixture extends ObjectReference

            Event OnLoad()
                While (IsEnabled())
                    Utility.Wait(1.0)
                EndWhile
            EndEvent
            """;

        var root = Directory.CreateTempSubdirectory("fo4re-lift-refuse-");
        try
        {
            File.WriteAllText(Path.Combine(root.FullName, "Fixture.psc"), text);
            var lifted = new GraphLifter(Index(root.FullName))
                .Lift(PapyrusParser.Parse(text, "Fixture.psc"));

            lifted.Success.Should().BeFalse("a condition that is called each pass cannot be lifted");
            lifted.Diagnostics.Should().Contain(d => d.Message.Contains("condition"),
                string.Join(" | ", lifted.Diagnostics.Select(d => d.Message)));
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    [Fact]
    public void A_source_that_does_not_parse_is_refused_without_a_document()
    {
        var lifted = new GraphLifter(Index(TestRoots.BaseStubs))
            .Lift(PapyrusParser.Parse("Scriptname Fixture extends\n Function (", "Fixture.psc"));

        lifted.Success.Should().BeFalse();
        lifted.Document.Should().BeNull("nothing should be handed back for a file that did not parse");
    }

    private enum Stage
    {
        Matched,
        ReferenceDidNotCompile,
        DecompiledToAssembly,
        DecompiledSourceDidNotParse,
        LiftRefused,
        GraphDidNotCompile,
        InstructionsDiffered,
    }

    private sealed record Outcome(string Name, Stage Stage, string Detail);

    private static string StripResultLine(string decompiled)
    {

        var newline = decompiled.IndexOf('\n');
        return newline >= 0 && decompiled.StartsWith("RESULT:", StringComparison.Ordinal)
            ? decompiled[(newline + 1)..]
            : decompiled;
    }

    private static Outcome RunOne(GraphFixture fixture)
    {
        var root = Directory.CreateTempSubdirectory("fo4re-lift-pex-");
        try
        {
            File.WriteAllText(Path.Combine(root.FullName, "Fixture.psc"), fixture.Reference);
            var index = Index(root.FullName);

            var original = new PapyrusCompiler(index)
                .Compile(PapyrusParser.Parse(fixture.Reference, "Fixture.psc"), sourceFileName: "Fixture.psc");
            if (!original.Success || original.Pex == null)
            {
                return new Outcome(fixture.Name, Stage.ReferenceDidNotCompile,
                    string.Join(" | ", original.Diagnostics.Select(d => $"{d.Code} {d.Message}")));
            }

            var pexPath = Path.Combine(root.FullName, "Fixture.pex");
            original.Pex.WriteFile(pexPath);

            var text = StripResultLine(PapyrusDecompiler.Decompile(pexPath, assembly: false));
            if (text.Contains(".code", StringComparison.OrdinalIgnoreCase))
            {
                return new Outcome(fixture.Name, Stage.DecompiledToAssembly,
                    "a body could not be structured, so that function came back as assembly");
            }

            var parsed = PapyrusParser.Parse(text, "Fixture.psc");
            var parseErrors = parsed.Diagnostics.Where(d => d.Severity == PapyrusSeverity.Error).ToList();
            if (parseErrors.Count > 0)
            {
                return new Outcome(fixture.Name, Stage.DecompiledSourceDidNotParse,
                    string.Join(" | ", parseErrors.Select(d => $"{d.Code} {d.Message}")));
            }

            File.WriteAllText(Path.Combine(root.FullName, "Fixture.psc"), text);
            var lifted = new GraphLifter(Index(root.FullName)).Lift(parsed);
            if (!lifted.Success || lifted.Document == null)
            {
                return new Outcome(fixture.Name, Stage.LiftRefused,
                    string.Join(" | ", lifted.Diagnostics.Select(d => $"{d.Code} {d.Message}")));
            }

            var rebuilt = new GraphCompiler(Index(root.FullName))
                .Compile(lifted.Document, new GraphCompileOptions());
            if (!rebuilt.Success || rebuilt.Pex == null)
            {
                return new Outcome(fixture.Name, Stage.GraphDidNotCompile,
                    string.Join(" | ", rebuilt.Diagnostics.Select(d => $"{d.Code} {d.Message}")));
            }

            var difference = PexComparer.FirstDifference(original.Pex, rebuilt.Pex, "original", "through the graph");
            return difference == null
                ? new Outcome(fixture.Name, Stage.Matched, "")
                : new Outcome(fixture.Name, Stage.InstructionsDiffered, difference);
        }
        finally
        {
            root.Delete(recursive: true);
        }
    }

    [Fact]
    public void A_compiled_binary_survives_a_trip_through_the_graph()
    {
        var outcomes = GraphFixtures.All.Select(RunOne).ToList();
        var matched = outcomes.Count(o => o.Stage == Stage.Matched);

        _output.WriteLine(
            $"PEX LIFT attempted={outcomes.Count} matched={matched} "
            + string.Join(" ", outcomes.Where(o => o.Stage != Stage.Matched)
                .GroupBy(o => o.Stage)
                .OrderBy(g => g.Key)
                .Select(g => $"{g.Key}={g.Count()}")));

        foreach (var failure in outcomes.Where(o => o.Stage != Stage.Matched))
        {
            _output.WriteLine($"  {failure.Name}: {failure.Stage}: {failure.Detail}");
        }

        matched.Should().Be(PexRoundTripBaseline,
            "the .pex round trip is pinned to its measured value, so a regression fails here; "
            + "raising the baseline is the only way to record an improvement");
    }

    private const int PexRoundTripBaseline = 28;
}
