using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

using Numerics = System.Numerics;

public readonly record struct HkxPlainPose(Numerics.Vector3 Translation, Numerics.Quaternion Rotation, Numerics.Vector3 Scale);

public sealed class HkxPlainTrack
{
    public required int Bone { get; init; }

    public required Numerics.Vector3[] Translations { get; init; }

    public required Numerics.Quaternion[] Rotations { get; init; }

    public required Numerics.Vector3[] Scales { get; init; }
}

public sealed class HkxPlainEvent
{
    public required float Time { get; init; }

    public required string Name { get; init; }
}

public sealed class HkxPlainRootMotion
{
    public required HkxMotionKind Kind { get; init; }

    // Constraint: each delta is (x, y, z, yaw about +Y) since the previous key in that key's heading frame; entry 0 is zero.
    public required Numerics.Vector4[] Deltas { get; init; }

    public required Numerics.Vector3 NetTranslation { get; init; }

    public required float NetYaw { get; init; }

    public required float MeanPlanarSpeed { get; init; }

    public required float MeanTurnRate { get; init; }
}

public sealed class HkxPlainClip
{
    public required float Length { get; init; }

    public required float Rate { get; init; }

    public required bool Loop { get; init; }

    public required int KeyCount { get; init; }

    public required HkxPlainTrack[] Tracks { get; init; }

    public required HkxPlainEvent[] Events { get; init; }

    public required HkxPlainRootMotion RootMotion { get; init; }
}

public sealed class HkxClipRole
{
    public required string Role { get; init; }

    public required string Entry { get; init; }

    public required bool Loop { get; init; }

    public required float PlaybackSpeed { get; init; }
}

// Constraint: every value is in engine axes and metres, so no converter is needed on the managed side.
public static class HkxPlain
{
    public static unsafe HkxPlainPose[] SkeletonRest(ReadOnlySpan<byte> skeletonFile)
    {
        var skeleton = Open(skeletonFile);
        try
        {
            float* values = null;
            nuint count = 0;
            Call.Check(RadicleNative.HkxPlainSkeletonRest(skeleton, &values, &count));
            var floats = Call.TakeFloats(values, count);
            var poses = new HkxPlainPose[floats.Length / 10];
            for (var bone = 0; bone < poses.Length; ++bone)
            {
                var at = bone * 10;
                poses[bone] = new HkxPlainPose(new Numerics.Vector3(floats[at], floats[at + 1], floats[at + 2]),
                                               new Numerics.Quaternion(floats[at + 3], floats[at + 4], floats[at + 5], floats[at + 6]),
                                               new Numerics.Vector3(floats[at + 7], floats[at + 8], floats[at + 9]));
            }
            return poses;
        }
        finally
        {
            RadicleNative.HkxDestroy(skeleton);
        }
    }

    // Requires rate 0 (the clip's own frame rate) or a finite rate up to 240 keys per second.
    public static unsafe HkxPlainClip Sample(ReadOnlySpan<byte> skeletonFile, ReadOnlySpan<byte> clipFile, float rate = 0, bool loop = false)
    {
        var skeleton = Open(skeletonFile);
        try
        {
            var clip = Open(clipFile);
            try
            {
                IntPtr sampled = IntPtr.Zero;
                Call.Check(RadicleNative.HkxPlainClipSample(skeleton, clip, rate, loop ? 1 : 0, &sampled));
                try
                {
                    return Read(sampled);
                }
                finally
                {
                    RadicleNative.HkxPlainClipDestroy(sampled);
                }
            }
            finally
            {
                RadicleNative.HkxDestroy(clip);
            }
        }
        finally
        {
            RadicleNative.HkxDestroy(skeleton);
        }
    }

