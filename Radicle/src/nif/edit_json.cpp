#include "fo4/nif/nif_write.hpp"

#include "fo4/error.hpp"

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
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
    edit.value = raw_value.get<float>();
    if (raw_value.is_number_integer()) {
      const auto whole = raw_value.get<std::int64_t>();
      if (static_cast<double>(std::get<float>(edit.value)) != static_cast<double>(whole)) {
        throw parse_error(0, "key '" + edit.key + "' value " + std::to_string(whole) +
            " cannot be stored exactly; NIF edit values are 32-bit floats");
      }
    }
  } else if (raw_value.is_array() && all_numbers(raw_value)) {
    edit.value = raw_value.get<std::vector<float>>();
  } else if (raw_value.is_array() && all_strings(raw_value)) {
    edit.value = raw_value.get<std::vector<std::string>>();
  } else {
    throw parse_error(0, "key '" + edit.key +
        "' has a value the NIF writer cannot set (text, numbers, number lists or flag-name lists only)");
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
