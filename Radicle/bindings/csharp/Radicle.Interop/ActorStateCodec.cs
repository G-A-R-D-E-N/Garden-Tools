using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

// Constraint: bits other than these are carried as written.
[Flags]
public enum ActorStateFlags : uint
{
    None = 0,
    Enabled = 1,
    Deleted = 2,
    Dead = 4,
}

public sealed record ActorReference(ActorRefKind Kind, FormKey? Form, uint CreatedId, FormKey? Base, ulong SpawnSeed);

public sealed record ActorInventoryEntry(FormKey? Item, int Count);

public sealed record ActorValueModifierEntry(string ActorValue, float Value);

public sealed record ActorFactionEntry(FormKey? Faction, int Rank);

public sealed record ActorStateRecord(ActorReference Ref, ActorStateFlags Flags, FormKey? Cell,
    (float X, float Y, float Z) Position, (float X, float Y, float Z) Rotation, float Scale,
    IReadOnlyList<ActorInventoryEntry> Inventory, IReadOnlyList<ActorValueModifierEntry> ActorValues,
    IReadOnlyList<ActorFactionEntry> Factions, FormKey? ForcedPackage, FormKey? CurrentPackage,
    ActorReference? CombatTarget, int Bounty, ushort SpawnLevel, byte[] UnknownTail);

public static unsafe class ActorStateCodec
{
    // Invariant: these mirror RADICLE_ACTOR_STATE_FORMAT_VERSION, _MAX_STRING_BYTES and _MAX_LIST_ENTRIES.
    public const uint FormatVersion = 1;
    public const int MaxStringBytes = 512;
    public const int MaxListEntries = 4096;

    static ActorStateCodec() => RadicleLibrary.Register();

    public static byte[] Encode(ActorStateRecord record)
    {
        using var strings = new NativeStrings();
        var inventory = record.Inventory.Select(entry => new Native.ActorInventoryDelta { Item = ToNative(strings, entry.Item), Count = entry.Count }).ToArray();
        var values = record.ActorValues.Select(entry => ToNativeValue(strings, entry)).ToArray();
        var factions = record.Factions.Select(entry => new Native.ActorFactionRank { Faction = ToNative(strings, entry.Faction), Rank = entry.Rank }).ToArray();
        var state = new Native.ActorState
        {
            Ref = ToNative(strings, record.Ref),
            Flags = (uint)record.Flags,
            Cell = ToNative(strings, record.Cell),
            Scale = record.Scale,
            Inventory = strings.AddArray(inventory),
            InventoryCount = (nuint)inventory.Length,
            ActorValues = strings.AddArray(values),
            ActorValueCount = (nuint)values.Length,
            Factions = strings.AddArray(factions),
            FactionCount = (nuint)factions.Length,
            ForcedPackage = ToNative(strings, record.ForcedPackage),
            CurrentPackage = ToNative(strings, record.CurrentPackage),
            HasCombatTarget = record.CombatTarget is null ? 0 : 1,
            Bounty = record.Bounty,
            SpawnLevel = record.SpawnLevel,
            UnknownTail = strings.AddArray(record.UnknownTail),
            UnknownTailSize = (nuint)record.UnknownTail.Length,
        };
        state.Position[0] = record.Position.X;
        state.Position[1] = record.Position.Y;
        state.Position[2] = record.Position.Z;
        state.Rotation[0] = record.Rotation.X;
        state.Rotation[1] = record.Rotation.Y;
        state.Rotation[2] = record.Rotation.Z;
        if (record.CombatTarget is { } target) state.CombatTarget = ToNative(strings, target);
        byte* bytes;
        nuint size;
        Call.Check(RadicleNative.ActorStateEncode(&state, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    public static ActorStateRecord Decode(ReadOnlySpan<byte> blob)
    {
        IntPtr decoded;
        fixed (byte* pointer = blob)
            Call.Check(RadicleNative.ActorStateDecode(pointer, (nuint)blob.Length, &decoded));
        try
        {
            Native.ActorState state;
            Call.Check(RadicleNative.ActorStateView(decoded, &state));
            return new(FromNative(state.Ref), (ActorStateFlags)state.Flags, FromNative(state.Cell),
                (state.Position[0], state.Position[1], state.Position[2]),
                (state.Rotation[0], state.Rotation[1], state.Rotation[2]), state.Scale,
                Call.Copy<Native.ActorInventoryDelta>(state.Inventory, state.InventoryCount)
                    .Select(entry => new ActorInventoryEntry(FromNative(entry.Item), entry.Count)).ToArray(),
                Call.Copy<Native.ActorValueModifier>(state.ActorValues, state.ActorValueCount)
                    .Select(entry => new ActorValueModifierEntry(Text(entry.ActorValue, entry.ActorValueSize), entry.Value)).ToArray(),
                Call.Copy<Native.ActorFactionRank>(state.Factions, state.FactionCount)
                    .Select(entry => new ActorFactionEntry(FromNative(entry.Faction), entry.Rank)).ToArray(),
                FromNative(state.ForcedPackage), FromNative(state.CurrentPackage),
                state.HasCombatTarget == 0 ? null : FromNative(state.CombatTarget), state.Bounty, state.SpawnLevel,
                Utf8.Raw(state.UnknownTail, state.UnknownTailSize));
        }
        finally { RadicleNative.ActorStateFree(decoded); }
    }

    private static string Text(IntPtr text, nuint size) => Encoding.UTF8.GetString(Utf8.Raw(text, size));

    private static FormKey? FromNative(Native.ActorFormKey key) =>
        key.PluginSize == 0 ? null : new FormKey(Text(key.Plugin, key.PluginSize), key.LocalId);

    private static ActorReference FromNative(Native.ActorRef reference) =>
        new(reference.Kind, FromNative(reference.Form), reference.CreatedId, FromNative(reference.Base), reference.SpawnSeed);

    private static Native.ActorFormKey ToNative(NativeStrings strings, FormKey? key)
    {
        if (key is not { } value) return default;
        var bytes = Encoding.UTF8.GetBytes(value.Plugin);
        return new Native.ActorFormKey { Plugin = strings.AddArray(bytes), PluginSize = (nuint)bytes.Length, LocalId = value.LocalId };
    }

    private static Native.ActorValueModifier ToNativeValue(NativeStrings strings, ActorValueModifierEntry entry)
    {
        var bytes = Encoding.UTF8.GetBytes(entry.ActorValue);
        return new Native.ActorValueModifier { ActorValue = strings.AddArray(bytes), ActorValueSize = (nuint)bytes.Length, Value = entry.Value };
    }

    private static Native.ActorRef ToNative(NativeStrings strings, ActorReference reference) => new()
    {
        Kind = reference.Kind,
        Form = ToNative(strings, reference.Form),
        CreatedId = reference.CreatedId,
        Base = ToNative(strings, reference.Base),
        SpawnSeed = reference.SpawnSeed,
    };
}
