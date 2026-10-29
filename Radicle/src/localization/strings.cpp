#include "fo4/localization/strings.hpp"

#include "fo4/io/atomic_write.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fo4::localization {
namespace {

struct TableRow {
  std::uint32_t id{0};
  std::uint32_t offset{0};
};

bool length_prefixed(StringTableKind kind) noexcept {
  return kind == StringTableKind::dlstrings || kind == StringTableKind::ilstrings;
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4) {
    throw StringTableError("truncated localized string u32");
  }
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 16) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 24);
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(std::byte{static_cast<std::uint8_t>(value)});
  out.push_back(std::byte{static_cast<std::uint8_t>(value >> 8)});
  out.push_back(std::byte{static_cast<std::uint8_t>(value >> 16)});
  out.push_back(std::byte{static_cast<std::uint8_t>(value >> 24)});
}

std::vector<std::byte> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) throw StringTableError("could not open localized string file");

  const auto end = stream.tellg();
  if (end == std::streampos(-1)) {
    throw StringTableError("could not determine localized string file size");
  }
  const auto signed_size = static_cast<std::streamoff>(end);
  if (signed_size < 0) throw StringTableError("localized string file size is negative");
  const auto size = static_cast<std::uintmax_t>(signed_size);
  if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
      size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    throw StringTableError("localized string file is too large for this process");
  }

  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw StringTableError("could not read localized string file");
  }
  return bytes;
}

void validate_text_bytes(std::span<const std::byte> text) {
  if (std::find(text.begin(), text.end(), std::byte{0}) != text.end()) {
    throw StringTableError("localized string text contains an embedded NUL");
  }
}

std::vector<std::byte> encode_text(std::string_view text) {
  std::vector<std::byte> encoded;
  encoded.reserve(text.size());
  for (const char ch : text) {
    encoded.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  validate_text_bytes(encoded);
  return encoded;
}

std::vector<std::size_t> record_starts(std::span<const std::byte> data,
                                       StringTableKind kind) {
  std::vector<std::size_t> starts;
  std::size_t position = 0;
  while (position < data.size()) {
    starts.push_back(position);
    if (!length_prefixed(kind)) {
      const auto nul = std::find(data.begin() + static_cast<std::ptrdiff_t>(position),
                                 data.end(), std::byte{0});
      if (nul == data.end()) {
        throw StringTableError("unterminated .STRINGS text record");
      }
      position = static_cast<std::size_t>(std::distance(data.begin(), nul)) + 1;
      continue;
    }

    const auto length = read_u32(data, position);
    if (length == 0) {
      throw StringTableError("DL/IL string record length must include a trailing NUL");
    }
    const auto payload = position + 4;
    if (payload > data.size() || length > data.size() - payload) {
      throw StringTableError("DL/IL string record exceeds the data block");
    }
    const auto end = payload + static_cast<std::size_t>(length);
    if (data[end - 1] != std::byte{0}) {
      throw StringTableError("DL/IL string record is missing its trailing NUL");
    }
    validate_text_bytes(data.subspan(payload, static_cast<std::size_t>(length) - 1));
    position = end;
  }

  if (position != data.size()) {
    throw StringTableError("localized string data block does not end on a record boundary");
  }
  return starts;
}

std::vector<std::byte> extract_text(std::span<const std::byte> data,
                                    std::size_t offset,
                                    StringTableKind kind) {
  if (!length_prefixed(kind)) {
    const auto begin = data.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto nul = std::find(begin, data.end(), std::byte{0});
    if (nul == data.end()) throw StringTableError("unterminated .STRINGS text record");
    return {begin, nul};
  }

  const auto length = read_u32(data, offset);
  if (length == 0) throw StringTableError("DL/IL string record length is zero");
  const auto payload = offset + 4;
  return {data.begin() + static_cast<std::ptrdiff_t>(payload),
          data.begin() + static_cast<std::ptrdiff_t>(payload + length - 1)};
}

}  // namespace

