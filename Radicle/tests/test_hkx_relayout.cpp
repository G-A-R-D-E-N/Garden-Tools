#include "fo4/hkx/convert_packfile.hpp"
#include "fo4/hkx/decode.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/pointer_relayout.hpp"
#include "fo4/hkx/relayout_widths.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::test::hkx::TestSection;
using Bytes = std::vector<std::byte>;

constexpr std::uint32_t kRootSignature = 0x2772c11eu;
constexpr std::uint32_t kBoneSignature = 0x35912f8au;
constexpr std::size_t kDataIndex = 2;

struct Located {
  std::size_t start{0};
  std::array<std::uint32_t, 6> marks{};
};

std::uint32_t read32(const Bytes& bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
  return value;
}

Located locate(const Bytes& bytes, std::string_view tag) {
  const std::size_t count = read32(bytes, 0x14);
  const std::size_t table = 0x40 + (static_cast<std::size_t>(bytes[0x3E]) | (static_cast<std::size_t>(bytes[0x3F]) << 8));
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t at = table + i * 0x40;
    std::string name;
    for (std::size_t k = 0; k < 20 && bytes[at + k] != std::byte{0}; ++k) name.push_back(static_cast<char>(bytes[at + k]));
    if (name != tag) continue;
    Located found;
    found.start = read32(bytes, at + 0x14);
    for (std::size_t k = 0; k < 6; ++k) found.marks[k] = read32(bytes, at + 0x18 + 4 * k);
    return found;
  }
  return {};
}

Bytes slice(const Bytes& bytes, const Located& section, std::size_t from, std::size_t to) {
  return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(section.start + from),
               bytes.begin() + static_cast<std::ptrdiff_t>(section.start + to));
}

Bytes data_of(const Bytes& bytes) {
  const Located data = locate(bytes, "__data__");
  return slice(bytes, data, 0, data.marks[0]);
}

std::vector<std::uint32_t> words(const Bytes& table) {
  std::vector<std::uint32_t> out;
  for (std::size_t at = 0; at + 4 <= table.size(); at += 4) out.push_back(read32(table, at));
  return out;
}

std::vector<std::uint32_t> real_words(const Bytes& table) {
  std::vector<std::uint32_t> out = words(table);
  while (!out.empty() && out.back() == 0xFFFFFFFFu) out.pop_back();
  return out;
}

void stamp_version(Bytes& bytes, std::string_view version) {
  for (std::size_t i = 0; i < 16; ++i) {
    bytes[0x28 + i] = i < version.size() ? static_cast<std::byte>(version[i]) : std::byte{0};
  }
}

void put_text(Bytes& data, std::size_t at, std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) data[at + i] = static_cast<std::byte>(text[i]);
}

// Constraint: the 8-byte graph is already in the order and alignment relayout_packfile places blocks, so a narrow-then-widen pass can reproduce it exactly.
Bytes root_graph(const std::vector<std::uint32_t>& global = {}, const Bytes& types = {},
                 const std::vector<std::uint32_t>& local = {0, 16, 16, 40, 24, 48}) {
  Bytes names;
  const std::uint32_t name_at = fo4::test::hkx::add_class_name(names, kRootSignature, "hkRootLevelContainer");
  Bytes data(64, std::byte{0});
  fo4::test::hkx::patch_u32(data, 8, 1);
  fo4::test::hkx::patch_u32(data, 12, 0x80000001u);
  put_text(data, 40, "root");
  put_text(data, 48, "hkaBone");
  Bytes bytes = fo4::test::hkx::build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", types, {}, {}, {}},
      TestSection{"__data__", data, local, global, {0, 0, name_at}},
  });
  fo4::test::hkx::patch_u32(bytes, 0x18, kDataIndex);
  stamp_version(bytes, "hk_2014.1.0-r1");
  return bytes;
}

// Constraint: two bones and a root at offset 48 with a gap before it, so a re-layout moves the root to 32; the file is valid but not canonical.
Bytes gap_graph() {
  Bytes names;
  const std::uint32_t bone_at = fo4::test::hkx::add_class_name(names, kBoneSignature, "hkaBone");
  const std::uint32_t root_at = fo4::test::hkx::add_class_name(names, kRootSignature, "hkRootLevelContainer");
  Bytes data(64, std::byte{0});
  data[8] = std::byte{1};
  data[24] = std::byte{1};
  Bytes bytes = fo4::test::hkx::build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", data, {}, {}, {0, 0, bone_at, 16, 0, bone_at, 48, 0, root_at}},
  });
  fo4::test::hkx::patch_u32(bytes, 0x18, kDataIndex);
  fo4::test::hkx::patch_u32(bytes, 0x1C, 48);
  stamp_version(bytes, "hk_2014.1.0-r1");
  return bytes;
}

std::uint32_t signature_of(std::string_view name) { return fo4::hkx::find_decode_class(name)->signature; }

