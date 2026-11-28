#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/localized_string_lookup.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fo4::env {

enum class StringSource : std::uint8_t {
  normal,
  dl,
  il,
};

StringSource string_source(Signature record, Signature subrecord) noexcept;

std::string_view string_source_extension(StringSource source) noexcept;

struct PluginStringTable {
  StringSource source{StringSource::normal};
  std::filesystem::path path;
  std::string archive;
  localization::LocalizedStringTable table;
};

struct PluginStringTableSnapshot {
  PluginStringTable resolved;
  std::vector<std::byte> bytes;
};

std::vector<PluginStringTableSnapshot> load_plugin_string_table_snapshots(
    std::span<const std::filesystem::path> strings_dirs, const Plugin& plugin,
    std::string_view language, const archive::AssetResolver* archives = nullptr);

std::vector<PluginStringTable> load_plugin_string_tables(std::span<const std::filesystem::path> strings_dirs,
                                                         const Plugin& plugin,
                                                         std::string_view language,
                                                         const archive::AssetResolver* archives = nullptr);

// Per-plugin string tables for one language; immutable after load and thread-safe for reads.
class StringTables final : public LocalizedStringLookup {
public:
  static StringTables load(const Environment& env,
                           const std::filesystem::path& strings_dir,
                           std::string_view language,
                           const archive::AssetResolver* archives = nullptr);

  // Invariant: each table is found as strings/<stem>_<language>.<ext>, loose over the resolver's roots first, then its archives; no strings directory is read.
  static StringTables load_from_assets(const Environment& env,
                                       std::string_view language,
                                       const archive::AssetResolver& assets);

  const std::string& language() const noexcept { return language_; }
  const std::filesystem::path* table_path(std::size_t plugin, StringSource source) const noexcept;
  std::optional<std::string_view> lookup(std::size_t plugin,
                                         StringSource source,
                                         std::uint32_t id) const noexcept;

  std::optional<std::string_view> lookup_field(std::size_t plugin,
                                               Signature record,
                                               Signature subrecord,
                                               std::uint32_t id) const noexcept override;

  std::optional<std::string_view> resolve_name(const Environment& env, const RecordRef& ref) const;

private:
  static StringTables load_tables(const Environment& env,
                                  const std::filesystem::path* strings_dir,
                                  std::string_view language,
                                  const archive::AssetResolver* archives);

  struct Table {
    std::filesystem::path path;
    std::unordered_map<std::uint32_t, std::string> strings;
  };

  std::string language_;
  std::vector<std::array<std::optional<Table>, 3>> tables_;
};

}  // namespace fo4::env
