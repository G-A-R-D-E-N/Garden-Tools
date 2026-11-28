#include "fo4/nif/hknp_packfile.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::uint32_t k_magic_0 = 0x57E0E057u;
constexpr std::uint32_t k_magic_1 = 0x10C0C010u;
constexpr std::size_t k_file_header = 0x40;
constexpr std::size_t k_section_header = 0x40;
constexpr std::size_t k_section_count = 3;
constexpr std::size_t k_tail_length_at = 0x3E;
constexpr std::size_t k_align = 16;
constexpr std::size_t k_local_entry = 8;
constexpr std::size_t k_fixup_entry = 12;
constexpr std::uint8_t k_pad = 0xFF;
constexpr std::uint8_t k_class_entry_tag = 0x09;
constexpr std::uint32_t k_classes_index = 0;
constexpr std::uint32_t k_data_index = 2;

std::span<const std::byte> checked_window(std::span<const std::byte> bytes, std::size_t offset, std::size_t length) {
  if (offset > bytes.size() || length > bytes.size() - offset) {
    throw parse_error(offset, "packfile range runs past the end of the data");
  }
  return bytes.subspan(offset, length);
}

std::uint8_t u8_at(std::span<const std::byte> bytes, std::size_t offset) {
  return binary::Cursor(checked_window(bytes, offset, 1), offset).read_u8();
}

std::uint16_t u16_at(std::span<const std::byte> bytes, std::size_t offset) {
  return binary::Cursor(checked_window(bytes, offset, 2), offset).read_u16();
}

std::uint32_t u32_at(std::span<const std::byte> bytes, std::size_t offset) {
  return binary::Cursor(checked_window(bytes, offset, 4), offset).read_u32();
}

bool all_pad(std::span<const std::byte> bytes, std::size_t offset, std::size_t length) {
  for (std::size_t i = 0; i < length; ++i) {
    if (u8_at(bytes, offset + i) != k_pad) return false;
  }
  return true;
}

// Constraint: a fixup table is its real entries followed only by 0xFF bytes, so the count stops at the first all-0xFF entry.
std::size_t table_entries(std::span<const std::byte> bytes, std::size_t begin, std::size_t end, std::size_t entry) {
  std::size_t count = 0;
  while (begin + (count + 1) * entry <= end && !all_pad(bytes, begin + count * entry, entry)) ++count;
  const auto tail = begin + count * entry;
  if (!all_pad(bytes, tail, end - tail)) {
    throw parse_error(tail, "fixup table has data after its 0xFF padding");
  }
  return count;
}

struct Section {
  std::size_t start{0};
  std::size_t local{0};
  std::size_t global{0};
  std::size_t virtuals{0};
  std::size_t end{0};
};

Section read_section(std::span<const std::byte> bytes, std::size_t at, std::string_view name) {
  const auto header = checked_window(bytes, at, k_section_header);
  const std::string_view tag(reinterpret_cast<const char*>(header.data()), 0x13);
  if (tag.substr(0, tag.find('\0')) != name) {
    throw parse_error(at, "unexpected packfile section name");
  }
  Section section;
  section.start = u32_at(bytes, at + 0x14);
  section.local = section.start + u32_at(bytes, at + 0x18);
  section.global = section.start + u32_at(bytes, at + 0x1C);
  section.virtuals = section.start + u32_at(bytes, at + 0x20);
  section.end = section.start + u32_at(bytes, at + 0x24);
  return section;
}

// Invariant: the owner is the last object starting at or before offset, and offset must lie inside that object.
std::size_t owning_object(const std::vector<std::size_t>& starts, std::size_t objects_end, std::size_t offset) {
  const auto it = std::upper_bound(starts.begin(), starts.end(), offset);
  if (it == starts.begin()) {
    throw parse_error(offset, "fixup source lies before the first object");
  }
  const auto index = static_cast<std::size_t>(it - starts.begin()) - 1;
  const auto end = index + 1 < starts.size() ? starts[index + 1] : objects_end;
  if (offset >= end) {
    throw parse_error(offset, "fixup source lies outside every object");
  }
  return index;
}

void append_u32(std::vector<std::byte>& out, std::size_t value) {
  const auto v = static_cast<std::uint32_t>(value);
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::byte>((v >> shift) & 0xFFu));
  }
}

