#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxSkeleton;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_packfile;
using fo4::hkx::read_skeleton;

namespace {

constexpr std::uint32_t kSkeletonSignature = 0xfec1cedbu;

struct TestSection {
  std::string tag;
  std::vector<std::byte> data;
  std::vector<std::uint32_t> local;
  std::vector<std::uint32_t> global;
  std::vector<std::uint32_t> virtuals;
};

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
}

void put_words(std::vector<std::byte>& out, const std::vector<std::uint32_t>& words) {
  for (std::uint32_t word : words) put_u32(out, word);
}

void patch_u32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
}

std::vector<std::byte> build_packfile(const std::vector<TestSection>& sections) {
  std::vector<std::byte> header;
  put_u32(header, 0x57E0E057u);
  put_u32(header, 0x10C0C010u);
  put_u32(header, 0);
  put_u32(header, 11);
  const std::uint8_t rules[] = {8, 1, 0, 1};
  for (std::uint8_t rule : rules) header.push_back(static_cast<std::byte>(rule));
  put_u32(header, static_cast<std::uint32_t>(sections.size()));
  header.resize(0x3E, std::byte{0});
  header.push_back(std::byte{16});
  header.push_back(std::byte{0});
  header.resize(0x50, std::byte{0});

  std::vector<std::byte> table;
  std::vector<std::byte> bodies;
  const std::size_t bodies_at = 0x50 + sections.size() * 0x40;
  for (const TestSection& section : sections) {
    const auto start = static_cast<std::uint32_t>(bodies_at + bodies.size());
    const auto data_end = static_cast<std::uint32_t>(section.data.size());
    const auto local_end = data_end + static_cast<std::uint32_t>(section.local.size() * 4);
    const auto global_end = local_end + static_cast<std::uint32_t>(section.global.size() * 4);
    const auto virtual_end = global_end + static_cast<std::uint32_t>(section.virtuals.size() * 4);
    for (std::size_t i = 0; i < 20; ++i) {
      table.push_back(i < section.tag.size() ? static_cast<std::byte>(section.tag[i]) : std::byte{0});
    }
    put_u32(table, start);
    for (std::uint32_t mark : {data_end, local_end, global_end, virtual_end, virtual_end, virtual_end}) {
      put_u32(table, mark);
    }
    table.resize(table.size() + 16, std::byte{0});
    bodies.insert(bodies.end(), section.data.begin(), section.data.end());
    put_words(bodies, section.local);
    put_words(bodies, section.global);
    put_words(bodies, section.virtuals);
  }
  header.insert(header.end(), table.begin(), table.end());
  header.insert(header.end(), bodies.begin(), bodies.end());
  return header;
}

std::uint32_t add_class_name(std::vector<std::byte>& names, std::uint32_t signature, const std::string& name) {
  put_u32(names, signature);
  names.push_back(std::byte{0x09});
  const auto name_at = static_cast<std::uint32_t>(names.size());
  for (char ch : name) names.push_back(static_cast<std::byte>(ch));
  names.push_back(std::byte{0});
  return name_at;
}

std::vector<std::byte> single_object_packfile(const std::string& class_name, std::uint32_t signature,
                                              std::vector<std::byte> data, std::vector<std::uint32_t> local) {
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, signature, class_name);
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

std::vector<std::byte> filled(std::size_t size, std::byte value) { return std::vector<std::byte>(size, value); }

void put_float(std::vector<std::byte>& out, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  put_u32(out, bits);
}

std::size_t member_at(const char* layout, const char* member) {
  return *fo4::hkx::find_member_offset(*fo4::hkx::find_layout(layout), member);
}

void link(std::vector<std::uint32_t>& local, std::size_t source, std::size_t destination) {
  local.push_back(static_cast<std::uint32_t>(source));
  local.push_back(static_cast<std::uint32_t>(destination));
}

