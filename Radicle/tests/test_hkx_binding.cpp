#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using fo4::hkx::HkxAnimationBinding;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_binding;
using namespace fo4::test::hkx;

namespace {

std::uint32_t signature_of(const char* class_name) { return fo4::hkx::find_layout(class_name)->signature; }

std::size_t append_string(std::vector<std::byte>& data, const std::string& text) {
  const std::size_t at = data.size();
  for (char ch : text) data.push_back(static_cast<std::byte>(ch));
  data.push_back(std::byte{0});
  return at;
}

std::vector<std::byte> binding_packfile() {
  std::vector<std::byte> names;
  const auto name_at = add_class_name(names, signature_of("hkaAnimationBinding"), "hkaAnimationBinding");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaAnimationBinding")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  link(local, member_at("hkaAnimationBinding", "originalSkeletonName"), append_string(data, "Rig"));
  patch_u32(data, member_at("hkaAnimationBinding", "blendHint"), 1);

  const std::size_t bones_at = data.size();
  for (std::int16_t index : {std::int16_t{0}, std::int16_t{4}, std::int16_t{-1}}) {
    const auto bits = static_cast<std::uint16_t>(index);
    data.push_back(static_cast<std::byte>(bits & 0xFF));
    data.push_back(static_cast<std::byte>(bits >> 8));
  }
  link(local, member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), bones_at);
  set_count(data, member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), 3);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

std::vector<std::byte> binding_with_animation_packfile() {
  std::vector<std::byte> names;
  const auto binding_name = add_class_name(names, signature_of("hkaAnimationBinding"), "hkaAnimationBinding");
  const auto animation_name = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                             "hkaInterleavedUncompressedAnimation");
  const std::size_t binding_offset = 0;
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaAnimationBinding")->size, std::byte{0});
  const std::size_t animation_offset = data.size();
  data.resize(data.size() + fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size, std::byte{0});

  std::vector<std::uint32_t> global;
  global.push_back(static_cast<std::uint32_t>(binding_offset + member_at("hkaAnimationBinding", "animation")));
  global.push_back(2);
  global.push_back(static_cast<std::uint32_t>(animation_offset));

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), {}, std::move(global),
                  {static_cast<std::uint32_t>(binding_offset), 0, binding_name,
                   static_cast<std::uint32_t>(animation_offset), 0, animation_name}},
  });
}

}  // namespace

TEST(HkxAnimationBinding, DecodesSkeletonNameTrackMapAndBlendHint) {
  const auto bytes = binding_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimationBinding binding;
  ASSERT_EQ(read_binding(packfile, binding), HkxStatus::Ok);
  EXPECT_EQ(binding.original_skeleton_name, "Rig");
  EXPECT_EQ(binding.transform_track_to_bone_indices, (std::vector<std::int16_t>{0, 4, -1}));
  EXPECT_TRUE(binding.float_track_to_float_slot_indices.empty());
  EXPECT_EQ(binding.blend_hint, 1);
  EXPECT_FALSE(binding.animation_offset.has_value());
}

TEST(HkxAnimationBinding, ResolvesTheAnimationPointerThroughItsGlobalFixup) {
  // Detects a binding report that never says which animation it must be paired with.
  const auto bytes = binding_with_animation_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimationBinding binding;
  ASSERT_EQ(read_binding(packfile, binding), HkxStatus::Ok);
  ASSERT_TRUE(binding.animation_offset.has_value());

  bool names_an_animation = false;
  for (const fo4::hkx::HkxObjectView& object : packfile.objects) {
    if (object.offset != *binding.animation_offset) continue;
    names_an_animation = object.class_name == "hkaInterleavedUncompressedAnimation";
  }
  EXPECT_TRUE(names_an_animation);
}

TEST(HkxAnimationBinding, IgnoresAGlobalFixupDestinationPastTheDataSection) {
  // Detects an offset from a fixup past the data section; the destination must be widened after the read.
  const auto bytes = binding_with_animation_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxSection& section = packfile.sections[packfile.data_section];
  ASSERT_EQ(section.global_fixups.size(), 1u);
  section.global_fixups.front().destination = static_cast<std::uint32_t>(section.data.size());

  HkxAnimationBinding binding;
  ASSERT_EQ(read_binding(packfile, binding), HkxStatus::Ok);
  EXPECT_FALSE(binding.animation_offset.has_value());
}

TEST(HkxAnimationBinding, ReadBindingReportsMissingObjectWhenNoBindingIsPresent) {
  const auto bytes = single_object_packfile("hkRootLevelContainer", signature_of("hkRootLevelContainer"),
                                            filled(32, std::byte{0}), {});
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxAnimationBinding binding;
  EXPECT_EQ(read_binding(packfile, binding), HkxStatus::MissingObject);
}