void set_u32(std::vector<std::byte>& out, std::size_t at, std::size_t value) {
  const auto v = static_cast<std::uint32_t>(value);
  for (std::size_t i = 0; i < 4; ++i) {
    out[at + i] = static_cast<std::byte>((v >> (8 * i)) & 0xFFu);
  }
}

void pad_to(std::vector<std::byte>& out, std::byte fill) {
  while (out.size() % k_align != 0) out.push_back(fill);
}

void append_section(std::vector<std::byte>& out, std::string_view name, std::size_t start, std::size_t local,
                    std::size_t global, std::size_t virtuals, std::size_t end) {
  const std::size_t at = out.size();
  out.resize(at + k_section_header, std::byte{0});
  for (std::size_t i = 0; i < name.size(); ++i) out[at + i] = static_cast<std::byte>(name[i]);
  out[at + 0x13] = std::byte{k_pad};
  set_u32(out, at + 0x14, start);
  set_u32(out, at + 0x18, local - start);
  set_u32(out, at + 0x1C, global - start);
  set_u32(out, at + 0x20, virtuals - start);
  set_u32(out, at + 0x24, end - start);
  set_u32(out, at + 0x28, end - start);
  set_u32(out, at + 0x2C, end - start);
  for (std::size_t i = 0x30; i < 0x40; ++i) out[at + i] = std::byte{k_pad};
}

}  // namespace

