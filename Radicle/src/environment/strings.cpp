#include "fo4/environment/strings.hpp"

#include "fo4/localization/strings.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <system_error>

namespace fo4::env {
namespace {

constexpr Signature sig(const char (&text)[5]) {
  return Signature{text[0], text[1], text[2], text[3]};
}

using io::utf8;


localization::StringTableKind table_kind(StringSource source) noexcept {
  switch (source) {
    case StringSource::dl:
      return localization::StringTableKind::dlstrings;
    case StringSource::il:
      return localization::StringTableKind::ilstrings;
    case StringSource::normal:
      break;
  }
  return localization::StringTableKind::strings;
}

void validate_language(std::string_view language) {
  const bool valid = !language.empty() && std::all_of(language.begin(), language.end(), [](unsigned char c) {
    return std::isalnum(c) != 0;
  });
  if (!valid) throw EnvironmentError("string table language must be a non-empty alphanumeric token");
}

using FileIndex = std::unordered_map<std::string, std::vector<std::filesystem::path>>;

FileIndex list_directory(const std::filesystem::path& dir) {
  FileIndex files;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return files;
  std::filesystem::directory_iterator it(dir, ec);
  if (ec) throw EnvironmentError("could not list strings directory: " + utf8(dir));
  for (const auto& entry : it) {
    if (!entry.is_regular_file(ec)) continue;
    files[text::fold(utf8(entry.path().filename()))].push_back(entry.path());
  }
  return files;
}

std::string plugin_stem(std::string_view filename) {
  const auto dot = filename.rfind('.');
  return std::string(dot == std::string_view::npos ? filename : filename.substr(0, dot));
}

std::vector<PluginStringTableSnapshot> resolve_plugin_tables(const std::vector<FileIndex>& dirs,
                                                     const Plugin& plugin,
                                                     std::string_view language,
                                                     const archive::AssetResolver* archives) {
  std::vector<PluginStringTableSnapshot> tables;
  if (!plugin.header.tes4_flags.is_localized()) return tables;
  const auto prefix = plugin_stem(plugin.key.filename()) + "_" + std::string(language) + ".";
  for (const auto source : {StringSource::normal, StringSource::dl, StringSource::il}) {
    const auto name = prefix + std::string(string_source_extension(source));
    PluginStringTable found{source, {}, {}, {}};
    std::vector<std::byte> bytes;
    bool loose = false;
    for (const auto& files : dirs) {
      const auto it = files.find(text::fold(name));
      if (it == files.end()) continue;
      if (it->second.size() > 1) {
        throw EnvironmentError("string table name matches several files differing only by case: " +
                               utf8(it->second.front()));
      }
      found.path = it->second.front();
      loose = true;
      break;
    }
    if (!loose) {
      if (archives == nullptr) continue;
      const auto location = archives->locate_in_archives("strings/" + name);
      if (!location) continue;
      const auto& reader = archives->archives()[location->archive];
      auto entry_path = reader.entries()[location->entry].path;
      std::replace(entry_path.begin(), entry_path.end(), '\\', '/');
      found.path = reader.path() / entry_path;
      found.archive = utf8(reader.path().filename());
      bytes = archives->read(*location);
    }
    try {
      if (loose) {
        std::ifstream stream(found.path, std::ios::binary | std::ios::ate);
        if (!stream) throw localization::StringTableError("could not open localized string file");
        const auto end = static_cast<std::streamoff>(stream.tellg());
        if (end < 0 || static_cast<std::uintmax_t>(end) > std::numeric_limits<std::size_t>::max() ||
            static_cast<std::uintmax_t>(end) > std::numeric_limits<std::streamsize>::max()) {
          throw localization::StringTableError("localized string file has invalid size");
        }
        bytes.resize(static_cast<std::size_t>(end));
        stream.seekg(0);
        if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
          throw localization::StringTableError("could not read localized string file");
        }
      }
      found.table = localization::read_string_table(bytes, table_kind(source));
    } catch (const localization::StringTableError& error) {
      throw EnvironmentError("malformed string table " + utf8(found.path) + ": " + error.what());
    }
    tables.push_back({std::move(found), std::move(bytes)});
  }
  return tables;
}

}  // namespace

