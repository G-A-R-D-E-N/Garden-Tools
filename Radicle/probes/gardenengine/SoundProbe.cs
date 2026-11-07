using Godot;
using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Radicle.Interop;

namespace OpenCommonwealth;

public partial class SoundProbe : Node
{
    public override void _Ready()
    {
        var data = System.Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(data))
        {
            GD.Print("sound_probe SKIP: FO4_DATA_DIR unset");
            GetTree().Quit(77);
            return;
        }
        try
        {
            using var env = RadicleEnvironment.Load(new[] { Path.Combine(data, "Fallout4.esm") });

            var (descriptorRecord, descriptor) = FirstWith<SoundDescriptor>(env, "SNDR", "a Category (GNAM)",
                d => d.Category is not null, env.SoundDescriptor);
            var kind = descriptor.Kind ?? throw new InvalidOperationException("SNDR kind absent");
            var standard = kind == 0x1EEF540Au;
            if (!standard && kind != 0x54651A43u && kind != 0xED157AE3u)
                throw new InvalidOperationException("SNDR kind unknown: " + kind);
            if (standard != (descriptor.Playback is not null))
                throw new InvalidOperationException("Playback presence does not match Standard kind");
            if (standard && descriptor.Controls?.Length != 4)
                throw new InvalidOperationException("Standard LNAM is not four bytes");
            Resolve(env, descriptor.Category, "SNCT", "category");
            Resolve(env, descriptor.Alternate, "SNDR", "alternate");
            Resolve(env, descriptor.OutputModel, "SOPM", "output model");
            Resolve(env, descriptor.BaseDescriptor, "SNDR", "base descriptor");
            foreach (var child in descriptor.Descriptors) Resolve(env, child, "SNDR", "descriptor");

            var (soundRecord, marker) = FirstWith<SoundMarker>(env, "SOUN", "a Descriptor (SDSC)",
                m => m.Descriptor is not null, env.SoundMarker);
            var markerDescriptor = Resolve(env, marker.Descriptor, "SNDR", "marker descriptor")
                ?? throw new InvalidOperationException("marker descriptor absent");
            env.SoundDescriptor(markerDescriptor);
            var markerFile = env.SoundFiles(soundRecord).FirstOrDefault(file => !string.IsNullOrEmpty(file))
                ?? throw new InvalidOperationException("marker resolves to no sound file");
            if (marker.Repeat?.Stackable is byte stackable && stackable > 1)
                throw new InvalidOperationException("REPT stackable byte outside 0..1");

            var (musicRecord, music) = FirstWith<MusicType>(env, "MUSC", "a non-null track (TNAM)",
                m => m.Tracks.Any(track => track is not null), env.MusicType);
            foreach (var track in music.Tracks) Resolve(env, track, "MUST", "music track");

            var (footstepRecord, footstep) = FirstWith<Footstep>(env, "FSTP", "an ImpactSet (DATA)",
                f => f.ImpactSet is not null, env.Footstep);
            Resolve(env, footstep.ImpactSet, "IPDS", "impact data set");

            GD.Print("sound_probe PASS " + JsonSerializer.Serialize(new
            {
                Descriptor = Describe(env, descriptorRecord), Kind = kind, Playback = descriptor.Playback is not null,
                Marker = Describe(env, soundRecord), MarkerDescriptor = Describe(env, markerDescriptor), MarkerFile = markerFile,
                Music = Describe(env, musicRecord), Tracks = music.Tracks.Count,
                Footstep = Describe(env, footstepRecord), Tag = footstep.Tag,
            }));
            GetTree().Quit(0);
        }
        catch (Exception error)
        {
            GD.PrintErr("sound_probe FAIL: " + error);
            GetTree().Quit(1);
        }
    }

    private static (Record, T) FirstWith<T>(RadicleEnvironment env, string signature, string link,
        Func<T, bool> carries, Func<Record, T> decode)
    {
        foreach (var record in env.WinnersOf(signature))
        {
            var value = decode(record);
            if (carries(value)) return (record, value);
        }
        throw new InvalidOperationException("no " + signature + " winner carries " + link);
    }

    private static Record? Resolve(RadicleEnvironment env, FormKey? key, string signature, string role)
    {
        if (key is null) return null;
        var target = env.Winner(key.Value) ?? throw new InvalidOperationException(role + " unresolved: " + key.Value);
        var actual = env.Info(target).Signature;
        return actual == signature ? target : throw new InvalidOperationException(role + " is " + actual + ", expected " + signature);
    }

    private static string Describe(RadicleEnvironment env, Record record)
    {
        var info = env.Info(record);
        return info.EditorId + " " + info.FormKey;
    }
}