HknpPackfile read_hknp_packfile(std::span<const std::byte> bytes) {
  if (bytes.size() < k_file_header) {
    throw parse_error(bytes.size(), "packfile is shorter than its file header");
  }
  if (u32_at(bytes, 0) != k_magic_0 || u32_at(bytes, 4) != k_magic_1) {
    throw parse_error(0, "not a Havok binary packfile");
  }
  const std::size_t header_size = k_file_header + u16_at(bytes, k_tail_length_at);
  const std::size_t table_start = header_size + k_section_count * k_section_header;
  if (bytes.size() < table_start) {
    throw parse_error(bytes.size(), "packfile is shorter than its section table");
  }
  if (u32_at(bytes, 0x14) != k_section_count) {
    throw parse_error(0x14, "packfile must have three sections");
  }
  const auto classes = read_section(bytes, header_size, "__classnames__");
  const auto types = read_section(bytes, header_size + k_section_header, "__types__");
  const auto data = read_section(bytes, header_size + 2 * k_section_header, "__data__");
  if (classes.start != table_start || types.start != classes.end || types.end != types.start ||
      data.start != types.end) {
    throw parse_error(0x40, "unexpected packfile section layout");
  }
  if (data.end != bytes.size()) {
    throw parse_error(data.end, "packfile size does not match its data section");
  }
  if (!(data.local <= data.global && data.global <= data.virtuals && data.virtuals <= data.end)) {
    throw parse_error(0x40, "packfile data section ranges are out of order or past its end");
  }
  if ((data.local - data.start) % k_align != 0) {
    throw parse_error(data.local, "packfile objects do not end on a 16-byte boundary");
  }

  HknpPackfile pack;
  const auto tail = checked_window(bytes, k_file_header, header_size - k_file_header);
  pack.header_tail.assign(tail.begin(), tail.end());
  std::vector<std::size_t> name_offsets;
  std::size_t p = classes.start;
  while (p + 5 <= classes.end && u8_at(bytes, p + 4) == k_class_entry_tag) {
    std::size_t nul = p + 5;
    while (nul < classes.end && u8_at(bytes, nul) != 0) ++nul;
    if (nul >= classes.end) {
      throw parse_error(p, "class name is not terminated inside its section");
    }
    HknpClassName entry;
    entry.hash = u32_at(bytes, p);
    entry.name.assign(reinterpret_cast<const char*>(bytes.data() + p + 5), nul - (p + 5));
    if (std::any_of(pack.classes.begin(), pack.classes.end(),
                    [&entry](const auto& known) { return known.name == entry.name; })) {
      throw parse_error(p + 5, "class table names a class twice");
    }
    pack.classes.push_back(std::move(entry));
    name_offsets.push_back(p + 5 - classes.start);
    p = nul + 1;
  }
  if (!all_pad(bytes, p, classes.end - p)) {
    throw parse_error(p, "class name table has data after its last entry");
  }

  std::vector<std::size_t> starts;
  std::vector<std::size_t> classes_of;
  const auto virtual_count = table_entries(bytes, data.virtuals, data.end, k_fixup_entry);
  if (virtual_count == 0) {
    throw parse_error(data.virtuals, "packfile has no objects");
  }
  for (std::size_t i = 0; i < virtual_count; ++i) {
    const auto at = data.virtuals + i * k_fixup_entry;
    if (u32_at(bytes, at + 4) != k_classes_index) {
      throw parse_error(at + 4, "virtual fixup names an unexpected section");
    }
    const auto name = static_cast<std::size_t>(u32_at(bytes, at + 8));
    const auto found = std::find(name_offsets.begin(), name_offsets.end(), name);
    if (found == name_offsets.end()) {
      throw parse_error(at + 8, "object names no class in the class table");
    }
    const auto start = data.start + u32_at(bytes, at);
    if (start >= data.local) {
      throw parse_error(at, "object offset lies outside the data objects");
    }
    if (i == 0 ? start != data.start : start <= starts.back()) {
      throw parse_error(at, "objects are not in ascending order from the start of the data section");
    }
    if ((start - data.start) % k_align != 0) {
      throw parse_error(at, "object is not on a 16-byte boundary");
    }
    starts.push_back(start);
    classes_of.push_back(static_cast<std::size_t>(found - name_offsets.begin()));
  }
  if (u32_at(bytes, 0x24) != name_offsets[classes_of[0]]) {
    throw parse_error(0x24, "file header names a root that is not the first object");
  }

  for (std::size_t i = 0; i < starts.size(); ++i) {
    const auto end = i + 1 < starts.size() ? starts[i + 1] : data.local;
    const auto chunk = checked_window(bytes, starts[i], end - starts[i]);
    HknpPackObject object;
    object.class_name = pack.classes[classes_of[i]].name;
    object.bytes.assign(chunk.begin(), chunk.end());
    pack.objects.push_back(std::move(object));
  }

  const auto local_count = table_entries(bytes, data.local, data.global, k_local_entry);
  std::size_t last_owner = 0;
  for (std::size_t k = 0; k < local_count; ++k) {
    const auto at = data.local + k * k_local_entry;
    const auto source = data.start + u32_at(bytes, at);
    const auto target = data.start + u32_at(bytes, at + 4);
    const auto owner = owning_object(starts, data.local, source);
    if (owner < last_owner) {
      throw parse_error(at, "local fixups are not grouped by object");
    }
    last_owner = owner;
    const auto owner_end = owner + 1 < starts.size() ? starts[owner + 1] : data.local;
    if (target < starts[owner] || target > owner_end) {
      throw parse_error(at + 4, "local fixup target lies outside its object");
    }
    pack.objects[owner].local.push_back({static_cast<std::uint32_t>(source - starts[owner]),
                                         static_cast<std::uint32_t>(target - starts[owner])});
  }

  const auto global_count = table_entries(bytes, data.global, data.virtuals, k_fixup_entry);
  last_owner = 0;
  for (std::size_t k = 0; k < global_count; ++k) {
    const auto at = data.global + k * k_fixup_entry;
    const auto source = data.start + u32_at(bytes, at);
    if (u32_at(bytes, at + 4) != k_data_index) {
      throw parse_error(at + 4, "global fixup names an unexpected section");
    }
    const auto target = data.start + u32_at(bytes, at + 8);
    const auto owner = owning_object(starts, data.local, source);
    if (owner < last_owner) {
      throw parse_error(at, "global fixups are not grouped by object");
    }
    last_owner = owner;
    const auto found = std::lower_bound(starts.begin(), starts.end(), target);
    if (found == starts.end() || *found != target) {
      throw parse_error(at + 8, "global fixup target is not an object start");
    }
    pack.objects[owner].global.push_back({static_cast<std::uint32_t>(source - starts[owner]),
                                          static_cast<std::uint32_t>(found - starts.begin())});
  }

  pack.global_fixup_bytes = static_cast<std::uint32_t>(data.virtuals - data.global);
  return pack;
}

