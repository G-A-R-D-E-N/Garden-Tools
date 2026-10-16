using System;
using System.Collections.Generic;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public partial class HkxBinaryReader
{
    // hkaSkeleton is 136 bytes in hk_2014.1.0-r1, and these member offsets are the ones the class
    // initializers in the shipped HavokClassLayouts.json declare for it. Every array member is an
    // hkArray<T>: an eight-byte data pointer at the member offset, an int32 count at member + 8 and an
    // int32 capacity at member + 12.
    private const int SkeletonSize = 136;
    private const int SkeletonNameAt = 16;
    private const int ParentIndicesAt = 24;
    private const int BonesAt = 40;
    private const int ReferencePoseAt = 56;
    private const int ReferenceFloatsAt = 72;
    private const int FloatSlotsAt = 88;
    private const int LocalFramesAt = 104;
    private const int PartitionsAt = 120;

    // Fallout 4 writes 64-bit packfiles and ParseHkx refuses any packfile version other than 11, so a
    // pointer field is eight bytes wide here. Pointer fields are still never read as pointers: they are
    // resolved through the local fixup table, falling back to the global fixup table.
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

        var skel = new HkxSkeleton();

        // Local fixups cover pointers inside __data__. Objects that live in another section are reached
        // through the global fixup table instead, so a miss falls back to it once.
        Dictionary<int, int>? globals = null;
        bool Target(int fieldRel, out int target)
        {
            if (fixups.TryGetValue(fieldRel, out target)) return true;
            globals ??= ParseGlobalFixups(data, dataSec);
            return globals.TryGetValue(fieldRel, out target);
        }

        if (Target(skelRel + SkeletonNameAt, out int nameRel))
            skel.Name = ReadNullTermString(data, dataAbs + nameRel, 256);

        int parCount = SafeReadI32(data, a + ParentIndicesAt + PointerWidth);
        if (Target(skelRel + ParentIndicesAt, out int parDataRel))
        {
            int parAbs = dataAbs + parDataRel;
            for (int i = 0, count = Fit(parCount, 2, parAbs, dataSec.End, data.Length); i < count; i++)
                skel.ParentIndices.Add(BitConverter.ToInt16(data, parAbs + i * 2));
        }

        int boneCount = SafeReadI32(data, a + BonesAt + PointerWidth);
        if (boneCount < 0) return null;
        if (Target(skelRel + BonesAt, out int bonesDataRel))
        {
            int bonesAbs = dataAbs + bonesDataRel;
            if (bonesAbs < dataAbs || bonesAbs > data.Length ||
                boneCount > (dataSec.End - bonesAbs) / boneStride) return null;
            for (int i = 0; i < boneCount; i++)
            {
                int boneOff = bonesDataRel + i * boneStride;
                if (Target(boneOff, out int bnRel))
                    skel.BoneNames.Add(ReadNullTermString(data, dataAbs + bnRel, 256));
                else
                    skel.BoneNames.Add($"Bone_{i}");

                int lockedAt = bonesAbs + i * boneStride + boneLockAt;
                skel.LockTranslation.Add(CanRead(data, lockedAt, 1) && data[lockedAt] != 0);
            }
        }

        int poseCount = SafeReadI32(data, a + ReferencePoseAt + PointerWidth);
        if (Target(skelRel + ReferencePoseAt, out int poseDataRel))
        {
            int poseAbs = dataAbs + poseDataRel;
            for (int i = 0, count = Fit(poseCount, ReferencePoseStride, poseAbs, dataSec.End, data.Length);
                 i < count; i++)
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
            int floatsAbs = dataAbs + floatsDataRel;
            for (int i = 0, count = Fit(floatCount, 4, floatsAbs, dataSec.End, data.Length); i < count; i++)
                skel.ReferenceFloats.Add(ReadF32(data, floatsAbs + i * 4));
        }

        int slotCount = SafeReadI32(data, a + FloatSlotsAt + PointerWidth);
        if (Target(skelRel + FloatSlotsAt, out int slotsDataRel))
        {
            int slotsAbs = dataAbs + slotsDataRel;
            for (int i = 0, count = Fit(slotCount, PointerWidth, slotsAbs, dataSec.End, data.Length);
                 i < count; i++)
                skel.FloatSlots.Add(Target(slotsDataRel + i * PointerWidth, out int slotRel)
                    ? ReadNullTermString(data, dataAbs + slotRel, 256)
                    : "");
        }

        int frameCount = SafeReadI32(data, a + LocalFramesAt + PointerWidth);
        if (Target(skelRel + LocalFramesAt, out int framesDataRel))
        {
            int framesAbs = dataAbs + framesDataRel;
            for (int i = 0, count = Fit(frameCount, frameStride, framesAbs, dataSec.End, data.Length);
                 i < count; i++)
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
            int partsAbs = dataAbs + partsDataRel;
            for (int i = 0, count = Fit(partitionItems, partitionStride, partsAbs, dataSec.End, data.Length);
                 i < count; i++)
            {
                int at = partsAbs + i * partitionStride;
                skel.Partitions.Add(new HkxSkeletonPartition
                {
                    Name = Target(partsDataRel + i * partitionStride, out int partNameRel)
                        ? ReadNullTermString(data, dataAbs + partNameRel, 256)
                        : "",
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

    /// <summary>
    /// How many <paramref name="stride"/>-byte elements starting at <paramref name="abs"/> are inside
    /// both the owning section and the file. A hostile count is trimmed to what really exists rather
    /// than refused, matching how the reference pose and parent indices were already read.
    /// </summary>
    private static int Fit(int count, int stride, int abs, int sectionEnd, int length)
    {
        if (count <= 0 || stride <= 0 || abs < 0) return 0;
        int room = Math.Min(sectionEnd, length) - abs;
        return room <= 0 ? 0 : Math.Min(count, room / stride);
    }

    private static int MemberOffset(string className, string member, int fallback) =>
        HavokClasses.Shipped.Field(className, member)?.Offset ?? fallback;

    private static int StructSize(string className, int fallback) =>
        HavokClasses.Shipped[className]?.Size is int size and > 0 ? size : fallback;
}
