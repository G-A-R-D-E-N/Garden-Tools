#pragma once

#include "fo4/plugin/container.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

class OmodError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct OmodInclude {
  FormKey mod{};
  std::uint8_t minimum_level{0};
  std::uint8_t optional{0};
  std::uint8_t use_all{0};
};

struct OmodItem {
  std::array<std::byte, 4> value1{};
  std::array<std::byte, 4> value2{};
};

struct OmodProperty {
  std::uint8_t value_type{0};
  std::uint8_t function_type{0};
  std::uint16_t property{0};
  std::array<std::byte, 4> value1{};
  std::array<std::byte, 4> value2{};
  float step{0.0f};
};

struct OmodData {
  std::uint8_t unknown_bool1{0};
  std::uint8_t unknown_bool2{0};
  std::uint32_t form_type{0};
  std::uint8_t max_rank{0};
  std::uint8_t level_tier_scaled_offset{0};
  FormKey attach_point{};
  std::vector<FormKey> attach_parent_slots;
  std::vector<OmodItem> items;
  std::vector<OmodInclude> includes;
  std::vector<OmodProperty> properties;
};

enum class OmodDataWrite {
  written,
  record_missing,
  unsupported_record,
  missing_data_subrecord,
};

OmodData read_omod_data(const Plugin& plugin, const Record& record);

OmodDataWrite write_omod_data(Plugin& plugin, const FormKey& key, const OmodData& data);

std::optional<FormKey> omod_property_value1_form(const Plugin& plugin,
                                                 const OmodProperty& property);
std::optional<float> omod_property_value1_float(const OmodProperty& property);
std::optional<std::uint32_t> omod_property_value1_int(const OmodProperty& property);
std::optional<float> omod_property_value2_float(const OmodProperty& property);
std::optional<std::uint32_t> omod_property_value2_int(const OmodProperty& property);

}  // namespace fo4::forms
