using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using FluentAssertions;
using FO4RecordEditor.Services.Papyrus;
using Xunit.Abstractions;

namespace FO4RecordEditor.Core.Tests;

/// <summary>
/// The duplicate declaration check, run over every real script on the roots.
/// </summary>
/// <remarks>
/// A refusal added to a compiler is only as good as the evidence it refuses nothing real. The stub
/// tree cannot supply that: it is 33 hand-written declarations chosen to make the graph gate run on
/// a bare checkout. This points the check at the shipped sources instead, where a single false
/// positive would mean a script the Creation Kit compiles and this one does not.
/// <para>
/// Gated on <c>FO4RE_PSC_ROOTS</c> and silent without it, like the other corpus sweeps, so a bare
/// checkout still runs green. The residual risk is the usual one: that nobody runs the gated sweep.
/// </para>
/// </remarks>
public class VanillaSweep
{
    private readonly ITestOutputHelper _output;

    public VanillaSweep(ITestOutputHelper output) => _output = output;

    [Fact]
    public void The_duplicate_check_refuses_nothing_that_ships()
    {
        var roots = TestRoots.RootsFrom(TestRoots.RealScriptRootsVariable);
        if (roots.Count == 0)
        {
            _output.WriteLine($"SKIP: {TestRoots.RealScriptRootsVariable} is not set.");
            return;
        }

        var files = roots
            .SelectMany(root => Directory.GetFiles(root, "*.psc", SearchOption.AllDirectories))
            .OrderBy(path => path, StringComparer.Ordinal)
            .ToList();

        var offenders = new List<string>();
        int parsed = 0;
        int unparseable = 0;

        foreach (var path in files)
        {
            PapyrusScript script;
            try
            {
                script = PapyrusParser.Parse(File.ReadAllText(path), Path.GetFileName(path));
            }
            catch (Exception e)
            {
                // Counted rather than failed: a source this parser cannot read is a separate
                // question from whether the duplicate check is too strict.
                unparseable++;
                _output.WriteLine($"UNPARSEABLE {Path.GetFileName(path)}: {e.GetType().Name}");
                continue;
            }

            if (script.HasErrors) { unparseable++; continue; }
            parsed++;

            foreach (var problem in PapyrusDeclarationCheck.Check(script))
                offenders.Add($"{Path.GetFileName(path)}: {problem.Message}");
        }

        _output.WriteLine(
            $"SWEEP roots={roots.Count} files={files.Count} parsed={parsed} "
            + $"unparseable={unparseable} flagged={offenders.Count}");
        foreach (var line in offenders.Take(40)) _output.WriteLine("  FLAGGED " + line);

        parsed.Should().BeGreaterThan(0, "the sweep is pointless if it read nothing");
        offenders.Should().BeEmpty(
            "a shipped script the Creation Kit compiles must not be refused here");
    }
}
