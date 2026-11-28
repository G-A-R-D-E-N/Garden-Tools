using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record SoundPlayback(sbyte FrequencyShift, sbyte FrequencyVariance, byte Priority,
                                   byte DbVariance, ushort AttenuationHundredths);
public sealed record SoundRateFile(uint Rpm, string Path);
public sealed record SoundDescriptor(uint? Kind, FormKey? Category, FormKey? Alternate, FormKey? OutputModel,
                                     FormKey? BaseDescriptor, IReadOnlyList<FormKey?> Descriptors,
                                     IReadOnlyList<string> Files, byte[]? Controls, SoundPlayback? Playback,
                                     IReadOnlyList<SoundRateFile> RateFiles);
public sealed record SoundRepeat(float MinTime, float MaxTime, byte? Stackable);
public sealed record SoundMarker(FormKey? Descriptor, SoundRepeat? Repeat);
public sealed record MusicType(uint? Flags, ushort? Priority, ushort? DuckingHundredths, float? FadeDuration,
                               IReadOnlyList<FormKey?> Tracks);
public sealed record Footstep(FormKey? ImpactSet, string? Tag);
public sealed record FootstepSet(IReadOnlyList<FormKey?> Walking, IReadOnlyList<FormKey?> Running,
                                 IReadOnlyList<FormKey?> Sprinting, IReadOnlyList<FormKey?> Sneaking,
                                 IReadOnlyList<FormKey?> Swimming);

public sealed unsafe partial class RadicleEnvironment
{
    public SoundDescriptor SoundDescriptor(Record record)
    {
        using var use = Use(record);
        Native.SoundDescriptor* d;
        Call.Check(RadicleNative.SoundDescriptorDecode(use.Pointer, record.Handle, &d));
        try
        {
            var p = d->Playback;
            SoundPlayback? playback = d->HasPlayback == 0 ? null : new(p.FrequencyShift, p.FrequencyVariance,
                p.Priority, p.DbVariance, p.AttenuationHundredths);
            return new(d->HasKind == 0 ? null : d->Kind, FormKey.FromNative(d->Category), FormKey.FromNative(d->Alternate),
                FormKey.FromNative(d->OutputModel), FormKey.FromNative(d->BaseDescriptor),
                Call.Copy<Native.FormKey>(d->Descriptors, d->DescriptorCount).Select(FormKey.FromNative).ToArray(),
                Call.Copy<Native.SoundFile>(d->Files, d->FileCount).Select(f => Utf8.Read(f.Path)!).ToArray(),
                d->HasControls == 0 ? null : new[] { d->Controls[0], d->Controls[1], d->Controls[2], d->Controls[3] },
                playback, Call.Copy<Native.SoundRateFile>(d->RateFiles, d->RateFileCount)
                    .Select(f => new SoundRateFile(f.Rpm, Utf8.Read(f.Path)!)).ToArray());
        }
        finally { RadicleNative.SoundDescriptorFree(d); }
    }

    public IReadOnlyList<string> SoundFiles(Record record)
    {
        using var use = Use(record);
        Native.SoundFileList* list;
        Call.Check(RadicleNative.SoundResolveFiles(use.Pointer, record.Handle, &list));
        try
        {
            return Call.Copy<Native.SoundFile>(list->Files, list->FileCount).Select(f => Utf8.Read(f.Path)!).ToArray();
        }
        finally { RadicleNative.SoundFileListFree(list); }
    }

    public SoundMarker SoundMarker(Record record)
    {
        using var use = Use(record);
        Native.SoundMarkerInfo m;
        Call.Check(RadicleNative.SoundMarkerGetInfo(use.Pointer, record.Handle, &m));
        return new(FormKey.FromNative(m.Descriptor), m.HasRepeat == 0 ? null :
            new SoundRepeat(m.MinTime, m.MaxTime, m.HasStackable == 0 ? null : m.Stackable));
    }

    public MusicType MusicType(Record record)
    {
        using var use = Use(record);
        Native.MusicType* m;
        Call.Check(RadicleNative.MusicTypeDecode(use.Pointer, record.Handle, &m));
        try
        {
            return new(m->HasFlags == 0 ? null : m->Flags, m->HasPriority == 0 ? null : m->Priority,
                m->HasPriority == 0 ? null : m->DuckingHundredths, m->HasFadeDuration == 0 ? null : m->FadeDuration,
                Call.Copy<Native.FormKey>(m->Tracks, m->TrackCount).Select(FormKey.FromNative).ToArray());
        }
        finally { RadicleNative.MusicTypeFree(m); }
    }

    public Footstep Footstep(Record record)
    {
        using var use = Use(record);
        Native.FootstepInfo f;
        Call.Check(RadicleNative.FootstepGetInfo(use.Pointer, record.Handle, &f));
        return new(FormKey.FromNative(f.ImpactSet), Utf8.Read(f.Tag));
    }

