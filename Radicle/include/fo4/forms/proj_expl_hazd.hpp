#pragma once

// Typed creation and decoding for the three record families whose payload is a
// fixed runtime struct: PROJ (Projectile), EXPL (Explosion) and HAZD (Hazard).
//
// Every field below is measured from the shipped masters, not derived from
// CommonLibF4's in-memory structs -- two of the three disk layouts differ from
// the memory layout. docs/FORMAT_NOTES.md carries the measured tables and the
// evidence; the field order here follows the file.
//
// Defaults follow xEdit's create defaults or attested vanilla values. Where
// neither gives a defensible default the field is required instead (HAZD has
// no usable default model, so HazardCreation::model has no default).

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::forms {

class ProjExplHazdError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Serialized payload extents. Creation always emits the current shape.
inline constexpr std::size_t k_proj_dnam_size = 93;
inline constexpr std::size_t k_expl_data_size = 84;
inline constexpr std::size_t k_hazd_dnam_size = 52;

// EXPL DATA also ships truncated -- 52, 56, 60 and 64 bytes, a clean prefix in
// each case. The decoder accepts these and leaves the absent tail at defaults.
inline constexpr std::array<std::size_t, 5> k_expl_data_accepted_sizes{52, 56, 60, 64, 84};

// Object bounds: six int16 in the order x1 y1 z1 x2 y2 z2. All-zero is the
// most common shipped value in every one of the three families.
using ObjectBounds = std::array<std::int16_t, 6>;

// The empty MODT every creator falls back to: version 4, zero texture entries.
// Ten shipped HAZD records carry exactly these bytes.
std::vector<std::byte> empty_model_texture_data();

struct ProjectileCreation {
  std::string editor_id;
  ObjectBounds bounds{};

  // DNAM, in file order.
  std::uint16_t flags{0};
  std::uint16_t type{1};
  float gravity{0.0F};
  float speed{1000.0F};
  float range{4000.0F};
  FormKey light{};                 // LIGH
  FormKey muzzle_flash_light{};    // LIGH
  float explosion_proximity{0.0F};
  float explosion_timer{0.0F};
  FormKey explosion_type{};        // EXPL
  FormKey active_sound_loop{};     // SNDR
  float muzzle_flash_duration{0.0F};
  float fade_out_time{0.0F};
  float force{0.0F};
  FormKey countdown_sound{};       // SNDR
  FormKey deactivate_sound{};      // SNDR
  FormKey default_weapon_source{}; // WEAP
  float cone_spread{0.0F};
  float collision_radius{10.0F};
  float lifetime{0.0F};
  float relaunch_interval{0.25F};
  FormKey decal_data{};            // TXST
  FormKey collision_layer{};       // COLL
  std::uint8_t tracer_frequency{0};
  FormKey vats_projectile{};       // PROJ

  // Muzzle flash model path. Empty emits the single-null-byte NAM1 that 25
  // shipped records carry.
  std::string muzzle_flash_model;
  std::uint32_t sound_level{1};    // VNAM

  std::optional<std::string> name;  // FULL, localized

  friend bool operator==(const ProjectileCreation&, const ProjectileCreation&) = default;
};

struct ExplosionCreation {
  std::string editor_id;
  ObjectBounds bounds{};

  // DATA, in file order.
  FormKey light{};                 // LIGH
  FormKey sound1{};                // SNDR
  FormKey sound2{};                // SNDR
  FormKey impact_data_set{};       // IPDS
  FormKey impact_placed_object{};  // any placeable base
  FormKey spawn_projectile{};      // PROJ
  float force{0.0F};
  float damage{0.0F};
  float inner_radius{0.0F};
  float outer_radius{0.0F};
  float image_space_radius{0.0F};
  float vertical_offset_mult{0.0F};
  std::uint32_t flags{0};
  std::uint32_t sound_level{1};
  float placed_object_fade_delay{0.0F};
  std::uint32_t stagger_magnitude{0};
  std::array<float, 3> spawn_projectile_vector{};
  float projectile_spread{0.0F};
  std::uint32_t projectile_count{0};

  std::optional<std::string> name;  // FULL, localized

  friend bool operator==(const ExplosionCreation&, const ExplosionCreation&) = default;
};

struct HazardCreation {
  std::string editor_id;
  ObjectBounds bounds{};

  // MODL is required: all 78 shipped HAZD records carry one, and there is no
  // defensible default path. MODT defaults to empty_model_texture_data().
  std::string model;
  std::optional<std::vector<std::byte>> model_texture_data;

  // DNAM, in file order.
  std::uint32_t limit{0};
  float radius{0.0F};
  float lifetime{0.0F};
  float image_space_radius{0.0F};
  float target_interval{1.0F};
  std::uint32_t flags{0};
  FormKey spell{};             // SPEL or ENCH
  FormKey light{};             // LIGH
  FormKey impact_data_set{};   // IPDS
  FormKey sound{};             // SNDR
  float full_effect_radius{0.0F};
  float taper_weight{0.0F};
  float taper_curve{0.0F};

  std::optional<std::string> name;  // FULL, localized

  friend bool operator==(const HazardCreation&, const HazardCreation&) = default;
};

// Creation is transactional: a rejected FormKey, a malformed field or a failed
// authoring preflight leaves the caller's plugin untouched.
FormKey create_projectile(Plugin& plugin, const ProjectileCreation& creation);
FormKey create_explosion(Plugin& plugin, const ExplosionCreation& creation);
FormKey create_hazard(Plugin& plugin, const HazardCreation& creation);

// Decoders read back what the creators write, and also read shipped records.
// A payload whose extent is not an accepted shape throws rather than guessing.
ProjectileCreation decode_projectile(const Plugin& plugin, const FormKey& key);
ExplosionCreation decode_explosion(const Plugin& plugin, const FormKey& key);
HazardCreation decode_hazard(const Plugin& plugin, const FormKey& key);

// Payload encoders, exposed so a round-trip test can prove that re-encoding a
// decoded shipped record reproduces its bytes exactly. They take the plugin
// because a FormKey naming a master can only be encoded against that plugin's
// master list -- there is no context-free way to write one.
std::vector<std::byte> encode_projectile_dnam(const Plugin& plugin,
                                              const ProjectileCreation& creation);
std::vector<std::byte> encode_explosion_data(const Plugin& plugin,
                                             const ExplosionCreation& creation);
std::vector<std::byte> encode_hazard_dnam(const Plugin& plugin,
                                          const HazardCreation& creation);

}  // namespace fo4::forms