std::vector<std::byte> skeleton_packfile(const std::vector<std::string>& names, const std::vector<std::int16_t>& parents,
                                         std::size_t pose_count) {
  const std::size_t bone_size = fo4::hkx::find_layout("hkaBone")->size;
  const std::size_t bone_name = member_at("hkaBone", "name");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSkeleton")->size, std::byte{0});
  std::vector<std::uint32_t> local;
  const auto append_string = [&](const std::string& text) {
    const std::size_t at = data.size();
    for (char ch : text) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  const auto set_count = [&](std::size_t field, std::size_t count) {
    patch_u32(data, field + 8, static_cast<std::uint32_t>(count));
  };

  const std::size_t bones_at = data.size();
  data.resize(bones_at + names.size() * bone_size, std::byte{0});
  for (std::size_t i = 0; i < names.size(); ++i) {
    link(local, bones_at + i * bone_size + bone_name, append_string(names[i]));
  }

  const std::size_t parents_at = data.size();
  for (std::int16_t parent : parents) {
    const auto bits = static_cast<std::uint16_t>(parent);
    data.push_back(static_cast<std::byte>(bits & 0xFF));
    data.push_back(static_cast<std::byte>(bits >> 8));
  }

  const std::size_t pose_at = data.size();
  for (std::size_t i = 0; i < pose_count; ++i) {
    const float translation_x = static_cast<float>(i + 1);
    for (float value : {translation_x, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f}) {
      put_float(data, value);
    }
  }

  const std::size_t skeleton_name = member_at("hkaSkeleton", "name");
  const std::size_t parents_field = member_at("hkaSkeleton", "parentIndices");
  const std::size_t bones_field = member_at("hkaSkeleton", "bones");
  const std::size_t pose_field = member_at("hkaSkeleton", "referencePose");
  link(local, skeleton_name, append_string("Rig"));
  link(local, parents_field, parents_at);
  set_count(parents_field, parents.size());
  link(local, bones_field, bones_at);
  set_count(bones_field, names.size());
  link(local, pose_field, pose_at);
  set_count(pose_field, pose_count);
  return single_object_packfile("hkaSkeleton", kSkeletonSignature, std::move(data), std::move(local));
}

}  // namespace

TEST(HkxPackfile, ObjectSpansRunToTheNextObjectAndKeepUnknownClasses) {
  std::vector<std::byte> names;
  const std::uint32_t skeleton_name = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const std::uint32_t unknown_name = add_class_name(names, 0x12345678u, "hkUnknownSample");
  std::vector<std::byte> data = filled(16, std::byte{0xA0});
  const auto second = filled(24, std::byte{0xB0});
  data.insert(data.end(), second.begin(), second.end());
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", data, {}, {}, {0, 0, skeleton_name, 16, 0, unknown_name}},
  });

  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  ASSERT_EQ(packfile.objects.size(), 2u);
  EXPECT_EQ(packfile.objects[0].offset, 0u);
  EXPECT_EQ(packfile.objects[0].signature, kSkeletonSignature);
  EXPECT_EQ(packfile.objects[0].class_name, "hkaSkeleton");
  EXPECT_EQ(packfile.objects[0].data.size(), 16u);
  EXPECT_EQ(packfile.objects[1].offset, 16u);
  EXPECT_EQ(packfile.objects[1].signature, 0x12345678u);
  EXPECT_EQ(packfile.objects[1].class_name, "hkUnknownSample");
  EXPECT_EQ(packfile.objects[1].data.size(), 24u);
  EXPECT_EQ(packfile.objects[1].data.front(), std::byte{0xB0});
}

TEST(HkxPackfile, RejectsBadMagicInsteadOfDecodingTheBody) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  patch_u32(bytes, 0, 0);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadMagic);
}

TEST(HkxPackfile, RejectsAFileCutOffInsideTheVirtualFixupTableOfTheDataSection) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  bytes.resize(bytes.size() - 4);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Truncated);
}

TEST(HkxPackfile, RejectsSectionMarksThatDecreaseInsteadOfSlicingBackwards) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  const std::size_t data_header = 0x50 + 2 * 0x40;
  patch_u32(bytes, data_header + 0x18 + 4, 0);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadSection);
}

TEST(HkxPackfile, RejectsLocalFixupTargetPastTheSectionData) {
  const auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {0, 999});
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::FixupOutOfRange);
}

TEST(HkxPackfile, RejectsGlobalFixupNamingASectionThatDoesNotExist) {
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {0, 7, 0}, {0, 0, name_at}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::FixupOutOfRange);
}

TEST(HkxPackfile, RejectsVirtualFixupNamingAClassPastTheClassNameTable) {
  std::vector<std::byte> names;
  add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {}, {0, 0, 9999}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::FixupOutOfRange);
}

TEST(HkxPackfile, RejectsTwoObjectsClaimingTheSameOffset) {
  std::vector<std::byte> names;
  const std::uint32_t first = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  const std::uint32_t second = add_class_name(names, 0x12345678u, "hkUnknownSample");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {}, {0, 0, first, 0, 0, second}},
  });
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::DuplicateFixup);
}

TEST(HkxPackfile, RejectsAPackfileVersionTheLayoutTableWasNotMeasuredFrom) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  patch_u32(bytes, 0x0C, 10);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::UnsupportedLayout);
}

TEST(HkxSkeleton, ReadSkeletonRejectsBoneArrayThatRunsPastTheSection) {
  auto data = filled(136, std::byte{0});
  patch_u32(data, 48, 1000);
  const auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, data, {40, 128});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::FixupOutOfRange);
}

