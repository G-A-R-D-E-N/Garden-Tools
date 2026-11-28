using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

using Numerics = System.Numerics;

public enum HkxAnimationKind
{
    Interleaved = 0,
    Spline = 1,
    View = 2,
    Lossless = 3,
    Predictive = 4,
    Quantized = 5,
}

public sealed class HkxBone
{
    public required string Name { get; init; }

    public required int ParentIndex { get; init; }

    public required Numerics.Vector3 Translation { get; init; }

    public required Numerics.Quaternion Rotation { get; init; }

    public required Numerics.Vector3 Scale { get; init; }

    public bool LockTranslation { get; init; }
}

public readonly record struct HkxLocalFrame(short BoneIndex, bool HasFrame);

public readonly record struct HkxPartition(string? Name, short StartBoneIndex, short BoneCount);

public sealed class HkxSkeleton
{
    public required string Name { get; init; }

    public required HkxBone[] Bones { get; init; }

    public float[] ReferenceFloats { get; init; } = Array.Empty<float>();

    public string?[] FloatSlots { get; init; } = Array.Empty<string?>();

    public HkxLocalFrame[] LocalFrames { get; init; } = Array.Empty<HkxLocalFrame>();

    public HkxPartition[] Partitions { get; init; } = Array.Empty<HkxPartition>();
}

public sealed class HkxAnimationTrack
{
    public required Numerics.Vector3[] Translations { get; init; }

    public required Numerics.Quaternion[] Rotations { get; init; }

    public required Numerics.Vector3[] Scales { get; init; }

    public required bool[] TranslationAnimated { get; init; }

    public required bool RotationAnimated { get; init; }

    public required bool[] ScaleAnimated { get; init; }
}

public sealed class HkxAnnotation
{
    public required float Time { get; init; }

    public required string Text { get; init; }
}

public sealed class HkxAnnotationTrack
{
    public required string Name { get; init; }

    public required HkxAnnotation[] Annotations { get; init; }
}

public sealed class HkxAnimation
{
    public required HkxAnimationKind Kind { get; init; }

    public required string ClassName { get; init; }

    public required int Type { get; init; }

    public required float Duration { get; init; }

    public required int FrameCount { get; init; }

    public required float FrameDuration { get; init; }

    public required int FloatTrackCount { get; init; }

    public required int BlockCount { get; init; }

    public required int MaxFramesPerBlock { get; init; }

    public required float BlockDuration { get; init; }

    public required HkxAnimationTrack[] Tracks { get; init; }

    public required HkxAnnotationTrack[] AnnotationTracks { get; init; }

    public required float[] FloatTracks { get; init; }
}

public sealed class HkxBinding
{
    public required string OriginalSkeletonName { get; init; }

    public required long? AnimationOffset { get; init; }

    public required short[] TransformTrackToBoneIndices { get; init; }

    public required short[] FloatTrackToFloatSlotIndices { get; init; }

    public required short[] PartitionIndices { get; init; }

    public required int BlendHint { get; init; }
}

public enum HkxMotionKind
{
    None = 0,
    Default = 1,
    Unsupported = 2,
}

public sealed class HkxMotion
{
    public required HkxMotionKind Kind { get; init; }

    public required float Duration { get; init; }

    public required Numerics.Vector3 Up { get; init; }

    public required Numerics.Vector3 Forward { get; init; }

    // Constraint: each sample is (x, y, z, w) and w is the yaw in radians about Up.
    public required Numerics.Vector4[] Samples { get; init; }

    public required Numerics.Vector3 NetTranslation { get; init; }

    public required float NetTurnRadians { get; init; }

    public required float MeanPlanarSpeed { get; init; }

    public required float MeanTurnRate { get; init; }
}

public sealed class HkxFile
{
    private HkxFile(IntPtr handle)
    {
        (Skeleton, SkeletonError) = Part<HkxSkeleton>(handle, ReadSkeleton);
        (Animation, AnimationError) = Part<HkxAnimation>(handle, ReadAnimation);
        (Binding, BindingError) = Part<HkxBinding>(handle, ReadBinding);
        (Motion, MotionError) = Part<HkxMotion>(handle, ReadMotion);
        (Behavior, BehaviorError) = Part<HkbGraph>(handle, HkbGraphReader.Read);
    }

    public HkxSkeleton? Skeleton { get; }

    public string? SkeletonError { get; }

    public HkxAnimation? Animation { get; }

    public string? AnimationError { get; }

    public HkxBinding? Binding { get; }

    public string? BindingError { get; }

    public HkxMotion? Motion { get; }

    public string? MotionError { get; }

    // Constraint: null with no error means the packfile holds no behaviour graph; a graph from another Havok build is the error.
    public HkbGraph? Behavior { get; }

    public string? BehaviorError { get; }