LocalizedStringTable read_string_table(std::span<const std::byte> bytes,
                                       StringTableKind kind) {
  if (bytes.size() < 8) throw StringTableError("localized string file is smaller than its header");

  const auto count = read_u32(bytes, 0);
  const auto data_size = read_u32(bytes, 4);
  // Widen before comparing so the check is meaningful (and warning-clean)
  // on 64-bit hosts where the constant exceeds the u32 operand's range.
  if (static_cast<std::size_t>(count) > (std::numeric_limits<std::size_t>::max() - 8) / 8) {
    throw StringTableError("localized string table row count overflows address space");
  }
  const auto table_size = static_cast<std::size_t>(count) * 8;
  const auto data_offset = 8 + table_size;
  if (data_offset > bytes.size()) throw StringTableError("localized string offset table is truncated");
  if (static_cast<std::size_t>(data_size) != bytes.size() - data_offset) {
    throw StringTableError("localized string dataSize does not match file size");
  }

  std::vector<TableRow> rows;
  rows.reserve(count);
  std::uint32_t previous_id = 0;
  bool have_previous = false;
  for (std::size_t i = 0; i < count; ++i) {
    const auto row_offset = 8 + i * 8;
    const TableRow row{read_u32(bytes, row_offset), read_u32(bytes, row_offset + 4)};
    if (have_previous && row.id <= previous_id) {
      throw StringTableError("localized string IDs must be strictly ascending");
    }
    if (data_size == 0 || row.offset >= data_size) {
      throw StringTableError("localized string table offset is outside the data block");
    }
    rows.push_back(row);
    previous_id = row.id;
    have_previous = true;
  }

  if (count == 0) {
    if (data_size != 0) throw StringTableError("empty localized string table has non-empty data");
    return LocalizedStringTable{kind, {}};
  }

  const auto data = bytes.subspan(data_offset, data_size);
  const auto starts = record_starts(data, kind);
  for (const auto& row : rows) {
    if (!std::binary_search(starts.begin(), starts.end(), static_cast<std::size_t>(row.offset))) {
      throw StringTableError("localized string table offset does not point to a record boundary");
    }
  }

  LocalizedStringTable table;
  table.kind = kind;
  table.entries.reserve(rows.size());
  for (const auto& row : rows) {
    table.entries.push_back({row.id, extract_text(data, row.offset, kind)});
  }
  return table;
}

LocalizedStringTable read_string_table_file(const std::filesystem::path& path,
                                            StringTableKind kind) {
  const auto bytes = read_file_bytes(path);
  return read_string_table(bytes, kind);
}

std::vector<std::byte> write_string_table(const LocalizedStringTable& table) {
  if (table.entries.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw StringTableError("localized string table has too many entries");
  }

  std::vector<TableRow> rows;
  rows.reserve(table.entries.size());
  std::vector<std::byte> data;
  // Keyed by std::string rather than std::vector<std::byte>: GCC 14's
  // -Wstringop-overread mis-reports the memcmp bound inside libstdc++'s
  // vector<byte> three-way comparison at -O3 (false positive that breaks
  // -Werror builds). The bytes are opaque payload, so string is equivalent.
  std::map<std::string, std::uint32_t> deduplicated;
  std::set<std::uint32_t> ids;

  for (const auto& entry : table.entries) {
    validate_text_bytes(entry.text);
    if (!ids.insert(entry.id).second) {
      throw StringTableError("localized string table contains a duplicate string ID");
    }

    const auto text_key = [](const std::vector<std::byte>& bytes) {
      if (bytes.empty()) return std::string{};
      const auto* data = reinterpret_cast<const char*>(bytes.data());
      return std::string(data, bytes.size());
    };

    auto found = deduplicated.find(text_key(entry.text));
    std::uint32_t offset = 0;
    if (found != deduplicated.end()) {
      offset = found->second;
    } else {
      if (data.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw StringTableError("localized string data block exceeds u32 offsets");
      }
      offset = static_cast<std::uint32_t>(data.size());
      if (length_prefixed(table.kind)) {
        if (entry.text.size() >= std::numeric_limits<std::uint32_t>::max()) {
          throw StringTableError("localized string text is too large");
        }
        append_u32(data, static_cast<std::uint32_t>(entry.text.size() + 1));
      }
      data.insert(data.end(), entry.text.begin(), entry.text.end());
      data.push_back(std::byte{0});
      deduplicated.emplace(text_key(entry.text), offset);
    }
    rows.push_back({entry.id, offset});
  }

  if (data.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw StringTableError("localized string data block exceeds u32 dataSize");
  }
  std::sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.id < rhs.id;
  });

  std::vector<std::byte> out;
  const auto table_bytes = rows.size() * 8;
  out.reserve(8 + table_bytes + data.size());
  append_u32(out, static_cast<std::uint32_t>(rows.size()));
  append_u32(out, static_cast<std::uint32_t>(data.size()));
  for (const auto& row : rows) {
    append_u32(out, row.id);
    append_u32(out, row.offset);
  }
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

void write_string_table_file(const LocalizedStringTable& table,
                             const std::filesystem::path& path) {
  const auto bytes = write_string_table(table);
  io::write_file_atomic(path, bytes);
}

const LocalizedStringEntry* find_string(const LocalizedStringTable& table,
                                        std::uint32_t id) noexcept {
  const auto it = std::find_if(table.entries.begin(), table.entries.end(),
                               [id](const auto& entry) { return entry.id == id; });
  return it == table.entries.end() ? nullptr : &*it;
}