// Constraint: the 8-byte blocks sit where relayout_packfile places them (container, pointer array, skeleton, then its string, int16 array, bone array, bone string, string array, element string), so narrowing and widening reproduces the data exactly.
Bytes skeleton_graph() {
  Bytes names;
  const std::uint32_t container_at =
      fo4::test::hkx::add_class_name(names, signature_of("hkaAnimationContainer"), "hkaAnimationContainer");
  const std::uint32_t skeleton_at = fo4::test::hkx::add_class_name(names, signature_of("hkaSkeleton"), "hkaSkeleton");
  Bytes data(336, std::byte{0});
  fo4::test::hkx::patch_u32(data, 24, 1);
  fo4::test::hkx::patch_u32(data, 28, 0x80000001u);
  fo4::test::hkx::patch_u32(data, 144, 2);
  fo4::test::hkx::patch_u32(data, 148, 0x80000002u);
  fo4::test::hkx::patch_u32(data, 160, 1);
  fo4::test::hkx::patch_u32(data, 164, 0x80000001u);
  fo4::test::hkx::patch_u32(data, 208, 1);
  fo4::test::hkx::patch_u32(data, 212, 0x80000001u);
  put_text(data, 256, "sk");
  data[272] = std::byte{0xFF};
  data[273] = std::byte{0xFF};
  data[296] = std::byte{1};
  put_text(data, 304, "b");
  put_text(data, 328, "slot");
  Bytes bytes = fo4::test::hkx::build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", data, {16, 96, 128, 256, 136, 272, 152, 288, 200, 320, 288, 304, 320, 328}, {96, kDataIndex, 112},
                  {0, 0, container_at, 112, 0, skeleton_at}},
  });
  fo4::test::hkx::patch_u32(bytes, 0x18, kDataIndex);
  stamp_version(bytes, "hk_2014.1.0-r1");
  return bytes;
}

Bytes bone_file(std::byte padding) {
  Bytes data(16, std::byte{0});
  data[8] = std::byte{1};
  data[9] = padding;
  Bytes bytes = fo4::test::hkx::single_object_packfile("hkaBone", kBoneSignature, data, {});
  fo4::test::hkx::patch_u32(bytes, 0x18, kDataIndex);
  stamp_version(bytes, "hk_2014.1.0-r1");
  return bytes;
}

void expect_refused(const Bytes& bytes, std::uint8_t width, std::string_view reason) {
  const auto result = fo4::hkx::relayout_packfile(bytes, width);
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(result.bytes.empty());
  EXPECT_NE(result.reason.find(reason), std::string::npos) << result.reason;
}

}  // namespace

TEST(HkxRelayoutLayout, VerifiedClassSizesMatchTheDecodeAndLayoutTables) {
  // Constraint: detects a drift between the generated 4-byte table, the decode table and the measured 8-byte layouts.
  std::size_t checked = 0;
  for (const auto& entry : fo4::hkx::decode_classes()) {
    const auto wide = fo4::hkx::relayout_class_size(entry.name, 8);
    const auto narrow = fo4::hkx::relayout_class_size(entry.name, 4);
    ASSERT_TRUE(wide.has_value()) << entry.name;
    ASSERT_TRUE(narrow.has_value()) << entry.name;
    EXPECT_EQ(*wide, entry.size) << entry.name;
    EXPECT_LT(*narrow, *wide) << entry.name;
    const auto* layout = fo4::hkx::find_layout(entry.name);
    ASSERT_NE(layout, nullptr) << entry.name;
    EXPECT_EQ(layout->size, entry.size) << entry.name;
    ++checked;
  }
  EXPECT_EQ(checked, 14u);
  EXPECT_FALSE(fo4::hkx::relayout_class_size("hkbBehaviorGraph", 4).has_value());
  EXPECT_FALSE(fo4::hkx::relayout_class_size("hkaBone", 6).has_value());
}

