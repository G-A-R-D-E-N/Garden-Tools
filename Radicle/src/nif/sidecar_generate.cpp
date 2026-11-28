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
constexpr std::size_t k_sub_shift = 8;
constexpr const char* k_base_bone = "DISABLED";

std::uint32_t delta_id(std::size_t segment, std::size_t sub) {
  return static_cast<std::uint32_t>(segment << 16U) | static_cast<std::uint32_t>(sub << k_sub_shift);
}

bool has_sub_segments(const SegmentTable& table) {
  return std::any_of(table.segments.begin(), table.segments.end(),
                     [](const Segment& segment) { return !segment.sub_segments.empty(); });
}

void add_unique(std::vector<std::uint32_t>& ids, std::uint32_t id) {
  if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
}

void require_disjoint_segment_ranges(const SegmentTable& table) {
  std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
  ranges.reserve(table.segments.size());
  for (std::size_t i = 0; i < table.segments.size(); ++i) {
    const auto start = static_cast<std::uint64_t>(table.segment_starts[i]);
    ranges.emplace_back(start, start + 1 + table.segments[i].sub_segments.size());
  }
  std::sort(ranges.begin(), ranges.end());
  for (std::size_t i = 1; i < ranges.size(); ++i) {
    if (ranges[i].first < ranges[i - 1].second) {
      throw parse_error(0, "segment user-data ranges overlap; an .ssf id cannot name both segments");
    }
  }
}

}  // namespace

SsfShape ssf_shape_from_segments(const SegmentTable& table, std::string_view shape_name,
                                 std::span<const std::string> strings, SsfDiagnostics* diagnostics) {
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
  require_disjoint_segment_ranges(table);

  std::map<std::uint32_t, std::map<std::string, std::string>> names;
  for (const auto& text : strings) {
    std::string folded = text;
    for (char& c : folded) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    names[bone_id_hash(text)].try_emplace(std::move(folded), text);
  }

  std::vector<std::string> order;
  std::map<std::string, std::vector<std::uint32_t>> ids;
  SsfDiagnostics report;
  for (std::size_t i = 0; i < table.segments.size(); ++i) {
    const auto& subs = table.segments[i].sub_segments;
    if (subs.size() > k_max_ssf_sub_segments) {
      throw parse_error(0, "too many sub-segments to encode in an .ssf id");
    }
    for (std::size_t k = 0; k < subs.size(); ++k) {
      const auto slot = static_cast<std::size_t>(table.segment_starts[i]) + 1 + k;
      if (slot >= table.user_data.size()) {
        throw parse_error(0, "sub-segment is past the end of the user data");
      }
      const auto& user = table.user_data[slot];
      if (user.bone_id == k_no_bone) continue;
      if (user.cut_offsets.empty()) {
        add_unique(report.unresolved_ids, user.bone_id);
        continue;
      }
      const auto found = names.find(user.bone_id);
      if (found == names.end()) {
        add_unique(report.unresolved_ids, user.bone_id);
        continue;
      }
      if (found->second.size() > 1) {
        add_unique(report.ambiguous_ids, user.bone_id);
        continue;
      }
      const std::string& name = found->second.begin()->second;
      if (!ids.contains(name)) order.push_back(name);
      auto& list = ids[name];
      const auto id = delta_id(i, k);
      if (std::find(list.begin(), list.end(), id) == list.end()) list.push_back(id);
    }
  }
  const bool incomplete = !report.unresolved_ids.empty() || !report.ambiguous_ids.empty();
  if (incomplete && diagnostics == nullptr) {
    throw parse_error(0, "segment table has unsupported, unresolved, or ambiguous bone IDs; pass SsfDiagnostics to accept an incomplete sidecar");
  }
  if (diagnostics != nullptr) *diagnostics = std::move(report);
  for (const auto& bone : order) {
    SsfBoneDelta delta;
    delta.bone_name = bone;
    delta.segment_ids = ids[bone];
    shape.deltas.push_back(std::move(delta));
  }
  return shape;
}

std::string generate_ssf(const SegmentTable& table, std::string_view shape_name,
                         std::span<const std::string> strings, SsfDiagnostics* diagnostics) {
  return write_ssf({ssf_shape_from_segments(table, shape_name, strings, diagnostics)});
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
