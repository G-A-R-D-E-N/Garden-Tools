using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public partial class HkxBinaryReader
{
    private static void ParsePredictiveAnimation(byte[] data, int dataAbs, int animRel,
        Dictionary<int, int> fixups, HkxAnimationData animation)
    {
        long objectAt = (long)dataAbs + animRel;
        if (animRel < 0 || objectAt < 0 || objectAt + 0xc0 > data.Length)
            throw new InvalidDataException("Predictive animation object is truncated.");
        int a = (int)objectAt;
        int frames = SafeReadI32(data, a + 0xa4), bones = SafeReadI32(data, a + 0x9c);
        int floats = SafeReadI32(data, a + 0xa0);
        if (frames == 0 && bones == 0 && floats == 0) return;
        if (frames <= 0 || bones < 0 || floats < 0 || (long)frames * (bones + (long)floats) > MaxFrameTracks)
            throw new InvalidDataException("Predictive animation has invalid or excessive frame/track counts.");
        if (SafeReadI32(data, a + 0x18) != bones || SafeReadI32(data, a + 0x1c) != floats)
            throw new InvalidDataException("Predictive animation base and codec track counts disagree.");
        var regions = new List<(long Start, long End)> { (objectAt, objectAt + 0xc0) };
        int Array(int member, int size, out int count)
        {
            count = SafeReadI32(data, a + member + 8);
            if (count < 0) throw new InvalidDataException("Predictive animation array count is negative.");
            if (count == 0) return 0;
            if (!fixups.TryGetValue(animRel + member, out int target))
                throw new InvalidDataException("Predictive animation array pointer is missing.");
            long start = (long)dataAbs + target, bytes = (long)count * size;
            if (target < 0 || start < 0 || start > data.Length || bytes > data.Length - start)
                throw new InvalidDataException("Predictive animation array is truncated.");
            foreach (var region in regions)
                if (start < region.End && start + bytes > region.Start)
                    throw new InvalidDataException("Predictive animation arrays overlap.");
            regions.Add((start, start + bytes));
            return (int)start;
        }
        int compressedAt = Array(0x38, 1, out int compressedCount);
        int intAt = Array(0x48, 2, out int intCount);
        int floatAt = Array(0x80, 4, out int floatCount);
        var ints = new ushort[intCount];
        for (int i = 0; i < intCount; i++) ints[i] = BitConverter.ToUInt16(data, intAt + 2 * i);
        var offsets = new int[10];
        for (int i = 0; i < 9; i++) offsets[i] = SafeReadI32(data, a + 0x58 + 4 * i);
        offsets[9] = intCount;
        for (int i = 0; i < 9; i++)
            if (offsets[i] < 0 || offsets[i] > offsets[i + 1])
                throw new InvalidDataException("Predictive animation integer section offsets are invalid.");
        var floatOffsets = new int[4];
        for (int i = 0; i < 3; i++) floatOffsets[i] = SafeReadI32(data, a + 0x90 + 4 * i);
        floatOffsets[3] = floatCount;
        for (int i = 0; i < 3; i++)
            if (floatOffsets[i] < 0 || floatOffsets[i] > floatOffsets[i + 1])
                throw new InvalidDataException("Predictive animation float section offsets are invalid.");
        if (offsets[2] != offsets[1] || SafeReadI32(data, a + 0xa8) != 0)
            throw PredictiveGap("float/bone boundary sampling");
        int dynamicCount = offsets[5] - offsets[4];
        if (floats != 0 || offsets[6] != offsets[5])
            throw PredictiveGap("float-track and float/bone boundary sampling");
        if (offsets[8] != offsets[6] || floatOffsets[1] != floatOffsets[0])
            throw PredictiveGap("static track index walks");
        if (offsets[9] != offsets[8]) throw PredictiveGap("quaternion renormalization index consumption");
        if (bones == 0 || dynamicCount == 0) throw PredictiveGap("reference/default pose reconstruction");
        int groups = (dynamicCount + 15) / 16;
        if (offsets[4] - offsets[3] < groups)
            throw new InvalidDataException("Predictive animation fixed-range bitmap is truncated.");
        if (offsets[3] - offsets[2] < groups)
            throw new InvalidDataException("Predictive animation animated bitmap is truncated.");
        bool fixedOnly = true, rangedOnly = true;
        for (int i = 0; i < dynamicCount; i++)
        {
            bool fixedRange = (ints[offsets[3] + i / 16] & (1 << (i % 16))) != 0;
            fixedOnly &= fixedRange;
            rangedOnly &= !fixedRange;
        }
        if (!fixedOnly && !rangedOnly) throw PredictiveGap("mixed fixed/ranged scale-array cursor association");
        int scalesCount = floatOffsets[2] - floatOffsets[1];
        int biasCount = floatOffsets[3] - floatOffsets[2];
        if (!fixedOnly && (scalesCount < dynamicCount || biasCount < dynamicCount))
            throw new InvalidDataException(
                "Predictive animation layout assumption violated: fewer dynamic scales or offsets than dynamic lanes.");
        var seen = new bool[12 * bones];
        for (int i = 0; i < dynamicCount; i++)
        {
            int slot = ints[offsets[4] + i];
            if (slot >= seen.Length || seen[slot])
                throw new InvalidDataException("Predictive animation dynamic scatter index is invalid or duplicated.");
            seen[slot] = true;
        }
        for (int b = 0; b < bones; b++)
            for (int c = 0; c < 12; c++)
                if (c != 3 && c != 11 && !seen[b * 12 + c])
                    throw PredictiveGap("reference/default pose reconstruction");
        int blocks = (frames - 1) / 15 + 1;
        if (offsets[1] - offsets[0] < 2 * blocks)
            throw new InvalidDataException(
                "Predictive animation layout assumption violated: the block-offset table is shorter than " +
                "one u32 entry per block.");
        var blockSamples = new List<int[,]>[blocks];
        int previous = 0;
        for (int block = 0; block < blocks; block++)
        {
            int end = PredictiveCompressedAnimationDecoder.ReadBlockOffset(ints, offsets[0], block);
            if (end <= previous || end > compressedCount)
                throw new InvalidDataException("Predictive animation block bounds are invalid or compressedData is truncated.");
            int cursor = previous;
            blockSamples[block] = new List<int[,]>(groups);
            for (int group = 0; group < groups; group++)
                blockSamples[block].Add(PredictiveCompressedAnimationDecoder.DecodeBlock(data.AsSpan(compressedAt, compressedCount), cursor, end, out cursor, dynamicCount - group * 16));
            if (cursor != end) throw new InvalidDataException("Predictive animation block has unaccounted payload bytes.");
            previous = end;
        }
        if (previous != compressedCount) throw new InvalidDataException("Predictive animation has trailing compressed data.");
        animation.Duration = ReadF32(data, a + 0x14);
        if (!float.IsFinite(animation.Duration) || animation.Duration < 0 || (frames > 1 && animation.Duration <= 0))
            throw new InvalidDataException("Predictive animation duration is invalid.");
        animation.NumFrames = frames;
        animation.NumTracks = bones;
        animation.NumBlocks = blocks;
        animation.MaxFramesPerBlock = 16;
        animation.FrameDuration = frames > 1 ? animation.Duration / (frames - 1) : 0;
        animation.BlockDuration = 15 * animation.FrameDuration;
        for (int b = 0; b < bones; b++)
        {
            var track = new HkxTrackData { RotationAnimated = true };
            for (int c = 0; c < 3; c++) track.TranslationAnimated[c] = track.ScaleAnimated[c] = true;
            animation.Tracks.Add(track);
        }
        var values = new float[12 * bones];
        for (int frame = 0; frame < frames; frame++)
        {
            int block = frame / 15, sample = frame % 15;
            for (int lane = 0; lane < dynamicCount; lane++)
            {
                float scale = fixedOnly ? BitConverter.Int32BitsToSingle(0x39000400) : ReadF32(data, floatAt + 4 * (floatOffsets[1] + lane));
                float bias = fixedOnly ? 0 : ReadF32(data, floatAt + 4 * (floatOffsets[2] + lane));
                values[ints[offsets[4] + lane]] = PredictiveCompressedAnimationDecoder.Dequantize(blockSamples[block][lane / 16][lane % 16, sample], scale, bias);
            }
            for (int b = 0; b < bones; b++)
            {
                int p = b * 12;
                animation.Tracks[b].Translations.Add(new Vector3(values[p], values[p + 1], values[p + 2]));
                animation.Tracks[b].Rotations.Add(new Quaternion(values[p + 4], values[p + 5], values[p + 6], values[p + 7]));
                animation.Tracks[b].Scales.Add(new Vector3(values[p + 8], values[p + 9], values[p + 10]));
            }
        }
        animation.CompressionDecoded = true;
        ParseAnnotationTracks(data, dataAbs, animRel + 0x28, fixups, animation);
    }

    private static NotSupportedException PredictiveGap(string feature) => new(
        $"Predictive animation {feature} is unresolved in the Fallout4.exe game-decoder evidence and is refused. The file has not been changed.");
}
