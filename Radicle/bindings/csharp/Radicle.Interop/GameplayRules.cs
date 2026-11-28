using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record ActorLevelResult(uint Level, uint RawLevel, LevelReason Reason, uint ZoneLevel);

public sealed record CombatProfileResult(FormKey Npc, FormKey? Style, CombatStyleSource Source);

public sealed record CrimeValuesResult(byte Arrest, byte AttackOnSight, ushort Murder, ushort Assault, ushort Trespass,
    ushort Pickpocket, ushort Unknown);

public sealed record CrimeRulesResult(FormKey Faction, CrimeValuesResult? Values, float? StealMultiplier, ushort? Escape,
    ushort? Werewolf, FormKey? SharedCrimeList);

public sealed unsafe partial class RadicleEnvironment
{
    // Constraint: a Reason other than Fixed means the level is not proven; the encounter zone key must name an ECZN.
    public ActorLevelResult ActorLevelForSpawn(Record npc, uint playerLevel, int levelModifier, FormKey? encounterZone = null)
    {
        using var use = Use(npc);
        using var strings = new NativeStrings();
        var zone = strings.Key(encounterZone);
        Native.ActorLevel level;
        Call.Check(RadicleNative.ActorLevelForSpawn(use.Pointer, npc.Handle, playerLevel, levelModifier,
            encounterZone is null ? null : &zone, &level));
        return new(level.Level, level.RawLevel, level.Reason, level.ZoneLevel);
    }

    // Constraint: AbsentFallbackUnproven means the NPC stores no combat style and Style is null.
    public CombatProfileResult CombatProfile(Record npc)
    {
        using var use = Use(npc);
        Native.CombatProfile profile;
        Call.Check(RadicleNative.CombatProfileDecode(use.Pointer, npc.Handle, &profile));
        return new(FormKey.Required(profile.Npc, "combat profile NPC"),
            profile.HasStyle == 0 ? null : FormKey.FromNative(profile.Style), profile.Source);
    }

    public CrimeRulesResult CrimeRules(Record faction)
    {
        using var use = Use(faction);
        Native.CrimeRules rules;
        Call.Check(RadicleNative.CrimeRulesDecode(use.Pointer, faction.Handle, &rules));
        return new(FormKey.Required(rules.Faction, "crime rules faction"),
            rules.HasCrimeValues == 0
                ? null
                : new CrimeValuesResult(rules.Arrest, rules.AttackOnSight, rules.Murder, rules.Assault, rules.Trespass,
                    rules.Pickpocket, rules.Unknown),
            rules.HasStealMultiplier == 0 ? null : rules.StealMultiplier,
            rules.HasEscape == 0 ? null : rules.Escape,
            rules.HasWerewolf == 0 ? null : rules.Werewolf,
            rules.HasSharedCrimeList == 0 ? null : FormKey.FromNative(rules.SharedCrimeList));
    }

    // Constraint: the owner is a faction or an actor; the actor passes the memberships it holds and needs at least requiredRank in the owner faction.
    public bool OwnershipAllowed(FormKey owner, FormKey actor, IReadOnlyList<NpcFaction> actorFactions, int requiredRank)
    {
        ArgumentNullException.ThrowIfNull(actorFactions);
        using var use = Use();
        using var strings = new NativeStrings();
        var members = actorFactions.Select(f => new Native.NpcFactionMembership { Faction = strings.Key(f.Faction), Rank = f.Rank }).ToArray();
        byte allowed;
        fixed (Native.NpcFactionMembership* memberPointer = members)
            Call.Check(RadicleNative.OwnershipCheck(use.Pointer, strings.Key(owner), strings.Key(actor), memberPointer,
                (nuint)members.Length, requiredRank, &allowed));
        return allowed != 0;
    }
}
