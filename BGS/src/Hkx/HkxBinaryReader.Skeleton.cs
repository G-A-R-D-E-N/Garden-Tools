using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public partial class HkxBinaryReader
{
    private const int SkeletonSize = 136;
    private const int SkeletonNameAt = 16;
    private const int ParentIndicesAt = 24;
    private const int BonesAt = 40;
    private const int ReferencePoseAt = 56;
    private const int ReferenceFloatsAt = 72;
    private const int FloatSlotsAt = 88;
    private const int LocalFramesAt = 104;
    private const int PartitionsAt = 120;

    private const int PointerWidth = 8;

    private const int ReferencePoseStride = 48;

    private static HkxSkeleton? ParseSkeleton(byte[] data, int dataAbs, int skelRel, SectionInfo dataSec,
        Dictionary<int, int> fixups)
    {
        int a = dataAbs + skelRel;
        if (skelRel < 0 || a < dataAbs || a + SkeletonSize > dataSec.End || a + SkeletonSize > data.Length)
            return null;

        int boneStride = StructSize("hkaBone", 0x10);
        int boneLockAt = MemberOffset("hkaBone", "lockTranslation", 8);
        int frameStride = StructSize("hkaSkeletonLocalFrameOnBone", 0x10);
        int frameBoneAt = MemberOffset("hkaSkeletonLocalFrameOnBone", "boneIndex", 8);
        int partitionStride = StructSize("hkaSkeletonPartition", 0x10);
        int partitionStartAt = MemberOffset("hkaSkeletonPartition", "startBoneIndex", 8);
        int partitionCountAt = MemberOffset("hkaSkeletonPartition", "numBones", 10);

        Require16BitMember("hkaSkeletonLocalFrameOnBone", "boneIndex", frameBoneAt, frameStride);
        Require16BitMember("hkaSkeletonPartition", "startBoneIndex", partitionStartAt, partitionStride);
        Require16BitMember("hkaSkeletonPartition", "numBones", partitionCountAt, partitionStride);

        var skel = new HkxSkeleton();

        long dataEnd = Math.Min(Math.Min((long)dataSec.LocalFixupAbs, (long)dataSec.End), data.Length);

        Dictionary<int, (int Section, int Destination)>? globals = null;
        bool Target(int fieldRel, out int target)
        {
            if (fixups.TryGetValue(fieldRel, out target)) return true;
            globals ??= ParseGlobalFixupsWithSections(data, dataSec);
            if (!globals.TryGetValue(fieldRel, out var global)) return false;
            if (global.Section != dataSec.Index)
                throw new InvalidDataException(
                    $"hkaSkeleton member at __data__+{fieldRel} is a global fixup into section " +
                    $"{global.Section}, not the __data__ section ({dataSec.Index})");
            target = global.Destination;
            return true;
        }

        int At(string member, int targetRel)
        {
            long abs = dataAbs + (long)targetRel;
            if (abs < dataAbs || abs >= dataEnd)
                throw new InvalidDataException(
                    $"{member} points at __data__+{targetRel}, outside the __data__ data " +
                    $"({dataAbs}..{dataEnd})");
            return (int)abs;
        }

        int ArrayCount(string member, int count, int stride, int targetRel, out int abs)
        {
            abs = 0;
            if (count == 0) return 0;
            if (count < 0)
                throw new InvalidDataException($"{member} declares a negative count {count}");
            if (stride <= 0)
                throw new InvalidDataException($"{member} has an unusable element size {stride}");

            long start = dataAbs + (long)targetRel;
            if (start < dataAbs || start >= dataEnd)
                throw new InvalidDataException(
                    $"{member} points at __data__+{targetRel}, outside the __data__ data " +
                    $"({dataAbs}..{dataEnd})");

            long wanted = (long)count * stride;
            if (wanted > dataEnd - start)
                throw new InvalidDataException(
                    $"{member} declares {count} elements of {stride} bytes, more than the " +
                    $"{dataEnd - start} bytes left in __data__");

            abs = (int)start;
            return count;
        }

        if (Target(skelRel + SkeletonNameAt, out int nameRel))
            skel.Name = ReadNullTermString(data, At("name", nameRel), 256);

        int parCount = SafeReadI32(data, a + ParentIndicesAt + PointerWidth);
        if (Target(skelRel + ParentIndicesAt, out int parDataRel))
        {
            int count = ArrayCount("parentIndices", parCount, 2, parDataRel, out int parAbs);
            for (int i = 0; i < count; i++)
                skel.ParentIndices.Add(BitConverter.ToInt16(data, parAbs + i * 2));
        }

        int boneCount = SafeReadI32(data, a + BonesAt + PointerWidth);
        if (boneCount < 0) return null;
        if (Target(skelRel + BonesAt, out int bonesDataRel))
        {
            long bonesAt = dataAbs + (long)bonesDataRel;
            if (bonesAt < dataAbs || bonesAt > data.Length ||
                boneCount > (dataSec.End - bonesAt) / boneStride) return null;
            int bonesAbs = (int)bonesAt;
            for (int i = 0; i < boneCount; i++)
            {
                int boneOff = bonesDataRel + i * boneStride;
                if (Target(boneOff, out int bnRel))
                    skel.BoneNames.Add(ReadNullTermString(data, At($"bones[{i}].name", bnRel), 256));
                else
                    skel.BoneNames.Add($"Bone_{i}");

                int lockedAt = bonesAbs + i * boneStride + boneLockAt;
                skel.LockTranslation.Add(CanRead(data, lockedAt, 1) && data[lockedAt] != 0);
            }
        }

        int poseCount = SafeReadI32(data, a + ReferencePoseAt + PointerWidth);
        if (Target(skelRel + ReferencePoseAt, out int poseDataRel))
        {
            int count = ArrayCount("referencePose", poseCount, ReferencePoseStride, poseDataRel,
                out int poseAbs);
            for (int i = 0; i < count; i++)
            {
                int p = poseAbs + i * ReferencePoseStride;
                var tx  = new Vector3(ReadF32(data, p),      ReadF32(data, p + 4),  ReadF32(data, p + 8));
                var rot = new Quaternion(ReadF32(data, p+16), ReadF32(data, p+20), ReadF32(data, p+24), ReadF32(data, p+28));
                var sc  = new Vector3(ReadF32(data, p + 32), ReadF32(data, p + 36), ReadF32(data, p + 40));
                skel.ReferencePose.Add(new HkxBonePose(tx, rot, sc));
            }
        }

        int floatCount = SafeReadI32(data, a + ReferenceFloatsAt + PointerWidth);
        if (Target(skelRel + ReferenceFloatsAt, out int floatsDataRel))
        {
            int count = ArrayCount("referenceFloats", floatCount, 4, floatsDataRel, out int floatsAbs);
            for (int i = 0; i < count; i++)
                skel.ReferenceFloats.Add(ReadF32(data, floatsAbs + i * 4));
        }

        int slotCount = SafeReadI32(data, a + FloatSlotsAt + PointerWidth);
        if (Target(skelRel + FloatSlotsAt, out int slotsDataRel))
        {
            int count = ArrayCount("floatSlots", slotCount, PointerWidth, slotsDataRel, out _);
            for (int i = 0; i < count; i++)
                skel.FloatSlots.Add(Target(slotsDataRel + i * PointerWidth, out int slotRel)
                    ? ReadNullTermString(data, At($"floatSlots[{i}]", slotRel), 256)
                    : null);
        }

        int frameCount = SafeReadI32(data, a + LocalFramesAt + PointerWidth);
        if (Target(skelRel + LocalFramesAt, out int framesDataRel))
        {
            int count = ArrayCount("localFrames", frameCount, frameStride, framesDataRel, out int framesAbs);
            for (int i = 0; i < count; i++)
            {
                int at = framesAbs + i * frameStride;
                bool hasFrame = Target(framesDataRel + i * frameStride, out int frameRel);
                skel.LocalFrames.Add(new HkxSkeletonLocalFrame
                {
                    BoneIndex = BitConverter.ToInt16(data, at + frameBoneAt),
                    HasFrame = hasFrame,
                    FrameOffset = hasFrame ? frameRel : -1,
                });
            }
        }

        int partitionItems = SafeReadI32(data, a + PartitionsAt + PointerWidth);
        if (Target(skelRel + PartitionsAt, out int partsDataRel))
        {
            int count = ArrayCount("partitions", partitionItems, partitionStride, partsDataRel,
                out int partsAbs);
            for (int i = 0; i < count; i++)
            {
                int at = partsAbs + i * partitionStride;
                skel.Partitions.Add(new HkxSkeletonPartition
                {
                    Name = Target(partsDataRel + i * partitionStride, out int partNameRel)
                        ? ReadNullTermString(data, At($"partitions[{i}].name", partNameRel), 256)
                        : null,
                    StartBoneIndex = BitConverter.ToInt16(data, at + partitionStartAt),
                    NumBones = BitConverter.ToInt16(data, at + partitionCountAt),
                });
            }
        }

        if (skel.BoneNames.Count == 0) return null;
        if (string.IsNullOrEmpty(skel.Name) && skel.BoneNames.Count > 0)
            skel.Name = skel.BoneNames[0];
        return skel;
    }

    private static void Require16BitMember(string className, string member, int offset, int stride)
    {
        if (offset < 0 || offset + 2 > stride)
            throw new InvalidDataException(
                $"{className}.{member} sits at {offset} in a {stride}-byte element, so its 16-bit " +
                "value would not fit inside that element");
    }

    private static int MemberOffset(string className, string member, int fallback) =>
        HavokClasses.Shipped.Field(className, member)?.Offset ?? fallback;

    private static int StructSize(string className, int fallback) =>
        HavokClasses.Shipped[className]?.Size is int size and > 0 ? size : fallback;
}
