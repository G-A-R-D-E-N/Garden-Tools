#include "fo4/plugin/reader.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/binary/zlib_bound.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/plugin/visit.hpp"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace fo4 {
namespace {

constexpr std::uint64_t k_max_record_uncompressed_bytes = std::uint64_t{1} << 30;
constexpr std::size_t k_max_group_depth = 16;

std::vector<std::byte> inflate_record_payload(std::span<const std::byte> compressed,
                                              std::uint32_t uncompressed_size,
                                              std::size_t offset) {
  if (uncompressed_size == 0) {
    return {};
  }
  if (uncompressed_size > static_cast<std::uint64_t>(compressed.size()) * binary::k_zlib_max_expansion +
                              binary::k_zlib_slack_bytes) {
    throw parse_error(offset, "compressed record uncompressed size exceeds zlib expansion bound");
  }
  if (uncompressed_size > k_max_record_uncompressed_bytes) {
    throw parse_error(offset, "compressed record uncompressed size exceeds the " +
                                  std::to_string(k_max_record_uncompressed_bytes) +
                                  " byte limit");
  }

  std::vector<std::byte> out(uncompressed_size);
  auto try_inflate = [&](int window_bits) -> std::optional<std::size_t> {
    z_stream stream{};
    if (inflateInit2(&stream, window_bits) != Z_OK) {
      return std::nullopt;
    }
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int rc = inflate(&stream, Z_FINISH);
    const auto produced = static_cast<std::size_t>(stream.total_out);
    inflateEnd(&stream);
    if (rc != Z_STREAM_END) {
      return std::nullopt;
    }
    return produced;
  };

  if (auto produced = try_inflate(MAX_WBITS)) {
    out.resize(*produced);
    return out;
  }
  throw parse_error(offset, "zlib inflate failed for compressed record");
}

RecordHeader read_record_header(binary::Cursor& cursor) {
  RecordHeader header;
  header.signature = cursor.read_signature();
  header.data_size = cursor.read_u32();
  header.flags = RecordFlags{cursor.read_u32()};
  header.form_id = RawFormId{cursor.read_u32()};
  header.timestamp = cursor.read_u16();
  header.version_control = cursor.read_u16();
  header.form_version = cursor.read_u16();
  header.vc_version = cursor.read_u16();
  return header;
}

GroupHeader read_group_header(binary::Cursor& cursor) {
  GroupHeader header;
  header.signature = cursor.read_signature();
  header.group_size = cursor.read_u32();
  const auto label = cursor.read_bytes(4);
  std::copy(label.begin(), label.end(), header.label.begin());
  header.group_type = cursor.read_i32();
  header.timestamp = cursor.read_u16();
  header.version_control = cursor.read_u16();
  header.form_version = cursor.read_u16();
  header.unknown = cursor.read_u16();
  return header;
}

std::vector<Subrecord> parse_subrecords(std::span<const std::byte> payload,
                                        std::size_t base_offset) {
  std::vector<Subrecord> subrecords;
  binary::Cursor cursor{payload, base_offset};
  std::optional<std::uint32_t> extended_size;
  while (!cursor.at_end()) {
    if (cursor.remaining() < k_subrecord_header_size) {
      throw parse_error(cursor.absolute_offset(), "truncated subrecord header");
    }
    Subrecord sub;
    sub.signature = cursor.read_signature();
    const auto stored_size = cursor.read_u16();
    std::uint32_t size = stored_size;
    if (extended_size) {
      size = *extended_size;
      sub.extended_size = true;
      extended_size.reset();
    }
    if (size > cursor.remaining()) {
      throw parse_error(cursor.absolute_offset(),
                        "subrecord " + sub.signature.to_string() + " overruns payload");
    }
    sub.data = cursor.read_vector(size);
    if (sub.signature == sig_xxxx) {
      if (sub.data.size() != 4) {
        throw parse_error(cursor.absolute_offset() - sub.data.size(),
                          "XXXX subrecord must contain a uint32 size");
      }
      binary::Cursor size_cursor{sub.data, cursor.absolute_offset() - 4};
      extended_size = size_cursor.read_u32();
    }
    subrecords.push_back(std::move(sub));
  }
  if (extended_size) {
    throw parse_error(base_offset + payload.size(), "XXXX without a following subrecord");
  }
  return subrecords;
}

Record read_record(binary::Cursor& cursor, const std::vector<std::byte>* owner) {
  const auto start = cursor.position();
  const auto start_abs = cursor.absolute_offset();
  Record record;
  record.header = read_record_header(cursor);
  if (record.header.data_size > cursor.remaining()) {
    throw parse_error(start_abs, "record " + record.header.signature.to_string() +
                                     " overruns file");
  }
  const auto payload = cursor.read_bytes(record.header.data_size);
  record.original_range = OriginalByteRange{owner, start, cursor.position() - start};
  record.compressed = record.header.flags.is_compressed();
  if (!record.compressed) {
    record.subrecords = parse_subrecords(payload, start_abs + k_record_header_size);
    return record;
  }
  if (payload.size() < 4) {
    throw parse_error(start_abs, "compressed record is missing uncompressed size");
  }
  binary::Cursor payload_cursor{payload, start_abs + k_record_header_size};
  record.uncompressed_size = payload_cursor.read_u32();
  try {
    const auto compressed = payload_cursor.read_bytes(payload.size() - 4);
    const auto inflated = inflate_record_payload(compressed, record.uncompressed_size, start_abs);
    record.subrecords = parse_subrecords(inflated, start_abs + k_record_header_size);
  } catch (const parse_error&) {
    if (record.header.signature == sig_tes4) {
      throw;
    }
    record.undecodable = true;
  }
  return record;
}

Group read_group(binary::Cursor& cursor, const std::vector<std::byte>* owner,
                 std::size_t depth = 1);

GroupChild read_child(binary::Cursor& cursor, const std::vector<std::byte>* owner,
                      std::size_t depth) {
  if (cursor.remaining() < 4) {
    throw parse_error(cursor.absolute_offset(), "truncated group child");
  }
  const auto sig = Signature{
      static_cast<char>(std::to_integer<std::uint8_t>(cursor.peek(4)[0])),
      static_cast<char>(std::to_integer<std::uint8_t>(cursor.peek(4)[1])),
      static_cast<char>(std::to_integer<std::uint8_t>(cursor.peek(4)[2])),
      static_cast<char>(std::to_integer<std::uint8_t>(cursor.peek(4)[3])),
  };
  if (sig == sig_grup) {
    return read_group(cursor, owner, depth + 1);
  }
  return read_record(cursor, owner);
}

Group read_group(binary::Cursor& cursor, const std::vector<std::byte>* owner,
                 std::size_t depth) {
  if (depth > k_max_group_depth) {
    throw parse_error(cursor.absolute_offset(), "GRUP nesting exceeds depth limit");
  }
  const auto start = cursor.position();
  const auto start_abs = cursor.absolute_offset();
  Group group;
  group.header = read_group_header(cursor);
  if (group.header.signature != sig_grup) {
    throw parse_error(start_abs, "expected GRUP");
  }
  if (group.header.group_size < k_group_header_size) {
    throw parse_error(start_abs, "GRUP size smaller than header");
  }
  const auto end = start + group.header.group_size;
  if (end > cursor.size()) {
    throw parse_error(start_abs, "GRUP overruns file");
  }
  while (cursor.position() < end) {
    group.children.push_back(read_child(cursor, owner, depth));
  }
  if (cursor.position() != end) {
    throw parse_error(start_abs, "GRUP children did not consume the declared size");
  }
  return group;
}

void collect_records_in_group(const Group& group, std::vector<const Record*>& out) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      out.push_back(record);
    } else {
      collect_records_in_group(std::get<Group>(child), out);
    }
  }
}