    // Constraint: a behavior file with no clip generators gives an empty array; roles come back in a fixed table order.
    public static unsafe HkxClipRole[] ClipRoles(ReadOnlySpan<byte> behaviorFile, string? animationRoot = null, string? directory = null)
    {
        var behavior = Open(behaviorFile);
        try
        {
            IntPtr roles = IntPtr.Zero;
            if (!Call.Found(RadicleNative.HkxClipRolesRead(behavior, animationRoot, directory, &roles))) return Array.Empty<HkxClipRole>();
            try
            {
                nuint count = 0;
                Call.Check(RadicleNative.HkxClipRolesCount(roles, &count));
                var result = new HkxClipRole[checked((int)count)];
                for (var index = 0; index < result.Length; ++index)
                {
                    byte* name = null;
                    nuint nameSize = 0;
                    Call.Check(RadicleNative.HkxClipRoleName(roles, (nuint)index, &name, &nameSize));
                    var roleName = Call.TakeText(name, nameSize);
                    byte* entry = null;
                    nuint entrySize = 0;
                    Call.Check(RadicleNative.HkxClipRoleEntry(roles, (nuint)index, &entry, &entrySize));
                    var entryPath = Call.TakeText(entry, entrySize);
                    HkxClipRoleInfo info;
                    Call.Check(RadicleNative.HkxClipRoleReadInfo(roles, (nuint)index, &info));
                    result[index] = new HkxClipRole {
                        Role = roleName,
                        Entry = entryPath,
                        Loop = info.Loop != 0,
                        PlaybackSpeed = info.PlaybackSpeed,
                    };
                }
                return result;
            }
            finally
            {
                RadicleNative.HkxClipRolesDestroy(roles);
            }
        }
        finally
        {
            RadicleNative.HkxDestroy(behavior);
        }
    }

    private static unsafe IntPtr Open(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("an HKX packfile needs at least a header", nameof(bytes));
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.HkxRead(pointer, (nuint)bytes.Length, &handle));
        }
        return handle;
    }

    private static unsafe HkxPlainClip Read(IntPtr clip)
    {
        HkxPlainClipInfo info;
        Call.Check(RadicleNative.HkxPlainClipReadInfo(clip, &info));
        var tracks = new HkxPlainTrack[checked((int)info.TrackCount)];
        for (var index = 0; index < tracks.Length; ++index)
        {
            int bone = 0;
            Call.Check(RadicleNative.HkxPlainClipTrackBone(clip, (nuint)index, &bone));
            tracks[index] = new HkxPlainTrack {
                Bone = bone,
                Translations = Call.Reinterpret<Numerics.Vector3>(Call.ReadFloats(clip, (nuint)index, TranslationReader)),
                Rotations = Call.Reinterpret<Numerics.Quaternion>(Call.ReadFloats(clip, (nuint)index, RotationReader)),
                Scales = Call.Reinterpret<Numerics.Vector3>(Call.ReadFloats(clip, (nuint)index, ScaleReader)),
            };
        }

        var events = new HkxPlainEvent[checked((int)info.EventCount)];
        for (var index = 0; index < events.Length; ++index)
        {
            float time = 0;
            Call.Check(RadicleNative.HkxPlainClipEventTime(clip, (nuint)index, &time));
            byte* name = null;
            nuint nameSize = 0;
            Call.Check(RadicleNative.HkxPlainClipEventName(clip, (nuint)index, &name, &nameSize));
            events[index] = new HkxPlainEvent { Time = time, Name = Call.TakeText(name, nameSize) };
        }

        var kind = (HkxMotionKind)info.RootMotionKind;
        var deltas = Array.Empty<Numerics.Vector4>();
        if (kind == HkxMotionKind.Default)
        {
            float* values = null;
            nuint count = 0;
            Call.Check(RadicleNative.HkxPlainClipRootMotion(clip, &values, &count));
            deltas = Call.Reinterpret<Numerics.Vector4>(Call.TakeFloats(values, count));
        }

        return new HkxPlainClip {
            Length = info.Length,
            Rate = info.Rate,
            Loop = info.Loop != 0,
            KeyCount = checked((int)info.KeyCount),
            Tracks = tracks,
            Events = events,
            RootMotion = new HkxPlainRootMotion {
                Kind = kind,
                Deltas = deltas,
                NetTranslation = new Numerics.Vector3(info.RootNetTranslation[0], info.RootNetTranslation[1], info.RootNetTranslation[2]),
                NetYaw = info.RootNetYaw,
                MeanPlanarSpeed = info.RootMeanPlanarSpeed,
                MeanTurnRate = info.RootMeanTurnRate,
            },
        };
    }

    private static unsafe Status TranslationReader(IntPtr clip, nuint track, float** values, nuint* count) =>
        RadicleNative.HkxPlainClipTrackTranslations(clip, track, values, count);

    private static unsafe Status RotationReader(IntPtr clip, nuint track, float** values, nuint* count) =>
        RadicleNative.HkxPlainClipTrackRotations(clip, track, values, count);

    private static unsafe Status ScaleReader(IntPtr clip, nuint track, float** values, nuint* count) =>
        RadicleNative.HkxPlainClipTrackScales(clip, track, values, count);
}
