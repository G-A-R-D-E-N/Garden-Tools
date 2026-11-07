using System;
using System.Collections.Generic;
using System.IO;

namespace OpenCommonwealth.Services.Hkx;

public partial class HkxBinaryReader
{
    private static void ParseQuantizedAnimation(byte[] data, int dataAbs, int animRel,
        Dictionary<int, int> fixups, HkxAnimationData animation)
    {
        long a = (long)dataAbs + animRel;
        if (animRel < 0 || a < 0 || a + 0x58 > data.Length)
            throw new InvalidDataException("Quantized animation object is truncated.");
        int count = SafeReadI32(data, (int)a + 0x40);
        if (count == 0) return;
        if (count < 0 || !fixups.TryGetValue(animRel + 0x38, out int target))
            throw new InvalidDataException("Quantized animation data has an invalid count or missing pointer.");
        long at = (long)dataAbs + target;
        if (target < 0 || at < 0 || at > data.Length || count > data.Length - at)
            throw new InvalidDataException("Quantized animation data extends beyond the packfile.");
        if (SafeReadI32(data, (int)a + 0x48) != 0)
            throw new NotSupportedException("Quantized animation endian normalization is not implemented. The file has not been changed.");
        QuantizedAnimationDecoder.Decode(data.AsSpan((int)at, count), animation);
        if (SafeReadI32(data, (int)a + 0x18) != animation.NumTracks ||
            SafeReadI32(data, (int)a + 0x1c) != animation.FloatTracks.Count)
            throw new InvalidDataException("Quantized animation header and member track counts disagree.");
        ParseAnnotationTracks(data, dataAbs, animRel + 0x28, fixups, animation);
    }
}
