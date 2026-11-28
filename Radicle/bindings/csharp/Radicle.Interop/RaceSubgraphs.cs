using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record RaceSubgraphRow(IReadOnlyList<FormKey?> ActorKeywords, IReadOnlyList<FormKey?> TargetKeywords,
    string? BehaviorGraph, IReadOnlyList<string> PathPrefixes, ushort Role, ushort Perspective);

public sealed unsafe partial class RadicleEnvironment
{
    public IReadOnlyList<RaceSubgraphRow> RaceSubgraphs(Record race)
    {
        using var use = Use(race);
        Native.RaceSubgraphs* s;
        Call.Check(RadicleNative.RaceSubgraphsDecode(use.Pointer, race.Handle, &s));
        try
        {
            return Call.Copy<Native.RaceSubgraph>(s->Rows, s->Count).Select(r => new RaceSubgraphRow(
                Call.Copy<Native.FormKey>(r.ActorKeywords, r.ActorKeywordCount).Select(FormKey.FromNative).ToArray(),
                Call.Copy<Native.FormKey>(r.TargetKeywords, r.TargetKeywordCount).Select(FormKey.FromNative).ToArray(),
                Utf8.Read(r.BehaviorGraph),
                Call.Copy<IntPtr>(r.PathPrefixes, r.PathPrefixCount).Select(p => Utf8.Read(p) ?? string.Empty).ToArray(),
                r.Role, r.Perspective)).ToArray();
        }
        finally { RadicleNative.RaceSubgraphsFree(s); }
    }

    public IReadOnlyList<FormKey?> FurnitureKeywords(Record furniture)
    {
        using var use = Use(furniture);
        Native.FurnitureKeywords* k;
        Call.Check(RadicleNative.FurnitureKeywordsDecode(use.Pointer, furniture.Handle, &k));
        try { return Call.Copy<Native.FormKey>(k->Keywords, k->Count).Select(FormKey.FromNative).ToArray(); }
        finally { RadicleNative.FurnitureKeywordsFree(k); }
    }
}
