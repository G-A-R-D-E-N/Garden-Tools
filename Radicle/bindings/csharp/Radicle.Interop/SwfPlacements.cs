using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record SwfPlacementRecord(
    int TagIndex,
    int TagCode,
    ushort Depth,
    ushort? CharacterId,
    bool InsideSprite);

// Constraint: the placement payload is patched in place so character, matrix and every later field keep their bytes.
public static class SwfPlacementCodec
{
    private static bool IsPlacement(int code) => code is 4 or 26 or 70;

    private static int DepthOffset(int code) => code switch
    {
        26 => 1,
        70 => 2,
        _ => 2,
    };

    public static SwfPlacementRecord Parse(SwfTagRecord tag, byte[] payload)
    {
        if (!IsPlacement(tag.Code)) throw new ArgumentException("the tag is not a placement", nameof(tag));
        if (payload.Length < DepthOffset(tag.Code) + 2) throw new ArgumentException("the placement payload is too short");
        var offset = DepthOffset(tag.Code);
        var depth = (ushort)(payload[offset] | (payload[offset + 1] << 8));
        ushort? characterId = null;
        if (tag.Code == 26 && payload.Length > 3 && (payload[0] & 0x02) != 0 && payload.Length >= 5)
        {
            characterId = (ushort)(payload[3] | (payload[4] << 8));
        }
        else if (tag.Code == 4 && payload.Length >= 4)
        {
            characterId = (ushort)(payload[0] | (payload[1] << 8));
        }
        else if (tag.Code == 70 && payload.Length > 4 && (payload[0] & 0x02) != 0 && payload.Length >= 6)
        {
            characterId = (ushort)(payload[4] | (payload[5] << 8));
        }
        return new SwfPlacementRecord(tag.Index, tag.Code, depth, characterId, tag.InsideSprite);
    }

    public static byte[] WithDepth(byte[] payload, int code, ushort depth)
    {
        if (!IsPlacement(code)) throw new ArgumentException("the tag is not a placement", nameof(code));
        var offset = DepthOffset(code);
        if (payload.Length < offset + 2) throw new ArgumentException("the placement payload is too short");
        var copy = (byte[])payload.Clone();
        copy[offset] = (byte)depth;
        copy[offset + 1] = (byte)(depth >> 8);
        return copy;
    }
}

public sealed partial class SwfFile
{
    // Constraint: only a decoded placement tag can be edited, and the depth patch keeps every other byte.
    public IReadOnlyList<SwfPlacementRecord> Placements() =>
        Tags.Where(tag => tag.Code is 4 or 26 or 70)
            .Select(tag => SwfPlacementCodec.Parse(tag, Payload(tag.Index)))
            .ToArray();

    public SwfPlanResultRecord SetDepth(int tagIndex, ushort depth)
    {
        var tag = Tags[tagIndex];
        var payload = SwfPlacementCodec.WithDepth(Payload(tagIndex), tag.Code, depth);
        return ApplyPlan(new SwfPlanSpec().Replace(tagIndex, payload));
    }
}