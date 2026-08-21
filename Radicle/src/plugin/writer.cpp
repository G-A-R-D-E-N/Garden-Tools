#include "fo4/plugin/writer.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_links.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>

namespace fo4 {
namespace {

class ByteWriter {
public:
  std::vector<std::byte> bytes;

  void u8(std::uint8_t value) { bytes.push_back(std::byte{value}); }

  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8));
  }

  void u32(std::uint32_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8));
    u8(static_cast<std::uint8_t>(value >> 16));
    u8(static_cast<std::uint8_t>(value >> 24));
  }

  void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }

  void sig(Signature signature) {
    for (char ch : signature.bytes()) {
      u8(static_cast<std::uint8_t>(ch));
    }
  }

  void raw(std::span<const std::byte> data) {
    bytes.insert(bytes.end(), data.begin(), data.end());
  }

  void patch_u32(std::size_t offset, std::uint32_t value) {
    bytes[offset] = std::byte{static_cast<std::uint8_t>(value)};
    bytes[offset + 1] = std::byte{static_cast<std::uint8_t>(value >> 8)};
    bytes[offset + 2] = std::byte{static_cast<std::uint8_t>(value >> 16)};
    bytes[offset + 3] = std::byte{static_cast<std::uint8_t>(value >> 24)};
  }
};

std::vector<std::byte> zlib_compress(std::span<const std::byte> data) {
  if (data.empty()) {
    return {};
  }
  uLongf bound = compressBound(static_cast<uLong>(data.size()));
  std::vector<std::byte> out(bound);
  const int rc = compress(reinterpret_cast<Bytef*>(out.data()), &bound,
                          reinterpret_cast<const Bytef*>(data.data()),
                          static_cast<uLong>(data.size()));
  if (rc != Z_OK) {
    throw std::runtime_error("zlib compress failed");
  }
  out.resize(bound);
  return out;
}

void write_subrecords(ByteWriter& out, const std::vector<Subrecord>& subrecords) {
  for (const auto& sub : subrecords) {
    if (sub.signature == sig_xxxx) {
      continue;
    }
    if (sub.extended_size || sub.data.size() > 0xFFFF) {
      out.sig(sig_xxxx);
      out.u16(4);
      out.u32(static_cast<std::uint32_t>(sub.data.size()));
      out.sig(sub.signature);
      out.u16(0);
      out.raw(sub.data);
      continue;
    }
    out.sig(sub.signature);
    out.u16(static_cast<std::uint16_t>(sub.data.size()));
    out.raw(sub.data);
  }
}

void write_record(ByteWriter& out, const Record& record);
void write_group(ByteWriter& out, const Group& group);

void write_record(ByteWriter& out, const Record& record) {
  if (!record.original_bytes.empty()) {
    out.raw(record.original_bytes);
    return;
  }

  const auto header_offset = out.bytes.size();
  out.sig(record.header.signature);
  out.u32(0);
  out.u32(record.header.flags.value());
  out.u32(record.header.form_id.value());
  out.u16(record.header.timestamp);
  out.u16(record.header.version_control);
  out.u16(record.header.form_version);
  out.u16(record.header.vc_version);

  if (record.compressed) {
    ByteWriter payload;
    write_subrecords(payload, record.subrecords);
    const auto compressed = zlib_compress(payload.bytes);
    out.u32(static_cast<std::uint32_t>(payload.bytes.size()));
    out.raw(compressed);
  } else {
    write_subrecords(out, record.subrecords);
  }

  const auto payload_size =
      static_cast<std::uint32_t>(out.bytes.size() - (header_offset + k_record_header_size));
  out.patch_u32(header_offset + 4, payload_size);
}

