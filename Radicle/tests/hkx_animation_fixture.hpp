#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_test_fixture.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hkx {

inline constexpr std::size_t kDataSectionOffset = 0x50 + 3 * 0x40;

inline std::uint32_t signature_of(const char* class_name) { return fo4::hkx::find_layout(class_name)->signature; }

inline void patch_float(std::vector<std::byte>& bytes, std::size_t at, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  patch_u32(bytes, at, bits);
}

inline void set_float_member(std::vector<std::byte>& bytes, const char* layout, const char* member, float value) {
  patch_float(bytes, member_at(layout, member), value);
}

inline void set_int_member(std::vector<std::byte>& bytes, const char* layout, const char* member, std::uint32_t value) {
  patch_u32(bytes, member_at(layout, member), value);
}

// Offset of the data section: the packfile builder lays sections out in order.
inline std::size_t data_section_offset(std::size_t names_size) { return kDataSectionOffset + names_size; }

inline void pad_to(std::vector<std::byte>& data, std::size_t data_offset, std::size_t to) {
  while ((data_offset + data.size()) % to != 0) data.push_back(std::byte{0});
}

inline std::vector<std::byte> spline_packfile(int block_offset_override, int mask_size_override) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  const std::size_t data_offset = data_section_offset(names.size());
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  const std::size_t blob_at = data.size();
  const std::size_t mask_size = mask_size_override < 0 ? 4 : static_cast<std::size_t>(mask_size_override);
  data.push_back(std::byte{0x14});
  data.push_back(std::byte{0x10});
  data.push_back(std::byte{0x01});
  data.push_back(std::byte{0x00});

  data.push_back(std::byte{0x01});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x01});
  for (const int knot : {0, 0, 1, 1}) data.push_back(static_cast<std::byte>(knot));
  pad_to(data, data_offset, 4);

  put_float(data, 0.0f);
  put_float(data, 10.0f);
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0xFF});
  pad_to(data, data_offset, 4);

  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 1.0f);
  pad_to(data, data_offset, 4);
  const std::size_t blob_size = data.size() - blob_at;

  pad_to(data, data_offset, 4);
  const std::size_t offsets_at = data.size();
  put_u32(data, static_cast<std::uint32_t>(block_offset_override < 0 ? 0 : block_offset_override));

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 1.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  set_int_member(data, "hkaSplineCompressedAnimation", "numFrames", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "numBlocks", 1);
  set_int_member(data, "hkaSplineCompressedAnimation", "maxFramesPerBlock", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "maskAndQuantizationSize",
                 static_cast<std::uint32_t>(mask_size));
  set_float_member(data, "hkaSplineCompressedAnimation", "blockDuration", 1.0f);
  set_float_member(data, "hkaSplineCompressedAnimation", "frameDuration", 1.0f);
  link(local, member_at("hkaSplineCompressedAnimation", "blockOffsets"), offsets_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "blockOffsets"), 1);
  link(local, member_at("hkaSplineCompressedAnimation", "data"), blob_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "data"), blob_size);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

inline std::vector<std::byte> spline_shape_packfile(std::uint32_t tracks, std::uint32_t frames, std::uint32_t blocks,
                                             std::uint32_t max_frames) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  const std::size_t blob_at = data.size();
  for (int i = 0; i < 4; ++i) data.push_back(std::byte{0});
  const std::size_t blob_size = data.size() - blob_at;

  const std::size_t offsets_at = data.size();
  const std::uint32_t offset_count = blocks == 0 ? 1u : blocks;
  for (std::uint32_t block = 0; block < offset_count; ++block) put_u32(data, 0);

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 1.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", tracks);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  set_int_member(data, "hkaSplineCompressedAnimation", "numFrames", frames);
  set_int_member(data, "hkaSplineCompressedAnimation", "numBlocks", blocks);
  set_int_member(data, "hkaSplineCompressedAnimation", "maxFramesPerBlock", max_frames);
  set_int_member(data, "hkaSplineCompressedAnimation", "maskAndQuantizationSize", 0);
  set_float_member(data, "hkaSplineCompressedAnimation", "blockDuration", 1.0f);
  set_float_member(data, "hkaSplineCompressedAnimation", "frameDuration", 1.0f);
  link(local, member_at("hkaSplineCompressedAnimation", "blockOffsets"), offsets_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "blockOffsets"), offset_count);
  link(local, member_at("hkaSplineCompressedAnimation", "data"), blob_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "data"), blob_size);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

inline std::vector<std::byte> spline_two_block_packfile() {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  const std::size_t data_offset = data_section_offset(names.size());
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  pad_to(data, data_offset, 4);
  const std::size_t blob_at = data.size();
  std::vector<std::uint32_t> block_offsets;
  for (const float value : {11.0f, 77.0f}) {
    block_offsets.push_back(static_cast<std::uint32_t>(data.size() - blob_at));
    data.push_back(std::byte{0x00});
    data.push_back(std::byte{0x01});
    data.push_back(std::byte{0x00});
    data.push_back(std::byte{0x00});
    put_float(data, value);
  }

  const std::size_t offsets_at = data.size();
  for (const std::uint32_t offset : block_offsets) put_u32(data, offset);
  const std::size_t blob_size = offsets_at - blob_at;

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 6.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  set_int_member(data, "hkaSplineCompressedAnimation", "numFrames", 7);
  set_int_member(data, "hkaSplineCompressedAnimation", "numBlocks", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "maxFramesPerBlock", 4);
  set_int_member(data, "hkaSplineCompressedAnimation", "maskAndQuantizationSize", 4);
  set_float_member(data, "hkaSplineCompressedAnimation", "blockDuration", 3.0f);
  set_float_member(data, "hkaSplineCompressedAnimation", "frameDuration", 1.0f);
  link(local, member_at("hkaSplineCompressedAnimation", "blockOffsets"), offsets_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "blockOffsets"), 2);
  link(local, member_at("hkaSplineCompressedAnimation", "data"), blob_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "data"), blob_size);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

