using System;
using System.Collections.Generic;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum ScriptCandidateKind
{
    Archive = 0,
    Loose = 1,
}

public readonly record struct ScriptCandidateRow(string Name, ScriptCandidateKind Kind, string Container, int LoadIndex, long Size);

// Invariant: CandidateIndex indexes the list handed to Resolve; Replaced counts the candidates the winner beat.
public sealed record ScriptWinnerRow(string Name, int CandidateIndex, int Replaced);

// Invariant: a script name is the lower-case Data-relative path under Scripts/ without the .pex extension.
public static unsafe class ScriptNames
{
    static ScriptNames() => RadicleLibrary.Register();

    // Invariant: InvalidArgument refuses this one name with ArgumentException; every other failure is a RadicleException.
    public static string Normalize(string relativePath)
    {
        ArgumentNullException.ThrowIfNull(relativePath);
        var input = Encoding.UTF8.GetBytes(relativePath.Replace('\\', '/'));
        byte* output = null;
        nuint size = 0;
        Status status;
        fixed (byte* bytes = input) status = RadicleNative.ScriptNormalizeName(bytes, (nuint)input.Length, &output, &size);
        if (status == Status.InvalidArgument)
            throw new ArgumentException($"Radicle refused the script name {relativePath}: {Utf8.Read(RadicleNative.LastError())}", nameof(relativePath));
        Call.Check(status);
        return Call.TakeText(output, size);
    }
}

// Invariant: loose beats archive, then the higher load index, then the ordinal container.
public static unsafe class ScriptSelection
{
    static ScriptSelection() => RadicleLibrary.Register();

    public static IReadOnlyList<ScriptWinnerRow> Resolve(IReadOnlyList<ScriptCandidateRow> candidates)
    {
        ArgumentNullException.ThrowIfNull(candidates);
        var rows = new ScriptCandidate[candidates.Count];
        using var strings = new NativeStrings();
        for (var i = 0; i < rows.Length; i++)
        {
            var candidate = candidates[i];
            var name = candidate.Name ?? string.Empty;
            var container = candidate.Container ?? string.Empty;
            rows[i] = new ScriptCandidate
            {
                Name = strings.Add(name),
                NameSize = (nuint)Encoding.UTF8.GetByteCount(name),
                Kind = (byte)candidate.Kind,
                Container = strings.Add(container),
                ContainerSize = (nuint)Encoding.UTF8.GetByteCount(container),
                LoadIndex = candidate.LoadIndex,
                Size = (ulong)Math.Max(0, candidate.Size),
            };
        }
        ScriptWinner* winners = null;
        nuint count = 0;
        fixed (ScriptCandidate* pointer = rows)
            Call.Check(RadicleNative.ScriptResolve(pointer, (nuint)rows.Length, &winners, &count));
        try
        {
            var result = new ScriptWinnerRow[checked((int)count)];
            for (var i = 0; i < result.Length; i++)
            {
                var index = checked((int)winners[i].CandidateIndex);
                result[i] = new ScriptWinnerRow(candidates[index].Name ?? string.Empty, index, checked((int)winners[i].Replaced));
            }
            return result;
        }
        finally
        {
            if (winners != null) RadicleNative.ScriptFreeWinners(winners);
        }
    }
}
