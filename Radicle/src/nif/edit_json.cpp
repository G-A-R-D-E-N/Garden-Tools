#include "fo4/nif/nif_write.hpp"

#include "fo4/error.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace fo4::nif {
namespace {

nlohmann::json parse_array(std::string_view text) {
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(text.begin(), text.end());
  } catch (const std::exception& error) {
    throw parse_error(0, std::string{"NIF edits are not valid JSON: "} + error.what());
  }
  if (!parsed.is_array()) throw parse_error(0, "NIF edits must be a JSON array");
  return parsed;
}

bool all_numbers(const nlohmann::json& values) {
  for (const auto& value : values) {
    if (!value.is_number()) return false;
  }
  return true;
}

bool all_strings(const nlohmann::json& values) {
  for (const auto& value : values) {
    if (!value.is_string()) return false;
  }
  return true;
}

// Invariant: a flag toggle is the editor's {key,label,on} object, so only a boolean "on" and a text "key" are accepted.
bool all_flag_toggles(const nlohmann::json& values) {
  for (const auto& value : values) {
    if (!value.is_object() || !value.contains("key") || !value.contains("on")) return false;
    if (!value.at("key").is_string() || !value.at("on").is_boolean()) return false;
  }
  return true;
}

std::vector<std::string> flag_names(const nlohmann::json& values) {
  std::vector<std::string> names;
  for (const auto& value : values) {
    if (value.at("on").get<bool>()) names.push_back(value.at("key").get<std::string>());
  }
  return names;
}

// Invariant: only the five on/off keys take a JSON boolean, so a float key cannot be set with true.
bool is_boolean_key(std::string_view key) noexcept {
  return key == "alphaBlend" || key == "alphaTest" || key == "editorMarker" || key == "envMapSSR" ||
         key == "envMapWetnessSSR";
}

// Invariant: a number is accepted only when the float it is stored as reads back as the number the caller sent.
bool stores_exactly(double value, float stored) {
  if (static_cast<double>(stored) == value) return true;
  char text[32];
  const auto written = std::to_chars(text, text + sizeof(text), stored);
  if (written.ec != std::errc{}) return false;
  double parsed = 0.0;
  const auto read = std::from_chars(text, written.ptr, parsed);
  return read.ec == std::errc{} && parsed == value;
}

float exact_float(const nlohmann::json& raw, const std::string& key) {
  const auto value = raw.get<double>();
  // Invariant: the range check runs before the narrowing cast, which is undefined for a double past FLT_MAX.
  if (!(std::fabs(value) <= static_cast<double>(std::numeric_limits<float>::max()))) {
    throw parse_error(0, "key '" + key + "' value " + raw.dump() +
        " is outside the range of a 32-bit float; NIF edit values are 32-bit floats");
  }
  const auto stored = static_cast<float>(value);
  if (!stores_exactly(value, stored)) {
    throw parse_error(0, "key '" + key + "' value " + raw.dump() +
        " cannot be stored exactly; NIF edit values are 32-bit floats");
  }
  return stored;
}

std::vector<float> exact_floats(const nlohmann::json& raw, const std::string& key) {
  std::vector<float> values;
  values.reserve(raw.size());
  for (const auto& value : raw) values.push_back(exact_float(value, key));
  return values;
}

const nlohmann::json& segment_field(const nlohmann::json& object, const char* name) {
  if (!object.is_object() || !object.contains(name)) {
    throw parse_error(0, std::string("segment table needs the field '") + name + "'");
  }
  return object.at(name);
}

const nlohmann::json& segment_list(const nlohmann::json& object, const char* name) {
  const auto& value = segment_field(object, name);
  if (!value.is_array()) throw parse_error(0, std::string("segment field '") + name + "' must be a list");
  return value;
}

std::uint32_t segment_unsigned(const nlohmann::json& value, const std::string& name) {
  if (!value.is_number_integer() || value.get<std::int64_t>() < 0 ||
      value.get<std::int64_t>() > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw parse_error(0, "segment field '" + name + "' must be an unsigned 32-bit integer");
  }
  return static_cast<std::uint32_t>(value.get<std::int64_t>());
}