    public static unsafe HkxFile FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("an HKX packfile needs at least a header", nameof(bytes));
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.HkxRead(pointer, (nuint)bytes.Length, &handle));
        }
        try
        {
            return new HkxFile(handle);
        }
        finally
        {
            RadicleNative.HkxDestroy(handle);
        }
    }

    private static (T? Value, string? Error) Part<T>(IntPtr handle, Func<IntPtr, T?> read) where T : class
    {
        try
        {
            return (read(handle), null);
        }
        catch (RadicleException error) when (error.Status == Status.ParseError)
        {
            return (null, error.Message);
        }
    }

    private static unsafe HkxSkeleton? ReadSkeleton(IntPtr handle)
    {
        nuint count = 0;
        if (!Call.Found(RadicleNative.HkxSkeletonBoneCount(handle, &count))) return null;
        byte* name = null;
        nuint nameSize = 0;
        Call.Check(RadicleNative.HkxSkeletonName(handle, &name, &nameSize));
        var skeletonName = Call.TakeText(name, nameSize);

        var bones = new HkxBone[checked((int)count)];
        for (var index = 0; index < bones.Length; ++index)
        {
            byte* boneName = null;
            nuint boneNameSize = 0;
            Call.Check(RadicleNative.HkxSkeletonBoneName(handle, (nuint)index, &boneName, &boneNameSize));
            var text = Call.TakeText(boneName, boneNameSize);
            short parent = 0;
            Call.Check(RadicleNative.HkxSkeletonBoneParent(handle, (nuint)index, &parent));
            HkxQsTransform pose;
            Call.Check(RadicleNative.HkxSkeletonBonePose(handle, (nuint)index, &pose));
            int locked = 0;
            Call.Check(RadicleNative.HkxSkeletonBoneLockTranslation(handle, (nuint)index, &locked));
            bones[index] = new HkxBone {
                Name = text,
                ParentIndex = parent,
                Translation = new Numerics.Vector3(pose.Translation[0], pose.Translation[1], pose.Translation[2]),
                Rotation = new Numerics.Quaternion(pose.Rotation[0], pose.Rotation[1], pose.Rotation[2], pose.Rotation[3]),
                Scale = new Numerics.Vector3(pose.Scale[0], pose.Scale[1], pose.Scale[2]),
                LockTranslation = locked != 0,
            };
        }
        float* values = null;
        nuint valueCount = 0;
        Call.Check(RadicleNative.HkxSkeletonReferenceFloats(handle, &values, &valueCount));
        var referenceFloats = Call.TakeFloats(values, valueCount);

        nuint slotCount = 0;
        Call.Check(RadicleNative.HkxSkeletonFloatSlotCount(handle, &slotCount));
        var slots = new string?[checked((int)slotCount)];
        for (var index = 0; index < slots.Length; ++index)
        {
            int named = 0;
            byte* slotBytes = null;
            nuint slotSize = 0;
            Call.Check(RadicleNative.HkxSkeletonFloatSlot(handle, (nuint)index, &named, &slotBytes, &slotSize));
            slots[index] = named != 0 ? Call.TakeText(slotBytes, slotSize) : null;
        }

        nuint frameCount = 0;
        Call.Check(RadicleNative.HkxSkeletonLocalFrameCount(handle, &frameCount));
        var frames = new HkxLocalFrame[checked((int)frameCount)];
        for (var index = 0; index < frames.Length; ++index)
        {
            short frameBone = 0;
            int hasFrame = 0;
            Call.Check(RadicleNative.HkxSkeletonLocalFrame(handle, (nuint)index, &frameBone, &hasFrame));
            frames[index] = new HkxLocalFrame(frameBone, hasFrame != 0);
        }

        nuint partitionCount = 0;
        Call.Check(RadicleNative.HkxSkeletonPartitionCount(handle, &partitionCount));
        var partitions = new HkxPartition[checked((int)partitionCount)];
        for (var index = 0; index < partitions.Length; ++index)
        {
            int named = 0;
            byte* partitionBytes = null;
            nuint partitionSize = 0;
            short start = 0;
            short boneCount = 0;
            Call.Check(RadicleNative.HkxSkeletonPartition(handle, (nuint)index, &named, &partitionBytes, &partitionSize, &start, &boneCount));
            partitions[index] = new HkxPartition(named != 0 ? Call.TakeText(partitionBytes, partitionSize) : null, start, boneCount);
        }
        return new HkxSkeleton
        {
            Name = skeletonName, Bones = bones, ReferenceFloats = referenceFloats, FloatSlots = slots,
            LocalFrames = frames, Partitions = partitions,
        };
    }

    private static unsafe HkxAnimation? ReadAnimation(IntPtr handle)
    {
        HkxAnimationHeader header;
        if (!Call.Found(RadicleNative.HkxReadAnimationHeader(handle, &header))) return null;
        byte* className = null;
        nuint classNameSize = 0;
        Call.Check(RadicleNative.HkxAnimationClassName(handle, &className, &classNameSize));
        var name = Call.TakeText(className, classNameSize);

        var tracks = new HkxAnimationTrack[checked((int)header.TrackCount)];
        for (var index = 0; index < tracks.Length; ++index) tracks[index] = ReadTrack(handle, (nuint)index);

        var annotationTracks = new HkxAnnotationTrack[checked((int)header.AnnotationTrackCount)];
        for (var index = 0; index < annotationTracks.Length; ++index) {
            annotationTracks[index] = ReadAnnotationTrack(handle, (nuint)index);
        }

        float* floats = null;
        nuint floatCount = 0;
        Call.Check(RadicleNative.HkxAnimationFloatTracks(handle, &floats, &floatCount));
        return new HkxAnimation {
            Kind = (HkxAnimationKind)header.Kind,
            ClassName = name,
            Type = header.Type,
            Duration = header.Duration,
            FrameCount = checked((int)header.FrameCount),
            FrameDuration = header.FrameDuration,
            FloatTrackCount = header.FloatTrackCount,
            BlockCount = checked((int)header.BlockCount),
            MaxFramesPerBlock = checked((int)header.BlockMaxFramesPerBlock),
            BlockDuration = header.BlockDuration,
            Tracks = tracks,
            AnnotationTracks = annotationTracks,
            FloatTracks = Call.TakeFloats(floats, floatCount),
        };
    }

    private static unsafe HkxAnimationTrack ReadTrack(IntPtr handle, nuint track)
    {
        HkxTrackFlags flags;
        Call.Check(RadicleNative.HkxAnimationTrackFlags(handle, track, &flags));
        var translations = Call.ReadFloats(handle, track, RadicleNative.HkxAnimationTrackTranslations);
        var rotations = Call.ReadFloats(handle, track, RadicleNative.HkxAnimationTrackRotations);
        var scales = Call.ReadFloats(handle, track, RadicleNative.HkxAnimationTrackScales);
        return new HkxAnimationTrack {
            Translations = Call.Reinterpret<Numerics.Vector3>(translations),
            Rotations = Call.Reinterpret<Numerics.Quaternion>(rotations),
            Scales = Call.Reinterpret<Numerics.Vector3>(scales),
            TranslationAnimated = new[] { flags.TranslationAnimated[0] != 0, flags.TranslationAnimated[1] != 0,
                                          flags.TranslationAnimated[2] != 0 },
            RotationAnimated = flags.RotationAnimated != 0,
            ScaleAnimated = new[] { flags.ScaleAnimated[0] != 0, flags.ScaleAnimated[1] != 0,
                                    flags.ScaleAnimated[2] != 0 },
        };
    }

    private static unsafe HkxAnnotationTrack ReadAnnotationTrack(IntPtr handle, nuint track)
    {
        byte* name = null;
        nuint nameSize = 0;
        Call.Check(RadicleNative.HkxAnimationAnnotationTrackName(handle, track, &name, &nameSize));
        var trackName = Call.TakeText(name, nameSize);
        nuint count = 0;
        Call.Check(RadicleNative.HkxAnimationAnnotationCount(handle, track, &count));
        var annotations = new HkxAnnotation[checked((int)count)];
        for (var index = 0; index < annotations.Length; ++index) {
            float time = 0;
            Call.Check(RadicleNative.HkxAnimationAnnotationTime(handle, track, (nuint)index, &time));
            byte* text = null;
            nuint textSize = 0;
            Call.Check(RadicleNative.HkxAnimationAnnotationText(handle, track, (nuint)index, &text, &textSize));
            annotations[index] = new HkxAnnotation { Time = time, Text = Call.TakeText(text, textSize) };
        }
        return new HkxAnnotationTrack { Name = trackName, Annotations = annotations };
    }

    private static unsafe HkxBinding? ReadBinding(IntPtr handle)
    {
        HkxBindingHeader header;
        if (!Call.Found(RadicleNative.HkxReadBindingHeader(handle, &header))) return null;
        byte* name = null;
        nuint nameSize = 0;
        Call.Check(RadicleNative.HkxBindingOriginalSkeletonName(handle, &name, &nameSize));
        var originalName = Call.TakeText(name, nameSize);
        short* indices = null;
        nuint indexCount = 0;
        Call.Check(RadicleNative.HkxBindingTransformTrackToBone(handle, &indices, &indexCount));
        var toBone = Call.TakeIndices(indices, indexCount);
        Call.Check(RadicleNative.HkxBindingFloatTrackToFloatSlot(handle, &indices, &indexCount));
        var floatSlots = Call.TakeIndices(indices, indexCount);
        Call.Check(RadicleNative.HkxBindingPartitions(handle, &indices, &indexCount));
        var partitions = Call.TakeIndices(indices, indexCount);
        return new HkxBinding {
            OriginalSkeletonName = originalName,
            AnimationOffset = header.HasAnimationOffset != 0 ? checked((long)header.AnimationOffset) : null,
            TransformTrackToBoneIndices = toBone,
            FloatTrackToFloatSlotIndices = floatSlots,
            PartitionIndices = partitions,
            BlendHint = header.BlendHint,
        };
    }

    private static unsafe HkxMotion? ReadMotion(IntPtr handle)
    {
        HkxMotionHeader header;
        if (!Call.Found(RadicleNative.HkxReadMotionHeader(handle, &header))) return null;
        float* values = null;
        nuint count = 0;
        Call.Check(RadicleNative.HkxMotionSamples(handle, &values, &count));
        return new HkxMotion {
            Kind = (HkxMotionKind)header.Kind,
            Duration = header.Duration,
            Up = new Numerics.Vector3(header.Up[0], header.Up[1], header.Up[2]),
            Forward = new Numerics.Vector3(header.Forward[0], header.Forward[1], header.Forward[2]),
            Samples = Call.Reinterpret<Numerics.Vector4>(Call.TakeFloats(values, count)),
            NetTranslation = new Numerics.Vector3(header.NetTranslation[0], header.NetTranslation[1],
                                                  header.NetTranslation[2]),
            NetTurnRadians = header.NetTurnRadians,
            MeanPlanarSpeed = header.MeanPlanarSpeed,
            MeanTurnRate = header.MeanTurnRate,
        };
    }
}

