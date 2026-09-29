#pragma once

#include "fo4/flags.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fo4::env {

class EnvironmentError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct PluginInfo {
  PluginKey key;
  std::filesystem::path path;
  PluginKind kind{PluginKind::full};
  bool master_flag{false};
  bool localized{false};
  std::vector<PluginKey> masters;
  // Masters that do not appear earlier in the load order.
  std::vector<PluginKey> unresolved_masters;
};

// One on-disk version of a record. `plugin` indexes Environment::plugins().
struct RecordRef {
  std::size_t plugin{0};
  const Record* record{nullptr};
};

enum class NameKind {
  absent,
  inline_text,
  string_id,
  malformed,
};

struct RecordName {
  NameKind kind{NameKind::absent};
  std::string_view text;
  std::uint32_t string_id{0};
};

struct ObjectBounds {
  std::array<std::int16_t, 3> min{};
  std::array<std::int16_t, 3> max{};
};

struct RecordMetadata {
  FormKey form_key;
  Signature signature{};
  std::size_t plugin{0};
  RecordFlags flags{};
  bool deleted{false};
  std::optional<std::string_view> editor_id;
  RecordName name{};
  std::optional<std::string_view> model;
  std::optional<ObjectBounds> bounds;
};

// Immutable load-order view; queries are thread-safe and results live as long as the Environment.
class Environment {
public:
  Environment() = default;
  Environment(Environment&&) noexcept = default;
  Environment& operator=(Environment&&) noexcept = default;
  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;

  // Paths are lowest priority first; any unreadable, duplicate or self-conflicting plugin fails the load.
  static Environment load(std::span<const std::filesystem::path> ordered_paths);

  const std::vector<PluginInfo>& plugins() const noexcept { return plugins_; }
  const Plugin& plugin(std::size_t index) const { return loaded_.at(index); }
  std::optional<std::size_t> find_plugin(std::string_view filename) const;

  // All versions of a record, lowest priority first; empty when unknown.
  std::span<const RecordRef> versions(const FormKey& key) const noexcept;
  const RecordRef* winner(const FormKey& key) const noexcept;

  // First record version, highest-priority plugin down, whose own EDID matches case-insensitively.
  const RecordRef* find_by_editor_id(std::string_view editor_id,
                                     std::optional<Signature> signature = std::nullopt) const;

  // Winners of this signature, grouped by highest-priority plugin, then on-disk order.
  std::vector<const RecordRef*> winners_of(Signature signature) const;

  RecordMetadata metadata(const RecordRef& ref) const;

private:
  struct FormKeyHash {
    std::size_t operator()(const FormKey& key) const noexcept;
  };

  std::vector<Plugin> loaded_;
  std::vector<PluginInfo> plugins_;
  std::unordered_map<FormKey, std::vector<RecordRef>, FormKeyHash> versions_;
  // Per plugin, the records it holds in on-disk order, as FormKeys.
  std::vector<std::vector<FormKey>> plugin_records_;
};

// Decoders shared with Environment::metadata; exposed for focused tests.
std::optional<std::string_view> zstring_view(const Subrecord& subrecord) noexcept;
RecordName decode_record_name(const Plugin& owner, const Record& record) noexcept;
std::optional<ObjectBounds> decode_object_bounds(const Record& record) noexcept;

}  // namespace fo4::env