SegmentTable read_segment_table(const nlohmann::json& raw) {
  SegmentTable table;
  table.primitives = segment_unsigned(segment_field(raw, "primitives"), "primitives");
  table.total_segments = segment_unsigned(segment_field(raw, "totalSegments"), "totalSegments");
  for (const auto& segment : segment_list(raw, "segments")) {
    Segment out;
    out.start_index = segment_unsigned(segment_field(segment, "startIndex"), "startIndex");
    out.primitives = segment_unsigned(segment_field(segment, "primitives"), "primitives");
    out.parent_array_index = segment_unsigned(segment_field(segment, "parentArrayIndex"), "parentArrayIndex");
    for (const auto& sub : segment_list(segment, "subSegments")) {
      SubSegment item;
      item.start_index = segment_unsigned(segment_field(sub, "startIndex"), "startIndex");
      item.primitives = segment_unsigned(segment_field(sub, "primitives"), "primitives");
      item.parent_array_index = segment_unsigned(segment_field(sub, "parentArrayIndex"), "parentArrayIndex");
      item.unused = segment_unsigned(segment_field(sub, "unused"), "unused");
      out.sub_segments.push_back(item);
    }
    table.segments.push_back(std::move(out));
  }
  for (const auto& start : segment_list(raw, "segmentStarts")) {
    table.segment_starts.push_back(segment_unsigned(start, "segmentStarts"));
  }
  for (const auto& user : segment_list(raw, "userData")) {
    SegmentUserData item;
    item.user_index = segment_unsigned(segment_field(user, "userIndex"), "userIndex");
    item.bone_id = segment_unsigned(segment_field(user, "boneId"), "boneId");
    for (const auto& offset : segment_list(user, "cutOffsets")) {
      if (!offset.is_number()) throw parse_error(0, "segment cut offsets must be finite numbers");
      item.cut_offsets.push_back(exact_float(offset, "cutOffsets"));
    }
    table.user_data.push_back(std::move(item));
  }
  const auto& ssf = segment_field(raw, "ssfFile");
  if (!ssf.is_string()) throw parse_error(0, "segment field 'ssfFile' must be text");
  table.ssf_file = ssf.get<std::string>();
  return table;
}

Edit read_edit(const nlohmann::json& raw) {
  if (!raw.is_object() || !raw.contains("id") || !raw.contains("key") || !raw.contains("value")) {
    throw parse_error(0, "NIF edit needs id, key and value");
  }
  const auto& raw_id = raw.at("id");
  const auto& raw_key = raw.at("key");
  const auto& raw_value = raw.at("value");
  if (!raw_id.is_number_integer() || !raw_key.is_string()) {
    throw parse_error(0, "NIF edit needs an unsigned block id and a text key");
  }
  const auto id = raw_id.get<std::int64_t>();
  if (id < 0 || id > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw parse_error(0, "NIF edit block id " + std::to_string(id) + " is out of range");
  }
  Edit edit{static_cast<std::uint32_t>(id), raw_key.get<std::string>(), std::string{}};
  if (raw_value.is_string()) {
    edit.value = raw_value.get<std::string>();
  } else if (raw_value.is_number()) {
    if (is_boolean_key(edit.key)) throw parse_error(0, "key '" + edit.key + "' takes true or false, not a number");
    edit.value = exact_float(raw_value, edit.key);
  } else if (raw_value.is_boolean()) {
    if (!is_boolean_key(edit.key)) {
      throw parse_error(0, "key '" + edit.key +
          "' does not take a boolean; only alphaBlend, alphaTest, editorMarker, envMapSSR and envMapWetnessSSR do");
    }
    edit.value = raw_value.get<bool>() ? 1.0f : 0.0f;
  } else if (raw_value.is_object() && edit.key == "segments") {
    edit.value = read_segment_table(raw_value);
  } else if (raw_value.is_array() && raw_value.empty()) {
    // Invariant: an empty list is an empty name list, because the only list-valued key is the shader flag list.
    edit.value = std::vector<std::string>{};
  } else if (raw_value.is_array() && all_strings(raw_value)) {
    edit.value = raw_value.get<std::vector<std::string>>();
  } else if (raw_value.is_array() && all_numbers(raw_value)) {
    edit.value = exact_floats(raw_value, edit.key);
  } else if (raw_value.is_array() && all_flag_toggles(raw_value)) {
    edit.value = flag_names(raw_value);
  } else {
    throw parse_error(0, "key '" + edit.key +
        "' has a value the NIF writer cannot set (text, numbers, booleans, number lists, flag-name lists or flag toggles only)");
  }
  return edit;
}

}  // namespace

std::vector<Edit> read_edits_json(std::string_view text) {
  std::vector<Edit> edits;
  for (const auto& raw : parse_array(text)) edits.push_back(read_edit(raw));
  return edits;
}

std::vector<std::byte> apply_edits_json(const NifFile& nif, std::string_view text) {
  return apply_edits(nif, read_edits_json(text));
}

}  // namespace fo4::nif
