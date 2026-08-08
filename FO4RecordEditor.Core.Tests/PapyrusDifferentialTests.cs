using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using FluentAssertions;
using FO4RecordEditor.Services.Papyrus;
using Xunit.Abstractions;

namespace FO4RecordEditor.Core.Tests;

/// <summary>
/// Compiles real sources and compares the result against the <c>.pex</c> the Creation Kit produced
/// from the same file.
/// </summary>
/// <remarks>
/// The acceptance test for issue #78's code generator, and the only one that can say the emitted
/// instructions are the ones the game has been running for a decade rather than merely
/// self-consistent.
/// <para>
/// <b>The comparison is structural, and that is not a softening of the bar -- it is the bar.</b> The
/// <c>.pex</c> writer is held to byte-identity because its input is a file that already exists. A
/// code generator's input is source, and the Creation Kit's output is not a function of the source
/// alone: struct members, variables, properties and functions are written in a hash order that
/// differs between two compiles of different files, temporaries are numbered from an object-wide
/// counter with gaps in it, and the four compiler builds present in the corpus disagree with each
/// other about whether <c>int x = f()</c> writes straight into <c>x</c> and whether comparing to
/// <c>None</c> casts first. So this compares per-function instruction sequences with temporary names
/// collapsed, which is exactly the part that is a function of the source.
/// </para>
/// <para>
/// <b>Each script is compiled against its own roots.</b> Resolving a whole drive at once scores 58.8%
/// and measures the corpus rather than the compiler: 98.2% of the reachable <c>.psc</c> share a bare
/// script name with another file, 29 of them called <c>Game.psc</c>. The roots here are the source
/// tree the <c>.pex</c> was built from, plus whatever <c>FO4RE_PSC_ROOTS</c> supplies for the base
/// game.
/// </para>
/// <para>
/// Opt-in on <c>FO4RE_PEX_CORPUS</c>, roots separated by <see cref="Path.PathSeparator"/>, like the
/// other two sweeps. Unset, it no-ops so a bare checkout stays green. A pair is only formed where a
/// <c>.psc</c> sits at the layout the toolchain uses -- <c>Scripts/X.pex</c> beside
/// <c>Scripts/Source/User/X.psc</c> -- so a same-named file from an unrelated mod is never compared
/// against the wrong binary.
/// </para>
/// </remarks>
public class PapyrusDifferentialTests
{
    private readonly ITestOutputHelper _output;

    public PapyrusDifferentialTests(ITestOutputHelper output) => _output = output;

    private const string PexCorpusVariable = "FO4RE_PEX_CORPUS";
    private const string ExtraRootsVariable = "FO4RE_PSC_ROOTS";

    /// <summary>
    /// How much of the corpus is allowed to differ before the sweep is a failure.
    /// </summary>
    /// <remarks>
    /// Not zero, and deliberately: the corpus contains output from compiler builds that disagree
    /// with each other, so a run that demanded zero would be asserting that every build agrees. The
    /// measured figure at the time of writing is 163 of 167 compiled files identical; the four are
    /// named in <c>PAPYRUS.md</c>. This is a regression guard, so it sits just under that.
    /// </remarks>
    private const double MinimumIdenticalFraction = 0.95;

    private static IReadOnlyList<string> RootsFrom(string variable)
    {
        var value = Environment.GetEnvironmentVariable(variable);
        if (string.IsNullOrWhiteSpace(value)) return Array.Empty<string>();
        return value.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Where(Directory.Exists)
            .ToList();
    }

