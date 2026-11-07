using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public static class QuantizedAnimationDecoder
{
    public static void Decode(ReadOnlySpan<byte> data, HkxAnimationData animation)
    {
        if (data.Length < 40) throw new InvalidDataException("Quantized animation header is truncated.");
        var w = new int[20];
        for (int i = 0; i < w.Length; i++) w[i] = BinaryPrimitives.ReadUInt16LittleEndian(data[(2 * i)..]);
        int bones = w[1], floats = w[2], frames = w[3];
        float duration = F32(data, 8);
        if (frames < 1 || !float.IsFinite(duration) || duration < 0 || (frames > 1 && duration <= 0))
            throw new InvalidDataException("Quantized animation has an invalid sample count or duration.");
        if ((long)frames * (bones + floats) > (1 << 22))
            throw new InvalidDataException("Quantized animation exceeds the decoded sample limit.");
        int ns = w[6] + w[8], nr = w[7], nf = w[9];
        int ds = w[10] + w[12], dr = w[11], df = w[13];
        int usedStride = 2 * ds + 6 * dr + 2 * df;
        if (w[14] < usedStride) throw new InvalidDataException("Quantized animation record stride is too small.");
        int dataLength = data.Length;
        byte[] blob = data.ToArray();
        var regions = new List<(int Start, int End)> { (0, 40) };
        void Region(int start, long size)
        {
            if (size == 0) return;
            if (start < 0 || size < 0 || start > dataLength || size > dataLength - start)
                throw new InvalidDataException("Quantized animation region is outside the data blob.");
            int end = start + (int)size;
            foreach (var region in regions)
                if (start < region.End && end > region.Start)
                    throw new InvalidDataException("Quantized animation regions overlap.");
            regions.Add((start, end));
        }

        Region(w[15], 2L * (ns + nr + nf));
        Region(w[16], 2L * (ds + dr + df));
        Region(w[17], 4L * ns + 6L * nr);
        int staticFloats = Align16(w[17] + 4 * ns + 6 * nr);
        Region(staticFloats, 4L * nf);
        Region(w[18], 4L * ds);
        Region(w[19], 4L * ds);
        int floatOffsets = Align16(w[18] + 4 * ds);
        int floatScales = Align16(w[19] + 4 * ds);
        Region(floatOffsets, 4L * df);
        Region(floatScales, 4L * df);
        Region(w[0], (long)w[14] * frames);
        var occupied = new bool[bones * 12];
        var occupiedFloats = new bool[floats];
        int[] Indices(int at, int count, bool rotation, bool floatTrack)
        {
            var result = new int[count];
            for (int i = 0; i < count; i++)
            {
                int index = BinaryPrimitives.ReadUInt16LittleEndian(blob.AsSpan(at + 2 * i));
                if (rotation) index *= 4;
                var slots = floatTrack ? occupiedFloats : occupied;
                int width = rotation ? 4 : 1;
                if (index < 0 || index > slots.Length - width || (rotation && index % 12 != 4))
                    throw new InvalidDataException(
                        "Quantized animation layout assumption violated: the scatter index is outside the " +
                        "twelve-float bone slots this decoder writes.");
                for (int c = 0; c < width; c++)
                {
                    if (slots[index + c]) throw new InvalidDataException("Quantized animation scatter slots overlap.");
                    slots[index + c] = true;
                }
                result[i] = index;
            }
            return result;
        }

        int[] si = Indices(w[15], ns, false, false);
        int[] ri = Indices(w[15] + 2 * ns, nr, true, false);
        int[] fi = Indices(w[15] + 2 * (ns + nr), nf, false, true);
        int[] di = Indices(w[16], ds, false, false);
        int[] dri = Indices(w[16] + 2 * ds, dr, true, false);
        int[] dfi = Indices(w[16] + 2 * (ds + dr), df, false, true);
        for (int b = 0; b < bones; b++)
            for (int c = 0; c < 12; c++)
                if (c != 3 && c != 11 && !occupied[b * 12 + c])
                    throw new NotSupportedException("Quantized animation leaves pose components unspecified; reference/default reconstruction is unresolved. The file has not been changed.");
        foreach (bool present in occupiedFloats)
            if (!present) throw new NotSupportedException("Quantized animation leaves float tracks unspecified. The file has not been changed.");
        animation.NumTracks = bones;
        animation.NumFrames = frames;
        animation.Duration = duration;
        animation.FrameDuration = frames > 1 ? duration / (frames - 1) : 0;
        animation.NumBlocks = 1;
        animation.MaxFramesPerBlock = frames;
        animation.BlockDuration = duration;
        for (int b = 0; b < bones; b++)
        {
            var track = new HkxTrackData { RotationAnimated = true };
            for (int c = 0; c < 3; c++) track.TranslationAnimated[c] = track.ScaleAnimated[c] = true;
            animation.Tracks.Add(track);
        }
        for (int f = 0; f < floats; f++) animation.FloatTracks.Add(new List<float>(frames));
        var values = new float[bones * 12];
        var floatValues = new float[floats];
        for (int frame = 0; frame < frames; frame++)
        {
            for (int i = 0; i < ns; i++) values[si[i]] = F32(data, w[17] + 4 * i);
            for (int i = 0; i < nr; i++) Rotation(values, ri[i], DecodeQuaternion(data[(w[17] + 4 * ns + 6 * i)..]));
            for (int i = 0; i < nf; i++) floatValues[fi[i]] = F32(data, staticFloats + 4 * i);
            int record = w[0] + frame * w[14];
            for (int i = 0; i < ds; i++) values[di[i]] = DecodeScalar(BinaryPrimitives.ReadUInt16LittleEndian(data[(record + 2 * i)..]), F32(data, w[19] + 4 * i), F32(data, w[18] + 4 * i));
            for (int i = 0; i < dr; i++) Rotation(values, dri[i], DecodeQuaternion(data[(record + 2 * ds + 6 * i)..]));
            for (int i = 0; i < df; i++) floatValues[dfi[i]] = DecodeScalar(BinaryPrimitives.ReadUInt16LittleEndian(data[(record + 2 * ds + 6 * dr + 2 * i)..]), F32(data, floatScales + 4 * i), F32(data, floatOffsets + 4 * i));
            for (int b = 0; b < bones; b++)
            {
                int p = 12 * b;
                animation.Tracks[b].Translations.Add(new Vector3(values[p], values[p + 1], values[p + 2]));
                animation.Tracks[b].Rotations.Add(new Quaternion(values[p + 4], values[p + 5], values[p + 6], values[p + 7]));
                animation.Tracks[b].Scales.Add(new Vector3(values[p + 8], values[p + 9], values[p + 10]));
            }
            for (int f = 0; f < floats; f++) animation.FloatTracks[f].Add(floatValues[f]);
        }
        animation.CompressionDecoded = true;
    }

    private static int Align16(int value) => (value + 15) & ~15;

    private static float F32(ReadOnlySpan<byte> data, int at)
    {
        float value = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data[at..]));
        if (!float.IsFinite(value)) throw new InvalidDataException("Quantized animation contains a non-finite float.");
        return value;
    }

    private static void Rotation(float[] values, int at, Quaternion q)
    {
        values[at] = q.X; values[at + 1] = q.Y; values[at + 2] = q.Z; values[at + 3] = q.W;
    }

    public static float DecodeScalar(ushort value, float scale, float offset)
    {
        if (!float.IsFinite(scale) || !float.IsFinite(offset))
            throw new InvalidDataException("Quantized animation has a non-finite scale or offset.");
        float decoded = value * scale + offset;
        if (!float.IsFinite(decoded))
            throw new InvalidDataException("Quantized animation scalar is not finite.");
        return decoded;
    }

    private const float QuantisationSlack = 0.001f;

    public static Quaternion DecodeQuaternion(ReadOnlySpan<byte> data)
    {
        if (data.Length < 6)
            throw new InvalidDataException("Quantized animation quaternion requires six bytes.");
        ushort a = BinaryPrimitives.ReadUInt16LittleEndian(data);
        ushort b = BinaryPrimitives.ReadUInt16LittleEndian(data[2..]);
        ushort c = BinaryPrimitives.ReadUInt16LittleEndian(data[4..]);
        int omitted = (a >> 15) | ((b >> 15) << 1);
        Span<float> components = stackalloc float[4];
        Span<ushort> words = stackalloc ushort[] { a, b, c };
        float sum = 0;
        for (int i = 0, j = 0; i < 4; i++)
        {
            if (i == omitted) continue;
            float value = (words[j++] & 0x7fff) * 4.3161006e-05f - 0.70710677f;
            components[i] = value;
            sum += value * value;
        }
        float remaining = 1 - sum;
        if (!float.IsFinite(remaining) || remaining < -QuantisationSlack)
            throw new InvalidDataException("Quantized animation quaternion does not describe a rotation: its stored components exceed unit length.");
        components[omitted] = MathF.Sqrt(MathF.Max(0, remaining)) * ((c & 0x8000) == 0 ? 1 : -1);
        return new Quaternion(components[0], components[1], components[2], components[3]);
    }
}