void write_group(ByteWriter& out, const Group& group) {
  if (!group.original_bytes.empty()) {
    out.raw(group.original_bytes);
    return;
  }

  const auto header_offset = out.bytes.size();
  out.sig(sig_grup);
  out.u32(0);
  out.raw(group.header.label);
  out.i32(group.header.group_type);
  out.u16(group.header.timestamp);
  out.u16(group.header.version_control);
  out.u16(group.header.form_version);
  out.u16(group.header.unknown);

  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      write_record(out, *record);
    } else {
      write_group(out, std::get<Group>(child));
    }
  }

  const auto size = static_cast<std::uint32_t>(out.bytes.size() - header_offset);
  out.patch_u32(header_offset + 4, size);
}

std::uint64_t counted_group_records(const Group& group) {
  // xEdit TwbGroupRecord.GetCountedRecordCount is 1 + its recursive children.
  std::uint64_t count = 1;
  for (const auto& child : group.children) {
    if (std::holds_alternative<Record>(child)) {
      ++count;
    } else {
      const auto nested = counted_group_records(std::get<Group>(child));
      if (nested > std::numeric_limits<std::uint64_t>::max() - count) {
        throw std::runtime_error("plugin record count overflow");
      }
      count += nested;
    }
  }
  return count;
}

std::uint32_t rebuilt_hedr_record_count(const Plugin& plugin) {
  // xEdit writes Pred(root.GetCountedRecordCount): each major record counts 1,
  // every GRUP counts 1 plus its children, and Pred removes TES4 itself.
  std::uint64_t count = 0;
  for (const auto& group : plugin.groups) {
    const auto group_count = counted_group_records(group);
    if (group_count > std::numeric_limits<std::uint64_t>::max() - count) {
      throw std::runtime_error("plugin record count overflow");
    }
    count += group_count;
  }
  if (count > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("plugin record count exceeds HEDR u32 storage");
  }
  return static_cast<std::uint32_t>(count);
}

void patch_hedr_record_count(Record& tes4, std::uint32_t count) {
  Subrecord* hedr = nullptr;
  for (auto& sub : tes4.subrecords) {
    if (sub.signature != sig_hedr) continue;
    if (hedr != nullptr) {
      throw std::runtime_error("TES4 contains duplicate HEDR subrecords");
    }
    hedr = &sub;
  }
  if (hedr == nullptr || hedr->data.size() != 12) {
    throw std::runtime_error("TES4 HEDR must be exactly 12 bytes on rebuild");
  }

  const auto current = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hedr->data[4])) |
                       (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hedr->data[5])) << 8) |
                       (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hedr->data[6])) << 16) |
                       (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hedr->data[7])) << 24);
  if (current == count) return;

  hedr->data[4] = std::byte{static_cast<std::uint8_t>(count)};
  hedr->data[5] = std::byte{static_cast<std::uint8_t>(count >> 8)};
  hedr->data[6] = std::byte{static_cast<std::uint8_t>(count >> 16)};
  hedr->data[7] = std::byte{static_cast<std::uint8_t>(count >> 24)};
  tes4.original_bytes.clear();
}

Subrecord zstring_subrecord(Signature signature, std::string_view text) {
  Subrecord sub;
  sub.signature = signature;
  sub.data.resize(text.size() + 1);
  for (std::size_t i = 0; i < text.size(); ++i) {
    sub.data[i] = std::byte{static_cast<std::uint8_t>(text[i])};
  }
  sub.data.back() = std::byte{0};
  return sub;
}

Subrecord data_size_subrecord(std::uint64_t size) {
  Subrecord sub;
  sub.signature = sig_data;
  sub.data.resize(8);
  std::memcpy(sub.data.data(), &size, 8);
  return sub;
}

void replace_master_subrecords(Record& tes4, const std::vector<MasterReference>& masters) {
  std::vector<Subrecord> next;
  next.reserve(tes4.subrecords.size() + masters.size() * 2);
  bool masters_written = false;
  auto write_masters = [&] {
    for (const auto& master : masters) {
      next.push_back(zstring_subrecord(sig_mast, master.key.filename()));
      next.push_back(data_size_subrecord(master.data_size));
    }
    masters_written = true;
  };

  for (const auto& sub : tes4.subrecords) {
    if (sub.signature == sig_mast || sub.signature == sig_data) {
      continue;
    }
    if (!masters_written &&
        (sub.signature == sig_onam || sub.signature == sig_intv || sub.signature == sig_incc)) {
      write_masters();
    }
    next.push_back(sub);
  }
  if (!masters_written) {
    write_masters();
  }
  tes4.subrecords = std::move(next);
  tes4.original_bytes.clear();
}

