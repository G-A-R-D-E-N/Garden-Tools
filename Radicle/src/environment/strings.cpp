#include "fo4/environment/strings.hpp"

#include "fo4/localization/strings.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cctype>
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

std::unordered_map<std::string, std::vector<std::filesystem::path>> list_directory(
    const std::filesystem::path& dir) {
  std::unordered_map<std::string, std::vector<std::filesystem::path>> files;
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

}  // namespace

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
  const auto files = list_directory(strings_dir);
  for (std::size_t plugin = 0; plugin < env.plugins().size(); ++plugin) {
    const auto& info = env.plugins()[plugin];
    if (!info.localized) continue;
    const auto prefix = plugin_stem(info.key.filename()) + "_" + result.language_ + ".";
    for (const auto source : {StringSource::normal, StringSource::dl, StringSource::il}) {
      const auto name = prefix + std::string(string_source_extension(source));
      const auto it = files.find(text::fold(name));
      Table table;
      std::vector<std::byte> archived;
      if (it != files.end()) {
        if (it->second.size() > 1) {
          throw EnvironmentError("string table name matches several files differing only by case: " +
                                 utf8(it->second.front()));
        }
        table.path = it->second.front();
      } else if (archives != nullptr) {
        const auto location = archives->locate_in_archives("strings/" + name);
        if (!location) continue;
        const auto& reader = archives->archives()[location->archive];
        auto entry_path = reader.entries()[location->entry].path;
        std::replace(entry_path.begin(), entry_path.end(), '\\', '/');
        table.path = reader.path() / entry_path;
        archived = archives->read(*location);
      } else {
        continue;
      }
      try {
        auto parsed = it != files.end() ? localization::read_string_table_file(table.path, table_kind(source))
                                        : localization::read_string_table(archived, table_kind(source));
        table.strings.reserve(parsed.entries.size());
        for (auto& entry : parsed.entries) {
          const auto* text = reinterpret_cast<const char*>(entry.text.data());
          table.strings.emplace(entry.id, std::string(text, entry.text.size()));
        }
      } catch (const localization::StringTableError& error) {
        throw EnvironmentError("malformed string table " + utf8(table.path) + ": " + error.what());
      }
      result.tables_[plugin][static_cast<std::size_t>(source)] = std::move(table);
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
