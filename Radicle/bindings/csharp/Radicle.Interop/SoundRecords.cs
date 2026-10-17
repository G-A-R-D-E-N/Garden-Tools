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
