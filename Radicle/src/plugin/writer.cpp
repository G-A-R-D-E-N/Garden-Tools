#include "fo4/plugin/writer.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/io/atomic_write.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/onam.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/text/encoding.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

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

void write_record(ByteWriter& out, const Plugin& plugin, const Record& record);
void write_group(ByteWriter& out, const Plugin& plugin, const Group& group);

void write_record(ByteWriter& out, const Plugin& plugin, const Record& record) {
  if (const auto original = record_original_bytes(plugin, record); !original.empty()) {
    out.raw(original);
    return;
  }
  if (record.undecodable) {
    throw std::invalid_argument("undecodable record " + record.header.signature.to_string() + " " +
                                record.form_key.to_string() +
                                " has no original bytes; it cannot be written");
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

void write_group(ByteWriter& out, const Plugin& plugin, const Group& group) {
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
      write_record(out, plugin, *record);
    } else {
      write_group(out, plugin, std::get<Group>(child));
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
  if (hedr == nullptr || hedr->data.size() < 12) {
    throw std::runtime_error("TES4 HEDR must be at least 12 bytes on rebuild");
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
  tes4.original_range.clear();
}

std::vector<std::byte> patched_original_hedr(std::span<const std::byte> original,
                                             std::uint32_t count) {
  binary::Cursor cursor{original};
  cursor.seek(k_record_header_size);
  std::optional<std::uint32_t> extended;
  while (!cursor.at_end()) {
    const auto signature = cursor.read_signature();
    const auto stored = cursor.read_u16();
    const auto size = extended.value_or(stored);
    extended.reset();
    const auto offset = cursor.position();
    const auto data = cursor.read_bytes(size);
    if (signature == sig_hedr) {
      if (size < 12) throw std::runtime_error("TES4 HEDR must be at least 12 bytes on rebuild");
      std::vector<std::byte> result{original.begin(), original.end()};
      for (unsigned i = 0; i != 4; ++i) {
        result[offset + 4 + i] = std::byte{static_cast<std::uint8_t>(count >> (8 * i))};
      }
      return result;
    }
    if (signature == sig_xxxx) {
      binary::Cursor size_cursor{data};
      extended = size_cursor.read_u32();
    }
  }
  throw std::runtime_error("TES4 HEDR must be at least 12 bytes on rebuild");
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
      const auto name = text::utf8_to_windows1252(master.key.filename());
      if (!name) throw std::runtime_error("master name is not representable in Windows-1252: " + std::string(master.key.filename()));
      next.push_back(zstring_subrecord(sig_mast, *name));
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
  tes4.original_range.clear();
}

void remap_group_form_id_labels(Plugin& plugin,
                                const PluginFormIdContext& old_context,
                                const PluginFormIdContext& new_context) {
  visit_groups(plugin, [&](Group& group) {
    if (!group_label_is_form_id(group.header.group_type)) return;
    const RawFormId raw{group.header.label_as_u32()};
    if (raw.is_null()) {
      throw FormIdError("FormID-bearing GRUP has a null label during master remap");
    }
    const auto key = resolve_plugin_form_id(raw, old_context);
    group.header.set_label_as_u32(encode_plugin_form_id(key, new_context).value());
  });
}

void clear_original_bytes(Plugin& plugin) {
  plugin.original_range.clear();
  visit_records(plugin, [](Record& record) { record.original_range.clear(); });
  plugin.original_data.reset();
}

std::string lower_extension(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

bool extension_flags_missing(const Plugin& plugin, const std::filesystem::path& path) {
  const auto extension = lower_extension(path);
  const auto& flags = plugin.tes4.header.flags;
  if (extension == ".esl") return !flags.is_small_file() || !flags.is_master();
  if (extension == ".esm") return !flags.is_master();
  return false;
}

}  // namespace

void repair_hedr_record_count(Plugin& plugin) {
  const auto count = rebuilt_hedr_record_count(plugin);
  const auto original = record_original_bytes(plugin, plugin.tes4);
  auto checked = plugin.tes4;
  patch_hedr_record_count(checked, count);
  std::vector<std::byte> repaired_bytes;
  if (!plugin.tes4.compressed && !original.empty()) {
    repaired_bytes = patched_original_hedr(original, count);
  }
  const auto original_range = plugin.tes4.original_range;
  patch_hedr_record_count(plugin.tes4, count);
  if (!original_range.empty() && plugin.tes4.original_range.empty()) {
    plugin.tes4_after_hedr_repair = plugin.tes4;
    plugin.tes4_repaired_bytes = std::move(repaired_bytes);
  }
  if (plugin.header.record_count != count || plugin.tes4.original_range.empty()) {
    plugin.original_range.clear();
  }
  plugin.header.record_count = count;
}

std::vector<std::byte> write_plugin(const Plugin& plugin) {
  if (const auto original = plugin_original_bytes(plugin); !original.empty()) {
    return {original.begin(), original.end()};
  }

  // Clean plugins return the original source range above. Once a master plugin is
  // dirty, rebuild ONAM from the current hierarchy before serialization so
  // stale temporary-override metadata cannot survive a mutation.
  auto prepared = plugin;
  if (is_master_plugin(prepared.header.tes4_flags, prepared.key)) {
    rebuild_tes4_onam(prepared, true);
  }

  auto tes4 = prepared.tes4;
  const auto groups = collect_groups(prepared).size();
  const auto records = collect_records(prepared).size();
  const auto fingerprint = (static_cast<std::uint64_t>(groups) << 32) |
                           static_cast<std::uint64_t>(records);
  const bool structure_changed = prepared.original_structure_fingerprint == 0 ||
                                 fingerprint != prepared.original_structure_fingerprint;
  if (structure_changed) {
    patch_hedr_record_count(tes4, rebuilt_hedr_record_count(prepared));
  }

  ByteWriter out;
  if (!structure_changed && !prepared.tes4_repaired_bytes.empty() &&
      tes4_matches_hedr_repair(prepared)) {
    out.raw(prepared.tes4_repaired_bytes);
  } else {
    write_record(out, prepared, tes4);
  }
  for (const auto& group : prepared.groups) {
    write_group(out, prepared, group);
  }
  return out.bytes;
}

std::vector<std::byte> write_plugin(const Plugin& plugin, const std::filesystem::path& path) {
  return extension_flags_missing(plugin, path) ? write_plugin(apply_extension_flags(plugin, path))
                                               : write_plugin(plugin);
}

Plugin apply_extension_flags(Plugin plugin, const std::filesystem::path& path) {
  if (!extension_flags_missing(plugin, path)) return plugin;
  const bool light = lower_extension(path) == ".esl";

  if (light && !plugin.tes4.header.flags.is_small_file()) {
    plugin = set_plugin_light_flag(std::move(plugin), true);
  }
  if (plugin.tes4.header.flags.is_master()) return plugin;

  plugin.tes4.header.flags = RecordFlags{plugin.tes4.header.flags.value() | RecordFlags::master};
  plugin.header.tes4_flags = RecordFlags{plugin.header.tes4_flags.value() | RecordFlags::master};
  plugin.original_range.clear();
  plugin.tes4.original_range.clear();
  return plugin;
}

void write_plugin_file(const Plugin& plugin, const std::filesystem::path& path) {
  io::write_file_atomic(path, write_plugin(plugin, path));
}

Plugin remap_plugin_masters(Plugin plugin, std::vector<MasterDescriptor> new_masters,
                            FormLinkTypeLookup type_lookup) {
  if (new_masters.size() > k_max_plugin_masters) {
    throw FormIdError("a plugin can carry at most " + std::to_string(k_max_plugin_masters) +
                      " masters, " + std::to_string(new_masters.size()) + " were requested");
  }
  assign_record_form_keys(plugin);
  std::vector<const Record*> undecodable;
  for (const auto* record : collect_records(plugin)) {
    if (record->undecodable) undecodable.push_back(record);
  }
  if (!undecodable.empty()) {
    throw std::invalid_argument("cannot remap masters: " + std::to_string(undecodable.size()) +
                                " undecodable record(s), first is " +
                                undecodable.front()->form_key.to_string());
  }
  if (classify_plugin_kind(plugin.header.tes4_flags, plugin.key) == PluginKind::light &&
      new_masters.empty() && allows_hardcoded_range(plugin)) {
    std::size_t stranded = 0;
    for (const auto* record : collect_records(plugin)) {
      if (record->header.signature == sig_tes4 || record->form_key.owner() != plugin.key) continue;
      if (record->form_key.local().value() < k_first_full_local_id) ++stranded;
    }
    if (stranded > 0) {
      throw FormIdError("removing the last master from light plugin " +
                        std::string(plugin.key.filename()) + " would invalidate " +
                        std::to_string(stranded) + " own record(s) below 0x800; renumber them first");
    }
  }
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

  remap_group_form_id_labels(plugin, old_context, new_context);
  if (!type_lookup) type_lookup = plugin_type_lookup(plugin);
  remap_form_links(plugin, old_context, new_context, type_lookup);
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

Plugin sort_plugin_masters(Plugin plugin, std::span<const PluginKey> load_order,
                           FormLinkTypeLookup type_lookup) {
  std::vector<std::pair<std::size_t, MasterDescriptor>> ranked;
  ranked.reserve(plugin.header.masters.size());
  for (const auto& master : plugin.header.masters) {
    const auto it = std::find(load_order.begin(), load_order.end(), master.key);
    if (it == load_order.end()) {
      throw FormIdError("master " + std::string(master.key.filename()) + " is not in the load order");
    }
    ranked.push_back({static_cast<std::size_t>(it - load_order.begin()), {master.key, master.kind}});
  }
  if (std::is_sorted(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; })) {
    return plugin;
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<MasterDescriptor> masters;
  masters.reserve(ranked.size());
  for (auto& [rank, master] : ranked) masters.push_back(std::move(master));
  return remap_plugin_masters(std::move(plugin), std::move(masters), std::move(type_lookup));
}

Plugin make_empty_plugin(std::string_view filename) {
  std::string lower(filename);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto ends_with = [&](std::string_view suffix) {
    return lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  std::uint32_t bits = 0;
  if (ends_with(".esm")) bits |= RecordFlags::master;
  if (ends_with(".esl")) bits |= RecordFlags::master | RecordFlags::small_file;
  const RecordFlags flags{bits};

  Plugin plugin;
  plugin.key = PluginKey::from_filename(filename);
  plugin.header.version = 1.0f;
  plugin.header.next_form_id = 0x800u;
  plugin.header.tes4_flags = flags;
  plugin.header.form_version = 131;
  plugin.tes4.header.signature = sig_tes4;
  plugin.tes4.header.flags = flags;
  plugin.tes4.header.form_id = RawFormId{0};
  plugin.tes4.header.form_version = 131;

  Subrecord hedr;
  hedr.signature = sig_hedr;
  hedr.data.resize(12);
  const float version = 1.0f;
  const std::uint32_t record_count = 0;
  const std::uint32_t next_form_id = 0x800u;
  std::memcpy(hedr.data.data(), &version, 4);
  std::memcpy(hedr.data.data() + 4, &record_count, 4);
  std::memcpy(hedr.data.data() + 8, &next_form_id, 4);
  plugin.tes4.subrecords.push_back(std::move(hedr));
  return plugin;
}

}  // namespace fo4
