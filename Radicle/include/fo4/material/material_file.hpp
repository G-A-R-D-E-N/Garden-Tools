#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::material {

enum class MaterialFormat : std::uint8_t { bgsm, bgem };

enum class FieldType : std::uint8_t { boolean, u8, u32, f32, color3, string };

struct RawBool {
  std::uint8_t raw{0};
  bool operator==(const RawBool&) const = default;
};

struct Color3 {
  float r{0.0F};
  float g{0.0F};
  float b{0.0F};
  bool operator==(const Color3&) const = default;
};

using FieldValue = std::variant<RawBool, std::uint8_t, std::uint32_t, float, Color3, std::string>;

struct MaterialField {
  std::string name;
  FieldValue value;

  FieldType type() const noexcept { return static_cast<FieldType>(value.index()); }
};

struct MaterialFile {
  MaterialFormat format{MaterialFormat::bgsm};
  std::vector<MaterialField> fields;
  std::vector<std::byte> trailing;

  std::uint32_t version() const;
};

MaterialFile read_material_file(std::span<const std::byte> bytes);
std::vector<std::byte> write_material_file(const MaterialFile& file);

const MaterialField* find_field(const MaterialFile& file, std::string_view name) noexcept;
MaterialField* find_field(MaterialFile& file, std::string_view name) noexcept;

std::string_view text(std::string_view raw) noexcept;
bool set_text(MaterialFile& file, std::string_view name, std::string_view value);
void check_settable(std::string_view name);

template <typename T>
std::optional<T> get_field(const MaterialFile& file, std::string_view name) {
  const auto* field = find_field(file, name);
  if (field == nullptr) {
    return std::nullopt;
  }
  if constexpr (std::is_same_v<T, bool>) {
    const auto* raw = std::get_if<RawBool>(&field->value);
    if (raw == nullptr) {
      return std::nullopt;
    }
    return raw->raw != 0;
  } else {
    const auto* value = std::get_if<T>(&field->value);
    if (value == nullptr) {
      return std::nullopt;
    }
    return *value;
  }
}

template <typename T>
bool set_field(MaterialFile& file, std::string_view name, T value) {
  check_settable(name);
  auto* field = find_field(file, name);
  if (field == nullptr) {
    return false;
  }
  if constexpr (std::is_same_v<T, bool>) {
    if (!std::holds_alternative<RawBool>(field->value)) {
      return false;
    }
    field->value = RawBool{static_cast<std::uint8_t>(value ? 1 : 0)};
  } else {
    if (!std::holds_alternative<T>(field->value)) {
      return false;
    }
    field->value = std::move(value);
  }
  return true;
}

}  // namespace fo4::material
