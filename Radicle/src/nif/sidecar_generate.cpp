#include "fo4/nif/sidecar_generate.hpp"

#include "fo4/error.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::uint32_t k_no_bone = 0xFFFFFFFFU;
constexpr std::uint32_t k_first_user_index = 1;
constexpr std::size_t k_sub_bit_base = 8;
constexpr std::size_t k_max_sub_segments = 8;
constexpr const char* k_base_bone = "DISABLED";

std::uint32_t delta_id(std::size_t segment, std::size_t sub) {
  return static_cast<std::uint32_t>(segment << 16U) | (1U << (k_sub_bit_base + sub));
}

bool has_sub_segments(const SegmentTable& table) {
  return std::any_of(table.segments.begin(), table.segments.end(),
                     [](const Segment& segment) { return !segment.sub_segments.empty(); });
}

}  // namespace

SsfShape ssf_shape_from_segments(const SegmentTable& table, std::string_view shape_name,
                                 std::span<const std::string> strings) {
  SsfShape shape;
  shape.name = std::string(shape_name);
  shape.base_bone = k_base_bone;
  if (!has_sub_segments(table) || table.user_data.empty()) return shape;

  if (table.segments.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw parse_error(0, "too many segments to encode in an .ssf id");
  }
  if (table.segment_starts.size() != table.segments.size() || table.user_data.size() != table.total_segments) {
    throw parse_error(0, "segment table layout is not understood: user data does not cover every total segment");
  }

  std::map<std::uint32_t, std::string> names;
  for (const auto& text : strings) names.emplace(bone_id_hash(text), text);

  std::vector<std::string> order;
  std::map<std::string, std::vector<std::uint32_t>> ids;
  for (std::size_t i = 0; i < table.segments.size(); ++i) {
    const auto& subs = table.segments[i].sub_segments;
    if (subs.size() > k_max_sub_segments) {
      throw parse_error(0, "too many sub-segments to encode in an .ssf id");
    }
    for (std::size_t k = 0; k < subs.size(); ++k) {
      const auto slot = static_cast<std::size_t>(table.segment_starts[i]) + 1 + k;
      if (slot >= table.user_data.size()) {
        throw parse_error(0, "sub-segment is past the end of the user data");
      }
      const auto& user = table.user_data[slot];
      if (user.user_index != k_first_user_index || !user.cut_offsets.empty() || user.bone_id == k_no_bone) continue;
      const auto name = names.find(user.bone_id);
      if (name == names.end()) continue;
      if (!ids.contains(name->second)) order.push_back(name->second);
      auto& list = ids[name->second];
      const auto id = delta_id(i, k);
      if (std::find(list.begin(), list.end(), id) == list.end()) list.push_back(id);
    }
  }
  for (const auto& bone : order) {
    SsfBoneDelta delta;
    delta.bone_name = bone;
    delta.segment_ids = ids[bone];
    shape.deltas.push_back(std::move(delta));
  }
  return shape;
}

std::string generate_ssf(const SegmentTable& table, std::string_view shape_name,
                         std::span<const std::string> strings) {
  return write_ssf({ssf_shape_from_segments(table, shape_name, strings)});
}

bool ssf_lists_segments(const SsfShape& shape, const SegmentTable& table) {
  std::set<std::uint32_t> listed;
  for (const auto& delta : shape.deltas) {
    for (const auto id : delta.segment_ids) listed.insert(id >> 16U);
  }
  std::set<std::uint32_t> expected;
  for (std::size_t i = 0; i < table.segments.size(); ++i) {
    if (!table.segments[i].sub_segments.empty()) expected.insert(static_cast<std::uint32_t>(i));
  }
  return listed == expected;
}

}  // namespace fo4::nif
