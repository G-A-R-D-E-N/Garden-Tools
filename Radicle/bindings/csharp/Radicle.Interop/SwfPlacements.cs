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
        _ => 2,
    };

    internal static int HeaderEnd(int code, byte[] payload, out ushort? characterId)
    {
        characterId = null;
        switch (code)
        {
            case 4:
                if (payload.Length < 4) throw new ArgumentException("the placement payload is too short");
                characterId = (ushort)(payload[0] | (payload[1] << 8));
                return 4;
            case 26:
            {
                if (payload.Length < 3) throw new ArgumentException("the placement payload is too short");
                var offset = 3;
                if ((payload[0] & 0x02) != 0)
                {
                    if (payload.Length < offset + 2) throw new ArgumentException("the placement payload is too short");
                    characterId = (ushort)(payload[offset] | (payload[offset + 1] << 8));
                    offset += 2;
                }
                return offset;
            }
            case 70:
            {
                if (payload.Length < 4) throw new ArgumentException("the placement payload is too short");
                var hasCharacter = (payload[0] & 0x02) != 0;
                var hasClassName = (payload[1] & 0x08) != 0 || ((payload[1] & 0x10) != 0 && !hasCharacter);
                var offset = 4;
                if (hasClassName)
                {
                    var terminator = Array.IndexOf(payload, (byte)0, offset);
                    if (terminator < 0) throw new ArgumentException("the placement payload is too short");
                    offset = terminator + 1;
                }
                if (hasCharacter)
                {
                    if (payload.Length < offset + 2) throw new ArgumentException("the placement payload is too short");
                    characterId = (ushort)(payload[offset] | (payload[offset + 1] << 8));
                    offset += 2;
                }
                return offset;
            }
            default:
                throw new ArgumentException("the tag is not a placement");
        }
    }

    public static SwfPlacementRecord Parse(SwfTagRecord tag, byte[] payload)
    {
        if (!IsPlacement(tag.Code)) throw new ArgumentException("the tag is not a placement", nameof(tag));
        var offset = DepthOffset(tag.Code);
        if (payload.Length < offset + 2) throw new ArgumentException("the placement payload is too short");
        var depth = (ushort)(payload[offset] | (payload[offset + 1] << 8));
        HeaderEnd(tag.Code, payload, out var characterId);
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
