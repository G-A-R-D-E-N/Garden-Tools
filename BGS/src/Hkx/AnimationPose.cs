using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public static class AnimationPose
{

    public readonly record struct Bone(int Index, string Name, int Parent, Vector3 Position, Quaternion Rotation, Matrix4x4 World)
    {
        public Bone(int Index, string Name, int Parent, Vector3 Position, Quaternion Rotation)
            : this(Index, Name, Parent, Position, Rotation,
                   Matrix4x4.CreateFromQuaternion(Rotation) * Matrix4x4.CreateTranslation(Position)) { }
    }

    public sealed class Pose
    {
        public int Frame;
        public float Time;
        public readonly List<Bone> Bones = new();
        public readonly List<float> Floats = new();

        public readonly List<(int From, int To)> Links = new();

        public Vector3 Min = new(float.MaxValue);
        public Vector3 Max = new(float.MinValue);

        public Vector3 Centre => Bones.Count == 0 ? Vector3.Zero : (Min + Max) * 0.5f;
        public float Radius
        {
            get
            {
                if (Bones.Count == 0) return 1f;
                var size = Max - Min;
                return Math.Max(0.001f, Math.Max(size.X, Math.Max(size.Y, size.Z)) * 0.5f);
            }
        }
    }

    public static Pose ReferencePose(HkxSkeleton skeleton) => At(skeleton, null, 0);

    public static Pose At(HkxSkeleton skeleton, HkxAnimationData? animation, int frame) => At(skeleton, animation, (float)frame);

    public static Pose At(HkxSkeleton skeleton, HkxAnimationData? animation, float sample)
    {
        if (!float.IsFinite(sample)) throw new ArgumentOutOfRangeException(nameof(sample));
        if (animation != null) sample = Math.Clamp(sample, 0, Math.Max(0, animation.NumFrames - 1));
        int frame = (int)sample;
        var pose = new Pose { Frame = frame };
        int count = skeleton.BoneNames.Count;
        if (count == 0) return pose;
        if (skeleton.ParentIndices.Count != count)
            throw new InvalidDataException(
                $"The skeleton names {count} bone(s) but has {skeleton.ParentIndices.Count} parent index(es).");

        if (animation != null)
        {
            frame = Math.Clamp(frame, 0, Math.Max(0, animation.NumFrames - 1));
            pose.Frame = frame;
            pose.Time = sample * animation.FrameDuration;
            foreach (var values in animation.FloatTracks)
            {
                if (values.Count == 0) { pose.Floats.Add(0f); continue; }
                int at = Math.Min(frame, values.Count - 1);
                pose.Floats.Add(float.Lerp(values[at], values[Math.Min(at + 1, values.Count - 1)], sample - frame));
            }
        }

        var trackForBone = TracksByBone(skeleton, animation);
        var world = new Matrix4x4[count];
        var rotation = new Quaternion[count];

        for (int i = 0; i < count; i++)
        {
            var local = Local(skeleton, animation, trackForBone[i], i, frame);
            if (sample > frame && animation != null)
            {
                var next = Local(skeleton, animation, trackForBone[i], i, Math.Min(frame + 1, animation.NumFrames - 1));
                float fraction = sample - frame;
                local = new HkxBonePose(Vector3.Lerp(local.Translation, next.Translation, fraction),
                    Quaternion.Slerp(local.Rotation, next.Rotation, fraction), Vector3.Lerp(local.Scale, next.Scale, fraction));
            }
            var matrix = Matrix4x4.CreateScale(local.Scale)
                       * Matrix4x4.CreateFromQuaternion(local.Rotation)
                       * Matrix4x4.CreateTranslation(local.Translation);

            int parent = skeleton.ParentIndices[i];

            bool composed = parent >= 0 && parent < i;
            if (parent != -1 && !composed)
                throw new InvalidDataException(
                    $"Bone '{skeleton.BoneNames[i]}' has parent index {parent}; a parent must be -1 or an earlier bone.");
            world[i] = composed ? matrix * world[parent] : matrix;
            rotation[i] = composed ? Quaternion.Normalize(rotation[parent] * local.Rotation) : local.Rotation;

            var position = world[i].Translation;
            pose.Bones.Add(new Bone(i, skeleton.BoneNames[i], composed ? parent : -1, position, rotation[i], world[i]));
            pose.Min = Vector3.Min(pose.Min, position);
            pose.Max = Vector3.Max(pose.Max, position);

            if (composed) pose.Links.Add((parent, i));
        }

        return pose;
    }

    private static bool UndrivenIsIdentity(HkxAnimationData animation) =>
        animation.AnimationClass == "hkaSplineCompressedAnimation";

    public static HkxBonePose Local(HkxSkeleton skeleton, HkxAnimationData? animation,
                                    int track, int bone, int frame)
    {
        if (bone >= skeleton.ReferencePose.Count)
            throw new InvalidDataException($"bone {bone} has no reference pose: the skeleton names " +
                                           $"{skeleton.BoneNames.Count} bone(s) and has {skeleton.ReferencePose.Count} reference pose(s).");
        var reference = skeleton.ReferencePose[bone];
        if (animation == null || track < 0 || track >= animation.Tracks.Count) return reference;

        var data = animation.Tracks[track];
        var undriven = UndrivenIsIdentity(animation)
            ? new HkxBonePose { Translation = Vector3.Zero, Rotation = Quaternion.Identity, Scale = Vector3.One }
            : reference;
        var pose = reference;

        var t = frame < data.Translations.Count ? data.Translations[frame] : undriven.Translation;
        pose.Translation = new Vector3(
            data.TranslationAnimated[0] ? t.X : undriven.Translation.X,
            data.TranslationAnimated[1] ? t.Y : undriven.Translation.Y,
            data.TranslationAnimated[2] ? t.Z : undriven.Translation.Z);

        if (data.RotationAnimated && frame < data.Rotations.Count)
        {
            var q = data.Rotations[frame];

            pose.Rotation = q.LengthSquared() > 1e-8f ? Quaternion.Normalize(q) : reference.Rotation;
        }
        else
        {
            pose.Rotation = undriven.Rotation;
        }

        var s = frame < data.Scales.Count ? data.Scales[frame] : undriven.Scale;
        pose.Scale = new Vector3(
            data.ScaleAnimated[0] ? s.X : undriven.Scale.X,
            data.ScaleAnimated[1] ? s.Y : undriven.Scale.Y,
            data.ScaleAnimated[2] ? s.Z : undriven.Scale.Z);

        return pose;
    }

    public static int[] TracksByBone(HkxSkeleton skeleton, HkxAnimationData? animation)
    {
        var forBone = new int[skeleton.BoneNames.Count];
        Array.Fill(forBone, -1);
        if (animation == null) return forBone;

        if (animation.TrackToBoneIndices.Count > 0)
        {
            for (int track = 0; track < animation.TrackToBoneIndices.Count; track++)
            {
                int bone = animation.TrackToBoneIndices[track];
                if (bone >= 0 && bone < forBone.Length) forBone[bone] = track;
            }
            return forBone;
        }

        if (animation.Tracks.Count == forBone.Length)
            for (int i = 0; i < forBone.Length; i++) forBone[i] = i;

        return forBone;
    }

    public static float Distance(Pose a, Pose b)
    {
        float total = 0;
        int count = Math.Min(a.Bones.Count, b.Bones.Count);
        for (int i = 0; i < count; i++) total += Vector3.Distance(a.Bones[i].Position, b.Bones[i].Position);
        return total;
    }

    public static string? WhyNotPosable(HkxSkeleton? skeleton, HkxAnimationData? animation)
    {
        if (skeleton == null || skeleton.BoneNames.Count == 0)
            return "No skeleton was resolved for this file. The Chain tab says which rig the character names.";
        if (animation == null || animation.NumFrames <= 0)
            return "This animation decoded to no frames, so there is nothing to play.";

        if (skeleton.ParentIndices.Count != skeleton.BoneNames.Count)
            return $"This skeleton names {skeleton.BoneNames.Count} bone(s) but has {skeleton.ParentIndices.Count} " +
                   "parent index(es), so the bone hierarchy is not readable.";
        if (skeleton.ReferencePose.Count != skeleton.BoneNames.Count)
            return $"This skeleton names {skeleton.BoneNames.Count} bone(s) but has {skeleton.ReferencePose.Count} " +
                   "reference pose(s), so there is no rest pose to show for every bone.";

        if (animation.TrackToBoneIndices.Count > 0)
        {
            if (animation.TrackToBoneIndices.Count != animation.Tracks.Count)
                return $"This animation has {animation.Tracks.Count} tracks but its bone map names " +
                       $"{animation.TrackToBoneIndices.Count}, so which bone each track drives is not readable.";
            if (new HashSet<int>(animation.TrackToBoneIndices).Count != animation.TrackToBoneIndices.Count)
                return "This animation drives one bone with two tracks, so which of them plays is not readable.";

            int highest = 0;
            foreach (int bone in animation.TrackToBoneIndices) highest = Math.Max(highest, bone);
            if (highest >= skeleton.BoneNames.Count)
                return $"This animation drives bone {highest}, and this skeleton has " +
                       $"{skeleton.BoneNames.Count}. It was authored against a different rig, so what " +
                       "is drawn would not be what plays.";
        }
        else if (animation.Tracks.Count != skeleton.BoneNames.Count)
        {
            return $"This animation has {animation.Tracks.Count} tracks and names no bone for any of " +
                   $"them, and the skeleton has {skeleton.BoneNames.Count} bones. Which track drives " +
                   "which bone is not readable, so the reference pose is shown instead.";
        }

        return null;
    }
}