public enum HkxScalarKind
{
    Bool = 0,
    Int8 = 1,
    Uint8 = 2,
    Int16 = 3,
    Uint16 = 4,
    Int32 = 5,
    Uint32 = 6,
    Real = 7,
}

// Invariant: Offset is relative to the object's start; Value is plain text (true, false, an integer or a real) that HkxScalarEdit.Set accepts back; EnumName is empty unless an enum or flags table names the value.
public sealed record HkxScalarMember(string Name, string Type, HkxScalarKind Kind, bool IsEnum, bool IsFlags, uint Offset,
                                     uint Size, string Value, string EnumName);

// Requires a packfile and an object index into its object list (HkbNode.Object for a behaviour node). Constraint: only fixed-size scalar members are reachable, so a string, pointer, struct or array member is refused, and the result is not validated as a behaviour graph and has not been loaded by the game.
public static unsafe class HkxScalarEdit
{
    public static HkxScalarMember[] List(ReadOnlySpan<byte> packfile, int objectIndex)
    {
        Require(packfile, objectIndex);
        IntPtr list = IntPtr.Zero;
        fixed (byte* pointer = packfile)
        {
            Call.Check(RadicleNative.HkxScalarListRead(pointer, (nuint)packfile.Length, (nuint)objectIndex, &list));
        }
        try
        {
            nuint count;
            Call.Check(RadicleNative.HkxScalarListCount(list, &count));
            var rows = new HkxScalarMember[checked((int)count)];
            for (var index = 0; index < rows.Length; ++index)
            {
                HkxScalarInfo info;
                Call.Check(RadicleNative.HkxScalarListReadInfo(list, (nuint)index, &info));
                rows[index] = new HkxScalarMember(Text(list, index, 0), Text(list, index, 1), (HkxScalarKind)info.Kind,
                                                  info.IsEnum != 0, info.IsFlags != 0, info.Offset, info.Size,
                                                  Text(list, index, 2), Text(list, index, 3));
            }

            return rows;
        }
        finally
        {
            RadicleNative.HkxScalarListDestroy(list);
        }
    }

    // Constraint: returns the whole packfile with only the member's bytes changed; the source is never modified.
    public static byte[] Set(ReadOnlySpan<byte> packfile, int objectIndex, string member, string value)
    {
        Require(packfile, objectIndex);
        ArgumentNullException.ThrowIfNull(member);
        ArgumentNullException.ThrowIfNull(value);
        byte* bytes = null;
        nuint size = 0;
        fixed (byte* pointer = packfile)
        {
            Call.Check(RadicleNative.HkxSetScalar(pointer, (nuint)packfile.Length, (nuint)objectIndex, member, value,
                                                  &bytes, &size));
        }

        return Call.TakeBytes(bytes, size);
    }

    private static void Require(ReadOnlySpan<byte> packfile, int objectIndex)
    {
        if (packfile.IsEmpty) throw new ArgumentException("an HKX packfile needs at least a header", nameof(packfile));
        if (objectIndex < 0) throw new ArgumentOutOfRangeException(nameof(objectIndex));
    }

    private static string Text(IntPtr list, int index, int which)
    {
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.HkxScalarListText(list, (nuint)index, which, &bytes, &size));
        return Call.TakeText(bytes, size);
    }
}