void collect_groups_in_group(const Group& group, std::vector<const Group*>& out) {
  out.push_back(&group);
  for (const auto& child : group.children) {
    if (const auto* nested = std::get_if<Group>(&child)) {
      collect_groups_in_group(*nested, out);
    }
  }
}

template <class Match>
const Record* find_first_in_group(const Group& group, const Match& match) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (match(*record)) {
        return record;
      }
    } else if (const auto* found = find_first_in_group(std::get<Group>(child), match)) {
      return found;
    }
  }
  return nullptr;
}

template <class Match>
const Record* find_first_record(const Plugin& plugin, const Match& match) {
  if (match(plugin.tes4)) {
    return &plugin.tes4;
  }
  for (const auto& group : plugin.groups) {
    if (const auto* found = find_first_in_group(group, match)) {
      return found;
    }
  }
  return nullptr;
}

Plugin read_plugin_from_owned_bytes(
    std::shared_ptr<const std::vector<std::byte>> original_data,
    std::filesystem::path path) {
  const auto& bytes = *original_data;
  if (bytes.size() < k_record_header_size) {
    throw parse_error(0, "file too small to contain a TES4 header");
  }
  Plugin plugin;
  plugin.path = std::move(path);
  plugin.original_data = std::move(original_data);
  plugin.original_range = OriginalByteRange{&bytes, 0, bytes.size()};
  if (!plugin.path.empty()) {
    plugin.key = PluginKey::from_filename(io::utf8(plugin.path.filename()));
  }
  binary::Cursor cursor{bytes};
  plugin.tes4 = read_record(cursor, &bytes);
  if (plugin.tes4.header.signature != sig_tes4) {
    throw parse_error(0, "first record is not TES4");
  }
  plugin.header = parse_tes4_header(plugin.tes4);
  while (!cursor.at_end()) {
    plugin.groups.push_back(read_group(cursor, &bytes));
  }
  assign_record_form_keys(plugin);
  const auto group_count = collect_groups(plugin).size();
  const auto record_count = collect_records(plugin).size();
  plugin.original_structure_fingerprint =
      (static_cast<std::uint64_t>(group_count) << 32) |
      static_cast<std::uint64_t>(record_count);
  return plugin;
}

