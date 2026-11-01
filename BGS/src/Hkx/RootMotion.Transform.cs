using System;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public static partial class RootMotion
{
    public static Sample AtTime(Motion motion, float time, bool loop = false)
    {
        if (!float.IsFinite(time) || motion.Duration <= 0) return At(motion, 0);
        if (loop) time = ((time % motion.Duration) + motion.Duration) % motion.Duration;
        return At(motion, time / motion.Duration);
    }

    public static Matrix4x4 TransformAt(Motion motion, float time, bool loop = false)
    {
        if (!motion.Previewable) return Matrix4x4.Identity;
        var sample = AtTime(motion, time, loop);
        var up = motion.Up.LengthSquared() > 1e-8f ? Vector3.Normalize(motion.Up) : Vector3.UnitZ;
        return Matrix4x4.CreateFromAxisAngle(up, sample.TurnRadians)
             * Matrix4x4.CreateTranslation(sample.Position);
    }

    public static AnimationPose.Pose Place(AnimationPose.Pose pose, Motion motion, float time)
    {
        if (!motion.Previewable) return pose;
        var transform = TransformAt(motion, time);
        var turn = Quaternion.CreateFromRotationMatrix(transform);
        var moved = new AnimationPose.Pose { Frame = pose.Frame, Time = pose.Time };
        moved.Links.AddRange(pose.Links);
        foreach (var bone in pose.Bones)
        {
            var position = Vector3.Transform(bone.Position, transform);
            moved.Bones.Add(bone with { Position = position, Rotation = turn * bone.Rotation });
            moved.Min = Vector3.Min(moved.Min, position);
            moved.Max = Vector3.Max(moved.Max, position);
        }
        return moved;
    }
}
