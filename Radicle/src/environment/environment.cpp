#include "fo4/environment/environment.hpp"

#include "fo4/environment/placed.hpp"
#include "fo4/io/path_text.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <format>
#include <new>
#include <set>
#include <utility>

namespace fo4::env {
namespace {

constexpr Signature k_edid{'E', 'D', 'I', 'D'};
constexpr Signature k_full{'F', 'U', 'L', 'L'};
constexpr Signature k_obnd{'O', 'B', 'N', 'D'};
constexpr Signature k_obte{'O', 'B', 'T', 'E'};
constexpr Signature k_stop{'S', 'T', 'O', 'P'};
constexpr Signature k_cell{'C', 'E', 'L', 'L'};
constexpr Signature k_land{'L', 'A', 'N', 'D'};

using io::utf8;

bool ascii(std::string_view value) noexcept {
  return std::all_of(value.begin(), value.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x80; });
}

bool stored_text_equals(std::string_view a, std::string_view b) {
  if (ascii(a) && ascii(b)) return text::equals_folded(a, b);
  return text::equals_folded(text::windows1252_to_utf8(a), text::windows1252_to_utf8(b));
}

const Subrecord* first_subrecord(const Record& record, Signature signature) noexcept {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

std::optional<std::string_view> editor_id_of(const Record& record) noexcept {
  const auto* edid = first_subrecord(record, k_edid);
  return edid == nullptr ? std::nullopt : zstring_view(*edid);
}

PluginKind kind_of(const Plugin& plugin) noexcept {
  return classify_plugin_kind(plugin.header.tes4_flags, plugin.key);
}

}  // namespace

std::size_t Environment::FormKeyHash::operator()(const FormKey& key) const noexcept {
  const auto owner = hash_value(key.owner());
  return owner ^ (static_cast<std::size_t>(key.local().value()) * 0x9E3779B97F4A7C15ull);
}

std::optional<std::string_view> zstring_view(const Subrecord& subrecord) noexcept {
  const auto& data = subrecord.data;
  if (data.empty() || data.back() != std::byte{0}) return std::nullopt;
  const auto* begin = reinterpret_cast<const char*>(data.data());
  const auto length = std::strlen(begin);
  if (length != data.size() - 1) return std::nullopt;
  return std::string_view{begin, length};
}

RecordName decode_record_name(const Plugin& owner, const Record& record) noexcept {
  const Subrecord* full = nullptr;
  bool in_template = false;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == k_obte) in_template = true;
    if (sub.signature == k_stop) in_template = false;
    if (!in_template && sub.signature == k_full) {
      full = &sub;
      break;
    }
  }
  if (full == nullptr) return {};
  if (owner.header.tes4_flags.is_localized()) {
    if (full->data.size() != 4) return {NameKind::malformed, {}, 0};
    std::uint32_t id = 0;
    std::memcpy(&id, full->data.data(), 4);
    return {NameKind::string_id, {}, id};
  }
  const auto text = zstring_view(*full);
  if (!text) return {NameKind::malformed, {}, 0};
  return {NameKind::inline_text, *text, 0};
}

std::optional<ObjectBounds> decode_object_bounds(const Record& record) noexcept {
  const auto* obnd = first_subrecord(record, k_obnd);
  if (obnd == nullptr || obnd->data.size() != 12) return std::nullopt;
  std::array<std::int16_t, 6> values{};
  std::memcpy(values.data(), obnd->data.data(), 12);
  ObjectBounds bounds;
  bounds.min = {values[0], values[1], values[2]};
  bounds.max = {values[3], values[4], values[5]};
  return bounds;
}

Environment Environment::load(std::span<const std::filesystem::path> ordered_paths) {
  std::vector<Plugin> plugins;
  plugins.reserve(ordered_paths.size());
  for (const auto& path : ordered_paths) {
    try {
      plugins.push_back(read_plugin_file(path));
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::exception& error) {
      throw EnvironmentError("failed to load " + utf8(path) + ": " + error.what());
    }
  }
  return from_plugins(std::move(plugins));
}