    public FootstepSet FootstepSet(Record record)
    {
        using var use = Use(record);
        Native.FootstepSet* f;
        Call.Check(RadicleNative.FootstepSetDecode(use.Pointer, record.Handle, &f));
        try
        {
            var steps = Call.Copy<Native.FormKey>(f->Steps, f->StepCount).Select(FormKey.FromNative).ToArray();
            var categories = new FormKey?[5][];
            var offset = 0;
            for (var i = 0; i < 5; ++i)
            {
                var count = checked((int)f->Counts[i]);
                categories[i] = steps.Skip(offset).Take(count).ToArray();
                offset += count;
            }
            return new(categories[0], categories[1], categories[2], categories[3], categories[4]);
        }
        finally { RadicleNative.FootstepSetFree(f); }
    }

    public static int? SelectRateFileIndex(IReadOnlyList<SoundRateFile> rateFiles, uint rpm)
    {
        var native = rateFiles.Select(f => new Native.SoundRateFile { Rpm = f.Rpm }).ToArray();
        fixed (Native.SoundRateFile* pointer = native)
        {
            int has;
            nuint index;
            Call.Check(RadicleNative.SoundSelectRateFile(pointer, (nuint)native.Length, rpm, &has, &index));
            return has == 0 ? null : checked((int)index);
        }
    }

    public static int SelectStandardFileIndex(int count, int drawIndex, int? previousIndex)
    {
        nuint index;
        Call.Check(RadicleNative.SoundSelectStandardFile(checked((nuint)count), checked((nuint)drawIndex),
            previousIndex.HasValue ? 1 : 0, checked((nuint)previousIndex.GetValueOrDefault()), &index));
        return checked((int)index);
    }

    public static double SoundFrequencyFactor(sbyte frequencyShift)
    {
        double value;
        Call.Check(RadicleNative.SoundFrequencyFactor(frequencyShift, &value));
        return value;
    }

    public static bool SoundIsLooping(byte loopControl)
    {
        int looping;
        Call.Check(RadicleNative.SoundIsLooping(loopControl, &looping));
        return looping != 0;
    }

    public static double SoundSidechainSend(byte sidechainControl)
    {
        double value;
        Call.Check(RadicleNative.SoundSidechainSend(sidechainControl, &value));
        return value;
    }

    public static double SoundSmallRumbleSend(byte rumbleControl)
    {
        double value;
        Call.Check(RadicleNative.SoundSmallRumbleSend(rumbleControl, &value));
        return value;
    }

    public static double SoundBigRumbleSend(byte rumbleControl)
    {
        double value;
        Call.Check(RadicleNative.SoundBigRumbleSend(rumbleControl, &value));
        return value;
    }
}

public sealed record XwmaPcm(int SampleRate, int Channels, int BadPackets, byte[] Pcm16)
{
    public int FrameCount => Channels == 0 ? 0 : Pcm16.Length / (2 * Channels);

    public byte[] ToWav()
    {
        var wav = new byte[44 + Pcm16.Length];
        "RIFF"u8.CopyTo(wav);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(4), (uint)(36 + Pcm16.Length));
        "WAVEfmt "u8.CopyTo(wav.AsSpan(8));
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(16), 16);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(20), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(22), (ushort)Channels);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(24), (uint)SampleRate);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(28), (uint)(SampleRate * Channels * 2));
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(32), (ushort)(Channels * 2));
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(34), 16);
        "data"u8.CopyTo(wav.AsSpan(36));
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(40), (uint)Pcm16.Length);
        Pcm16.CopyTo(wav.AsSpan(44));
        return wav;
    }
}

public static class XwmaAudio
{
    public static unsafe XwmaPcm Decode(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("an xWMA or FUZ stream is required", nameof(bytes));
        byte* pcm = null;
        nuint size = 0;
        uint sampleRate = 0;
        ushort channels = 0;
        nuint bad = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.AudioDecodeXwma(pointer, (nuint)bytes.Length, &pcm, &size, &sampleRate, &channels, &bad));
        }
        return new XwmaPcm(checked((int)sampleRate), channels, checked((int)bad), Call.TakeBytes(pcm, size));
    }
}

public enum SoundContainer { Wav = 0, Xwma = 1, Fuz = 2 }

public sealed record SoundPcm(SoundContainer Container, int SampleRate, int Channels, int BitsPerSample, int BadPackets, byte[] Pcm)
{
    public int FrameCount => Channels == 0 ? 0 : Pcm.Length / (Channels * BitsPerSample / 8);

    public double DurationSeconds => SampleRate == 0 ? 0 : (double)FrameCount / SampleRate;

    public byte[] ToWav()
    {
        var bytesPerFrame = Channels * BitsPerSample / 8;
        var wav = new byte[44 + Pcm.Length];
        "RIFF"u8.CopyTo(wav);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(4), (uint)(36 + Pcm.Length));
        "WAVEfmt "u8.CopyTo(wav.AsSpan(8));
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(16), 16);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(20), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(22), (ushort)Channels);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(24), (uint)SampleRate);
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(28), (uint)(SampleRate * bytesPerFrame));
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(32), (ushort)bytesPerFrame);
        BinaryPrimitives.WriteUInt16LittleEndian(wav.AsSpan(34), (ushort)BitsPerSample);
        "data"u8.CopyTo(wav.AsSpan(36));
        BinaryPrimitives.WriteUInt32LittleEndian(wav.AsSpan(40), (uint)Pcm.Length);
        Pcm.CopyTo(wav.AsSpan(44));
        return wav;
    }
}

