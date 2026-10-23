#pragma once

#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fo4::localization {

enum class StringTableKind {
  strings,
  dlstrings,
  ilstrings,
};

struct LocalizedStringEntry {
  std::uint32_t id{0};
  std::vector<std::byte> text;
};

struct LocalizedStringTable {
  StringTableKind kind{StringTableKind::strings};
  std::vector<LocalizedStringEntry> entries;
};

class StringTableError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

LocalizedStringTable read_string_table(std::span<const std::byte> bytes,
                                       StringTableKind kind);
LocalizedStringTable read_string_table_file(const std::filesystem::path& path,
                                            StringTableKind kind);

std::vector<std::byte> write_string_table(const LocalizedStringTable& table);
void write_string_table_file(const LocalizedStringTable& table,
                             const std::filesystem::path& path);

const LocalizedStringEntry* find_string(const LocalizedStringTable& table,
                                        std::uint32_t id) noexcept;

std::uint32_t next_string_id(const LocalizedStringTable& table) noexcept;
std::uint32_t add_string(LocalizedStringTable& table, std::string_view text);
void insert_string(LocalizedStringTable& table, std::uint32_t id, std::string_view text);
bool set_string(LocalizedStringTable& table, std::uint32_t id, std::string_view text);
bool remove_string(LocalizedStringTable& table, std::uint32_t id);

StringTableKind string_table_kind(Signature record, Signature subrecord) noexcept;
std::string_view string_table_extension(StringTableKind kind) noexcept;
std::filesystem::path string_table_path(std::string_view plugin_file_name,
                                        StringTableKind kind,
                                        std::string_view language);

struct LocalizationTables {
  LocalizedStringTable strings{StringTableKind::strings, {}};
  LocalizedStringTable dlstrings{StringTableKind::dlstrings, {}};
  LocalizedStringTable ilstrings{StringTableKind::ilstrings, {}};
};

LocalizedStringTable& table_for(LocalizationTables& tables, StringTableKind kind) noexcept;
const LocalizedStringTable& table_for(const LocalizationTables& tables,
                                      StringTableKind kind) noexcept;

void write_localization_tables(const LocalizationTables& tables,
                               std::string_view plugin_file_name,
                               std::string_view language,
                               const std::filesystem::path& data_dir);

}  // namespace fo4::localization