std::span<const std::byte> original_bytes_for_range(
    const Plugin& plugin, OriginalByteRange range) {
  if (range.empty()) {
    return {};
  }
  if (!plugin.original_data) {
    return {};
  }
  if (range.owner != plugin.original_data.get()) {
    throw std::logic_error("original byte range belongs to a different plugin backing store");
  }
  const auto& original_data = *plugin.original_data;
  if (range.offset > original_data.size() ||
      range.size > original_data.size() - range.offset) {
    throw std::out_of_range("original byte range exceeds plugin backing storage");
  }
  return std::span<const std::byte>{original_data}.subspan(range.offset, range.size);
}

}  // namespace

Plugin read_plugin_file(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  if (!in) {
    throw parse_error(0, "unable to open " + io::utf8(path));
  }
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  auto bytes = std::make_shared<std::vector<std::byte>>(size);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size));
    if (!in) {
      throw parse_error(0, "unable to read " + io::utf8(path));
    }
  }
  return read_plugin_from_owned_bytes(std::move(bytes), path);
}

Plugin read_plugin(std::span<const std::byte> bytes, std::filesystem::path path) {
  auto owned = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
  return read_plugin_from_owned_bytes(std::move(owned), std::move(path));
}

std::span<const std::byte> plugin_original_bytes(const Plugin& plugin) {
  if (!plugin.original_data) {
    return {};
  }
  return original_bytes_for_range(plugin, plugin.original_range);
}

std::span<const std::byte> record_original_bytes(const Plugin& plugin,
                                                 const Record& record) {
  if (!plugin.original_data) {
    return {};
  }
  return original_bytes_for_range(plugin, record.original_range);
}

std::vector<const Record*> collect_records(const Plugin& plugin) {
  std::vector<const Record*> out;
  out.push_back(&plugin.tes4);
  for (const auto& group : plugin.groups) {
    collect_records_in_group(group, out);
  }
  return out;
}

std::vector<const Record*> collect_first_records(const Plugin& plugin) {
  std::vector<const Record*> out;
  std::set<FormKey> seen;
  for (const auto* record : collect_records(plugin)) {
    if (record->header.signature == sig_tes4 || seen.insert(record->form_key).second) {
      out.push_back(record);
    }
  }
  return out;
}

std::vector<const Group*> collect_groups(const Plugin& plugin) {
  std::vector<const Group*> out;
  for (const auto& group : plugin.groups) {
    collect_groups_in_group(group, out);
  }
  return out;
}

const Record* find_record(const Plugin& plugin, RawFormId form_id) {
  return find_first_record(plugin, [form_id](const Record& record) {
    return record.header.form_id == form_id && record.header.signature != sig_tes4;
  });
}

const Record* find_record(const Plugin& plugin, const FormKey& form_key) {
  return find_first_record(plugin, [&form_key](const Record& record) {
    return record.form_key.local() == form_key.local() && record.form_key == form_key &&
           record.header.signature != sig_tes4;
  });
}

RecordIndex::RecordIndex(const Plugin& plugin) : plugin_(&plugin) {
  visit_records(plugin, [this](const Record& record) {
    if (record.header.signature != sig_tes4) {
      records_.try_emplace(record.form_key, &record);
    }
  });
}

const Record* RecordIndex::find(const FormKey& form_key) const {
  const auto found = records_.find(form_key);
  return found == records_.end() ? nullptr : found->second;
}

std::size_t RecordIndex::FormKeyHash::operator()(const FormKey& form_key) const noexcept {
  const auto owner = hash_value(form_key.owner());
  return owner ^ (static_cast<std::size_t>(form_key.local().value()) * 0x9E3779B97F4A7C15ull);
}

PluginFormIdContext plugin_form_id_context(const Plugin& plugin) {
  PluginFormIdContext context;
  context.current_plugin = plugin.key;
  context.current_kind = classify_plugin_kind(plugin.header.tes4_flags, plugin.key);
  context.masters.reserve(plugin.header.masters.size());
  for (const auto& master : plugin.header.masters) {
    context.masters.push_back(MasterDescriptor{master.key, master.kind});
  }
  return context;
}

void apply_master_kinds(Plugin& plugin, std::span<const MasterDescriptor> known) {
  for (auto& master : plugin.header.masters) {
    for (const auto& desc : known) {
      if (master.key == desc.key) {
        master.kind = desc.kind;
        break;
      }
    }
  }
}

void stamp_master_kinds(Plugin& plugin, std::span<const Plugin> loaded_masters) {
  std::vector<MasterDescriptor> known;
  known.reserve(loaded_masters.size());
  for (const auto& loaded : loaded_masters) {
    const auto kind = classify_plugin_kind(loaded.header.tes4_flags, loaded.key);
    known.push_back(MasterDescriptor{loaded.key, kind});
  }
  apply_master_kinds(plugin, known);
}

void assign_record_form_keys(Plugin& plugin) {
  const auto context = plugin_form_id_context(plugin);
  visit_records(plugin, [&](Record& record) {
    if (record.header.signature == sig_tes4) {
      return;
    }
    if (!record.form_key.owner().empty()) {
      return;
    }
    if (record.header.form_id.is_null()) {
      return;
    }
    try {
      record.form_key = resolve_plugin_form_id(record.header.form_id, context);
    } catch (const FormIdError&) {
    }
  });
}

}  // namespace fo4
