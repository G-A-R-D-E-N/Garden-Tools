#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::forms {

class RaceError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// RACE BodyData is a repeated structural sequence, not a signature-keyed map:
// NAM1 -> MNAM -> (INDX + generic-model bundle)* -> FNAM ->
// (INDX + generic-model bundle)*. The generic model is sparse/unordered in
// xEdit, so preserve its proven MODL/MODT/MODC/MODS/MODF subrecords verbatim.
struct RaceBodyPart {
  // INDX carries a uint32 body-part index; it is not an empty separator and
  // it is not derivable from list position. Across Fallout4.esm's 90 gender
  // blocks, 17 are not a plain 0..n-1 run (they start 0,0,1,2,...), so the
  // stored value is preserved rather than recomputed on write.
  std::uint32_t index{0};
  std::vector<Subrecord> model_subrecords;
};

struct RaceBodyData {
  std::vector<RaceBodyPart> male;
  std::vector<RaceBodyPart> female;
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
  std::optional<FormKey> male_voice;
  std::optional<FormKey> female_voice;
  float facegen_main_clamp{0.0f};
  float facegen_face_clamp{0.0f};
  RaceBodyData body_data;
};

struct Race {
  FormKey key{};
  std::optional<std::string> editor_id;
  // FULL and DESC are localized: in a localized plugin they hold a four-byte
  // string ID rather than inline text. Resolve through fo4::localization when
  // display text is needed; the stored form round-trips unchanged.
  std::optional<schema::LocalizedString> full_name;
  schema::LocalizedString description;
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
  std::optional<FormKey> male_voice;
  std::optional<FormKey> female_voice;
  float facegen_main_clamp{0.0f};
  float facegen_face_clamp{0.0f};
  RaceBodyData body_data;
};

Race decode_race(const Plugin& plugin, const FormKey& key);
FormKey create_race(Plugin& plugin, const RaceCreation& creation);

RaceBodyData decode_race_body_data(const Plugin& plugin, const FormKey& key);
bool set_race_body_data(Plugin& plugin,
                        const FormKey& key,
                        std::span<const RaceBodyPart> male,
                        std::span<const RaceBodyPart> female);

}  // namespace fo4::forms
