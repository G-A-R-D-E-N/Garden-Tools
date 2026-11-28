using System;
using System.Collections.Generic;
using System.Text;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tests;

internal static class SyntheticAnimation
{
    public const string LosslessClass = "hkaLosslessCompressedAnimation";

    public const int FrameRate = 60;

    public static byte[] Clip(int frames, IReadOnlyList<(float Time, string Text)> notes,
                              IReadOnlyList<RootMotion.Sample>? motion = null, System.Numerics.Vector3? up = null,
                              bool sharedFrame = false)
    {
        if (frames < 2) throw new ArgumentOutOfRangeException(nameof(frames));

        var crafted = new CraftedPackfile();
        var animation = crafted.Object(LosslessClass);
        var binding = crafted.Object("hkaAnimationBinding");
        crafted.Attach(binding, "animation", animation);

        crafted.SetScalar(animation, "hkaAnimation", "numberOfTransformTracks", 1);
        crafted.SetScalar(animation, LosslessClass, "numFrames", frames);
        crafted.SetReal(animation, "hkaAnimation", "duration", (frames - 1) / (float)FrameRate);

        crafted.SetArray(animation, LosslessClass, "translationTypeAndOffsets", 1, crafted.AppendBlock(8));
        crafted.SetArray(animation, LosslessClass, "rotationTypeAndOffsets", 1, crafted.AppendBlock(2));
        crafted.SetArray(animation, LosslessClass, "scaleTypeAndOffsets", 1, crafted.AppendBlock(8));

        int trackField = crafted.Offset(animation, "hkaAnimation", "annotationTracks");
        int tracks = crafted.AppendBlock(0x18);
        crafted.SetPointer(trackField, tracks);
        crafted.SetWord(trackField + 8, 1);
        crafted.SetWord(trackField + 12, unchecked((int)0x80000001u));

        crafted.SetPointer(tracks, crafted.AppendData(Encoding.ASCII.GetBytes("all\0")));

        int notesAt = notes.Count == 0 ? 0 : crafted.AppendBlock(notes.Count * 0x10);
        if (notes.Count > 0) crafted.SetPointer(tracks + 8, notesAt);
        crafted.SetWord(tracks + 16, notes.Count);
        crafted.SetWord(tracks + 20, unchecked((int)(0x80000000u | (uint)notes.Count)));

        for (int n = 0; n < notes.Count; n++)
        {
            crafted.SetFloat(notesAt + n * 0x10, notes[n].Time);
            crafted.SetPointer(notesAt + n * 0x10 + 8,
                               crafted.AppendData(Encoding.ASCII.GetBytes(notes[n].Text + "\0")));
        }

        if (motion is { Count: > 0 })
        {
            var frame = AttachMotion(crafted, animation, frames, motion);
            if (up is { } axis)
            {
                int upField = crafted.Offset(frame, NativeAnimation.ReferenceFrameClass, "up");
                crafted.SetFloat(upField, axis.X);
                crafted.SetFloat(upField + 4, axis.Y);
                crafted.SetFloat(upField + 8, axis.Z);
            }
            if (sharedFrame) crafted.Attach(crafted.Object(LosslessClass), "extractedMotion", frame);
        }

        return crafted.Build(out _);
    }

    public static NativeAppend.Added AttachMotion(CraftedPackfile crafted, NativeAppend.Added animation,
                                                  int frames, IReadOnlyList<RootMotion.Sample> motion)
    {
        var frame = crafted.Object(NativeAnimation.ReferenceFrameClass);
        crafted.Attach(animation, "extractedMotion", frame);
        crafted.SetReal(frame, NativeAnimation.ReferenceFrameClass, "duration",
                        (frames - 1) / (float)FrameRate);

        int samplesField = crafted.Offset(frame, NativeAnimation.ReferenceFrameClass,
                                          "referenceFrameSamples");
        int samples = crafted.AppendBlock(motion.Count * 0x10);
        crafted.SetPointer(samplesField, samples);
        crafted.SetWord(samplesField + 8, motion.Count);
        crafted.SetWord(samplesField + 12, unchecked((int)(0x80000000u | (uint)motion.Count)));

        for (int i = 0; i < motion.Count; i++)
        {
            crafted.SetFloat(samples + i * 0x10, motion[i].Position.X);
            crafted.SetFloat(samples + i * 0x10 + 4, motion[i].Position.Y);
            crafted.SetFloat(samples + i * 0x10 + 8, motion[i].Position.Z);
            crafted.SetFloat(samples + i * 0x10 + 12, motion[i].TurnRadians);
        }

        return frame;
    }

    public static RootMotion.Motion Travel(int samples, float perSample)
    {
        var motion = new RootMotion.Motion { Duration = (samples - 1) * perSample };
        for (int i = 0; i < samples; i++)
            motion.Samples.Add(new RootMotion.Sample(
                new System.Numerics.Vector3(i * 10f, 0f, -i), i * 0.25f));

        return motion;
    }
}