public sealed record SoundAsset(string Path, SoundPcm Sound);

public static unsafe class SoundAudio
{
    static SoundAudio() => RadicleLibrary.Register();

    public static SoundPcm Decode(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a WAV, xWMA or FUZ stream is required", nameof(bytes));
        byte* pcm = null;
        nuint size = 0;
        uint rate = 0;
        ushort channels = 0, bits = 0;
        int container = 0;
        nuint bad = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.AudioDecodeSound(pointer, (nuint)bytes.Length, &pcm, &size, &rate, &channels, &bits, &container, &bad));
        }
        var taken = Call.TakeBytes(pcm, size);
        return new SoundPcm((SoundContainer)container, checked((int)rate), channels, bits, checked((int)bad), taken);
    }

    // Constraint: name is a descriptor file name as stored; null means no loose file or archive provides it under any tried path.
    public static SoundAsset? Read(AssetResolver assets, string name)
    {
        ArgumentNullException.ThrowIfNull(assets);
        ArgumentNullException.ThrowIfNull(name);
        byte* path = null;
        nuint pathSize = 0;
        byte* pcm = null;
        nuint size = 0;
        uint rate = 0;
        ushort channels = 0, bits = 0;
        int container = 0;
        nuint bad = 0;
        using var use = assets.Use();
        if (!Call.Found(RadicleNative.AssetsReadSound(use.Pointer, name, &path, &pathSize, &pcm, &size, &rate, &channels, &bits, &container, &bad)))
            return null;
        var resolved = Call.TakeText(path, pathSize);
        var taken = Call.TakeBytes(pcm, size);
        return new SoundAsset(resolved, new SoundPcm((SoundContainer)container, checked((int)rate), channels, bits, checked((int)bad), taken));
    }
}

public sealed record LipAnimation(int FrameCount, int PrerollFrames, int PhonemeCount, int ModifierCount, float[] Weights)
{
    public const int FramesPerSecond = 30;

    public int SlotCount => PhonemeCount + ModifierCount;

    public double DurationSeconds => (double)FrameCount / FramesPerSecond;

    public float Weight(int frame, int slot)
    {
        if ((uint)frame >= (uint)FrameCount) throw new ArgumentOutOfRangeException(nameof(frame));
        if ((uint)slot >= (uint)SlotCount) throw new ArgumentOutOfRangeException(nameof(slot));
        return Weights[frame * SlotCount + slot];
    }

    public static unsafe LipAnimation Decode(ReadOnlySpan<byte> fuzOrLip)
    {
        if (fuzOrLip.IsEmpty) throw new ArgumentException("a FUZ file or a lip block is required", nameof(fuzOrLip));
        uint frames = 0;
        int preroll = 0;
        uint phonemes = 0;
        uint modifiers = 0;
        float* weights = null;
        nuint count = 0;
        fixed (byte* pointer = fuzOrLip)
        {
            Call.Check(RadicleNative.AudioDecodeLip(pointer, (nuint)fuzOrLip.Length, &frames, &preroll, &phonemes, &modifiers, &weights, &count));
        }
        return new LipAnimation(checked((int)frames), preroll, checked((int)phonemes), checked((int)modifiers), Call.TakeFloats(weights, count));
    }
}

public readonly record struct LipMorphWeight(string Morph, float Weight);

public static class LipPhonemes
{
    public const int Count = 43;

    public static unsafe string Name(int slot)
    {
        if ((uint)slot >= Count) throw new ArgumentOutOfRangeException(nameof(slot));
        IntPtr name = IntPtr.Zero;
        Call.Check(RadicleNative.LipPhonemeName((uint)slot, &name));
        return Utf8.Read(name)!;
    }

    public static unsafe LipMorphWeight[] MorphsFor(int slot)
    {
        if ((uint)slot >= Count) throw new ArgumentOutOfRangeException(nameof(slot));
        nuint count = 0;
        Call.Check(RadicleNative.LipSlotMorphCount((uint)slot, &count));
        var result = new LipMorphWeight[checked((int)count)];
        for (var i = 0; i < result.Length; i++)
        {
            IntPtr morph = IntPtr.Zero;
            float weight = 0;
            Call.Check(RadicleNative.LipSlotMorph((uint)slot, (nuint)i, &morph, &weight));
            result[i] = new LipMorphWeight(Utf8.Read(morph)!, weight);
        }
        return result;
    }
}

public static class SoundPaths
{
    public static unsafe string FormatVoicePath(string plugin, string voiceType, uint infoFormId, uint responseIndex,
                                                string? language = null, string? variant = null, string? extension = null)
    {
        ArgumentNullException.ThrowIfNull(plugin);
        ArgumentNullException.ThrowIfNull(voiceType);
        byte* path = null;
        nuint size = 0;
        Call.Check(RadicleNative.AudioFormatVoicePath(plugin, voiceType, infoFormId, responseIndex, language, variant, extension, &path, &size));
        return Call.TakeText(path, size);
    }
}
