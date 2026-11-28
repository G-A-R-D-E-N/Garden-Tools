using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record ActorValueBaseOutcome(int Base, AvSource Source, AvEvidence Evidence);

public sealed unsafe partial class RadicleEnvironment
{
    public ActorValueBaseOutcome ActorValueBase(Record npc, FormKey actorValue)
    {
        using var use = Use(npc);
        using var strings = new NativeStrings();
        var result = default(Native.ActorValueBaseResult);
        Call.Check(RadicleNative.ActorValueBase(use.Pointer, npc.Handle, strings.Key(actorValue), &result));
        return new(result.Base, result.Source, result.Evidence);
    }
}