TEST(HkxSkeleton, ReadSkeletonRefusesASkeletonWhoseSignatureDoesNotMatchTheLayout) {
  const auto bytes = single_object_packfile("hkaSkeleton", 0x00000001u, filled(136, std::byte{0}), {});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::UnsupportedLayout);
}

TEST(HkxSkeleton, ReadSkeletonReportsMissingObjectWhenNoSkeletonIsPresent) {
  const auto bytes = single_object_packfile("hkRootLevelContainer", 0x4B3E2F10u, filled(32, std::byte{0}), {});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::MissingObject);
}

TEST(HkxSkeleton, DecodesBoneNamesParentsAndPoseOfASmallSkeleton) {
  const auto bytes = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  ASSERT_EQ(read_skeleton(packfile, skeleton), HkxStatus::Ok);
  EXPECT_EQ(skeleton.name, "Rig");
  EXPECT_EQ(skeleton.bone_names, (std::vector<std::string>{"Root", "Spine", "Head"}));
  EXPECT_EQ(skeleton.parent_indices, (std::vector<std::int16_t>{-1, 0, 1}));
  ASSERT_EQ(skeleton.reference_pose.size(), 3u);
  EXPECT_FLOAT_EQ(skeleton.reference_pose[2].translation[0], 3.0f);
  EXPECT_FLOAT_EQ(skeleton.reference_pose[1].rotation[3], 1.0f);
  EXPECT_FLOAT_EQ(skeleton.reference_pose[1].scale[0], 1.0f);
}

TEST(HkxSkeleton, RejectsParentIndicesThatAreOutOfRangeSelfOrForwardOrMiscounted) {
  struct Case {
    const char* what;
    std::vector<std::int16_t> parents;
    std::size_t poses;
  };
  const std::vector<Case> cases{
      {"parent below -1", {-1, 0, -2}, 3},
      {"self parent", {-1, 1, 0}, 3},
      {"forward parent", {-1, 2, 0}, 3},
      {"parent count short of the bone count", {-1, 0}, 3},
      {"pose count short of the bone count", {-1, 0, 1}, 2},
  };
  for (const Case& c : cases) {
    const auto bytes = skeleton_packfile({"Root", "Spine", "Head"}, c.parents, c.poses);
    HkxPackfile packfile;
    ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok) << c.what;
    HkxSkeleton skeleton;
    EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::BadSection) << c.what;
  }
}

TEST(HkxSkeleton, RejectsABoneNameLongerThanTheScanCapInsteadOfCopyingIt) {
  const auto bytes = skeleton_packfile({std::string(300, 'b'), "Spine"}, {-1, 0}, 2);
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxSkeleton skeleton;
  EXPECT_EQ(read_skeleton(packfile, skeleton), HkxStatus::FixupOutOfRange);
}

TEST(HkxPackfile, EveryCutOffLengthOfASkeletonFileIsRejectedWithoutReadingPastTheEnd) {
  const auto bytes = skeleton_packfile({"Root", "Spine", "Head"}, {-1, 0, 1}, 3);
  const std::span<const std::byte> whole(bytes);
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    HkxPackfile packfile;
    const auto status = read_packfile(whole.first(length), packfile);
    EXPECT_NE(status, HkxStatus::Ok) << "length " << length;
    if (status == HkxStatus::Ok) {
      HkxSkeleton skeleton;
      EXPECT_NE(read_skeleton(packfile, skeleton), HkxStatus::Ok) << "length " << length;
    }
  }
}

TEST(HkxPackfile, RejectsAPackfileWithMoreSectionsThanTheCapAllows) {
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, kSkeletonSignature, "hkaSkeleton");
  std::vector<TestSection> sections{
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", filled(16, std::byte{0}), {}, {}, {0, 0, name_at}},
  };
  for (int i = 0; i < 14; ++i) sections.push_back(TestSection{"__types__", {}, {}, {}, {}});
  const auto bytes = build_packfile(sections);
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadSection);
}

TEST(HkxPackfile, RejectsASectionWhoseBytesOverlapAnotherSection) {
  auto bytes = single_object_packfile("hkaSkeleton", kSkeletonSignature, filled(16, std::byte{0}), {});
  const std::size_t classnames_at = 0x50 + 3 * 0x40;
  const std::size_t data_header = 0x50 + 2 * 0x40;
  patch_u32(bytes, data_header + 0x14, static_cast<std::uint32_t>(classnames_at));
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadSection);
}

TEST(HkxPackfile, RejectsAClassNameLongerThanTheScanCapInsteadOfCopyingIt) {
  const auto bytes = single_object_packfile(std::string(300, 'a'), kSkeletonSignature, filled(16, std::byte{0}), {});
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::BadClassName);
}
