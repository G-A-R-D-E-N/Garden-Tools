#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

class WeaponError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

inline constexpr std::uint16_t k_weapon_damage_curve_form_version = 152;

struct WeaponDamageType {
  FormKey type{};
  std::uint32_t amount{0};
  FormKey curve_table{};

  friend bool operator==(const WeaponDamageType&, const WeaponDamageType&) = default;
};

std::vector<WeaponDamageType> decode_weapon_damage_types(const Plugin& plugin, const FormKey& key);

}  // namespace fo4::forms