TEST(HkxRelayoutLayout, ClassLayoutsMatchHandWrittenHavok2014Layouts) {
  // Constraint: detects a wrong generated 4-byte class size or member offset, which the 8 to 4 to 8 round trip cannot see because it reads the same table in both directions; the literals come from the member lists of Radicle/data/hkx/HavokClassTypes.json with hkReferencedObject = 8/16 bytes, hkArray = 12/16 (pointer, size, capacity), hkStringPtr = 4/8 and vector4 aligned to 16, re-derived by hand and not from hkx_relayout_codegen.py, so the rule itself is still unverified against a real 32-bit hk_2014.1.0-r1 file (issue 1731).
  struct GoldenOffset {
    std::uint32_t wide;
    std::uint32_t narrow;
  };
  struct GoldenClass {
    std::string_view name;
    std::uint32_t wide_size;
    std::uint32_t narrow_size;
    std::vector<GoldenOffset> offsets;
  };
  const std::vector<GoldenClass> golden{
      {"hkRootLevelContainer", 16u, 12u, {{0u, 0u}}},
      {"hkRootLevelContainerNamedVariant", 24u, 12u, {{0u, 0u}, {8u, 4u}, {16u, 8u}}},
      {"hkaAnimationContainer", 96u, 68u, {{16u, 8u}, {32u, 20u}, {48u, 32u}, {64u, 44u}, {80u, 56u}}},
      {"hkaSkeleton", 136u, 96u,
       {{16u, 8u}, {24u, 12u}, {40u, 24u}, {56u, 36u}, {72u, 48u}, {88u, 60u}, {104u, 72u}, {120u, 84u}}},
      {"hkaBone", 16u, 8u, {{0u, 0u}, {8u, 4u}}},
      {"hkaSkeletonLocalFrameOnBone", 16u, 8u, {{0u, 0u}, {8u, 4u}}},
      {"hkaSkeletonPartition", 16u, 8u, {{0u, 0u}, {8u, 4u}, {10u, 6u}}},
      {"hkaAnimationBinding", 88u, 56u,
       {{16u, 8u}, {24u, 12u}, {32u, 16u}, {48u, 28u}, {64u, 40u}, {80u, 52u}}},
      {"hkaSplineCompressedAnimation", 176u, 132u,
       {{16u, 8u},  {20u, 12u},  {24u, 16u},  {28u, 20u},  {32u, 24u},  {40u, 28u},  {56u, 40u},
        {60u, 44u}, {64u, 48u},  {68u, 52u},  {72u, 56u},  {76u, 60u},  {80u, 64u},  {88u, 68u},
        {104u, 80u}, {120u, 92u}, {136u, 104u}, {152u, 116u}, {168u, 128u}}},
      {"hkaDefaultAnimatedReferenceFrame", 96u, 64u, {{32u, 16u}, {48u, 32u}, {64u, 48u}, {72u, 52u}}},
      {"hkaAnnotationTrack", 24u, 16u, {{0u, 0u}, {8u, 4u}}},
      {"hkaAnnotationTrackAnnotation", 16u, 8u, {{0u, 0u}, {8u, 4u}}},
      {"hkMemoryResourceContainer", 64u, 40u, {{16u, 8u}, {32u, 16u}, {48u, 28u}}},
      {"hkSimpleLocalFrame", 128u, 112u,
       {{16u, 16u}, {80u, 80u}, {96u, 92u}, {104u, 96u}, {112u, 100u}}},
  };
  const auto table = fo4::hkx::relayout_classes();
  ASSERT_EQ(table.size(), 14u);
  ASSERT_EQ(golden.size(), table.size());
  for (const GoldenClass& item : golden) {
    const fo4::hkx::HkxRelayoutClass* found = nullptr;
    for (const fo4::hkx::HkxRelayoutClass& entry : table) {
      if (entry.name == item.name) found = &entry;
    }
    ASSERT_NE(found, nullptr) << item.name;
    EXPECT_EQ(found->wide_size, item.wide_size) << item.name;
    EXPECT_EQ(found->narrow_size, item.narrow_size) << item.name;
    ASSERT_EQ(found->offsets.size(), item.offsets.size()) << item.name;
    for (std::size_t at = 0; at < item.offsets.size(); ++at) {
      EXPECT_EQ(found->offsets[at].wide, item.offsets[at].wide) << item.name << " offset " << at;
      EXPECT_EQ(found->offsets[at].narrow, item.offsets[at].narrow) << item.name << " offset " << at;
    }
  }
}

