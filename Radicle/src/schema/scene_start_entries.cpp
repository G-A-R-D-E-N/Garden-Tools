#include "scene_start_entries.hpp"

#include "fo4/schema/ctda.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace fo4::detail {
namespace {

Signature tag(const char* value) { return Signature::from_string(value); }

}  // namespace

env::detail::SceneLayout checked_scene_layout(const Record& record) {
  if (record.header.signature != Signature::from_string("SCEN")) {
    throw schema::ScalarError("scene fields do not apply to " +
                              record.header.signature.to_string() + " records");
  }
  try {
    return env::detail::scene_layout(record.subrecords);
  } catch (const env::detail::SceneLayoutError& error) {
    throw schema::ScalarError("malformed scene " + error.tag.to_string() + ": " + error.what());
  }
}

std::optional<StartSceneLayout> start_scene_layout(const Record& record, std::size_t action) {
  const auto layout = checked_scene_layout(record);
  if (action >= layout.actions.size()) return std::nullopt;
  const auto& group = layout.actions[action];
  const auto& type_data = record.subrecords[group.open].data;
  if (type_data.size() != 2 || type_data[0] != std::byte{4} || type_data[1] != std::byte{0}) {
    return std::nullopt;
  }
  std::vector<std::pair<std::size_t, std::size_t>> entries;
  std::size_t marker = group.close;
  bool topic = false, greeting = false;
  for (std::size_t i = group.header_end; i < group.close;) {
    const auto& sub = record.subrecords[i];
    if (sub.signature == tag("STSC") && !topic && entries.empty() && !greeting && sub.data.size() == 4) {
      topic = true; ++i; continue;
    }
    if (sub.signature == tag("HTID") && !greeting && sub.data.empty()) {
      greeting = true; marker = i; ++i; continue;
    }
    if (sub.signature != tag("LCEP") || greeting || sub.data.size() != 4) {
      throw schema::FormLinkFieldError("unsupported Start Scene action payload");
    }
    const auto begin = i++;
    unsigned order = 0;
    bool condition = false, cis1 = false, cis2 = false;
    std::optional<std::uint32_t> condition_count;
    for (; i < group.close; ++i) {
      const auto& field = record.subrecords[i];
      if (field.signature == tag("LCEP") || field.signature == tag("HTID")) break;
      if (field.signature == tag("INTT") && order < 1 && field.data.size() == 2) order = 1;
      else if (field.signature == tag("SSPN") && order < 2 && !field.data.empty() && field.data.back() == std::byte{0}) order = 2;
      else if (field.signature == tag("CITC") && order < 3 && field.data.size() == 4) {
        order = 3;
        std::uint32_t count = 0;
        for (unsigned n = 0; n < 4; ++n) count |= std::to_integer<std::uint32_t>(field.data[n]) << (n * 8);
        condition_count = count;
      }
      else if (field.signature == tag("CTDA") && field.data.size() == 32) {
        order = 4; condition = true; cis1 = false; cis2 = false;
      } else if (field.signature == tag("CIS1") && condition && !cis1 && !cis2) cis1 = true;
      else if (field.signature == tag("CIS2") && condition && !cis2) cis2 = true;
      else throw schema::FormLinkFieldError("unsupported Start Scene entry payload");
    }
    try {
      const auto conditions = read_ctda_conditions_in(record, begin + 1, i);
      if (condition_count && *condition_count != conditions.size())
        throw schema::FormLinkFieldError("Start Scene condition count does not match");
    }
    catch (const CtdaMultipleListsError&) { throw schema::FormLinkFieldError("malformed Start Scene conditions"); }
    catch (const CtdaNotLosslessError&) { throw schema::FormLinkFieldError("malformed Start Scene conditions"); }
    entries.emplace_back(begin, i);
  }
  return StartSceneLayout{std::move(entries), marker};
}

std::optional<std::vector<std::pair<std::size_t, std::size_t>>> start_scene_entries(const Record& record, std::size_t action) {
  const auto parsed = start_scene_layout(record, action);
  if (!parsed) return std::nullopt;
  return parsed->entries;
}

}  // namespace fo4::detail
