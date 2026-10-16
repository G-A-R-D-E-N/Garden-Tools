using System;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class AnimationGeometryTests
{
    [Fact]
    public void LoopMakesTheLastFrameMatchTheFirst()
    {
        var clip = Ramp(6);
        var untouched = clip.Tracks[0].Translations.ToList();

        var looped = AnimationEdit.Loop(clip, null, 1);

        Assert.Equal(clip.NumFrames, looped.Animation.NumFrames);
        Assert.Equal(clip.Duration, looped.Animation.Duration);
        Assert.Equal(clip.FrameDuration, looped.Animation.FrameDuration);
        Assert.Equal(clip.AnimationClass, looped.Animation.AnimationClass);
        Assert.Equal(1, looped.BlendFrames);

        foreach (var track in looped.Animation.Tracks)
        {
            Assert.True((track.Translations[^1] - track.Translations[0]).Length() < 1e-4f);
            Assert.True((track.Scales[^1] - track.Scales[0]).Length() < 1e-4f);
            Assert.True(SplineQuat.AngleBetween(track.Rotations[0], track.Rotations[^1]) < 1e-4f);
        }

        Assert.Equal(untouched, clip.Tracks[0].Translations);
        Assert.Equal(5f, clip.Tracks[0].Translations[5].X, 4);
        Assert.Equal(MathF.Sqrt(150f), looped.WorstPosition, 3);
    }

    [Fact]
    public void LoopEasesTheLastFramesIntoTheFirst()
    {
        var clip = Ramp(8);

        var looped = AnimationEdit.Loop(clip, null, 3);

        var positions = looped.Animation.Tracks[0].Translations.Select(t => t.X).ToArray();
        Assert.Equal(8, positions.Length);
        Assert.Equal(4f, positions[4], 4);
        Assert.Equal(5f - 5f / 3f, positions[5], 4);
        Assert.Equal(2f, positions[6], 4);
        Assert.Equal(0f, positions[7], 4);
        Assert.Equal(MathF.Sqrt(294f), looped.WorstPosition, 3);
        Assert.Equal(3, looped.BlendFrames);
    }

    [Fact]
    public void LoopCarriesAnnotationsOverUnchanged()
    {
        var clip = Ramp(5);
        clip.Annotations.Add(new HkxAnnotation { Time = 0.05f, Text = "half" });
        clip.Annotations.Add(new HkxAnnotation { Time = 0.1f, Text = "end" });

        var looped = AnimationEdit.Loop(clip, null, 2);

        Assert.Equal(2, looped.Animation.Annotations.Count);
        Assert.Equal(0.05f, looped.Animation.Annotations[0].Time, 5);
        Assert.Equal("end", looped.Animation.Annotations[1].Text);
    }

    [Fact]
    public void LoopRefusesAClipWithOneFrame()
    {
        var clip = Ramp(1);
        Assert.Throws<InvalidOperationException>(() => AnimationEdit.Loop(clip, null, 1));
    }

    [Fact]
    public void LoopRefusesAZeroBlend()
    {
        var clip = Ramp(5);
        Assert.Throws<InvalidOperationException>(() => AnimationEdit.Loop(clip, null, 0));
    }

    [Fact]
    public void LoopRefusesABlendThatWouldSwallowTheFirstFrame()
    {
        var clip = Ramp(5);
        Assert.Throws<InvalidOperationException>(() => AnimationEdit.Loop(clip, null, 5));
    }

    [Fact]
    public void LoopRefusesATrackShorterThanTheAnimation()
    {
        var clip = Ramp(5);
        clip.Tracks[0].Translations.RemoveAt(4);
        Assert.Throws<InvalidOperationException>(() => AnimationEdit.Loop(clip, null, 1));
    }

    [Fact]
    public void LoopBlendsFrameSampledTravel()
    {
        var clip = Ramp(5);
        var motion = new RootMotion.Motion { Duration = clip.Duration };
        for (int f = 0; f < 5; f++)
            motion.Samples.Add(new RootMotion.Sample(new Vector3(f, 0, 0), f * 0.2f));

        var looped = AnimationEdit.Loop(clip, motion, 2);

        Assert.True(looped.MotionBlended);
        Assert.NotNull(looped.Motion);
        Assert.Equal(5, looped.Motion!.Samples.Count);
        Assert.Equal(0f, looped.Motion.Samples[4].Position.Length(), 4);
        Assert.Equal(0f, looped.Motion.Samples[4].TurnRadians, 4);
        Assert.Equal(clip.Duration, looped.Motion.Duration);
        Assert.Equal(4f, motion.Samples[4].Position.X, 4);
    }

    [Fact]
    public void LoopLeavesTravelThatIsNotSampledOncePerFrame()
    {
        var clip = Ramp(5);
        var motion = new RootMotion.Motion { Duration = clip.Duration };
        motion.Samples.Add(new RootMotion.Sample(Vector3.Zero, 0f));
        motion.Samples.Add(new RootMotion.Sample(new Vector3(9, 0, 0), 1.5f));

        var looped = AnimationEdit.Loop(clip, motion, 1);

        Assert.False(looped.MotionBlended);
        Assert.Same(motion, looped.Motion);
        Assert.Equal(new Vector3(9, 0, 0), motion.Samples[1].Position);
    }

    [Fact]
    public void TrimShiftsAndDropsAnnotationTimes()
    {
        var clip = Ramp(9);
        clip.Annotations.Add(new HkxAnnotation { Time = 0f, Text = "start" });
        clip.Annotations.Add(new HkxAnnotation { Time = 0.1f, Text = "early" });
        clip.Annotations.Add(new HkxAnnotation { Time = 0.2f, Text = "middle" });
        clip.Annotations.Add(new HkxAnnotation { Time = 0.3f, Text = "late" });

        var cut = AnimationEdit.Trim(clip, null, 3, 6);

        Assert.Equal(4, cut.Animation.NumFrames);
        Assert.Equal(0.1f, cut.Animation.Duration, 5);
        Assert.Equal(2, cut.AnnotationsDropped);
        Assert.Equal(0.1f, cut.FromTime, 5);
        Assert.Equal(new[] { "early", "middle" }, cut.Animation.Annotations.Select(a => a.Text));

        var kept = new[] { 0.1f, 0.2f };
        for (int i = 0; i < kept.Length; i++)
        {
            float rebased = Math.Clamp((kept[i] - cut.FromTime) * 1f, 0f, cut.Animation.Duration);
            Assert.Equal(rebased, cut.Animation.Annotations[i].Time, 5);
            Assert.InRange(cut.Animation.Annotations[i].Time, 0f, cut.Animation.Duration + 1e-4f);
        }
    }

    [Fact]
    public void RetimeScalesAnnotationTimesIntoTheClip()
    {
        var clip = Ramp(5);
        clip.Annotations.Add(new HkxAnnotation { Time = 0.05f, Text = "half" });
        clip.Annotations.Add(new HkxAnnotation { Time = 0.1f, Text = "end" });

        var made = AnimationEdit.Retime(clip, null, 2f);

        Assert.Equal(9, made.Animation.NumFrames);
        Assert.Equal(2f, made.Scale, 4);
        Assert.Equal(2, made.Animation.Annotations.Count);
        Assert.Equal(0.05f * made.Scale, made.Animation.Annotations[0].Time, 4);
        Assert.Equal(0.1f * made.Scale, made.Animation.Annotations[1].Time, 4);
        foreach (var note in made.Animation.Annotations)
            Assert.InRange(note.Time, 0f, made.Animation.Duration + 1e-5f);
    }

    [Fact]
    public void TrimmedSplineClipSavesTheFramesDurationAndAnnotationsItShows()
    {
        string source = Fixture("Meshes", "Actors", "RadRoach", "Animations", "TurnLeft90.hkx");
        if (!File.Exists(source)) return;

        string folder = Directory.CreateTempSubdirectory("bgs-geometry-spline").FullName;
        try
        {
            string path = Path.Combine(folder, "TurnLeft90.hkx");
            File.Copy(source, path);
            byte[] original = File.ReadAllBytes(path);

            var reader = new HkxBinaryReader();
            var clip = reader.ReadAnimation(path);
            Assert.Equal(NativeAnimation.SplineClass, clip.AnimationClass);
            Assert.Equal(19, clip.NumFrames);
            Assert.Equal(4, clip.Annotations.Count);

            var cut = AnimationEdit.Trim(clip, RootMotion.Read(path), 4, 14);
            Assert.Equal(11, cut.Animation.NumFrames);
            Assert.Equal(1, cut.AnnotationsDropped);
            Assert.Equal(3, cut.Animation.Annotations.Count);

            var result = AnimationSaveTransaction.Commit(
                path, cut.Animation, DocumentSourceStamp.Capture(path),
                editedTrack: -1, editedFrame: -1, timeline: NativeAnimation.Timeline.Of(cut));

            Assert.True(result.Committed, result.Message);
            Assert.Equal(original, File.ReadAllBytes(path + ".bak"));

            var written = reader.ReadAnimation(path);
            Assert.Equal(NativeAnimation.SplineClass, written.AnimationClass);
            Assert.Equal(11, written.NumFrames);
            Assert.Equal(cut.Animation.Duration, written.Duration, 3);
            Assert.Equal(3, written.Annotations.Count);
            Assert.Equal(new[] { "FootLeft", "FootRight", "FootLeft" },
                         written.Annotations.OrderBy(a => a.Time).Select(a => a.Text));
            foreach (var note in written.Annotations)
                Assert.InRange(note.Time, 0f, written.Duration + 1f / 60f + 1e-4f);
        }
        finally { Directory.Delete(folder, true); }
    }

    [Fact]
    public void TrimmedLosslessClipSavesThroughTheInterleavedWriter()
    {
        string source = Fixture("Meshes", "Actors", "Character", "Animations", "Paired",
                                "PairedKill2HMBashKneeAndHead_AttackerLead.hkx");
        if (!File.Exists(source)) return;

        string folder = Directory.CreateTempSubdirectory("bgs-geometry-lossless").FullName;
        try
        {
            string path = Path.Combine(folder, "paired.hkx");
            File.Copy(source, path);

            var reader = new HkxBinaryReader();
            var clip = reader.ReadAnimation(path);
            Assert.Equal("hkaLosslessCompressedAnimation", clip.AnimationClass);
            Assert.Equal(79, clip.NumFrames);
            Assert.Equal(7, clip.Annotations.Count);

            var cut = AnimationEdit.Trim(clip, RootMotion.Read(path), 10, 60);
            Assert.True(cut.AnnotationsDropped > 0);

            var result = AnimationSaveTransaction.Commit(
                path, cut.Animation, DocumentSourceStamp.Capture(path),
                editedTrack: -1, editedFrame: -1, timeline: NativeAnimation.Timeline.Of(cut));

            Assert.True(result.Committed, result.Message);

            var written = reader.ReadAnimation(path);
            Assert.Equal(NativeAnimation.InterleavedClass, written.AnimationClass);
            Assert.Equal(cut.Animation.NumFrames, written.NumFrames);
            Assert.Equal(cut.Animation.Duration, written.Duration, 3);
            Assert.Equal(cut.Animation.Annotations.Count, written.Annotations.Count);
            foreach (var note in written.Annotations)
                Assert.InRange(note.Time, 0f, written.Duration + 1f / 60f + 1e-4f);
        }
        finally { Directory.Delete(folder, true); }
    }

    [Fact]
    public void LoopedSplineClipSavesTheFirstFrameIntoTheLast()
    {
        string source = Fixture("Meshes", "Actors", "RadRoach", "Animations", "TurnLeft90.hkx");
        if (!File.Exists(source)) return;

        string folder = Directory.CreateTempSubdirectory("bgs-geometry-loop").FullName;
        try
        {
            string path = Path.Combine(folder, "TurnLeft90.hkx");
            File.Copy(source, path);

            var reader = new HkxBinaryReader();
            var clip = reader.ReadAnimation(path);
            var looped = AnimationEdit.Loop(clip, RootMotion.Read(path), 4);

            var result = AnimationSaveTransaction.Commit(
                path, looped.Animation, DocumentSourceStamp.Capture(path),
                editedTrack: -1, editedFrame: -1,
                timeline: new NativeAnimation.Timeline(looped.Animation.Duration, 0f, clip.Duration, 1f,
                                                       looped.Motion));

            Assert.True(result.Committed, result.Message);

            var written = reader.ReadAnimation(path);
            Assert.Equal(19, written.NumFrames);
            Assert.Equal(clip.Duration, written.Duration, 3);

            for (int t = 0; t < written.Tracks.Count; t++)
            {
                float drift = (written.Tracks[t].Translations[^1] - written.Tracks[t].Translations[0]).Length();
                Assert.True(drift < 0.05f, $"track {t} landed {drift} from the first frame");
            }
        }
        finally { Directory.Delete(folder, true); }
    }

    [Fact]
    public void CommitRefusesATimelineThatDoesNotMatchTheAnimation()
    {
        string source = Fixture("Meshes", "Actors", "RadRoach", "Animations", "TurnLeft90.hkx");
        if (!File.Exists(source)) return;

        string folder = Directory.CreateTempSubdirectory("bgs-geometry-mismatch").FullName;
        try
        {
            string path = Path.Combine(folder, "TurnLeft90.hkx");
            File.Copy(source, path);
            byte[] original = File.ReadAllBytes(path);

            var clip = new HkxBinaryReader().ReadAnimation(path);
            var cut = AnimationEdit.Trim(clip, RootMotion.Read(path), 4, 14);

            var result = AnimationSaveTransaction.Commit(
                path, clip, DocumentSourceStamp.Capture(path),
                editedTrack: -1, editedFrame: -1, timeline: NativeAnimation.Timeline.Of(cut));

            Assert.False(result.Committed);
            Assert.Contains("duration", result.Message, StringComparison.OrdinalIgnoreCase);
            Assert.Equal(original, File.ReadAllBytes(path));
            Assert.False(File.Exists(path + ".bak"));
        }
        finally { Directory.Delete(folder, true); }
    }

    private static string Fixture(params string[] parts) =>
        Path.Combine(new[] { AppContext.BaseDirectory, "fixtures", "vanilla" }.Concat(parts).ToArray());

    private static HkxAnimationData Ramp(int frames, int tracks = 1)
    {
        var animation = new HkxAnimationData
        {
            AnimationClass = "hkaInterleavedUncompressedAnimation",
            NumFrames = frames,
            NumTracks = tracks,
            FrameDuration = 1f / 30f,
            Duration = (frames - 1) / 30f,
        };

        for (int t = 0; t < tracks; t++)
        {
            var track = new HkxTrackData { RotationAnimated = true };
            for (int axis = 0; axis < 3; axis++)
            {
                track.TranslationAnimated[axis] = true;
                track.ScaleAnimated[axis] = true;
            }

            for (int frame = 0; frame < frames; frame++)
            {
                track.Translations.Add(new Vector3(frame + t * 10f, frame * 2f, -frame));
                track.Rotations.Add(Quaternion.CreateFromAxisAngle(Vector3.UnitZ, frame * 0.2f + t * 0.1f));
                track.Scales.Add(Vector3.One);
            }

            animation.Tracks.Add(track);
        }

        return animation;
    }
}
