using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed class NifCollisionOwnerInfo
{
    public required int OwnerBlock { get; init; }

    public required ushort Flags { get; init; }

    public required uint BodyId { get; init; }

    public required int TargetBlock { get; init; }

    public required int TargetNodeIndex { get; init; }

    public NifNode? TargetNode { get; init; }

    // Invariant: the target node chain without the root, in engine axes and metres; identity when the target is not a node or is the root.
    public PlainCollisionTransform Transform { get; init; } = PlainCollisionTransform.Identity;
}

internal static class NifCollisionOwners
{
    // Invariant: one entry per bhkNPCollisionObject naming the system, in block order.
    internal static unsafe NifCollisionOwnerInfo[] Read(IntPtr handle, nuint system, NifNode[] nodes)
    {
        nuint ownerCount = 0;
        Call.Check(RadicleNative.NifCollisionOwnerCount(handle, system, &ownerCount));
        var owners = new NifCollisionOwnerInfo[checked((int)ownerCount)];
        for (var at = 0; at < owners.Length; ++at)
        {
            NifCollisionOwner entry;
            Call.Check(RadicleNative.NifCollisionOwnerRead(handle, system, (nuint)at, &entry));
            owners[at] = new NifCollisionOwnerInfo {
                OwnerBlock = entry.OwnerBlock,
                Flags = entry.Flags,
                BodyId = entry.BodyId,
                TargetBlock = entry.TargetBlock,
                TargetNodeIndex = entry.TargetNode,
                TargetNode = entry.TargetNode >= 0 && entry.TargetNode < nodes.Length ? nodes[entry.TargetNode] : null,
                Transform = ReadTransform(handle, system, (nuint)at),
            };
        }
        return owners;
    }

    private static unsafe PlainCollisionTransform ReadTransform(IntPtr handle, nuint system, nuint owner)
    {
        var translation = stackalloc float[3];
        var rotation = stackalloc float[9];
        float scale = 1f;
        Call.Check(RadicleNative.NifCollisionOwnerTransform(handle, system, owner, translation, rotation, &scale));
        return PlainCollisionTransform.FromBuffers(translation, rotation, scale);
    }
}
