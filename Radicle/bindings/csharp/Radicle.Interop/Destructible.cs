using System;
using System.Collections.Generic;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

// Constraint: <summary>One subrecord of a destructible stage, copied out of the plugin.</summary>
public sealed record DestructibleGroupMember(string Signature, byte[] Data);

// Constraint: <summary>One DSTD..DSTF run, in record order.</summary>
public sealed record DestructibleGroupStage(IReadOnlyList<DestructibleGroupMember> Members);

// Constraint: <summary>One DAMC entry: the damage-type link and its resistance value.</summary>
public readonly record struct DestructibleGroupResistance(FormKey? DamageType, uint Value);

// Constraint: <summary>The DEST header: health, the declared stage count and the flags byte.</summary>
public readonly record struct DestructibleGroupHeader(int Health, byte StageCount, byte Flags);

// Constraint: <summary>A whole DEST group whose payload bytes are managed copies, not plugin memory.</summary>
public sealed record DestructibleGroup(DestructibleGroupHeader Header,
    IReadOnlyList<DestructibleGroupResistance> Resistances, IReadOnlyList<DestructibleGroupStage> Stages);

// Constraint: <summary>A group read: <see cref="Group"/> is set only for <see cref="DestructibleStageStatus.Present"/>.</summary>
public sealed record DestructibleGroupRead(DestructibleStageStatus Status, DestructibleGroup? Group)
{
    public bool Present => Group is not null;
}

// Constraint: <summary>The destructible group read and stage mutators over <c>fo4/c_api_destructible.h</c>.</summary>
public sealed unsafe partial class PluginFile
{
// Constraint: <summary>Creates the whole DEST group; <see cref="DestructibleHeaderStatus.Present"/> when it is already there.</summary>
    public DestructibleHeaderStatus InsertDestructibleHeader(FormKey record)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        DestructibleHeaderStatus status;
        Call.Check(RadicleNative.PluginInsertDestructibleHeader(use.Pointer, strings.Key(record), &status));
        return status;
    }

// Constraint: <summary>Drops the DEST group; <see cref="DestructibleHeaderStatus.Missing"/> when there is none.</summary>
    public DestructibleHeaderStatus RemoveDestructibleHeader(FormKey record)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        DestructibleHeaderStatus status;
        Call.Check(RadicleNative.PluginRemoveDestructibleHeader(use.Pointer, strings.Key(record), &status));
        return status;
    }

    /// <summary>Adds one empty DSTD..DSTF run at <paramref name="index"/>; the header must exist.</summary>
    public DestructibleStageStatus InsertDestructibleStage(FormKey record, int index)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        DestructibleStageStatus status;
        Call.Check(RadicleNative.PluginInsertDestructibleStage(use.Pointer, strings.Key(record), Index(index), &status));
        return status;
    }

// Constraint: <summary>Drops one stage run at <paramref name="index"/>.</summary>
    public DestructibleStageStatus RemoveDestructibleStage(FormKey record, int index)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        DestructibleStageStatus status;
        Call.Check(RadicleNative.PluginRemoveDestructibleStage(use.Pointer, strings.Key(record), Index(index), &status));
        return status;
    }

// Constraint: <summary>Adds one optional stage member (<c>DSTA</c>/<c>DMDL</c>/<c>DMDT</c>/<c>DMDC</c>/<c>DMDS</c>).</summary>
    public DestructibleStageMemberStatus InsertDestructibleStageMember(FormKey record, int stageIndex, string member)
    {
        ArgumentNullException.ThrowIfNull(member);
        using var use = Use();
        using var strings = new NativeStrings();
        DestructibleStageMemberStatus status;
        Call.Check(RadicleNative.PluginInsertDestructibleStageMember(
            use.Pointer, strings.Key(record), Index(stageIndex), member, &status));
        return status;
    }

// Constraint: <summary> Reads the whole DEST group in one native call. The group arrays and payload bytes are copied out before the native owner is freed, and a refusal returns no partial group. </summary>
    public DestructibleGroupRead ReadDestructibleGroup(FormKey record)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.DestructibleGroup native;
        DestructibleStageStatus status;
        Call.Check(RadicleNative.PluginDestructibleGroupRead(
            use.Pointer, strings.Key(record), &native, &status));
        try
        {
            if (status != DestructibleStageStatus.Present) return new(status, null);

            var resistances = new DestructibleGroupResistance[checked((int)native.ResistanceCount)];
            var resistanceRows = (Native.DestructibleResistance*)native.Resistances;
            for (var i = 0; i < resistances.Length; ++i)
                resistances[i] = new(FormKey.FromNative(resistanceRows[i].DamageType), resistanceRows[i].Value);

            var stageRows = (Native.DestructibleStage*)native.Stages;
            var stages = new DestructibleGroupStage[checked((int)native.StageCount)];
            for (var stage = 0; stage < stages.Length; ++stage)
            {
                var memberRows = (Native.DestructibleMember*)stageRows[stage].Members;
                var members = new DestructibleGroupMember[checked((int)stageRows[stage].MemberCount)];
                for (var member = 0; member < members.Length; ++member)
                {
                    members[member] = new DestructibleGroupMember(
                        Encoding.Latin1.GetString(memberRows[member].Signature, 4),
                        Utf8.Raw(memberRows[member].Data, memberRows[member].Size));
                }
                stages[stage] = new DestructibleGroupStage(members);
            }

            var header = new DestructibleGroupHeader(native.Header.Health, native.Header.StageCount, native.Header.Flags);
            return new(DestructibleStageStatus.Present, new DestructibleGroup(header, resistances, stages));
        }
        finally
        {
            RadicleNative.FreeDestructibleGroup(&native);
        }
    }

// Constraint: <summary>Drops one optional stage member.</summary>
    public DestructibleStageMemberStatus RemoveDestructibleStageMember(FormKey record, int stageIndex, string member)
    {
        ArgumentNullException.ThrowIfNull(member);
        using var use = Use();
        using var strings = new NativeStrings();
        DestructibleStageMemberStatus status;
        Call.Check(RadicleNative.PluginRemoveDestructibleStageMember(
            use.Pointer, strings.Key(record), Index(stageIndex), member, &status));
        return status;
    }
}
