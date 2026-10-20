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
#include <set>
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
  static constexpr std::uint32_t k_no_container = 0xFFFFFFFFu;

  std::size_t plugin{0};
  const Record* record{nullptr};
  // Type of the group that directly holds the record.
  GroupType group{GroupType::top};
  // Indexes into Environment::container(): the enclosing WRLD and CELL, if any.
  std::uint32_t world{k_no_container};
  std::uint32_t cell{k_no_container};
};

struct CellChild {
  const RecordRef* ref{nullptr};
  bool persistent{false};
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

  // In-memory load order: plugins must be lowest priority first, same validation as load().
  static Environment from_plugins(std::vector<Plugin> ordered_plugins);

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

  std::vector<const RecordRef*> plugin_records(std::size_t plugin) const;

  RecordMetadata metadata(const RecordRef& ref) const;

  // Resolves a raw FormID stored in `ref`'s plugin; null IDs give an empty FormKey.
  FormKey resolve_link(const RecordRef& ref, RawFormId raw) const;
  // The same key with its owner name borrowed from the environment, for lifetime-safe views.
  const PluginKey* stable_owner(const FormKey& key) const noexcept;

  const FormKey& container(std::uint32_t index) const { return containers_.at(index); }
  std::optional<FormKey> world_of(const RecordRef& ref) const;
  std::optional<FormKey> cell_of(const RecordRef& ref) const;

  // Persistent CELL directly under the world's children group, from the highest-priority plugin.
  const RecordRef* worldspace_top_cell(const FormKey& world) const;
  // Block/sub-block CELLs under the world, each from the highest plugin listing it; ascending plugin, on-disk order.
  std::vector<const RecordRef*> worldspace_cells(const FormKey& world) const;
  // Winning CELLs whose winning version sits in an interior sub-block.
  std::vector<const RecordRef*> interior_cells() const;
  // Persistent/temporary children merged low to high priority: later copies replace, deleted copies remove.
  std::vector<CellChild> cell_children(const FormKey& cell) const;
  // LAND child of the highest-priority CELL version that has one.
  const RecordRef* cell_landscape(const FormKey& cell) const;

private:
  struct FormKeyHash {
    std::size_t operator()(const FormKey& key) const noexcept;
  };

  std::uint32_t intern_container(std::size_t plugin, const GroupHeader& header);
  void index_group(std::size_t plugin, const Group& group, std::uint32_t world, std::uint32_t cell);
  const std::vector<const RecordRef*>* members(
      const std::vector<std::vector<const RecordRef*>>& table, const FormKey& key) const;

  std::vector<Plugin> loaded_;
  std::vector<PluginInfo> plugins_;
  std::unordered_map<FormKey, std::vector<RecordRef>, FormKeyHash> versions_;
  // Per plugin, the records it holds in on-disk order, as FormKeys.
  std::vector<std::vector<FormKey>> plugin_records_;
  std::vector<PluginFormIdContext> contexts_;
  std::vector<FormKey> containers_;
  std::unordered_map<FormKey, std::uint32_t, FormKeyHash> container_index_;
  std::set<PluginKey> owners_;
  // Per container, records it holds (cell children, or world-level records outside any cell).
  std::vector<std::vector<const RecordRef*>> world_members_;
  std::vector<std::vector<const RecordRef*>> cell_members_;
};

// Decoders shared with Environment::metadata; exposed for focused tests.
std::optional<std::string_view> zstring_view(const Subrecord& subrecord) noexcept;
RecordName decode_record_name(const Plugin& owner, const Record& record) noexcept;
std::optional<ObjectBounds> decode_object_bounds(const Record& record) noexcept;

}  // namespace fo4::env
