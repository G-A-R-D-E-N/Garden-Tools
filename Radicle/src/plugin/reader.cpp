#include "fo4/plugin/reader.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/plugin/visit.hpp"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <utility>

namespace fo4 {
namespace {

std::vector<std::byte> inflate_record_payload(std::span<const std::byte> compressed,
                                              std::uint32_t uncompressed_size,
                                              std::size_t offset) {
  if (uncompressed_size == 0) {
    return {};
  }

  auto try_inflate = [&](int window_bits) -> std::optional<std::vector<std::byte>> {
    z_stream stream{};
    if (inflateInit2(&stream, window_bits) != Z_OK) {
      return std::nullopt;
    }
    std::vector<std::byte> out(uncompressed_size);
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int rc = inflate(&stream, Z_FINISH);
    const auto produced = stream.total_out;
    inflateEnd(&stream);
    if (rc != Z_STREAM_END) {
      return std::nullopt;
    }
    out.resize(produced);
    return out;
  };

  if (auto out = try_inflate(MAX_WBITS)) {
    return *out;
  }
  if (auto out = try_inflate(-MAX_WBITS)) {
    return *out;
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

Record read_record(binary::Cursor& cursor) {
  const auto start = cursor.position();
  const auto start_abs = cursor.absolute_offset();
  Record record;
  record.header = read_record_header(cursor);
  if (record.header.data_size > cursor.remaining()) {
    throw parse_error(start_abs, "record " + record.header.signature.to_string() +
                                     " overruns file");
  }
  const auto payload = cursor.read_bytes(record.header.data_size);
  record.original_bytes.assign(cursor.data().begin() + static_cast<std::ptrdiff_t>(start),
                               cursor.data().begin() + static_cast<std::ptrdiff_t>(cursor.position()));
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
  const auto compressed = payload_cursor.read_bytes(payload.size() - 4);
  record.decompressed_payload =
      inflate_record_payload(compressed, record.uncompressed_size, start_abs);
  record.subrecords =
      parse_subrecords(record.decompressed_payload, start_abs + k_record_header_size);
  return record;
}

Group read_group(binary::Cursor& cursor);

GroupChild read_child(binary::Cursor& cursor) {
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
    return read_group(cursor);
  }
  return read_record(cursor);
}

Group read_group(binary::Cursor& cursor) {
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
    group.children.push_back(read_child(cursor));
  }
  if (cursor.position() != end) {
    throw parse_error(start_abs, "GRUP children did not consume the declared size");
  }
  group.original_bytes.assign(cursor.data().begin() + static_cast<std::ptrdiff_t>(start),
                              cursor.data().begin() + static_cast<std::ptrdiff_t>(end));
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

}  // namespace

Plugin read_plugin(std::span<const std::byte> bytes, std::filesystem::path path) {
  if (bytes.size() < k_record_header_size) {
    throw parse_error(0, "file too small to contain a TES4 header");
  }
  Plugin plugin;
  plugin.path = std::move(path);
  plugin.original_bytes.assign(bytes.begin(), bytes.end());
  if (!plugin.path.empty()) {
    plugin.key = PluginKey::from_filename(plugin.path.filename().string());
  }
  binary::Cursor cursor{bytes};
  plugin.tes4 = read_record(cursor);
  if (plugin.tes4.header.signature != sig_tes4) {
    throw parse_error(0, "first record is not TES4");
  }
  plugin.header = parse_tes4_header(plugin.tes4);
  while (!cursor.at_end()) {
    plugin.groups.push_back(read_group(cursor));
  }
  assign_record_form_keys(plugin);
  return plugin;
}

Plugin read_plugin_file(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  if (!in) {
    throw parse_error(0, "unable to open " + path.string());
  }
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  std::vector<std::byte> bytes(size);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!in) {
      throw parse_error(0, "unable to read " + path.string());
    }
  }
  return read_plugin(bytes, path);
}

std::vector<const Record*> collect_records(const Plugin& plugin) {
  std::vector<const Record*> out;
  out.push_back(&plugin.tes4);
  for (const auto& group : plugin.groups) {
    collect_records_in_group(group, out);
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
  for (const auto* record : collect_records(plugin)) {
    if (record->header.form_id == form_id && record->header.signature != sig_tes4) {
      return record;
    }
  }
  return nullptr;
}

const Record* find_record(const Plugin& plugin, const FormKey& form_key) {
  for (const auto* record : collect_records(plugin)) {
    if (record->form_key == form_key && record->header.signature != sig_tes4) {
      return record;
    }
  }
  return nullptr;
}

PluginFormIdContext plugin_form_id_context(const Plugin& plugin) {
  PluginFormIdContext context;
  context.current_plugin = plugin.key;
  context.current_kind =
      plugin.header.tes4_flags.is_small_file() ? PluginKind::light : PluginKind::full;
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
    const auto kind =
        loaded.header.tes4_flags.is_small_file() ? PluginKind::light : PluginKind::full;
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
