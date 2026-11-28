using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record FactionReactionOutcome(CombatReaction Reaction, ReactionReason Reason,
    FormKey? DeciderActorFaction, FormKey? DeciderTargetFaction);

public sealed unsafe partial class RadicleEnvironment
{
    public FactionReactionOutcome FactionReaction(IReadOnlyList<FormKey> actorFactions, IReadOnlyList<FormKey> targetFactions)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        var actors = actorFactions.Select(faction => strings.Key(faction)).ToArray();
        var targets = targetFactions.Select(faction => strings.Key(faction)).ToArray();
        var result = default(Native.FactionReactionResult);
        fixed (Native.FormKey* actorPointer = actors)
        fixed (Native.FormKey* targetPointer = targets)
            Call.Check(RadicleNative.FactionReaction(use.Pointer, actorPointer, (nuint)actors.Length,
                targetPointer, (nuint)targets.Length, &result));
        return new(result.Reaction, result.Reason,
            FormKey.FromNative(result.DeciderActorFaction), FormKey.FromNative(result.DeciderTargetFaction));
    }
}
