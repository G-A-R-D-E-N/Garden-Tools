#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_test_fixture.hpp"

#include <array>
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

inline std::vector<std::byte> spline_packfile(int block_offset_override, int mask_size_override,
                                              const std::vector<int>& knots = {0, 0, 1, 1}) {
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
  for (const int knot : knots) data.push_back(static_cast<std::byte>(knot));
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
                                             std::uint32_t max_frames, std::uint32_t float_tracks = 0) {
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
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", float_tracks);
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

inline std::vector<std::byte> spline_two_block_packfile(std::vector<std::uint32_t> stored_offsets = {}) {
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
  for (const std::uint32_t offset : stored_offsets.empty() ? block_offsets : stored_offsets) put_u32(data, offset);
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

inline std::vector<std::byte> spline_rotation_packfile(int format, int degree, bool descending = false) {
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
    data.push_back(static_cast<std::byte>(((i < 2) != descending) ? 0 : 1));
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

inline std::vector<std::byte> spline_static_rotation_packfile(const std::array<float, 4>& quaternion) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  const std::size_t data_offset = data_section_offset(names.size());
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  pad_to(data, data_offset, 4);
  const std::size_t blob_at = data.size();
  data.push_back(std::byte{0x14});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x01});
  data.push_back(std::byte{0x00});
  for (const float value : quaternion) put_float(data, value);
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

inline std::vector<std::byte> spline_float_tracks_packfile(std::uint32_t float_block_offset, std::uint32_t mask_size) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  const std::size_t data_offset = data_section_offset(names.size());
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  pad_to(data, data_offset, 4);
  const std::size_t blob_at = data.size();
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x01});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0x01});
  data.push_back(std::byte{0x10});
  while (data.size() - blob_at < mask_size) data.push_back(std::byte{0});
  put_float(data, 2.5f);
  while (data.size() - blob_at < 16) data.push_back(std::byte{0});
  put_float(data, 7.0f);
  for (const int knot : {1, 0, 1, 0, 0, 1, 1}) data.push_back(static_cast<std::byte>(knot));
  pad_to(data, data_offset, 4);
  put_float(data, 1.0f);
  put_float(data, 4.0f);
  data.push_back(std::byte{0x00});
  data.push_back(std::byte{0xFF});
  pad_to(data, data_offset, 4);
  const std::size_t blob_size = data.size() - blob_at;
  pad_to(data, data_offset, 4);

  const std::size_t offsets_at = data.size();
  put_u32(data, 0);
  put_u32(data, float_block_offset);

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 1.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "numFrames", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "numBlocks", 1);
  set_int_member(data, "hkaSplineCompressedAnimation", "maxFramesPerBlock", 2);
  set_int_member(data, "hkaSplineCompressedAnimation", "maskAndQuantizationSize", mask_size);
  set_float_member(data, "hkaSplineCompressedAnimation", "blockDuration", 1.0f);
  set_float_member(data, "hkaSplineCompressedAnimation", "frameDuration", 1.0f);
  link(local, member_at("hkaSplineCompressedAnimation", "blockOffsets"), offsets_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "blockOffsets"), 1);
  link(local, member_at("hkaSplineCompressedAnimation", "floatBlockOffsets"), offsets_at + 4);
  set_count(data, member_at("hkaSplineCompressedAnimation", "floatBlockOffsets"), 1);
  link(local, member_at("hkaSplineCompressedAnimation", "data"), blob_at);
  set_count(data, member_at("hkaSplineCompressedAnimation", "data"), blob_size);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

inline std::vector<std::byte> spline_zero_frame_packfile(std::size_t data_size = 0, std::int32_t float_tracks = 0) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaSplineCompressedAnimation"), "hkaSplineCompressedAnimation");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaSplineCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 0.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 1);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", float_tracks);
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

inline void put_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFF));
  out.push_back(static_cast<std::byte>(value >> 8));
}

inline void put_u64(std::vector<std::byte>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
}

inline std::size_t append_cstring(std::vector<std::byte>& data, const std::string& text) {
  const std::size_t at = data.size();
  for (char ch : text) data.push_back(static_cast<std::byte>(ch));
  data.push_back(std::byte{0});
  return at;
}