inline std::vector<std::byte> spline_rotation_packfile(int format, int degree) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  const std::size_t data_offset = data_section_offset(names.size());
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  pad_to(data, data_offset, 4);
  const std::size_t blob_at = data.size();
  data.push_back(static_cast<std::byte>((format << 2) & 0x3C));
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0xF0});
  data.push_back(std::byte{0x00});

  data.push_back(std::byte{0x01});
  data.push_back(std::byte{0x00});
  data.push_back(static_cast<std::byte>(degree));
  const std::size_t knot_count = static_cast<std::size_t>(degree) + 3;
  for (std::size_t i = 0; i < knot_count; ++i) {
    data.push_back(static_cast<std::byte>(i < 2 ? 0 : 1));
  }
  pad_to(data, data_offset, format == 4 ? 2 : 1);
  for (int point = 0; point < 2; ++point) {
    if (format == 3) {
      data.push_back(std::byte{0xBF});
      data.push_back(std::byte{0x3F});
      data.push_back(std::byte{0x3F});
    } else {
      data.push_back(std::byte{0x7E});
      data.push_back(std::byte{0x77});
    }
  }
  const std::size_t blob_size = data.size() - blob_at;
  pad_to(data, data_offset, 4);

  const std::size_t offsets_at = data.size();
  put_u32(data, 0);

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 1.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  set_int_member(data, "hkaSplineCompressedAnimation", "numFrames", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "numBlocks", 1);
  set_int_member(data, "hkaSplineCompressedAnimation", "maxFramesPerBlock", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "maskAndQuantizationSize", 4);
  set_float_member(data, "hkaSplineCompressedAnimation", "blockDuration", 1.0f);
  set_float_member(data, "hkaSplineCompressedAnimation", "frameDuration", 1.0f);
  link(local, member_at("hkaSplineCompressedAnimation", "blockOffsets"), offsets_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "blockOffsets"), 1);
  link(local, member_at("hkaSplineCompressedAnimation", "data"), blob_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "data"), blob_size);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

inline std::vector<std::byte> spline_mixed_position_packfile() {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  const std::size_t data_offset = data_section_offset(names.size());
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  pad_to(data, data_offset, 4);
  const std::size_t blob_at = data.size();
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x12});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x07});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x00});

  data.push_back(std::byte{0x07});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x01});
  for (const int knot : {0, 0, 1, 2, 3, 4, 5, 6, 7, 7}) {
    data.push_back(static_cast<std::byte>(knot));
  }
  pad_to(data, data_offset, 4);
  put_float(data, 0.0f);
  put_float(data, 10.0f);
  put_float(data, 4.0f);
  for (int point = 0; point < 8; ++point) data.push_back(static_cast<std::byte>(point * 30));
  pad_to(data, data_offset, 4);
  put_float(data, 7.0f);
  put_float(data, 8.0f);
  put_float(data, 9.0f);
  const std::size_t blob_size = data.size() - blob_at;
  pad_to(data, data_offset, 4);

  const std::size_t offsets_at = data.size();
  put_u32(data, 0);

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 1.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 2);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  set_int_member(data, "hkaSplineCompressedAnimation", "numFrames", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "numBlocks", 1);
  set_int_member(data, "hkaSplineCompressedAnimation", "maxFramesPerBlock", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "maskAndQuantizationSize", 8);
  set_float_member(data, "hkaSplineCompressedAnimation", "blockDuration", 1.0f);
  set_float_member(data, "hkaSplineCompressedAnimation", "frameDuration", 1.0f);
  link(local, member_at("hkaSplineCompressedAnimation", "blockOffsets"), offsets_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "blockOffsets"), 1);
  link(local, member_at("hkaSplineCompressedAnimation", "data"), blob_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "data"), blob_size);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

inline std::vector<std::byte> spline_zero_frame_packfile(std::size_t data_size = 0) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 0.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  set_int_member(data, "hkaSplineCompressedAnimation", "numFrames", 0);
  set_int_member(data, "hkaSplineCompressedAnimation", "numBlocks", 1);
  set_int_member(data, "hkaSplineCompressedAnimation", "maxFramesPerBlock", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "maskAndQuantizationSize", 4);
  set_float_member(data, "hkaSplineCompressedAnimation", "blockDuration", 0.0f);
  set_float_member(data, "hkaSplineCompressedAnimation", "frameDuration", 0.0f);
  const std::size_t blob_at = data.size();
  data.resize(blob_at + data_size, std::byte{0});
  if (data_size > 0) link(local, member_at("hkaSplineCompressedAnimation", "data"), blob_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "data"), data_size);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

}  // namespace fo4::test::hkx
