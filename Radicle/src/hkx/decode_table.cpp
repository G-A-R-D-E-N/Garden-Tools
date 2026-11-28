#include "fo4/hkx/decode.hpp"

namespace fo4::hkx {
namespace {

using K = HkxMemberKind;

constexpr HkxMemberType k_members_hkRootLevelContainer[] = {
    {"namedVariants", 0u, K::Array, K::Struct, "hkRootLevelContainerNamedVariant"},
};
constexpr HkxMemberType k_members_hkRootLevelContainerNamedVariant[] = {
    {"name", 0u, K::StringPtr, K::None, ""},
    {"className", 8u, K::StringPtr, K::None, ""},
    {"variant", 16u, K::Pointer, K::None, ""},
};
constexpr HkxMemberType k_members_hkaAnimationContainer[] = {
    {"skeletons", 16u, K::Array, K::Pointer, ""},
    {"animations", 32u, K::Array, K::Pointer, ""},
    {"bindings", 48u, K::Array, K::Pointer, ""},
    {"attachments", 64u, K::Array, K::Pointer, ""},
    {"skins", 80u, K::Array, K::Pointer, ""},
};
constexpr HkxMemberType k_members_hkaSkeleton[] = {
    {"name", 16u, K::StringPtr, K::None, ""},
    {"parentIndices", 24u, K::Array, K::Int16, ""},
    {"bones", 40u, K::Array, K::Struct, "hkaBone"},
    {"referencePose", 56u, K::Array, K::QsTransform, ""},
    {"referenceFloats", 72u, K::Array, K::Real, ""},
    {"floatSlots", 88u, K::Array, K::StringPtr, ""},
    {"localFrames", 104u, K::Array, K::Struct, "hkaSkeletonLocalFrameOnBone"},
    {"partitions", 120u, K::Array, K::Struct, "hkaSkeletonPartition"},
};
constexpr HkxMemberType k_members_hkaBone[] = {
    {"name", 0u, K::StringPtr, K::None, ""},
    {"lockTranslation", 8u, K::Bool, K::None, ""},
};
constexpr HkxMemberType k_members_hkaSkeletonLocalFrameOnBone[] = {
    {"localFrame", 0u, K::Pointer, K::None, ""},
    {"boneIndex", 8u, K::Int16, K::None, ""},
};
constexpr HkxMemberType k_members_hkaSkeletonPartition[] = {
    {"name", 0u, K::StringPtr, K::None, ""},
    {"startBoneIndex", 8u, K::Int16, K::None, ""},
    {"numBones", 10u, K::Int16, K::None, ""},
};
constexpr HkxMemberType k_members_hkaAnimationBinding[] = {
    {"originalSkeletonName", 16u, K::StringPtr, K::None, ""},
    {"animation", 24u, K::Pointer, K::None, ""},
    {"transformTrackToBoneIndices", 32u, K::Array, K::Int16, ""},
    {"floatTrackToFloatSlotIndices", 48u, K::Array, K::Int16, ""},
    {"partitionIndices", 64u, K::Array, K::Int16, ""},
    {"blendHint", 80u, K::Int8, K::None, ""},
};
constexpr HkxMemberType k_members_hkaSplineCompressedAnimation[] = {
    {"type", 16u, K::Int32, K::None, ""},
    {"duration", 20u, K::Real, K::None, ""},
    {"numberOfTransformTracks", 24u, K::Int32, K::None, ""},
    {"numberOfFloatTracks", 28u, K::Int32, K::None, ""},
    {"extractedMotion", 32u, K::Pointer, K::None, ""},
    {"annotationTracks", 40u, K::Array, K::Struct, "hkaAnnotationTrack"},
    {"numFrames", 56u, K::Int32, K::None, ""},
    {"numBlocks", 60u, K::Int32, K::None, ""},
    {"maxFramesPerBlock", 64u, K::Int32, K::None, ""},
    {"maskAndQuantizationSize", 68u, K::Int32, K::None, ""},
    {"blockDuration", 72u, K::Real, K::None, ""},
    {"blockInverseDuration", 76u, K::Real, K::None, ""},
    {"frameDuration", 80u, K::Real, K::None, ""},
    {"blockOffsets", 88u, K::Array, K::Uint32, ""},
    {"floatBlockOffsets", 104u, K::Array, K::Uint32, ""},
    {"transformOffsets", 120u, K::Array, K::Uint32, ""},
    {"floatOffsets", 136u, K::Array, K::Uint32, ""},
    {"data", 152u, K::Array, K::Uint8, ""},
    {"endian", 168u, K::Int32, K::None, ""},
};
constexpr HkxMemberType k_members_hkaDefaultAnimatedReferenceFrame[] = {
    {"up", 32u, K::Vector4, K::None, ""},
    {"forward", 48u, K::Vector4, K::None, ""},
    {"duration", 64u, K::Real, K::None, ""},
    {"referenceFrameSamples", 72u, K::Array, K::Vector4, ""},
};
constexpr HkxMemberType k_members_hkaAnnotationTrack[] = {
    {"trackName", 0u, K::StringPtr, K::None, ""},
    {"annotations", 8u, K::Array, K::Struct, "hkaAnnotationTrackAnnotation"},
};
constexpr HkxMemberType k_members_hkaAnnotationTrackAnnotation[] = {
    {"time", 0u, K::Real, K::None, ""},
    {"text", 8u, K::StringPtr, K::None, ""},
};
constexpr HkxMemberType k_members_hkMemoryResourceContainer[] = {
    {"name", 16u, K::StringPtr, K::None, ""},
    {"resourceHandles", 32u, K::Array, K::Pointer, ""},
    {"children", 48u, K::Array, K::Pointer, ""},
};
constexpr HkxMemberType k_members_hkSimpleLocalFrame[] = {
    {"transform", 16u, K::Transform, K::None, ""},
    {"children", 80u, K::Array, K::Pointer, ""},
    {"parentFrame", 96u, K::Pointer, K::None, ""},
    {"group", 104u, K::Pointer, K::None, ""},
    {"name", 112u, K::StringPtr, K::None, ""},
};

// Constraint: sources are commonlib (commonlibf4-nomad commit 7a939d9), the Fallout4.exe registry layouts and HavokClassTypes.json; the code note lists the rung that answered each class.
constexpr HkxDecodeClass k_decode_classes[] = {
    {"hkRootLevelContainer", 0x2772c11eu, 16u, k_members_hkRootLevelContainer,
     "commonlib 7a939d9 include/RE/H/hkRootLevelContainer.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkRootLevelContainerNamedVariant", 0xb103a2cdu, 24u, k_members_hkRootLevelContainerNamedVariant,
     "commonlib 7a939d9 include/RE/H/hkRootLevelContainer.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkaAnimationContainer", 0x26859f4cu, 96u, k_members_hkaAnimationContainer,
     "Fallout4.exe registry layouts (RE Docs vault Havok_Research packfile README); HavokClassTypes.json; no commonlib header"},
    {"hkaSkeleton", 0xfec1cedbu, 136u, k_members_hkaSkeleton,
     "commonlib 7a939d9 include/RE/H/hkaSkeleton.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkaBone", 0x35912f8au, 16u, k_members_hkaBone,
     "commonlib 7a939d9 include/RE/H/hkaBone.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkaSkeletonLocalFrameOnBone", 0xe9151edcu, 16u, k_members_hkaSkeletonLocalFrameOnBone,
     "commonlib 7a939d9 include/RE/H/hkaSkeleton.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkaSkeletonPartition", 0x7c8e6a55u, 16u, k_members_hkaSkeletonPartition,
     "commonlib 7a939d9 include/RE/H/hkaSkeleton.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkaAnimationBinding", 0x0faf9150u, 88u, k_members_hkaAnimationBinding,
     "commonlib 7a939d9 include/RE/H/hkaAnimationBinding.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkaSplineCompressedAnimation", 0x8c3b5f7eu, 176u, k_members_hkaSplineCompressedAnimation,
     "commonlib 7a939d9 include/RE/H/hkaAnimation.h and hkaSplineCompressedAnimation.h; Fallout4.exe registry layouts; "
     "HavokClassTypes.json"},
    {"hkaDefaultAnimatedReferenceFrame", 0x60f8e0b8u, 96u, k_members_hkaDefaultAnimatedReferenceFrame,
     "commonlib 7a939d9 include/RE/H/hkaDefaultAnimatedReferenceFrame.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkaAnnotationTrack", 0xd4114fddu, 24u, k_members_hkaAnnotationTrack,
     "commonlib 7a939d9 include/RE/H/hkaAnnotationTrack.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkaAnnotationTrackAnnotation", 0x623bf34fu, 16u, k_members_hkaAnnotationTrackAnnotation,
     "commonlib 7a939d9 include/RE/H/hkaAnnotationTrack.h; Fallout4.exe registry layouts; HavokClassTypes.json"},
    {"hkMemoryResourceContainer", 0x1de13a73u, 64u, k_members_hkMemoryResourceContainer,
     "Fallout4.exe registry layouts (RE Docs vault Havok_Research RE-024); HavokClassTypes.json; no commonlib header"},
    {"hkSimpleLocalFrame", 0x798adf92u, 128u, k_members_hkSimpleLocalFrame,
     "Fallout4.exe registry layouts (RE Docs vault Havok_Research RE-024); HavokClassTypes.json; no commonlib header"},
};

}  // namespace

std::span<const HkxDecodeClass> decode_classes() { return k_decode_classes; }

const HkxDecodeClass* find_decode_class(std::string_view class_name) {
  for (const HkxDecodeClass& entry : k_decode_classes) {
    if (entry.name == class_name) return &entry;
  }
  return nullptr;
}

std::uint32_t kind_size(HkxMemberKind kind) {
  switch (kind) {
    case K::Bool:
    case K::Int8:
    case K::Uint8:
      return 1;
    case K::Int16:
    case K::Uint16:
      return 2;
    case K::Int32:
    case K::Uint32:
    case K::Real:
      return 4;
    case K::Int64:
    case K::Uint64:
    case K::StringPtr:
    case K::Pointer:
      return 8;
    case K::Vector4:
    case K::Quaternion:
    case K::Array:
      return 16;
    case K::QsTransform:
      return 48;
    case K::Transform:
      return 64;
    case K::None:
    case K::Struct:
      return 0;
  }
  return 0;
}

std::optional<std::uint32_t> member_size(const HkxMemberType& member) {
  if (member.kind != K::Struct) {
    const std::uint32_t size = kind_size(member.kind);
    return size == 0 ? std::nullopt : std::optional<std::uint32_t>{size};
  }
  const HkxDecodeClass* entry = find_decode_class(member.struct_class);
  return entry == nullptr ? std::nullopt : std::optional<std::uint32_t>{entry->size};
}

}  // namespace fo4::hkx
