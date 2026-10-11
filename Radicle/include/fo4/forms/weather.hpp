#pragma once

#include "fo4/flags.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::forms {

inline constexpr const char k_weather_api_status[] = "STABLE READ / SCHEMA FIELD EDITS / OPAQUE UNKNOWN FIELDS";

class WeatherError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

inline constexpr std::size_t k_weather_cloud_layer_slots = 32;
inline constexpr std::size_t k_weather_time_slots = 8;
inline constexpr std::size_t k_weather_imagespace_slots = 8;
inline constexpr std::size_t k_weather_god_ray_slots = 8;
inline constexpr std::uint16_t k_weather_eight_times_form_version = 111;

constexpr std::size_t weather_time_slots(std::uint16_t form_version) noexcept {
  return form_version >= k_weather_eight_times_form_version ? k_weather_time_slots : 4;
}

constexpr std::size_t weather_color_groups(std::uint16_t form_version) noexcept {
  return 10 + (form_version >= 31 ? 3 : 0) + (form_version >= 35 ? 1 : 0) + (form_version >= 37 ? 3 : 0) +
         (form_version >= 119 ? 2 : 0);
}

constexpr std::size_t weather_nam0_size(std::uint16_t form_version) noexcept {
  return weather_color_groups(form_version) * weather_time_slots(form_version) * 4;
}

constexpr std::size_t weather_fnam_size(std::uint16_t form_version) noexcept {
  return 32 + (form_version >= 119 ? 24 : 0) + (form_version >= 120 ? 16 : 0);
}

constexpr std::size_t weather_data_size(std::uint16_t form_version) noexcept {
  return form_version >= 119 ? 20 : 19;
}

constexpr std::size_t weather_unam_size(std::uint16_t form_version) noexcept {
  return form_version >= 130 ? 24 : 12;
}

constexpr std::size_t weather_imsp_size(std::uint16_t form_version) noexcept {
  return form_version >= k_weather_eight_times_form_version ? 32 : 16;
}

constexpr std::size_t weather_dalc_count(std::uint16_t form_version) noexcept {
  return form_version >= k_weather_eight_times_form_version ? 8 : 4;
}

struct WeatherSound {
  std::optional<FormKey> sound;
  std::uint32_t type{0};
};

struct WeatherUnamSlot {
  std::optional<FormKey> spell;
  float threshold{0.0f};
};

struct WeatherAmbientColors {
  std::array<std::uint32_t, 6> directional{};
  std::optional<std::uint32_t> specular;
  std::optional<float> fresnel;
};

struct Weather {
  FormKey key{};
  RecordFlags record_flags{};
  std::uint16_t form_version{0};
  std::optional<std::string> editor_id;

  std::array<std::optional<std::string>, k_weather_cloud_layer_slots> cloud_textures{};
  std::array<std::uint8_t, k_weather_cloud_layer_slots> cloud_speed_y{};
  std::array<std::uint8_t, k_weather_cloud_layer_slots> cloud_speed_x{};
  std::uint32_t cloud_color_layer_count{0};
  std::vector<std::uint32_t> cloud_colors;
  std::vector<float> cloud_alphas;
  std::uint32_t nam1_bits{0};

  std::vector<std::byte> lnam;
  std::optional<FormKey> mnam;
  std::optional<FormKey> nnam;
  std::optional<FormKey> gnam;

  std::vector<std::byte> nam0;
  std::vector<std::byte> nam4;
  std::vector<std::byte> fnam;
  std::vector<std::byte> data;

  std::vector<WeatherSound> snam;
  std::vector<std::optional<FormKey>> tnam;
  std::array<std::optional<FormKey>, k_weather_imagespace_slots> imsp{};
  std::uint8_t imsp_count{0};
  std::array<std::optional<FormKey>, k_weather_god_ray_slots> wgdr{};
  bool has_wgdr{false};
  std::vector<WeatherAmbientColors> dalc;
  std::optional<std::string> modl;
  std::vector<std::byte> modt;
  std::array<WeatherUnamSlot, 2> unam{};
  bool has_unam{false};
  std::optional<float> vnam;
  std::optional<float> wnam;
};

Weather decode_weather(const Plugin& plugin, const FormKey& key);

// source:docs/WTHR_SEMANTIC_PROOF.md; mutation is limited to the verified MNAM link slot.
bool set_weather_mnam(Plugin& plugin,
                      const FormKey& key,
                      const std::optional<FormKey>& mnam,
                      std::span<const Plugin* const> environment = {});

}