std::uint32_t next_string_id(const LocalizedStringTable& table) noexcept {
  std::uint32_t next = 1;
  for (const auto& entry : table.entries) {
    if (entry.id == std::numeric_limits<std::uint32_t>::max()) return 0;
    if (entry.id >= next) next = entry.id + 1;
  }
  return next;
}

std::uint32_t add_string(LocalizedStringTable& table, std::string_view text) {
  if (text.empty()) return 0;
  auto encoded = encode_text(text);
  const auto id = next_string_id(table);
  if (id == 0) throw StringTableError("localized string table has no free ID");
  table.entries.push_back({id, std::move(encoded)});
  return id;
}

void insert_string(LocalizedStringTable& table, std::uint32_t id, std::string_view text) {
  auto encoded = encode_text(text);
  const auto it = std::lower_bound(table.entries.begin(), table.entries.end(), id,
                                   [](const LocalizedStringEntry& entry, std::uint32_t value) {
                                     return entry.id < value;
                                   });
  if (it != table.entries.end() && it->id == id) {
    throw StringTableError("localized string table already has this string ID");
  }
  table.entries.insert(it, LocalizedStringEntry{id, std::move(encoded)});
}

bool set_string(LocalizedStringTable& table, std::uint32_t id, std::string_view text) {
  auto encoded = encode_text(text);
  const auto it = std::find_if(table.entries.begin(), table.entries.end(),
                               [id](const auto& entry) { return entry.id == id; });
  if (it == table.entries.end()) return false;
  it->text = std::move(encoded);
  return true;
}

bool remove_string(LocalizedStringTable& table, std::uint32_t id) {
  const auto it = std::find_if(table.entries.begin(), table.entries.end(),
                               [id](const auto& entry) { return entry.id == id; });
  if (it == table.entries.end()) return false;
  table.entries.erase(it);
  return true;
}

StringTableKind string_table_kind(Signature record, Signature subrecord) noexcept {
  constexpr auto k_desc = Signature::from_string("DESC");
  constexpr auto k_cnam = Signature::from_string("CNAM");
  constexpr auto k_rnam = Signature::from_string("RNAM");
  constexpr auto k_lscr = Signature::from_string("LSCR");
  constexpr auto k_qust = Signature::from_string("QUST");
  constexpr auto k_book = Signature::from_string("BOOK");
  constexpr auto k_info = Signature::from_string("INFO");

  if (record != k_lscr && subrecord == k_desc) return StringTableKind::dlstrings;
  if (record == k_qust && subrecord == k_cnam) return StringTableKind::dlstrings;
  if (record == k_book && subrecord == k_cnam) return StringTableKind::dlstrings;
  if (record == k_info && subrecord != k_rnam) return StringTableKind::ilstrings;
  return StringTableKind::strings;
}

std::string_view string_table_extension(StringTableKind kind) noexcept {
  switch (kind) {
    case StringTableKind::dlstrings:
      return ".DLSTRINGS";
    case StringTableKind::ilstrings:
      return ".ILSTRINGS";
    case StringTableKind::strings:
      return ".STRINGS";
  }
  return ".STRINGS";
}

std::filesystem::path string_table_path(std::string_view plugin_file_name,
                                        StringTableKind kind,
                                        std::string_view language) {
  const auto stem = std::filesystem::path(plugin_file_name).stem().string();
  std::string name = stem;
  name += '_';
  name += language;
  name += string_table_extension(kind);
  return std::filesystem::path("Strings") / name;
}

LocalizedStringTable& table_for(LocalizationTables& tables, StringTableKind kind) noexcept {
  switch (kind) {
    case StringTableKind::dlstrings:
      return tables.dlstrings;
    case StringTableKind::ilstrings:
      return tables.ilstrings;
    case StringTableKind::strings:
      return tables.strings;
  }
  return tables.strings;
}

const LocalizedStringTable& table_for(const LocalizationTables& tables,
                                      StringTableKind kind) noexcept {
  switch (kind) {
    case StringTableKind::dlstrings:
      return tables.dlstrings;
    case StringTableKind::ilstrings:
      return tables.ilstrings;
    case StringTableKind::strings:
      return tables.strings;
  }
  return tables.strings;
}

void write_localization_tables(const LocalizationTables& tables,
                               std::string_view plugin_file_name,
                               std::string_view language,
                               const std::filesystem::path& data_dir) {
  const auto directory = data_dir / "Strings";
  std::filesystem::create_directories(directory);
  for (const auto kind : {StringTableKind::strings, StringTableKind::dlstrings,
                          StringTableKind::ilstrings}) {
    const auto path = data_dir / string_table_path(plugin_file_name, kind, language);
    write_string_table_file(table_for(tables, kind), path);
  }
}

}  // namespace fo4::localization
