#include "fo4/environment/environment.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <set>
#include <utility>

namespace fo4::env {
namespace {

constexpr Signature k_edid{'E', 'D', 'I', 'D'};
constexpr Signature k_full{'F', 'U', 'L', 'L'};
constexpr Signature k_modl{'M', 'O', 'D', 'L'};
constexpr Signature k_obnd{'O', 'B', 'N', 'D'};

char ascii_lower(char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
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
  return plugin.header.tes4_flags.is_small_file() ? PluginKind::light : PluginKind::full;
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
  const auto* full = first_subrecord(record, k_full);
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
  Environment env;
  env.loaded_.reserve(ordered_paths.size());
  std::set<PluginKey> seen;
  for (const auto& path : ordered_paths) {
    try {
      env.loaded_.push_back(read_plugin_file(path));
    } catch (const std::exception& error) {
      throw EnvironmentError("failed to load " + path.string() + ": " + error.what());
    }
    const auto& key = env.loaded_.back().key;
    if (key.empty() || !seen.insert(key).second) {
      throw EnvironmentError("duplicate or unnamed plugin in load order: " + path.string());
    }
  }

  std::vector<MasterDescriptor> known;
  known.reserve(env.loaded_.size());
  for (const auto& plugin : env.loaded_) known.push_back({plugin.key, kind_of(plugin)});
  for (auto& plugin : env.loaded_) apply_master_kinds(plugin, known);

  env.plugins_.reserve(env.loaded_.size());
  env.plugin_records_.resize(env.loaded_.size());
  std::set<PluginKey> earlier;
  for (std::size_t index = 0; index < env.loaded_.size(); ++index) {
    const auto& plugin = env.loaded_[index];
    PluginInfo info;
    info.key = plugin.key;
    info.path = plugin.path;
    info.kind = kind_of(plugin);
    info.master_flag = plugin.header.tes4_flags.is_master();
    info.localized = plugin.header.tes4_flags.is_localized();
    for (const auto& master : plugin.header.masters) {
      info.masters.push_back(master.key);
      if (!earlier.contains(master.key)) info.unresolved_masters.push_back(master.key);
    }
    env.plugins_.push_back(std::move(info));
    earlier.insert(plugin.key);

    auto& order = env.plugin_records_[index];
    visit_records(plugin, [&](const Record& record) {
      if (&record == &plugin.tes4 || record.form_key.owner().empty()) return;
      auto& versions = env.versions_[record.form_key];
      if (!versions.empty() && versions.back().plugin == index) {
        throw EnvironmentError("record " + record.form_key.to_string() +
                               " appears more than once in " + plugin.path.string());
      }
      versions.push_back({index, &record});
      order.push_back(record.form_key);
    });
  }
  return env;
}

std::optional<std::size_t> Environment::find_plugin(std::string_view filename) const {
  for (std::size_t index = 0; index < plugins_.size(); ++index) {
    if (iequals(plugins_[index].key.filename(), filename)) return index;
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
  return found.empty() ? nullptr : &found.back();
}

const RecordRef* Environment::find_by_editor_id(std::string_view editor_id,
                                                std::optional<Signature> signature) const {
  if (editor_id.find_first_not_of(" \t\r\n\v\f") == std::string_view::npos) return nullptr;
  for (std::size_t index = loaded_.size(); index-- > 0;) {
    for (const auto& key : plugin_records_[index]) {
      for (const auto& ref : versions(key)) {
        if (ref.plugin != index) continue;
        if (signature && ref.record->header.signature != *signature) continue;
        const auto edid = editor_id_of(*ref.record);
        if (edid && iequals(*edid, editor_id)) return &ref;
      }
    }
  }
  return nullptr;
}

std::vector<const RecordRef*> Environment::winners_of(Signature signature) const {
  std::vector<const RecordRef*> result;
  for (std::size_t index = loaded_.size(); index-- > 0;) {
    for (const auto& key : plugin_records_[index]) {
      const auto* win = winner(key);
      if (win == nullptr || win->plugin != index) continue;
      if (win->record->header.signature == signature) result.push_back(win);
    }
  }
  return result;
}

RecordMetadata Environment::metadata(const RecordRef& ref) const {
  const auto& record = *ref.record;
  const auto& owner = loaded_.at(ref.plugin);
  RecordMetadata meta;
  meta.form_key = record.form_key;
  meta.signature = record.header.signature;
  meta.plugin = ref.plugin;
  meta.flags = record.header.flags;
  meta.deleted = record.header.flags.is_deleted();
  meta.editor_id = editor_id_of(record);
  meta.name = decode_record_name(owner, record);
  if (const auto* modl = first_subrecord(record, k_modl)) meta.model = zstring_view(*modl);
  meta.bounds = decode_object_bounds(record);
  return meta;
}

}  // namespace fo4::env