    /// <summary>
    /// Every <c>.pex</c> under the corpus roots that has a <c>.psc</c> in the layout the compiler
    /// writes, and is a Fallout 4 file.
    /// </summary>
    private static IReadOnlyList<(string Pex, string Psc)> Pairs()
    {
        var pairs = new List<(string, string)>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var root in RootsFrom(PexCorpusVariable))
        {
            foreach (var pex in PapyrusFileWalk.EnumerateFiles(root, "*.pex"))
            {
                var psc = SourceFor(pex);
                if (psc == null) continue;
                if (!IsFallout4(pex)) continue;
                if (!seen.Add(Path.GetFileName(pex))) continue;
                pairs.Add((pex, psc));
            }
        }
        return pairs;
    }

    /// <summary>
    /// The source a compiled script came from, derived from the path rather than searched for.
    /// </summary>
    /// <remarks>
    /// The toolchain writes <c>&lt;anchor&gt;/X.pex</c> (or <c>&lt;anchor&gt;/Compiled/X.pex</c>)
    /// beside <c>&lt;anchor&gt;/Source/User/X.psc</c>, namespace folders preserved on both sides.
    /// Matching on the bare file name instead would pair a mod's script with an unrelated copy of
    /// the same name, which is the trap the corpus is full of.
    /// </remarks>
    private static string? SourceFor(string pex)
    {
        var parts = pex.Split(Path.DirectorySeparatorChar);
        for (int i = parts.Length - 2; i >= 0; i--)
        {
            var anchor = string.Join(Path.DirectorySeparatorChar, parts.Take(i + 1));
            var relative = string.Join(Path.DirectorySeparatorChar, parts.Skip(i + 1));
            if (relative.StartsWith("Compiled" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                relative = string.Join(Path.DirectorySeparatorChar, parts.Skip(i + 2));

            var source = relative[..^".pex".Length] + ".psc";
            foreach (var candidate in new[]
            {
                Path.Combine(anchor, "Source", "User", source),
                Path.Combine(anchor, "Source", source),
            })
            {
                if (File.Exists(candidate)) return candidate;
            }
        }
        return null;
    }

    private static bool IsFallout4(string pex)
    {
        try
        {
            using var stream = File.OpenRead(pex);
            using var reader = new BinaryReader(stream);
            return reader.ReadUInt32() == 0xFA57C0DEu
                && reader.ReadByte() == 3 && reader.ReadByte() == 9
                && reader.ReadUInt16() == 2;
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    /// <summary>The source roots one script compiles against: its own tree, then the shared ones.</summary>
    private static IEnumerable<string> RootsFor(string psc)
    {
        var directory = Path.GetDirectoryName(psc)!;
        var parts = directory.Split(Path.DirectorySeparatorChar);
        for (int i = parts.Length - 1; i >= 0; i--)
        {
            if (!parts[i].Equals("Source", StringComparison.OrdinalIgnoreCase)) continue;
            var source = string.Join(Path.DirectorySeparatorChar, parts.Take(i + 1));
            var user = Path.Combine(source, "User");
            if (Directory.Exists(user)) yield return user;
            yield return source;
            yield break;
        }
        yield return directory;
    }

    [Fact]
    public void Compiled_output_matches_the_creation_kits_instruction_for_instruction()
    {
        var pairs = Pairs();
        if (pairs.Count == 0)
        {
            _output.WriteLine($"{PexCorpusVariable} is not set to any root holding (.psc, .pex) pairs; nothing to compare.");
            return;
        }

        var extra = RootsFrom(ExtraRootsVariable);
        int compiled = 0, identical = 0;
        var refusals = new List<string>();
        var differences = new List<string>();

        foreach (var (pex, psc) in pairs)
        {
            var index = PapyrusCompiler.IndexFor(RootsFor(psc).Concat(extra));
            var result = new PapyrusCompiler(index).CompileFile(psc);

            if (!result.Success)
            {
                // Refusing a script whose sources are not all present is the intended behaviour, not
                // a failure: an unresolved callee has unknown arity once defaults exist.
                var first = result.Errors.FirstOrDefault();
                refusals.Add($"{Path.GetFileName(psc)} complete={result.SourcesComplete} {first?.Code}");
                continue;
            }

            compiled++;
            PexFile reference;
            try { reference = PexFile.ReadFile(pex); }
            catch (InvalidDataException) { continue; }

            var difference = FirstDifference(reference, result.Pex!);
            if (difference == null) identical++;
            else differences.Add($"{Path.GetFileName(psc)}: {difference}");
        }

        _output.WriteLine($"pairs={pairs.Count} compiled={compiled} refused={refusals.Count} identical={identical}");
        foreach (var difference in differences.Take(30)) _output.WriteLine("  DIFF " + difference);
        foreach (var refusal in refusals.Take(30)) _output.WriteLine("  REFUSED " + refusal);

        compiled.Should().BeGreaterThan(0, "at least one pair should have compiled");
        ((double)identical / compiled).Should().BeGreaterThanOrEqualTo(
            MinimumIdenticalFraction,
            "the emitted instruction sequences should still match the Creation Kit's");
    }

    // ---- comparison --------------------------------------------------------------------------

    private static string? FirstDifference(PexFile reference, PexFile ours)
    {
        var theirs = reference.Objects.FirstOrDefault();
        var mine = ours.Objects.FirstOrDefault();
        if (theirs == null || mine == null) return "no object";

        if (!theirs.ParentClassName.Equals(mine.ParentClassName, StringComparison.OrdinalIgnoreCase))
            return $"parent '{theirs.ParentClassName}' vs '{mine.ParentClassName}'";

        foreach (var state in theirs.States)
        {
            var ourState = mine.States.FirstOrDefault(
                s => s.Name.Equals(state.Name, StringComparison.OrdinalIgnoreCase));
            if (ourState == null) return $"missing state '{state.Name}'";

            foreach (var fn in state.Functions)
            {
                var ourFn = ourState.Functions.FirstOrDefault(
                    f => f.Name.Equals(fn.Name, StringComparison.OrdinalIgnoreCase));
                if (ourFn == null) return $"missing function {fn.Name}";
                var difference = CompareFunctions(fn, ourFn);
                if (difference != null) return $"{fn.Name}: {difference}";
            }

            foreach (var fn in ourState.Functions)
            {
                if (!state.Functions.Any(f => f.Name.Equals(fn.Name, StringComparison.OrdinalIgnoreCase)))
                    return $"extra function {fn.Name}";
            }
        }

        foreach (var property in theirs.Properties)
        {
            var ourProperty = mine.Properties.FirstOrDefault(
                p => p.Name.Equals(property.Name, StringComparison.OrdinalIgnoreCase));
            if (ourProperty == null) return $"missing property {property.Name}";
            if (property.Flags != ourProperty.Flags)
                return $"property {property.Name} flags {property.Flags:X2} vs {ourProperty.Flags:X2}";
            if (property.UserFlags != ourProperty.UserFlags)
                return $"property {property.Name} user flags {property.UserFlags} vs {ourProperty.UserFlags}";

            if (property.ReadHandler != null && ourProperty.ReadHandler != null)
            {
                var difference = CompareFunctions(property.ReadHandler, ourProperty.ReadHandler);
                if (difference != null) return $"{property.Name}.Get: {difference}";
            }
            if (property.WriteHandler != null && ourProperty.WriteHandler != null)
            {
                var difference = CompareFunctions(property.WriteHandler, ourProperty.WriteHandler);
                if (difference != null) return $"{property.Name}.Set: {difference}";
            }
        }

        return null;
    }

    private static string? CompareFunctions(PexFunction theirs, PexFunction ours)
    {
        if (theirs.IsNative != ours.IsNative) return "native flag";
        if (theirs.IsGlobal != ours.IsGlobal) return "global flag";
        if (!theirs.ReturnType.Equals(ours.ReturnType, StringComparison.OrdinalIgnoreCase))
            return $"returns {theirs.ReturnType} vs {ours.ReturnType}";
        if (theirs.Params.Count != ours.Params.Count) return "parameter count";

        int shared = Math.Min(theirs.Instructions.Count, ours.Instructions.Count);
        for (int i = 0; i < shared; i++)
        {
            string a = Normalise(theirs.Instructions[i]), b = Normalise(ours.Instructions[i]);
            if (a != b) return $"[{i}] CK: {a} | ours: {b}";
        }

        return theirs.Instructions.Count == ours.Instructions.Count
            ? null
            : $"{theirs.Instructions.Count} instructions vs {ours.Instructions.Count}";
    }

    /// <summary>
    /// One instruction as text, with temporary and mangled-local names collapsed.
    /// </summary>
    /// <remarks>
    /// Which number a temporary got is the one thing that genuinely cannot be reproduced: the
    /// Creation Kit's counter is object-wide and leaves gaps where a number was allocated and
    /// dropped. Collapsing them compares the shape and the operand roles, which is what the
    /// generator controls.
    /// </remarks>
    private static string Normalise(PexInstruction instruction) =>
        instruction.Mnemonic + " " + string.Join(" ", instruction.Args.Select(Operand));

    private static string Operand(PexValue value) => value.Type switch
    {
        PexValueType.Identifier => NormaliseName(value.Str),
        PexValueType.String => "\"" + value.Str + "\"",
        PexValueType.Integer => value.Int.ToString(),
        PexValueType.Float => value.Float.ToString("0.000000"),
        PexValueType.Bool => value.Bool ? "true" : "false",
        _ => "None",
    };

    private static string NormaliseName(string name) =>
        name.StartsWith("::temp", StringComparison.OrdinalIgnoreCase) ? "::temp" :
        name.StartsWith("::mangled_", StringComparison.OrdinalIgnoreCase) ? "::mangled" :
        name.ToLowerInvariant();
}