Environment Environment::from_plugins(std::vector<Plugin> ordered_plugins) {
  Environment env;
  env.loaded_ = std::move(ordered_plugins);
  std::set<PluginKey> seen;
  for (const auto& plugin : env.loaded_) {
    if (plugin.key.empty() || !seen.insert(plugin.key).second) {
      throw EnvironmentError("duplicate or unnamed plugin in load order: " + utf8(plugin.path));
    }
  }

  std::vector<MasterDescriptor> known;
  known.reserve(env.loaded_.size());
  for (const auto& plugin : env.loaded_) known.push_back({plugin.key, kind_of(plugin)});
  for (auto& plugin : env.loaded_) apply_master_kinds(plugin, known);

  env.view_.reserve(env.loaded_.size());
  for (const auto& plugin : env.loaded_) env.view_.push_back(&plugin);
  env.index_plugins();
  return env;
}

Environment Environment::borrow(std::span<const Plugin* const> ordered) {
  Environment env;
  env.view_.reserve(ordered.size());
  std::set<PluginKey> seen;
  for (const auto* plugin : ordered) {
    if (plugin == nullptr) throw EnvironmentError("null plugin in borrowed load order");
    if (plugin->key.empty() || !seen.insert(plugin->key).second) {
      throw EnvironmentError("duplicate or unnamed plugin in load order: " + utf8(plugin->path));
    }
    env.view_.push_back(plugin);
  }
  env.index_plugins();
  return env;
}

void Environment::index_plugins() {
  plugins_.reserve(view_.size());
  plugin_records_.resize(view_.size());
  std::set<PluginKey> earlier;
  for (std::size_t index = 0; index < view_.size(); ++index) {
    const auto& plugin = *view_[index];
    PluginInfo info;
    info.key = plugin.key;
    info.path = plugin.path;
    info.kind = kind_of(plugin);
    info.master_flag = is_master_plugin(plugin.header.tes4_flags, plugin.key);
    info.localized = plugin.header.tes4_flags.is_localized();
    for (const auto& master : plugin.header.masters) {
      info.masters.push_back(master.key);
      if (!earlier.contains(master.key)) info.unresolved_masters.push_back(master.key);
    }
    plugins_.push_back(std::move(info));
    earlier.insert(plugin.key);

    contexts_.push_back(plugin_form_id_context(plugin));
    owners_.insert(plugin.key);
    for (const auto& master : plugin.header.masters) owners_.insert(master.key);
    for (const auto& group : plugin.groups) {
      index_group(index, group, RecordRef::k_no_container, RecordRef::k_no_container);
    }
  }

  // Version vectors are final now, so pointers into them stay valid.
  world_members_.resize(containers_.size());
  cell_members_.resize(containers_.size());
  for (std::size_t index = 0; index < view_.size(); ++index) {
    for (const auto& key : plugin_records_[index]) {
      for (const auto& ref : versions_.at(key)) {
        if (ref.plugin != index) continue;
        if (ref.cell != RecordRef::k_no_container) {
          cell_members_[ref.cell].push_back(&ref);
        } else if (ref.world != RecordRef::k_no_container) {
          world_members_[ref.world].push_back(&ref);
        }
      }
    }
  }
}

std::uint32_t Environment::intern_container(std::size_t plugin, const GroupHeader& header) {
  FormKey key;
  try {
    key = resolve_plugin_form_id(RawFormId{header.label_as_u32()}, contexts_[plugin]);
  } catch (const std::exception& error) {
    throw EnvironmentError("unresolvable group label in " + utf8(view_[plugin]->path) + ": " + error.what());
  }
  if (key.owner().empty()) {
    throw EnvironmentError("null group label in " + utf8(view_[plugin]->path));
  }
  const auto [it, inserted] =
      container_index_.try_emplace(key, static_cast<std::uint32_t>(containers_.size()));
  if (inserted) containers_.push_back(key);
  return it->second;
}

