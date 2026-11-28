#pragma once

#include <cstddef>
#include <cstdint>

namespace fo4::env {

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

}  // namespace fo4::env
