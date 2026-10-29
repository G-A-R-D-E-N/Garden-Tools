#pragma once

#include "fo4/flags.hpp"
#include "fo4/plugin_key.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {

enum class PluginKind {
  full,
  light,
};

class RawFormId {
public:
  constexpr RawFormId() noexcept = default;
  constexpr explicit RawFormId(std::uint32_t value) noexcept : value_(value) {}

  constexpr std::uint32_t value() const noexcept { return value_; }
  constexpr std::uint8_t index_byte() const noexcept {
    return static_cast<std::uint8_t>(value_ >> 24);
  }
  constexpr std::uint32_t local_24() const noexcept { return value_ & 0x00FFFFFFu; }
  constexpr std::uint16_t light_slot() const noexcept {
    return static_cast<std::uint16_t>((value_ >> 12) & 0x0FFFu);
  }
  constexpr std::uint16_t light_local() const noexcept {
    return static_cast<std::uint16_t>(value_ & 0x0FFFu);
  }

  constexpr bool is_null() const noexcept { return value_ == 0; }
  constexpr bool is_none() const noexcept { return value_ == 0xFFFFFFFFu; }
  constexpr bool is_created() const noexcept { return index_byte() == 0xFF; }
  constexpr bool is_runtime_light() const noexcept { return index_byte() == 0xFE; }

  friend constexpr bool operator==(const RawFormId&, const RawFormId&) = default;
  friend constexpr auto operator<=>(const RawFormId&, const RawFormId&) = default;

private:
  std::uint32_t value_{0};
};

class LocalFormId {
public:
  constexpr LocalFormId() noexcept = default;
  constexpr explicit LocalFormId(std::uint32_t value) noexcept : value_(value) {}

  constexpr std::uint32_t value() const noexcept { return value_; }

  friend constexpr bool operator==(const LocalFormId&, const LocalFormId&) = default;
  friend constexpr auto operator<=>(const LocalFormId&, const LocalFormId&) = default;

private:
  std::uint32_t value_{0};
};

class FormKey {
public:
  FormKey() = default;
  FormKey(PluginKey owner, LocalFormId local) : owner_(std::move(owner)), local_(local) {}

  const PluginKey& owner() const noexcept { return owner_; }
  LocalFormId local() const noexcept { return local_; }

  std::string to_string() const;
  static FormKey parse(std::string_view text);

  friend bool operator==(const FormKey&, const FormKey&) = default;
  friend bool operator<(const FormKey& a, const FormKey& b) noexcept;

private:
  PluginKey owner_;
  LocalFormId local_{};
};

struct MasterDescriptor {
  PluginKey key;
  PluginKind kind{PluginKind::full};
};

struct PluginFormIdContext {
  PluginKey current_plugin;
  PluginKind current_kind{PluginKind::full};
  std::vector<MasterDescriptor> masters;
};

struct RuntimeFormIdContext {
  std::vector<MasterDescriptor> full_plugins;
  std::vector<MasterDescriptor> light_plugins;
};

class FormIdError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

FormKey resolve_plugin_form_id(RawFormId raw, const PluginFormIdContext& context);
FormKey resolve_plugin_link(RawFormId raw, const PluginFormIdContext& context);
RawFormId encode_plugin_form_id(const FormKey& key, const PluginFormIdContext& dest);
PluginKind classify_plugin_kind(RecordFlags tes4_flags, const PluginKey& key) noexcept;
bool is_master_plugin(RecordFlags tes4_flags, const PluginKey& key) noexcept;

FormKey resolve_runtime_form_id(RawFormId raw, const RuntimeFormIdContext& context);

constexpr std::uint8_t k_created_index = 0xFF;
constexpr std::uint8_t k_light_runtime_index = 0xFE;
constexpr std::uint32_t k_light_local_mask = 0x00000FFFu;
constexpr std::uint32_t k_full_local_mask = 0x00FFFFFFu;
constexpr std::size_t k_max_plugin_masters = 0xFDu;

// Lowest local FormID a plugin may allocate for its own records.
//
// Full plugins reserve everything below 0x800: their runtime FormID is
// compileIndex << 24 | local, so a low local lands in the engine's reserved
// range. Light plugins have no such floor -- their runtime FormID is
// 0xFE | slot << 12 | local, which can never collide with a reserved low form.
// Six of the nine shipped Bethesda ESLs allocate below 0x800.
//
// 0x000 is excluded for both: no shipped plugin uses it and nothing in the
// engine rules it in, so it stays unavailable rather than guessed at.
constexpr std::uint32_t k_first_full_local_id = 0x00000800u;
constexpr std::uint32_t k_first_light_local_id = 0x00000001u;

}  // namespace fo4