void Environment::index_group(std::size_t plugin,
                              const Group& group,
                              std::uint32_t world,
                              std::uint32_t cell) {
  const auto type = static_cast<GroupType>(group.header.group_type);
  if (type == GroupType::world_children) world = intern_container(plugin, group.header);
  if (type == GroupType::cell_children) cell = intern_container(plugin, group.header);
  for (const auto& child : group.children) {
    if (const auto* nested = std::get_if<Group>(&child)) {
      index_group(plugin, *nested, world, cell);
      continue;
    }
    const auto& record = std::get<Record>(child);
    if (record.form_key.owner().empty()) {
      if (record.header.form_id.is_null()) continue;
      throw EnvironmentError(std::format("record {:08X} in {} uses master index {} past its {}-entry master table",
                                         record.header.form_id.value(), utf8(view_[plugin]->path),
                                         record.header.form_id.index_byte(), contexts_[plugin].masters.size()));
    }
    auto& versions = versions_[record.form_key];
    if (!versions.empty() && versions.back().plugin == plugin) {
      throw EnvironmentError("record " + record.form_key.to_string() + " appears more than once in " +
                             utf8(view_[plugin]->path));
    }
    versions.push_back({plugin, &record, type, world, cell});
    plugin_records_[plugin].push_back(record.form_key);
  }
}

std::optional<std::size_t> Environment::find_plugin(std::string_view filename) const {
  for (std::size_t index = 0; index < plugins_.size(); ++index) {
    if (text::equals_folded(plugins_[index].key.filename(), filename)) return index;
  }
  return std::nullopt;
}

std::span<const RecordRef> Environment::versions(const FormKey& key) const noexcept {
  const auto it = versions_.find(key);
  if (it == versions_.end()) return {};
  return it->second;
}

const RecordRef* Environment::winner(const FormKey& key) const noexcept {
  const auto found = versions(key);
  if (found.empty()) return nullptr;
  for (auto it = found.rbegin(); it + 1 != found.rend(); ++it) {
    if (!is_partial_form(*it->record)) return &*it;
  }
  return &found.front();
}

const RecordRef* Environment::find_by_editor_id(std::string_view editor_id,
                                                std::optional<Signature> signature) const {
  if (editor_id.find_first_not_of(" \t\r\n\v\f") == std::string_view::npos) return nullptr;
  for (std::size_t index = view_.size(); index-- > 0;) {
    for (const auto& key : plugin_records_[index]) {
      for (const auto& ref : versions(key)) {
        if (ref.plugin != index) continue;
        if (signature && ref.record->header.signature != *signature) continue;
        const auto edid = editor_id_of(*ref.record);
        if (edid && stored_text_equals(*edid, editor_id)) return &ref;
      }
    }
  }
  return nullptr;
}

std::vector<const RecordRef*> Environment::winners_of(Signature signature) const {
  std::vector<const RecordRef*> result;
  for (std::size_t index = view_.size(); index-- > 0;) {
    for (const auto& key : plugin_records_[index]) {
      const auto* win = winner(key);
      if (win == nullptr || win->plugin != index) continue;
      if (win->record->header.signature == signature) result.push_back(win);
    }
  }
  return result;
}

std::vector<const RecordRef*> Environment::plugin_records(std::size_t plugin) const {
  std::vector<const RecordRef*> result;
  if (plugin >= plugin_records_.size()) return result;
  result.reserve(plugin_records_[plugin].size());
  for (const auto& key : plugin_records_[plugin]) {
    for (const auto& version : versions(key)) {
      if (version.plugin == plugin) {
        result.push_back(&version);
        break;
      }
    }
  }
  return result;
}

FormKey Environment::resolve_link(const RecordRef& ref, RawFormId raw) const {
  return resolve_plugin_link(raw, contexts_.at(ref.plugin));
}

const PluginKey* Environment::stable_owner(const FormKey& key) const noexcept {
  const auto it = owners_.find(key.owner());
  return it == owners_.end() ? nullptr : &*it;
}