void clear_original_bytes(Plugin& plugin) {
  plugin.original_bytes.clear();
  plugin.tes4.original_bytes.clear();
  visit_records(plugin, [](Record& record) { record.original_bytes.clear(); });
  visit_groups(plugin, [](Group& group) { group.original_bytes.clear(); });
}

}  // namespace

std::vector<std::byte> write_plugin(const Plugin& plugin) {
  if (!plugin.original_bytes.empty()) {
    return plugin.original_bytes;
  }

  auto tes4 = plugin.tes4;
  patch_hedr_record_count(tes4, rebuilt_hedr_record_count(plugin));

  ByteWriter out;
  write_record(out, tes4);
  for (const auto& group : plugin.groups) {
    write_group(out, group);
  }
  return out.bytes;
}

void write_plugin_file(const Plugin& plugin, const std::filesystem::path& path) {
  const auto bytes = write_plugin(plugin);
  std::ofstream out{path, std::ios::binary};
  if (!out) {
    throw std::runtime_error("unable to write " + path.string());
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    throw std::runtime_error("unable to write " + path.string());
  }
}

Plugin remap_plugin_masters(Plugin plugin, std::vector<MasterDescriptor> new_masters) {
  assign_record_form_keys(plugin);
  const auto old_context = plugin_form_id_context(plugin);
  const auto old_masters = plugin.header.masters;

  plugin.header.masters.clear();
  plugin.header.masters.reserve(new_masters.size());
  for (auto& desc : new_masters) {
    MasterReference master;
    master.key = std::move(desc.key);
    master.kind = desc.kind;
    const auto old = std::find_if(old_masters.begin(), old_masters.end(), [&](const auto& item) {
      return item.key == master.key;
    });
    if (old != old_masters.end()) {
      master.data_size = old->data_size;
      master.data_bytes = old->data_bytes;
    }
    plugin.header.masters.push_back(std::move(master));
  }

  replace_master_subrecords(plugin.tes4, plugin.header.masters);
  const auto new_context = plugin_form_id_context(plugin);

  visit_records(plugin, [&](Record& record) {
    if (record.header.signature == sig_tes4) {
      return;
    }
    if (record.form_key.owner().empty() && !record.header.form_id.is_null()) {
      record.form_key = resolve_plugin_form_id(record.header.form_id, old_context);
    }
    record.header.form_id = encode_plugin_form_id(record.form_key, new_context);
  });

  remap_form_links(plugin, old_context, new_context);
  const auto kinds = [&] {
    std::vector<PluginKind> stored;
    stored.reserve(plugin.header.masters.size());
    for (const auto& master : plugin.header.masters) {
      stored.push_back(master.kind);
    }
    return stored;
  }();
  plugin.header = parse_tes4_header(plugin.tes4);
  for (std::size_t i = 0; i < plugin.header.masters.size() && i < kinds.size(); ++i) {
    plugin.header.masters[i].kind = kinds[i];
  }
  clear_original_bytes(plugin);
  return plugin;
}

Plugin remap_plugin_masters(Plugin plugin, std::vector<PluginKey> new_masters) {
  std::vector<MasterDescriptor> descriptors;
  descriptors.reserve(new_masters.size());
  for (auto& key : new_masters) {
    const auto kind = key.looks_light() ? PluginKind::light : PluginKind::full;
    descriptors.push_back(MasterDescriptor{std::move(key), kind});
  }
  return remap_plugin_masters(std::move(plugin), std::move(descriptors));
}

}  // namespace fo4
