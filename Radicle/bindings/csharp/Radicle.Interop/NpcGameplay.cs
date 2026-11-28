using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record NpcFaction(FormKey Faction, int Rank);

public sealed record NpcPerk(FormKey Perk, uint Rank);

public sealed record NpcInventoryEntry(FormKey Item, int Count, bool HasExtra, FormKey? Owner, uint GlobalOrRank,
    float Condition);

public sealed record NpcAi(byte Aggression, byte Confidence, byte EnergyLevel, byte Morality, byte Mood, byte Assistance);

public sealed record NpcGameplay(FormKey Npc, uint ConfigurationFlags, uint LevelRaw, uint CalcMinLevel, uint CalcMaxLevel,
    uint TemplateFlags, bool LevelIsMultiplier, FormKey? Race, FormKey? ClassForm, FormKey? CombatStyle,
    FormKey? CrimeFaction, FormKey? TemplateBase, IReadOnlyList<FormKey> TemplateChain, IReadOnlyList<FormKey> Spells,
    IReadOnlyList<NpcFaction> Factions, IReadOnlyList<NpcPerk> Perks, NpcAi? Ai,
    (uint Health, uint ActionPoints)? CalculatedStats, IReadOnlyList<string> Stubs);

public sealed record NpcTemplateSourceResult(FormKey Source, NpcTemplateReason Reason, uint Hops, FormKey? Blocker);

public sealed record NpcPackageListResult(IReadOnlyList<FormKey>? Keys, NpcTemplateSourceResult Origin);

public sealed record NpcVoiceTypeResult(FormKey? Voice, NpcTemplateSourceResult Origin);

public sealed record GlobalValueResult(double? Value, GlobReason Reason);

public sealed unsafe partial class RadicleEnvironment
{
    public NpcGameplay NpcGameplay(Record npc)
    {
        using var use = Use(npc);
        Native.NpcGameplay* g;
        Call.Check(RadicleNative.NpcGameplayDecode(use.Pointer, npc.Handle, &g));
        try
        {
            (uint, uint)? calculated = null;
            if (g->HasCalculatedStats != 0) calculated = (g->CalculatedHealth, g->CalculatedActionPoints);
            return new(
                FormKey.Required(g->Npc, "NPC form key"), g->ConfigurationFlags, g->LevelRaw, g->CalcMinLevel, g->CalcMaxLevel,
                g->TemplateFlags, g->LevelIsMultiplier != 0,
                g->HasRace == 0 ? null : FormKey.FromNative(g->Race),
                g->HasClass == 0 ? null : FormKey.FromNative(g->ClassForm),
                g->HasCombatStyle == 0 ? null : FormKey.FromNative(g->CombatStyle),
                g->HasCrimeFaction == 0 ? null : FormKey.FromNative(g->CrimeFaction),
                g->HasTemplate == 0 ? null : FormKey.FromNative(g->TemplateBase),
                RequiredKeys(g->TemplateChain, g->TemplateChainCount),
                RequiredKeys(g->Spells, g->SpellCount),
                Call.Copy<Native.NpcFactionMembership>(g->Factions, g->FactionCount)
                    .Select(f => new NpcFaction(FormKey.Required(f.Faction, "NPC faction"), f.Rank)).ToArray(),
                Call.Copy<Native.NpcPerkRank>(g->Perks, g->PerkCount)
                    .Select(p => new NpcPerk(FormKey.Required(p.Perk, "NPC perk"), p.Rank)).ToArray(),
                g->HasAiData == 0 ? null : new NpcAi(g->Ai.Aggression, g->Ai.Confidence, g->Ai.EnergyLevel,
                    g->Ai.Morality, g->Ai.Mood, g->Ai.Assistance),
                calculated,
                Call.Copy<IntPtr>(g->Stubs, g->StubCount).Select(s => Utf8.Read(s) ?? string.Empty).ToArray());
        }
        finally { RadicleNative.NpcGameplayFree(g); }
    }

    // Constraint: rows are the NPC_ own CNTO rows in stored order; Owner, GlobalOrRank and Condition are meaningful only when HasExtra, and a list item is returned as its key.
    public IReadOnlyList<NpcInventoryEntry> NpcInventory(Record npc)
    {
        using var use = Use(npc);
        Native.NpcInventory* inventory;
        Call.Check(RadicleNative.NpcInventoryDecode(use.Pointer, npc.Handle, &inventory));
        try
        {
            return Call.Copy<Native.NpcInventoryItem>(inventory->Items, inventory->ItemCount).Select(i =>
                new NpcInventoryEntry(FormKey.Required(i.Item, "NPC inventory item"), i.Count, i.HasExtra != 0,
                    i.HasOwner == 0 ? null : FormKey.FromNative(i.Owner), i.GlobalOrRank, i.Condition)).ToArray();
        }
        finally { RadicleNative.NpcInventoryFree(inventory); }
    }

    // Constraint: Own and TemplateNpc are resolved; any other reason leaves the slot undecided at Source, with Blocker naming the link.
    public NpcTemplateSourceResult NpcTemplateSource(Record npc, NpcTemplateSlot slot)
    {
        using var use = Use(npc);
        Native.NpcTemplateSource source;
        Call.Check(RadicleNative.NpcTemplateSourceResolve(use.Pointer, npc.Handle, (uint)slot, &source));
        return SourceOf(source);
    }

    // Constraint: Keys is null when a template chain stopped (Origin names the slot's last NPC_ and blocker); otherwise PKID keys then DPLT FLST members, in stored order, not deduplicated.
    public NpcPackageListResult NpcPackageList(Record npc)
    {
        using var use = Use(npc);
        Native.FormKey* keys;
        nuint count;
        Native.NpcTemplateSource origin;
        Call.Check(RadicleNative.NpcPackageList(use.Pointer, npc.Handle, &keys, &count, &origin));
        try
        {
            var result = SourceOf(origin);
            return new(origin.Reason is NpcTemplateReason.Own or NpcTemplateReason.TemplateNpc ? RequiredKeys((IntPtr)keys, count) : null, result);
        }
        finally { RadicleNative.FreeFormKeys(keys); }
    }

    // Constraint: Value is null unless Reason is Ok; the Constant record flag is required only when constantOnly is set.
    public GlobalValueResult GlobalValue(Record glob, bool constantOnly)
    {
        using var use = Use(glob);
        double value;
        GlobReason reason;
        Call.Check(RadicleNative.GlobValue(use.Pointer, glob.Handle, (byte)(constantOnly ? 1 : 0), &value, &reason));
        return new(reason == GlobReason.Ok ? value : null, reason);
    }

    // Constraint: own VTCK first, else TPLT is followed regardless of template flags; Voice is null unless Origin.Reason is Own or TemplateNpc.
    public NpcVoiceTypeResult NpcVoiceType(Record npc)
    {
        using var use = Use(npc);
        Native.FormKey voice;
        Native.NpcTemplateSource origin;
        Call.Check(RadicleNative.NpcVoiceType(use.Pointer, npc.Handle, &voice, &origin));
        return new(FormKey.FromNative(voice), SourceOf(origin));
    }

    private static NpcTemplateSourceResult SourceOf(Native.NpcTemplateSource source) =>
        new(FormKey.Required(source.Source, "NPC template source"), source.Reason, source.Hops,
            source.HasBlocker == 0 ? null : FormKey.FromNative(source.Blocker));

    private static FormKey[] RequiredKeys(IntPtr items, nuint count) =>
        Call.Copy<Native.FormKey>(items, count).Select(key => FormKey.Required(key, "NPC form list entry")).ToArray();
}
