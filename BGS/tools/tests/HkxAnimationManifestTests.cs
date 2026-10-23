using System;
using System.Globalization;
using System.IO;
using System.Numerics;
using System.Text;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class HkxAnimationManifestTests
{
    // Radicle/tests/hkx_animation_manifest.hpp must produce this exact text from the same clip.
    [Fact]
    public void FormatsTheManifestThatRadicleComparesAgainst()
    {
        var animation = new HkxAnimationData
        {
            AnimationClass = "hkaSplineCompressedAnimation",
            Duration = 1.0f,
            FrameDuration = 1.0f,
            NumFrames = 2,
            NumTracks = 1,
            NumBlocks = 1,
            MaxFramesPerBlock = 2,
        };
        var track = new HkxTrackData { RotationAnimated = true };
        track.TranslationAnimated[0] = true;
        track.Translations.Add(new Vector3(0f, 0f, 0f));
        track.Translations.Add(new Vector3(10f, 0f, 0f));
        track.Rotations.Add(Quaternion.Identity);
        track.Rotations.Add(Quaternion.Identity);
        track.Scales.Add(Vector3.One);
        track.Scales.Add(Vector3.One);
        animation.Tracks.Add(track);

        Assert.Equal(
            "hkx-animation-manifest 1\n" +
            "class hkaSplineCompressedAnimation\n" +
            "frames 2\n" +
            "tracks 1\n" +
            "blocks 1\n" +
            "max_frames_per_block 2\n" +
            "duration_tenk 10000\n" +
            "frame_duration_tenk 10000\n" +
            "track 0 flags 100 1 000\n" +
            "track 0 first 0 0 0 0 0 0 10000 10000 10000 10000\n" +
            "track 0 middle 100000 0 0 0 0 0 10000 10000 10000 10000\n" +
            "track 0 last 100000 0 0 0 0 0 10000 10000 10000 10000\n" +
            "hash e91624ff10f43fd1\n",
            Format(animation));
    }

    // Requires HKX_MANIFEST_HKX and HKX_MANIFEST_OUT to regenerate the manifest Radicle/testdata/hkx holds.
    [Fact]
    public void WritesTheRequestedClipManifest()
    {
        string? clip = Environment.GetEnvironmentVariable("HKX_MANIFEST_HKX");
        string? output = Environment.GetEnvironmentVariable("HKX_MANIFEST_OUT");
        if (string.IsNullOrEmpty(clip) || string.IsNullOrEmpty(output)) return;

        var animation = new HkxBinaryReader().ReadAnimation(File.ReadAllBytes(clip));
        File.WriteAllText(output, Format(animation), new UTF8Encoding(false));
    }

    private static string Format(HkxAnimationData animation)
    {
        var text = new StringBuilder();
        text.Append("hkx-animation-manifest 1\n");
        text.Append("class ").Append(animation.AnimationClass).Append('\n');
        Line(text, "frames", animation.NumFrames);
        Line(text, "tracks", animation.Tracks.Count);
        Line(text, "blocks", animation.NumBlocks);
        Line(text, "max_frames_per_block", animation.MaxFramesPerBlock);
        Line(text, "duration_tenk", TenThousandths(animation.Duration));
        Line(text, "frame_duration_tenk", TenThousandths(animation.FrameDuration));

        int frames = animation.NumFrames;
        int[] poseIndices = { 0, frames / 2, frames == 0 ? 0 : frames - 1 };
        string[] poseNames = { "first", "middle", "last" };
        for (int track = 0; track < animation.Tracks.Count; track++)
        {
            HkxTrackData data = animation.Tracks[track];
            var flags = new StringBuilder();
            flags.Append("track ").Append(track.ToString(CultureInfo.InvariantCulture)).Append(" flags ");
            for (int axis = 0; axis < 3; axis++) flags.Append(data.TranslationAnimated[axis] ? '1' : '0');
            flags.Append(' ').Append(data.RotationAnimated ? '1' : '0').Append(' ');
            for (int axis = 0; axis < 3; axis++) flags.Append(data.ScaleAnimated[axis] ? '1' : '0');
            text.Append(flags).Append('\n');
            if (frames == 0) continue;

            for (int pose = 0; pose < 3; pose++)
            {
                int frame = poseIndices[pose];
                if (frame >= data.Translations.Count || frame >= data.Rotations.Count || frame >= data.Scales.Count)
                {
                    throw new InvalidDataException(
                        $"track {track} holds {data.Translations.Count}/{data.Rotations.Count}/{data.Scales.Count} " +
                        $"samples, so frame {frame} of {frames} must not be read");
                }

                Vector3 translation = data.Translations[frame];
                Quaternion rotation = data.Rotations[frame];
                Vector3 scale = data.Scales[frame];
                var line = new StringBuilder();
                line.Append("track ").Append(track.ToString(CultureInfo.InvariantCulture)).Append(' ')
                    .Append(poseNames[pose]);
                foreach (float value in new[]
                         {
                             translation.X, translation.Y, translation.Z,
                             rotation.X, rotation.Y, rotation.Z, rotation.W,
                             scale.X, scale.Y, scale.Z,
                         })
                {
                    line.Append(' ').Append(TenThousandths(value).ToString(CultureInfo.InvariantCulture));
                }

                text.Append(line).Append('\n');
            }
        }

        string body = text.ToString();
        ulong hash = 14695981039346656037UL;
        foreach (byte value in Encoding.ASCII.GetBytes(body))
        {
            hash ^= value;
            hash *= 1099511628211UL;
        }

        return body + "hash " + hash.ToString("x16", CultureInfo.InvariantCulture) + "\n";
    }

    private static void Line(StringBuilder text, string name, long value) =>
        text.Append(name).Append(' ').Append(value.ToString(CultureInfo.InvariantCulture)).Append('\n');

    private static long TenThousandths(float value) =>
        (long)Math.Round((double)value * 10000.0, MidpointRounding.AwayFromZero);
}