// Requires every channel kind in one clip: dynamic, static and clear translation, rotation and scale.
inline std::vector<std::byte> lossless_packfile() {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaLosslessCompressedAnimation"), "hkaLosslessCompressedAnimation");
  const std::size_t data_offset = data_section_offset(names.size());
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaLosslessCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  pad_to(data, data_offset, 4);
  const std::size_t dynamic_translations_at = data.size();
  for (const float value : {0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f}) put_float(data, value);

  const std::size_t static_translations_at = data.size();
  put_float(data, 4.0f);

  const std::size_t translation_words_at = data.size();
  put_u64(data, (std::uint64_t{1} << 16) | 2u);
  put_u64(data, 6u);

  const std::size_t dynamic_rotations_at = data.size();
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 1.0f);
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 0.70710678f);
  put_float(data, 0.70710678f);
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 1.0f);
  put_float(data, 0.0f);

  const std::size_t static_rotations_at = data.size();
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 0.0f);
  put_float(data, 1.0f);

  const std::size_t rotation_words_at = data.size();
  put_u16(data, 1);
  put_u16(data, 2);

  const std::size_t static_scales_at = data.size();
  put_float(data, 1.0f);

  const std::size_t scale_words_at = data.size();
  put_u64(data, std::uint64_t{1} | (std::uint64_t{1} << 16) | (std::uint64_t{1} << 32));
  put_u64(data, 0u);

  pad_to(data, data_offset, 4);
  const std::size_t annotation_tracks_at = data.size();
  data.resize(annotation_tracks_at + 24, std::byte{0});
  const std::size_t events_at = data.size();
  data.resize(events_at + 2 * 16, std::byte{0});
  link(local, annotation_tracks_at + member_at("hkaAnnotationTrack", "trackName"), append_cstring(data, "Root"));
  link(local, annotation_tracks_at + member_at("hkaAnnotationTrack", "annotations"), events_at);
  set_count(data, annotation_tracks_at + member_at("hkaAnnotationTrack", "annotations"), 2);
  patch_float(data, events_at + member_at("hkaAnnotationTrackAnnotation", "time"), 0.0f);
  link(local, events_at + member_at("hkaAnnotationTrackAnnotation", "text"), append_cstring(data, "start"));
  patch_float(data, events_at + 16 + member_at("hkaAnnotationTrackAnnotation", "time"), 0.5f);
  link(local, events_at + 16 + member_at("hkaAnnotationTrackAnnotation", "text"), append_cstring(data, "mid"));
  link(local, member_at("hkaAnimation", "annotationTracks"), annotation_tracks_at);
  set_count(data, member_at("hkaAnimation", "annotationTracks"), 1);

  link(local, member_at("hkaLosslessCompressedAnimation", "dynamicTranslations"), dynamic_translations_at);
  set_count(data, member_at("hkaLosslessCompressedAnimation", "dynamicTranslations"), 6);
  link(local, member_at("hkaLosslessCompressedAnimation", "staticTranslations"), static_translations_at);
  set_count(data, member_at("hkaLosslessCompressedAnimation", "staticTranslations"), 1);
  link(local, member_at("hkaLosslessCompressedAnimation", "translationTypeAndOffsets"), translation_words_at);
  set_count(data, member_at("hkaLosslessCompressedAnimation", "translationTypeAndOffsets"), 2);
  link(local, member_at("hkaLosslessCompressedAnimation", "dynamicRotations"), dynamic_rotations_at);
  set_count(data, member_at("hkaLosslessCompressedAnimation", "dynamicRotations"), 3);
  link(local, member_at("hkaLosslessCompressedAnimation", "staticRotations"), static_rotations_at);
  set_count(data, member_at("hkaLosslessCompressedAnimation", "staticRotations"), 1);
  link(local, member_at("hkaLosslessCompressedAnimation", "rotationTypeAndOffsets"), rotation_words_at);
  set_count(data, member_at("hkaLosslessCompressedAnimation", "rotationTypeAndOffsets"), 2);
  link(local, member_at("hkaLosslessCompressedAnimation", "staticScales"), static_scales_at);
  set_count(data, member_at("hkaLosslessCompressedAnimation", "staticScales"), 1);
  link(local, member_at("hkaLosslessCompressedAnimation", "scaleTypeAndOffsets"), scale_words_at);
  set_count(data, member_at("hkaLosslessCompressedAnimation", "scaleTypeAndOffsets"), 2);

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 2.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", 2);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
  set_int_member(data, "hkaLosslessCompressedAnimation", "numFrames", 3);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

// Requires only declared numbers: the arrays exist but hold no samples, so each test varies one count.
inline std::vector<std::byte> lossless_shape_packfile(std::uint32_t frames, std::uint32_t tracks,
                                                      std::uint32_t dynamic_translation_count,
                                                      std::uint32_t channel_word_count,
                                                      std::uint32_t float_track_count = 0,
                                                      std::uint32_t float_count = 0,
                                                      std::uint64_t translation_word = 0,
                                                      float first_dynamic_translation = 0.0f,
                                                      bool link_floats = true, float first_float = 0.0f) {
  std::vector<std::byte> names;
  const auto name_at =
      add_class_name(names, signature_of("hkaLosslessCompressedAnimation"), "hkaLosslessCompressedAnimation");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaLosslessCompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;

  const auto append_array = [&](const char* member, std::uint32_t count, std::size_t stride) -> std::size_t {
    if (count == 0) return 0;
    const std::size_t field = member_at("hkaLosslessCompressedAnimation", member);
    const std::size_t at = data.size();
    data.resize(at + static_cast<std::size_t>(count) * stride, std::byte{0});
    link(local, field, at);
    set_count(data, field, count);
    return at;
  };
  const std::size_t dynamic_translations_at = append_array("dynamicTranslations", dynamic_translation_count, 4);
  const std::size_t translation_words_at = append_array("translationTypeAndOffsets", channel_word_count, 8);
  append_array("rotationTypeAndOffsets", channel_word_count, 2);
  append_array("scaleTypeAndOffsets", channel_word_count, 8);
  const std::size_t floats_at = append_array("floats", float_count, 4);
  // Requires a non-zero first float sample: a zero-filled array cannot prove the samples were read back.
  if (float_count > 0) patch_float(data, floats_at, first_float);
  // Requires a declared float count with no data pointer when link_floats is false: the array is unreadable.
  if (!link_floats && float_count > 0) local.resize(local.size() - 2);
  if (dynamic_translation_count > 0) patch_float(data, dynamic_translations_at, first_dynamic_translation);
  if (channel_word_count > 0) {
    for (int byte = 0; byte < 8; ++byte) {
      data[translation_words_at + byte] = static_cast<std::byte>((translation_word >> (byte * 8)) & 0xFFu);
    }
  }

  set_int_member(data, "hkaAnimation", "type", 0);
  set_float_member(data, "hkaAnimation", "duration", 2.0f);
  set_int_member(data, "hkaAnimation", "numberOfTransformTracks", tracks);
  set_int_member(data, "hkaAnimation", "numberOfFloatTracks", float_track_count);
  set_int_member(data, "hkaLosslessCompressedAnimation", "numFrames", frames);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}
}  // namespace fo4::test::hkx