TEST(HkxRelayoutLayout, NarrowedScalarsMatchTheDecodeTableOffsets) {
  // Constraint: detects a wrong generated 4-byte offset that still round-trips through the same table.
  const Bytes wide = root_graph();
  const auto narrow = fo4::hkx::relayout_packfile(wide, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  EXPECT_TRUE(fo4::hkx::verify_narrow_layout_crosscheck(wide, narrow.bytes, narrow.object_moves).empty());
}

TEST(HkxRelayoutLayout, SkeletonNarrowedScalarsMatchTheDecodeTableOffsets) {
  const Bytes wide = skeleton_graph();
  const auto narrow = fo4::hkx::relayout_packfile(wide, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  EXPECT_TRUE(fo4::hkx::verify_narrow_layout_crosscheck(wide, narrow.bytes, narrow.object_moves).empty());
}

TEST(HkxRelayoutLayout, ACrossCheckCatchesANarrowedScalarMovedOffItsGeneratedOffset) {
  // Constraint: detects a narrowed scalar written at an offset other than the generated 4-byte one; a wrong table that is also used to read it back is not detectable here.
  const Bytes wide = bone_file(std::byte{0});
  const auto narrow = fo4::hkx::relayout_packfile(wide, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  ASSERT_TRUE(fo4::hkx::verify_narrow_layout_crosscheck(wide, narrow.bytes, narrow.object_moves).empty());
  Bytes moved = narrow.bytes;
  const std::size_t base = locate(moved, "__data__").start;
  ASSERT_EQ(static_cast<unsigned>(moved[base + 4]), 1u);
  moved[base + 4] = std::byte{0};
  moved[base + 5] = std::byte{1};
  const std::string reason = fo4::hkx::verify_narrow_layout_crosscheck(wide, moved, narrow.object_moves);
  EXPECT_NE(reason.find("does not match the 8-byte source"), std::string::npos) << reason;
}

TEST(HkxRelayout, NarrowsAGraphToFourBytePointers) {
  // Constraint: detects a wrong block order, alignment, member offset, capacity word, fixup remap or rule byte in the 8 to 4 direction.
  const Bytes wide = root_graph();
  const auto narrow = fo4::hkx::relayout_packfile(wide, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  EXPECT_EQ(narrow.source_pointer_size, 8);
  EXPECT_EQ(narrow.target_pointer_size, 4);
  EXPECT_FALSE(narrow.unchanged);
  EXPECT_EQ(narrow.object_count, 1u);
  EXPECT_EQ(narrow.block_count, 4u);
  EXPECT_EQ(narrow.classes, std::vector<std::string>{"hkRootLevelContainer"});
  EXPECT_EQ(fo4::hkx::packfile_pointer_size(narrow.bytes), std::optional<std::uint8_t>{4});
  EXPECT_EQ(static_cast<unsigned>(narrow.bytes[0x11]), 1u);
  EXPECT_EQ(static_cast<unsigned>(narrow.bytes[0x12]), 0u);
  EXPECT_EQ(static_cast<unsigned>(narrow.bytes[0x13]), 1u);
  const auto version = fo4::hkx::probe_havok_version(narrow.bytes);
  ASSERT_TRUE(version.has_value());
  EXPECT_EQ(version->version, "hk_2014.1.0-r1");

  const Located section = locate(narrow.bytes, "__data__");
  const Bytes data = slice(narrow.bytes, section, 0, section.marks[0]);
  ASSERT_EQ(data.size(), 64u);
  EXPECT_EQ(read32(data, 0), 0u);
  EXPECT_EQ(read32(data, 4), 1u);
  EXPECT_EQ(read32(data, 8), 0x80000001u);
  EXPECT_EQ(read32(data, 16), 0u);
  EXPECT_EQ(read32(data, 20), 0u);
  EXPECT_EQ(static_cast<char>(data[28]), 'r');
  EXPECT_EQ(static_cast<char>(data[48]), 'h');
  const auto local = real_words(slice(narrow.bytes, section, section.marks[0], section.marks[1]));
  EXPECT_EQ(local, (std::vector<std::uint32_t>{0, 16, 16, 28, 20, 48}));
  const auto virtuals = real_words(slice(narrow.bytes, section, section.marks[2], section.marks[3]));
  ASSERT_EQ(virtuals.size(), 3u);
  EXPECT_EQ(virtuals[0], 0u);
  EXPECT_EQ(read32(narrow.bytes, 0x1C), 0u);
}

TEST(HkxRelayout, WidensTheNarrowedGraphBackToTheSameDataAndFixups) {
  // Constraint: detects a conversion that loses a fixup, a string or a count, or that cannot read its own 4-byte output.
  const Bytes wide = root_graph();
  const auto narrow = fo4::hkx::relayout_packfile(wide, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  const auto back = fo4::hkx::relayout_packfile(narrow.bytes, 8);
  ASSERT_TRUE(back.ok) << back.reason;
  EXPECT_EQ(back.source_pointer_size, 4);
  EXPECT_EQ(back.target_pointer_size, 8);
  EXPECT_EQ(data_of(back.bytes), data_of(wide));
  const Located before = locate(wide, "__data__");
  const Located after = locate(back.bytes, "__data__");
  EXPECT_EQ(real_words(slice(back.bytes, after, after.marks[0], after.marks[1])),
            real_words(slice(wide, before, before.marks[0], before.marks[1])));
  EXPECT_EQ(real_words(slice(back.bytes, after, after.marks[2], after.marks[3])),
            real_words(slice(wide, before, before.marks[2], before.marks[3])));
  const auto again = fo4::hkx::relayout_packfile(back.bytes, 4);
  ASSERT_TRUE(again.ok) << again.reason;
  EXPECT_TRUE(again.bytes == narrow.bytes);
}

TEST(HkxRelayout, SameWidthReturnsTheBytesUnchanged) {
  // Constraint: BGS restamps nothing for an equal width; detects a no-op that rewrites or refuses a canonical file.
  const Bytes wide = root_graph();
  const auto same = fo4::hkx::relayout_packfile(wide, 8);
  ASSERT_TRUE(same.ok) << same.reason;
  EXPECT_TRUE(same.unchanged);
  EXPECT_TRUE(same.bytes == wide);
}

TEST(HkxRelayout, SameWidthStillChecksClassSignatures) {
  // Constraint: BGS validates class signatures even when it converts nothing.
  Bytes bytes = root_graph();
  const Located names = locate(bytes, "__classnames__");
  fo4::test::hkx::patch_u32(bytes, names.start, 0x12345678u);
  expect_refused(bytes, 8, "signature");
}

TEST(HkxRelayoutRefusal, ATargetWidthOtherThanFourOrEight) {
  expect_refused(root_graph(), 6, "must be 4 or 8");
  expect_refused(root_graph(), 0, "must be 4 or 8");
}

TEST(HkxRelayoutRefusal, AHeaderThatIsNotAVersion11PackfileWithCanonicalRules) {
  Bytes bad_rules = root_graph();
  bad_rules[0x10] = std::byte{6};
  expect_refused(bad_rules, 4, "layout rules");
  Bytes reuse_padding = root_graph();
  reuse_padding[0x12] = std::byte{1};
  expect_refused(reuse_padding, 4, "layout rules");
  Bytes big_endian = root_graph();
  big_endian[0x11] = std::byte{0};
  expect_refused(big_endian, 4, "layout rules");
  Bytes version = root_graph();
  fo4::test::hkx::patch_u32(version, 0xC, 8);
  expect_refused(version, 4, "version");
  Bytes magic = root_graph();
  magic[0] = std::byte{0};
  expect_refused(magic, 4, "magic");
  expect_refused(Bytes(10, std::byte{0}), 4, "shorter");
}

TEST(HkxRelayoutRefusal, ATruncatedSectionTable) {
  Bytes bytes = root_graph();
  bytes.resize(0x60);
  expect_refused(bytes, 4, "past the end");
}

TEST(HkxRelayoutRefusal, AClassSignatureThatDiffersFromTheLayoutTable) {
  // Constraint: a same-named class from another Havok version must not be read with this layout.
  Bytes bytes = root_graph();
  const Located names = locate(bytes, "__classnames__");
  fo4::test::hkx::patch_u32(bytes, names.start, 0x12345678u);
  expect_refused(bytes, 4, "signature");
}

TEST(HkxRelayoutRefusal, AClassOutsideTheVerifiedSet) {
  // Constraint: BGS tests drive synthetic behaviour classes; Radicle refuses any class without a verified 4-byte layout.
  const auto* layout = fo4::hkx::find_layout("hkbBehaviorGraph");
  ASSERT_NE(layout, nullptr);
  Bytes data(64, std::byte{0});
  Bytes bytes = fo4::test::hkx::single_object_packfile("hkbBehaviorGraph", layout->signature, data, {});
  fo4::test::hkx::patch_u32(bytes, 0x18, kDataIndex);
  stamp_version(bytes, "hk_2014.1.0-r1");
  expect_refused(bytes, 4, "hkbBehaviorGraph has no verified 4-byte layout");
}

TEST(HkxRelayoutRefusal, ADataByteNoVerifiedObjectOwns) {
  Bytes bytes = root_graph();
  const Located data = locate(bytes, "__data__");
  bytes[data.start + 60] = std::byte{0x77};
  expect_refused(bytes, 4, "no verified object owns");
}

TEST(HkxRelayoutRefusal, ANonZeroByteOutsideTheStoredMembers) {
  // Constraint: runtime-only members and padding are never invented or carried at another width.
  expect_refused(bone_file(std::byte{0x55}), 4, "non-zero byte at +9");
}

TEST(HkxRelayout, NarrowsAnObjectWithPaddingAndKeepsItsStoredMembers) {
  const auto narrow = fo4::hkx::relayout_packfile(bone_file(std::byte{0}), 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  const Bytes data = data_of(narrow.bytes);
  ASSERT_EQ(data.size(), 16u);
  EXPECT_EQ(static_cast<unsigned>(data[4]), 1u);
  EXPECT_EQ(static_cast<unsigned>(data[5]), 0u);
  EXPECT_EQ(narrow.classes, std::vector<std::string>{"hkaBone"});
}

TEST(HkxRelayoutRefusal, AnArrayCountThatLeavesTheDataSection) {
  Bytes bytes = root_graph();
  const Located data = locate(bytes, "__data__");
  fo4::test::hkx::patch_u32(bytes, data.start + 8, 0x7000u);
  expect_refused(bytes, 4, "does not fit the data section");
}

TEST(HkxRelayoutRefusal, AnArrayWithElementsButNoFixup) {
  Bytes names;
  const std::uint32_t name_at = fo4::test::hkx::add_class_name(names, kRootSignature, "hkRootLevelContainer");
  Bytes data(32, std::byte{0});
  fo4::test::hkx::patch_u32(data, 8, 1);
  Bytes bytes = fo4::test::hkx::build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", data, {}, {}, {0, 0, name_at}},
  });
  fo4::test::hkx::patch_u32(bytes, 0x18, kDataIndex);
  stamp_version(bytes, "hk_2014.1.0-r1");
  expect_refused(bytes, 4, "does not fit the data section");
}

TEST(HkxRelayoutRefusal, APointerArrayElementWithoutAFixupThatIsNotZero) {
  // Constraint: an element pointer with no fixup must be null, as for an object member; a stray value is never carried to the target width.
  const auto* layout = fo4::hkx::find_layout("hkaAnimationContainer");
  ASSERT_NE(layout, nullptr);
  Bytes names;
  const std::uint32_t name_at = fo4::test::hkx::add_class_name(names, layout->signature, "hkaAnimationContainer");
  const auto build = [&](std::byte element) {
    Bytes data(112, std::byte{0});
    fo4::test::hkx::patch_u32(data, 24, 1);
    fo4::test::hkx::patch_u32(data, 28, 0x80000001u);
    data[96] = element;
    Bytes bytes = fo4::test::hkx::build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", data, {16, 96}, {}, {0, 0, name_at}},
    });
    fo4::test::hkx::patch_u32(bytes, 0x18, kDataIndex);
    stamp_version(bytes, "hk_2014.1.0-r1");
    return bytes;
  };
  expect_refused(build(std::byte{0x77}), 4, "pointer array element is not zero");
  const auto null_element = fo4::hkx::relayout_packfile(build(std::byte{0}), 4);
  EXPECT_TRUE(null_element.ok) << null_element.reason;
}

TEST(HkxRelayout, RemapsAContentsObjectThatIsNotAtOffsetZeroAndMoves) {
  // Constraint: detects a contents offset that is copied instead of remapped to the moved root object.
  const Bytes bytes = gap_graph();
  const auto narrow = fo4::hkx::relayout_packfile(bytes, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  EXPECT_EQ(narrow.object_count, 3u);
  EXPECT_EQ(narrow.object_moves, (std::vector<std::pair<std::uint32_t, std::uint32_t>>{{0, 0}, {16, 16}, {48, 32}}));
  EXPECT_EQ(read32(narrow.bytes, 0x1C), 32u);
  const Located section = locate(narrow.bytes, "__data__");
  EXPECT_EQ(section.marks[0], 48u);
  const auto virtuals = real_words(slice(narrow.bytes, section, section.marks[2], section.marks[3]));
  ASSERT_EQ(virtuals.size(), 9u);
  EXPECT_EQ(virtuals[0], 0u);
  EXPECT_EQ(virtuals[3], 16u);
  EXPECT_EQ(virtuals[6], 32u);
  const auto widened = fo4::hkx::relayout_packfile(narrow.bytes, 8);
  ASSERT_TRUE(widened.ok) << widened.reason;
  EXPECT_EQ(read32(widened.bytes, 0x1C), 32u);
}

TEST(HkxRelayout, NarrowsAndWidensEveryBlockKindOfASkeletonContainer) {
  // Constraint: detects a wrong placement, member offset, capacity word or fixup remap for a non-null pointer array element, a string, a value array, a struct array, a string array and an element string.
  const Bytes wide = skeleton_graph();
  const auto narrow = fo4::hkx::relayout_packfile(wide, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  EXPECT_EQ(narrow.object_count, 2u);
  EXPECT_EQ(narrow.block_count, 9u);
  EXPECT_EQ(narrow.local_fixups, 7u);
  EXPECT_EQ(narrow.global_fixups, 1u);
  EXPECT_EQ(narrow.virtual_fixups, 2u);
  EXPECT_EQ(narrow.classes, (std::vector<std::string>{"hkaAnimationContainer", "hkaSkeleton"}));
  EXPECT_EQ(narrow.object_moves, (std::vector<std::pair<std::uint32_t, std::uint32_t>>{{0, 0}, {112, 96}}));

  const Located section = locate(narrow.bytes, "__data__");
  const Bytes data = slice(narrow.bytes, section, 0, section.marks[0]);
  ASSERT_EQ(data.size(), 256u);
  EXPECT_EQ(read32(data, 12), 1u);
  EXPECT_EQ(read32(data, 16), 0x80000001u);
  EXPECT_EQ(read32(data, 80), 0u);
  EXPECT_EQ(read32(data, 112), 2u);
  EXPECT_EQ(read32(data, 116), 0x80000002u);
  EXPECT_EQ(read32(data, 124), 1u);
  EXPECT_EQ(read32(data, 128), 0x80000001u);
  EXPECT_EQ(read32(data, 160), 1u);
  EXPECT_EQ(read32(data, 164), 0x80000001u);
  EXPECT_EQ(static_cast<char>(data[192]), 's');
  EXPECT_EQ(static_cast<char>(data[193]), 'k');
  EXPECT_EQ(static_cast<unsigned>(data[208]), 0xFFu);
  EXPECT_EQ(static_cast<unsigned>(data[209]), 0xFFu);
  EXPECT_EQ(static_cast<unsigned>(data[228]), 1u);
  EXPECT_EQ(static_cast<char>(data[232]), 'b');
  EXPECT_EQ(static_cast<char>(data[244]), 's');
  EXPECT_EQ(static_cast<char>(data[247]), 't');

  EXPECT_EQ(real_words(slice(narrow.bytes, section, section.marks[0], section.marks[1])),
            (std::vector<std::uint32_t>{8, 80, 104, 192, 108, 208, 120, 224, 156, 240, 224, 232, 240, 244}));
  const std::vector<std::uint32_t> global = real_words(slice(narrow.bytes, section, section.marks[1], section.marks[2]));
  EXPECT_EQ(global, (std::vector<std::uint32_t>{80, static_cast<std::uint32_t>(kDataIndex), 96}));
  const Located before = locate(wide, "__data__");
  std::vector<std::uint32_t> virtuals = real_words(slice(wide, before, before.marks[2], before.marks[3]));
  ASSERT_EQ(virtuals.size(), 6u);
  virtuals[3] = 96;
  EXPECT_EQ(real_words(slice(narrow.bytes, section, section.marks[2], section.marks[3])), virtuals);

  const auto back = fo4::hkx::relayout_packfile(narrow.bytes, 8);
  ASSERT_TRUE(back.ok) << back.reason;
  EXPECT_EQ(data_of(back.bytes), data_of(wide));
  const Located after = locate(back.bytes, "__data__");
  for (std::size_t table = 0; table < 3; ++table) {
    EXPECT_EQ(real_words(slice(back.bytes, after, after.marks[table], after.marks[table + 1])),
              real_words(slice(wide, before, before.marks[table], before.marks[table + 1])));
  }
}

TEST(HkxRelayout, AGlobalFixupIntoTheClassNamesKeepsItsTargetAndMovesItsSite) {
  // Constraint: detects a global fixup into __classnames__ that is dropped, or whose destination is remapped as if it pointed into __data__.
  const Bytes wide = root_graph({32, 0, 5});
  const auto narrow = fo4::hkx::relayout_packfile(wide, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  EXPECT_EQ(narrow.global_fixups, 1u);
  const Located section = locate(narrow.bytes, "__data__");
  EXPECT_EQ(real_words(slice(narrow.bytes, section, section.marks[1], section.marks[2])),
            (std::vector<std::uint32_t>{24, 0, 5}));
  const auto back = fo4::hkx::relayout_packfile(narrow.bytes, 8);
  ASSERT_TRUE(back.ok) << back.reason;
  const Located after = locate(back.bytes, "__data__");
  EXPECT_EQ(real_words(slice(back.bytes, after, after.marks[1], after.marks[2])), (std::vector<std::uint32_t>{32, 0, 5}));
}

TEST(HkxRelayoutRefusal, AGlobalFixupThatPointsIntoAnotherSection) {
  expect_refused(root_graph({32, 1, 0}), 4, "a global fixup points outside __data__ and __classnames__");
}

TEST(HkxRelayoutRefusal, ASectionOtherThanDataAndClassNamesThatHoldsData) {
  expect_refused(root_graph({}, Bytes(16, std::byte{1})), 4, "section __types__ holds data or fixups this port does not move");
}

TEST(HkxRelayoutRefusal, TwoLocalFixupsAtOneOffset) {
  expect_refused(root_graph({}, {}, {0, 16, 16, 40, 24, 48, 24, 48}), 4, "two local fixups start at offset 24");
}

TEST(HkxRelayoutRefusal, TwoObjectsThatOverlap) {
  Bytes names;
  const std::uint32_t bone_at = fo4::test::hkx::add_class_name(names, kBoneSignature, "hkaBone");
  Bytes bytes = fo4::test::hkx::build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", Bytes(32, std::byte{0}), {}, {}, {0, 0, bone_at, 8, 0, bone_at}},
  });
  fo4::test::hkx::patch_u32(bytes, 0x18, kDataIndex);
  stamp_version(bytes, "hk_2014.1.0-r1");
  expect_refused(bytes, 4, "two blocks overlap at offset 8");
}

TEST(HkxRelayoutRefusal, ExportsAndImports) {
  Bytes bytes = root_graph();
  const std::size_t table = 0x40 + 16;
  const std::size_t at = table + kDataIndex * 0x40;
  bytes.resize(bytes.size() + 16, std::byte{0});
  fo4::test::hkx::patch_u32(bytes, at + 0x18 + 16, read32(bytes, at + 0x18 + 16) + 16);
  fo4::test::hkx::patch_u32(bytes, at + 0x18 + 20, read32(bytes, at + 0x18 + 20) + 16);
  expect_refused(bytes, 4, "exports or imports");
}

TEST(HkxRelayoutRefusal, AContentsObjectOutsideTheDataSection) {
  Bytes bytes = root_graph();
  fo4::test::hkx::patch_u32(bytes, 0x18, 1);
  expect_refused(bytes, 4, "contents object is not in the __data__");
}

TEST(HkxRelayoutRefusal, AContentsOffsetThatIsNotAConvertedBlock) {
  Bytes bytes = root_graph();
  fo4::test::hkx::patch_u32(bytes, 0x1C, 8);
  expect_refused(bytes, 4, "contents object is not one of the converted blocks");
}

TEST(HkxRelayoutRefusal, AVirtualFixupThatIsNotAClassNameRecordStart) {
  Bytes bytes = root_graph();
  const Located data = locate(bytes, "__data__");
  const std::size_t virtuals = data.start + data.marks[2];
  fo4::test::hkx::patch_u32(bytes, virtuals + 8, 6);
  expect_refused(bytes, 4, "start of a class name record");
}

TEST(HkxRelayoutPackfileSize, ReadsTheWidthFromTheHeaderOnly) {
  const Bytes wide = root_graph();
  EXPECT_EQ(fo4::hkx::packfile_pointer_size(wide), std::optional<std::uint8_t>{8});
  EXPECT_FALSE(fo4::hkx::packfile_pointer_size(Bytes(64, std::byte{0})).has_value());
  EXPECT_FALSE(fo4::hkx::packfile_pointer_size(Bytes{}).has_value());
}

TEST(HkxConvertPointerSize, StatusesNameTheirReasonAndNeverReturnBytes) {
  // Constraint: a refusal of the pointer path never returns bytes and always says why.
  const auto garbage = fo4::hkx::convert_packfile_pointer_size(Bytes(8, std::byte{1}), 4);
  EXPECT_EQ(garbage.status, fo4::hkx::HkxPackfileConvertStatus::Unreadable);
  EXPECT_TRUE(garbage.bytes.empty());

  Bytes no_version = root_graph();
  stamp_version(no_version, "");
  EXPECT_EQ(fo4::hkx::convert_packfile_pointer_size(no_version, 4).status, fo4::hkx::HkxPackfileConvertStatus::UnknownVersion);

  Bytes other = root_graph();
  stamp_version(other, "hk_2010.2.0-r1");
  const auto pair = fo4::hkx::convert_packfile_pointer_size(other, 4);
  EXPECT_EQ(pair.status, fo4::hkx::HkxPackfileConvertStatus::UnsupportedPair);
  EXPECT_NE(pair.reason.find("hk_2010.2.0-r1"), std::string::npos);
  EXPECT_TRUE(pair.bytes.empty());

  Bytes unverified = root_graph();
  const Located names = locate(unverified, "__classnames__");
  fo4::test::hkx::patch_u32(unverified, names.start, 0x12345678u);
  const auto refused = fo4::hkx::convert_packfile_pointer_size(unverified, 4);
  EXPECT_EQ(refused.status, fo4::hkx::HkxPackfileConvertStatus::ConversionRefused);
  EXPECT_NE(refused.reason.find("signature"), std::string::npos);
  EXPECT_TRUE(refused.bytes.empty());
  EXPECT_EQ(refused.source_pointer_size, 8);
  EXPECT_EQ(refused.target_pointer_size, 4);
}

TEST(HkxConvertPointerSize, SameWidthIsAnUnchangedNoOpWithoutAProof) {
  const Bytes wide = root_graph();
  const auto same = fo4::hkx::convert_packfile_pointer_size(wide, 8);
  ASSERT_EQ(same.status, fo4::hkx::HkxPackfileConvertStatus::Ok) << same.reason;
  EXPECT_FALSE(same.relaid_out);
  EXPECT_FALSE(same.derived_table_consistency_checked);
  EXPECT_FALSE(same.narrow_layout_measured);
  EXPECT_FALSE(same.round_trip_checked);
  EXPECT_FALSE(same.fields_compared);
  EXPECT_TRUE(same.bytes == wide);
}

TEST(HkxConvertPointerSize, AnEightByteSourceNarrowsAndItsFieldsAreCompared) {
  // Constraint: derived-table consistency must not be reported as real 32-bit layout measurement; only an 8-byte source compares fields.
  const Bytes wide = root_graph();
  const auto narrowed = fo4::hkx::convert_packfile_pointer_size(wide, 4);
  ASSERT_EQ(narrowed.status, fo4::hkx::HkxPackfileConvertStatus::Ok) << narrowed.reason;
  EXPECT_EQ(narrowed.source_pointer_size, 8);
  EXPECT_EQ(narrowed.target_pointer_size, 4);
  EXPECT_TRUE(narrowed.relaid_out);
  EXPECT_TRUE(narrowed.derived_table_consistency_checked);
  EXPECT_FALSE(narrowed.narrow_layout_measured);
  EXPECT_TRUE(narrowed.round_trip_checked);
  EXPECT_EQ(narrowed.derived_table_consistency_checked, narrowed.round_trip_checked);
  EXPECT_TRUE(narrowed.fields_compared);
  EXPECT_EQ(narrowed.relayout_blocks, 4u);
  EXPECT_EQ(fo4::hkx::packfile_pointer_size(narrowed.bytes), std::optional<std::uint8_t>{4});
}

TEST(HkxConvertPointerSize, ANonCanonicalEightByteFileIsProvedByItsFieldsAfterRenumbering) {
  // Constraint: a valid file whose root moves from offset 48 to 32 must not be refused because the decoded ids are file offsets.
  const Bytes wide = gap_graph();
  const auto narrowed = fo4::hkx::convert_packfile_pointer_size(wide, 4);
  ASSERT_EQ(narrowed.status, fo4::hkx::HkxPackfileConvertStatus::Ok) << narrowed.reason;
  EXPECT_TRUE(narrowed.round_trip_checked);
  EXPECT_TRUE(narrowed.fields_compared);
  EXPECT_EQ(narrowed.object_count, 3u);
  EXPECT_EQ(read32(narrowed.bytes, 0x1C), 32u);
}

TEST(HkxConvertPointerSize, ASkeletonContainerIsProvedByItsFieldsAndItsReference) {
  // Constraint: the proof decodes the reference from the pointer array and every array of the skeleton, not only the block counts.
  const auto narrowed = fo4::hkx::convert_packfile_pointer_size(skeleton_graph(), 4);
  ASSERT_EQ(narrowed.status, fo4::hkx::HkxPackfileConvertStatus::Ok) << narrowed.reason;
  EXPECT_TRUE(narrowed.fields_compared);
  EXPECT_EQ(narrowed.object_count, 2u);
  EXPECT_EQ(narrowed.relayout_blocks, 9u);
  EXPECT_EQ(narrowed.relayout_classes, (std::vector<std::string>{"hkaAnimationContainer", "hkaSkeleton"}));
}

TEST(HkxConvertPointerSize, AFourByteSourceWidensAndIsCheckedByStructure) {
  // Constraint: a 4-byte source is proved by block, fixup and class counts only; the report must still say it was re-laid out and checked.
  const Bytes wide = root_graph();
  const auto narrow = fo4::hkx::relayout_packfile(wide, 4);
  ASSERT_TRUE(narrow.ok) << narrow.reason;
  const auto widened = fo4::hkx::convert_packfile_pointer_size(narrow.bytes, 8);
  ASSERT_EQ(widened.status, fo4::hkx::HkxPackfileConvertStatus::Ok) << widened.reason;
  EXPECT_EQ(widened.source_pointer_size, 4);
  EXPECT_EQ(widened.target_pointer_size, 8);
  EXPECT_TRUE(widened.relaid_out);
  EXPECT_TRUE(widened.derived_table_consistency_checked);
  EXPECT_FALSE(widened.narrow_layout_measured);
  EXPECT_TRUE(widened.round_trip_checked);
  EXPECT_FALSE(widened.fields_compared);
  EXPECT_EQ(widened.relayout_classes, std::vector<std::string>{"hkRootLevelContainer"});
  EXPECT_EQ(data_of(widened.bytes), data_of(wide));
}
