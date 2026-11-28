#include "fo4/archive/asset_scan.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace fo4::archive {
namespace {

Signature sig(std::string_view text) { return Signature::from_string(text); }

// MODL/MOD2 through MOD5 paths must resolve under Meshes and ICON/MICO under Textures.
std::string rooted_path(std::string_view text, std::string_view root) {
  std::string folded;
  folded.reserve(text.size());
  for (const char ch : text) {
    folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  std::replace(folded.begin(), folded.end(), '\\', '/');
  // Invariant: a leading separator or space is not part of the path, so it cannot hide the root.
  folded.erase(0, folded.find_first_not_of("/ "));
  const std::string prefix = std::string{root} + '/';
  if (folded.starts_with("data/")) return folded;
  if (folded.compare(0, prefix.size(), prefix) == 0) return folded;
  return prefix + folded;
}

std::optional<std::string_view> path_text(const Subrecord& subrecord) noexcept {
  const auto& data = subrecord.data;
  const auto nul = std::find(data.begin(), data.end(), std::byte{0});
  if (nul == data.begin() || nul == data.end()) return std::nullopt;
  const auto* begin = reinterpret_cast<const char*>(data.data());
  return std::string_view{begin, static_cast<std::size_t>(nul - data.begin())};
}

struct ExtensionRoot {
  std::string_view extension;
  std::string_view root;
};

constexpr std::array<ExtensionRoot, 11> k_asset_extensions{{
    {".nif", "meshes"},
    {".hkx", "meshes"},
    {".dds", "textures"},
    {".bgsm", "materials"},
    {".bgem", "materials"},
    {".wav", "sound"},
    {".xwm", "sound"},
    {".fuz", "sound"},
    {".swf", "interface"},
    {".pex", "scripts"},
    {".seq", "seq"},
}};

constexpr std::array<std::string_view, 8> k_data_roots{
    {"meshes", "textures", "materials", "sound", "interface", "scripts", "seq", "music"}};

std::string lowered_slashed(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    out.push_back(ch == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  out.erase(0, out.find_first_not_of("/ "));
  return out;
}

const ExtensionRoot* asset_extension(std::string_view path) {
  const auto dot = path.rfind('.');
  if (dot == std::string_view::npos || path.find('/', dot) != std::string_view::npos) return nullptr;
  const auto ext = path.substr(dot);
  for (const auto& entry : k_asset_extensions) {
    if (ext == entry.extension) return &entry;
  }
  return nullptr;
}

}  // namespace

std::vector<std::string> record_asset_paths(const Record& record) {
  static constexpr std::array<std::string_view, 7> k_path_subrecords{
      {"MODL", "MOD2", "MOD3", "MOD4", "MOD5", "ICON", "MICO"}};
  const auto rec = record.header.signature;
  std::vector<std::string> out;
  for (const auto& subrecord : record.subrecords) {
    bool wanted = false;
    for (const auto name : k_path_subrecords) {
      if (subrecord.signature == sig(name)) {
        wanted = true;
        break;
      }
    }
    if (!wanted) continue;
    if (subrecord.signature == sig("MODL") && (rec == sig("ARMO") || rec == sig("ARMA"))) continue;
    if ((subrecord.signature == sig("MOD3") || subrecord.signature == sig("MOD5")) && rec != sig("ARMA")) continue;
    if (const auto text = path_text(subrecord)) {
      const bool mesh = subrecord.signature != sig("ICON") && subrecord.signature != sig("MICO");
      std::string path = rooted_path(*text, mesh ? "meshes" : "textures");
      if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(std::move(path));
    }
  }
  return out;
}

std::vector<std::string> record_schema_asset_strings(const Record& record) {
  std::vector<std::string> out;
  for (const auto& field : schema::scalar_fields(record.header.signature)) {
    if (field.scalar.storage != schema::ScalarStorage::zstring || field.scalar.localized ||
        schema::is_localized_field(field.record, field.scalar.subrecord)) {
      continue;
    }
    const auto resolved = schema::resolve_field_slots(record, schema::scalar_field_shape(field.scalar),
                                                      field.scalar.address);
    for (const auto& slot : resolved.slots) {
      if (!slot.valid) continue;
      std::string text;
      try {
        const auto bytes = schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar);
        text = schema::decode_scalar(bytes, field.scalar);
      } catch (const schema::ScalarError&) {
        continue;
      }
      auto path = lowered_slashed(text);
      if (asset_extension(path) == nullptr) continue;
      if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(std::move(path));
    }
  }
  return out;
}

std::vector<AssetReference> collect_asset_references(const Plugin& plugin) {
  std::vector<AssetReference> out;
  for (const auto* record : collect_records(plugin)) {
    if (record == nullptr || record->header.signature == sig("TES4") || record->form_key.owner() != plugin.key) {
      continue;
    }
    for (auto& path : record_schema_asset_strings(*record)) {
      out.push_back(AssetReference{record->form_key, std::move(path)});
    }
  }
  return out;
}

std::vector<std::string> asset_path_candidates(std::string_view path) {
  auto normalized = normalize_asset_path(path);
  const auto* entry = asset_extension(normalized);
  const auto slash = normalized.find('/');
  const auto first = std::string_view{normalized}.substr(0, slash);
  const bool rooted = slash != std::string::npos &&
                      (entry != nullptr
                           ? first == entry->root
                           : std::find(k_data_roots.begin(), k_data_roots.end(), first) != k_data_roots.end());
  std::vector<std::string> out;
  if (entry != nullptr && !rooted) {
    out.push_back(normalized);
    out.push_back(std::string{entry->root} + '/' + normalized);
  } else {
    out.push_back(std::move(normalized));
  }
  return out;
}

std::vector<MissingAsset> find_missing_assets(const Plugin& plugin, const AssetResolver& resolver) {
  std::vector<MissingAsset> out;
  for (const auto* record : collect_records(plugin)) {
    if (record == nullptr) continue;
    for (auto& path : record_asset_paths(*record)) {
      std::optional<AssetLocation> found;
      try {
        found = resolver.locate(path);
      } catch (const AssetPathError&) {
        // Unnormalizable paths must not end the scan; they are skipped, not reported.
        continue;
      }
      if (!found.has_value()) out.push_back(MissingAsset{record->form_key, std::move(path)});
    }
  }
  return out;
}

}  // namespace fo4::archive