std::vector<PluginStringTable> load_plugin_string_tables(std::span<const std::filesystem::path> strings_dirs,
                                                         const Plugin& plugin,
                                                         std::string_view language,
                                                         const archive::AssetResolver* archives) {
  auto snapshots = load_plugin_string_table_snapshots(strings_dirs, plugin, language, archives);
  std::vector<PluginStringTable> tables;
  tables.reserve(snapshots.size());
  for (auto& snapshot : snapshots) tables.push_back(std::move(snapshot.resolved));
  return tables;
}

std::vector<PluginStringTableSnapshot> load_plugin_string_table_snapshots(
    std::span<const std::filesystem::path> strings_dirs, const Plugin& plugin,
    std::string_view language, const archive::AssetResolver* archives) {
  validate_language(language);
  std::vector<FileIndex> dirs;
  dirs.reserve(strings_dirs.size());
  for (const auto& dir : strings_dirs) dirs.push_back(list_directory(dir));
  return resolve_plugin_tables(dirs, plugin, language, archives);
}

StringSource string_source(Signature record, Signature subrecord) noexcept {
  if (record != sig("LSCR") && subrecord == sig("DESC")) return StringSource::dl;
  if ((record == sig("QUST") || record == sig("BOOK")) && subrecord == sig("CNAM")) return StringSource::dl;
  if (record == sig("INFO") && subrecord != sig("RNAM")) return StringSource::il;
  return StringSource::normal;
}

std::string_view string_source_extension(StringSource source) noexcept {
  switch (source) {
    case StringSource::dl:
      return "DLSTRINGS";
    case StringSource::il:
      return "ILSTRINGS";
    case StringSource::normal:
      break;
  }
  return "STRINGS";
}

StringTables StringTables::load(const Environment& env,
                                const std::filesystem::path& strings_dir,
                                std::string_view language,
                                const archive::AssetResolver* archives) {
  validate_language(language);
  StringTables result;
  result.language_ = std::string(language);
  result.tables_.resize(env.plugins().size());
  const std::vector<FileIndex> dirs{list_directory(strings_dir)};
  for (std::size_t plugin = 0; plugin < env.plugins().size(); ++plugin) {
    for (auto& snapshot : resolve_plugin_tables(dirs, env.plugin(plugin), result.language_, archives)) {
      auto& found = snapshot.resolved;
      Table table;
      table.path = std::move(found.path);
      table.strings.reserve(found.table.entries.size());
      for (auto& entry : found.table.entries) {
        const auto* text = reinterpret_cast<const char*>(entry.text.data());
        table.strings.emplace(entry.id, std::string(text, entry.text.size()));
      }
      result.tables_[plugin][static_cast<std::size_t>(found.source)] = std::move(table);
    }
  }
  return result;
}

const std::filesystem::path* StringTables::table_path(std::size_t plugin, StringSource source) const noexcept {
  if (plugin >= tables_.size()) return nullptr;
  const auto& table = tables_[plugin][static_cast<std::size_t>(source)];
  return table ? &table->path : nullptr;
}

std::optional<std::string_view> StringTables::lookup(std::size_t plugin,
                                                     StringSource source,
                                                     std::uint32_t id) const noexcept {
  if (plugin >= tables_.size()) return std::nullopt;
  const auto& table = tables_[plugin][static_cast<std::size_t>(source)];
  if (!table) return std::nullopt;
  const auto it = table->strings.find(id);
  if (it == table->strings.end()) return std::nullopt;
  return std::string_view{it->second};
}

std::optional<std::string_view> StringTables::resolve_name(const Environment& env, const RecordRef& ref) const {
  if (ref.record == nullptr) return std::nullopt;
  const auto name = decode_record_name(env.plugin(ref.plugin), *ref.record);
  switch (name.kind) {
    case NameKind::inline_text:
      return name.text;
    case NameKind::string_id:
      return lookup(ref.plugin, StringSource::normal, name.string_id);
    case NameKind::absent:
    case NameKind::malformed:
      break;
  }
  return std::nullopt;
}

}  // namespace fo4::env