std::optional<FormKey> Environment::world_of(const RecordRef& ref) const {
  if (ref.world == RecordRef::k_no_container) return std::nullopt;
  return containers_.at(ref.world);
}

std::optional<FormKey> Environment::cell_of(const RecordRef& ref) const {
  if (ref.cell == RecordRef::k_no_container) return std::nullopt;
  return containers_.at(ref.cell);
}

const std::vector<const RecordRef*>* Environment::members(
    const std::vector<std::vector<const RecordRef*>>& table, const FormKey& key) const {
  const auto it = container_index_.find(key);
  return it == container_index_.end() ? nullptr : &table[it->second];
}

const RecordRef* Environment::worldspace_top_cell(const FormKey& world) const {
  const auto* list = members(world_members_, world);
  if (list == nullptr) return nullptr;
  for (auto it = list->rbegin(); it != list->rend(); ++it) {
    const auto* ref = *it;
    if (ref->group == GroupType::world_children && ref->record->header.signature == k_cell) {
      return ref;
    }
  }
  return nullptr;
}

std::vector<const RecordRef*> Environment::worldspace_cells(const FormKey& world) const {
  std::vector<const RecordRef*> result;
  const auto* list = members(world_members_, world);
  if (list == nullptr) return result;
  std::set<FormKey> seen;
  for (auto it = list->rbegin(); it != list->rend(); ++it) {
    const auto* ref = *it;
    if (ref->group != GroupType::exterior_cell_subblock ||
        ref->record->header.signature != k_cell) {
      continue;
    }
    if (seen.insert(ref->record->form_key).second) result.push_back(ref);
  }
  // Collected highest plugin first with each plugin reversed; restore ascending on-disk order.
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<const RecordRef*> Environment::interior_cells() const {
  std::vector<const RecordRef*> result;
  for (const auto* ref : winners_of(k_cell)) {
    if (ref->group == GroupType::interior_cell_subblock) result.push_back(ref);
  }
  return result;
}

std::vector<CellChild> Environment::cell_children(const FormKey& cell) const {
  std::vector<CellChild> result;
  const auto* list = members(cell_members_, cell);
  if (list == nullptr) return result;
  std::vector<CellChild> merged;
  std::vector<bool> removed;
  std::unordered_map<FormKey, std::size_t, FormKeyHash> slots;
  for (const auto* ref : *list) {
    const bool persistent = ref->group == GroupType::cell_persistent_children;
    if (!persistent && ref->group != GroupType::cell_temporary_children) continue;
    const auto [slot, inserted] = slots.try_emplace(ref->record->form_key, merged.size());
    if (inserted) {
      merged.push_back({ref, persistent});
      removed.push_back(false);
    } else {
      merged[slot->second] = {ref, persistent};
    }
    removed[slot->second] = ref->record->header.flags.is_deleted();
  }
  for (std::size_t i = 0; i < merged.size(); ++i) {
    if (removed[i] || versions_.at(merged[i].ref->record->form_key).back().cell != merged[i].ref->cell) continue;
    result.push_back(merged[i]);
  }
  return result;
}

const RecordRef* Environment::cell_landscape(const FormKey& cell) const {
  const auto* list = members(cell_members_, cell);
  if (list == nullptr) return nullptr;
  for (auto it = list->rbegin(); it != list->rend(); ++it) {
    if ((*it)->record->header.signature == k_land) return *it;
  }
  return nullptr;
}

RecordMetadata Environment::metadata(const RecordRef& ref) const {
  const auto& record = *ref.record;
  const auto& owner = plugin(ref.plugin);
  RecordMetadata meta;
  meta.form_key = record.form_key;
  meta.signature = record.header.signature;
  meta.plugin = ref.plugin;
  meta.flags = record.header.flags;
  meta.deleted = record.header.flags.is_deleted();
  meta.editor_id = editor_id_of(record);
  meta.name = decode_record_name(owner, record);
  meta.model = primary_model_path(ref);
  meta.bounds = decode_object_bounds(record);
  return meta;
}

}  // namespace fo4::env
