#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fo4::forms {

class RaceError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct RaceCreation {
  std::optional<std::string_view> editor_id;
  std::optional<std::string_view> full_name;
  std::string_view description;
  float male_height{0.0f};
  float female_height{0.0f};
  std::array<float, 3> male_default_weight{};
  std::array<float, 3> female_default_weight{};
  std::uint32_t flags{0};
  float acceleration_rate{0.0f};
  float deceleration_rate{0.0f};
  std::uint32_t size{0};
  float injured_health_pct{0.0f};
  std::int32_t shield_biped_object{0};
  std::int32_t beard_biped_object{0};
  std::int32_t body_biped_object{0};
  float aim_angle_tolerance{0.0f};
  float flight_radius{0.0f};
  float angular_acceleration_rate{0.0f};
  float angular_tolerance{0.0f};
  std::uint32_t flags2{0};
  std::int32_t pipboy_biped_object{0};
  std::int16_t xp_value{0};
};

struct Race {
  FormKey key{};
  std::optional<std::string> editor_id;
  std::optional<std::string> full_name;
  std::string description;
  float male_height{0.0f};
  float female_height{0.0f};
  std::array<float, 3> male_default_weight{};
  std::array<float, 3> female_default_weight{};
  std::uint32_t flags{0};
  float acceleration_rate{0.0f};
  float deceleration_rate{0.0f};
  std::uint32_t size{0};
  float injured_health_pct{0.0f};
  std::int32_t shield_biped_object{0};
  std::int32_t beard_biped_object{0};
  std::int32_t body_biped_object{0};
  float aim_angle_tolerance{0.0f};
  float flight_radius{0.0f};
  float angular_acceleration_rate{0.0f};
  float angular_tolerance{0.0f};
  std::uint32_t flags2{0};
  std::int32_t pipboy_biped_object{0};
  std::int16_t xp_value{0};
};

Race decode_race(const Plugin& plugin, const FormKey& key);
FormKey create_race(Plugin& plugin, const RaceCreation& creation);

}  // namespace fo4::forms