std::vector<std::byte> write_hknp_packfile(const HknpPackfile& pack) {
  if (pack.objects.empty()) {
    throw std::invalid_argument("a packfile needs at least a root object");
  }
  if (pack.header_tail.size() > 0xFFFFu) {
    throw std::invalid_argument("a packfile header tail must fit the u16 at 0x3E");
  }
  const auto class_index = [&pack](const std::string& name) {
    for (std::size_t i = 0; i < pack.classes.size(); ++i) {
      if (pack.classes[i].name == name) return i;
    }
    throw std::invalid_argument("object class " + name + " is not in the packfile class table");
  };

  std::vector<std::byte> names;
  std::vector<std::size_t> name_offsets;
  for (const auto& entry : pack.classes) {
    name_offsets.push_back(names.size() + 5);
    append_u32(names, entry.hash);
    names.push_back(std::byte{k_class_entry_tag});
    for (const char ch : entry.name) names.push_back(static_cast<std::byte>(ch));
    names.push_back(std::byte{0});
  }
  pad_to(names, std::byte{k_pad});

  std::vector<std::size_t> at(pack.objects.size());
  std::vector<std::byte> data;
  for (std::size_t i = 0; i < pack.objects.size(); ++i) {
    at[i] = data.size();
    data.insert(data.end(), pack.objects[i].bytes.begin(), pack.objects[i].bytes.end());
    pad_to(data, std::byte{0});
  }

  std::vector<std::byte> local;
  std::vector<std::byte> global;
  std::vector<std::byte> virtuals;
  for (std::size_t i = 0; i < pack.objects.size(); ++i) {
    const auto& object = pack.objects[i];
    for (const auto& fixup : object.local) {
      append_u32(local, at[i] + fixup.source);
      append_u32(local, at[i] + fixup.target);
    }
    for (const auto& fixup : object.global) {
      if (fixup.object >= pack.objects.size()) {
        throw std::invalid_argument("global fixup names an object outside the packfile");
      }
      append_u32(global, at[i] + fixup.source);
      append_u32(global, k_data_index);
      append_u32(global, at[fixup.object]);
    }
    append_u32(virtuals, at[i]);
    append_u32(virtuals, k_classes_index);
    append_u32(virtuals, name_offsets[class_index(object.class_name)]);
  }
  pad_to(local, std::byte{k_pad});
  pad_to(global, std::byte{k_pad});
  while (global.size() < pack.global_fixup_bytes) global.push_back(std::byte{k_pad});
  pad_to(virtuals, std::byte{k_pad});

  const std::size_t table_start = k_file_header + pack.header_tail.size() + k_section_count * k_section_header;
  const std::size_t names_end = table_start + names.size();
  const std::size_t local_abs = names_end + data.size();
  const std::size_t global_abs = local_abs + local.size();
  const std::size_t virtual_abs = global_abs + global.size();
  const std::size_t end = virtual_abs + virtuals.size();

  std::vector<std::byte> out(k_file_header, std::byte{0});
  set_u32(out, 0, k_magic_0);
  set_u32(out, 4, k_magic_1);
  set_u32(out, 0x0C, 11);
  out[0x10] = std::byte{8};
  out[0x11] = std::byte{1};
  out[0x13] = std::byte{1};
  set_u32(out, 0x14, k_section_count);
  set_u32(out, 0x18, 2);
  set_u32(out, 0x24, name_offsets[class_index(pack.objects.front().class_name)]);
  constexpr std::string_view version = "hk_2014.1.0-r1";
  for (std::size_t i = 0; i < version.size(); ++i) out[0x28 + i] = static_cast<std::byte>(version[i]);
  out[0x28 + version.size() + 1] = std::byte{k_pad};
  set_u32(out, 0x3C, 21 | (pack.header_tail.size() << 16));
  out.insert(out.end(), pack.header_tail.begin(), pack.header_tail.end());

  append_section(out, "__classnames__", table_start, names_end, names_end, names_end, names_end);
  append_section(out, "__types__", names_end, names_end, names_end, names_end, names_end);
  append_section(out, "__data__", names_end, local_abs, global_abs, virtual_abs, end);

  out.insert(out.end(), names.begin(), names.end());
  out.insert(out.end(), data.begin(), data.end());
  out.insert(out.end(), local.begin(), local.end());
  out.insert(out.end(), global.begin(), global.end());
  out.insert(out.end(), virtuals.begin(), virtuals.end());
  return out;
}

}  // namespace fo4::nif
