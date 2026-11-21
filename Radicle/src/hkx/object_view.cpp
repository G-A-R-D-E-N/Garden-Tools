#include "fo4/hkx/object_view.hpp"

#include "fo4/hkx/layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {
namespace {

std::string hex_of(std::span<const std::byte> bytes, std::uint32_t from, std::uint32_t count) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text;
  // Constraint: a truncated dump stops at the limit instead of copying the whole member.
  const std::size_t limit = std::min<std::size_t>(count, kObjectHexLimit);
  for (std::size_t at = 0; at < limit; ++at) {
    const std::size_t offset = from + at;
    if (offset >= bytes.size()) break;
    const auto byte = std::to_integer<unsigned char>(bytes[offset]);
    text.push_back(kDigits[byte >> 4]);
    text.push_back(kDigits[byte & 0x0F]);
    if ((at & 1) == 1 && at + 1 < limit) text.push_back(' ');
  }
  return text;
}

// Requires the object list to be ordered by offset, which read_packfile guarantees for the data section.
std::int32_t object_at(const std::vector<HkxObjectView>& objects, std::uint32_t offset) {
  const auto after = std::upper_bound(objects.begin(), objects.end(), offset,
                                      [](std::uint32_t value, const HkxObjectView& object) {
                                        return value < object.offset;
                                      });
  if (after == objects.begin()) return -1;
  const auto found = after - 1;
  if (static_cast<std::uint64_t>(offset) >=
      static_cast<std::uint64_t>(found->offset) + static_cast<std::uint64_t>(found->data.size())) {
    return -1;
  }
  return static_cast<std::int32_t>(found - objects.begin());
}

std::vector<HkxObjectField> fields_of(const HkxObjectView& object, const HkxClassLayout& layout) {
  std::vector<HkxObjectField> fields;
  for (std::size_t at = 0; at < layout.members.size(); ++at) {
    const std::uint32_t begin = layout.members[at].offset;
    const std::uint32_t end = at + 1 < layout.members.size() ? layout.members[at + 1].offset : layout.size;
    if (begin >= layout.size || end < begin) continue;
    fields.push_back({std::string(layout.members[at].name), begin, end - begin,
                      hex_of(object.data, begin, end - begin)});
  }
  return fields;
}

std::string member_of(const std::vector<HkxObjectField>& fields, std::uint32_t relative) {
  for (const HkxObjectField& field : fields) {
    if (relative >= field.offset && relative - field.offset < field.size) return field.name;
  }
  return {};
}

std::string hex_signature(std::uint32_t signature) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text{"0x"};
  for (int shift = 28; shift >= 0; shift -= 4) text.push_back(kDigits[(signature >> shift) & 0xFu]);
  return text;
}

}  // namespace

HkxStatus inspect_objects(const HkxPackfile& packfile, const HkxObjectQuery& query, HkxObjectReport& out) {
  out = HkxObjectReport{};
  if (packfile.sections.empty() || packfile.data_section >= packfile.sections.size()) {
    return HkxStatus::MissingSection;
  }
  const HkxSection& data = packfile.sections[packfile.data_section];
  out.object_count = packfile.objects.size();

  std::vector<std::string> unknown;
  for (const HkxObjectView& object : packfile.objects) {
    if (std::find(out.classes.begin(), out.classes.end(), object.class_name) == out.classes.end()) {
      out.classes.push_back(std::string(object.class_name));
    }
  }

  const std::size_t limit = query.limit == 0 ? packfile.objects.size() : query.limit;
  for (std::size_t index = 0; index < packfile.objects.size(); ++index) {
    const HkxObjectView& object = packfile.objects[index];
    if (!query.class_prefix.empty() && object.class_name.rfind(query.class_prefix, 0) != 0) continue;
    const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
    if (query.known_layout_only && layout == nullptr) continue;
    ++out.matched;
    if (out.objects.size() >= limit) continue;

    HkxObjectDetail detail;
    detail.index = index;
    detail.offset = object.offset;
    detail.signature = object.signature;
    detail.class_name = object.class_name;
    detail.size = object.data.size();
    detail.layout_known = layout != nullptr;
    if (layout == nullptr) {
      if (std::find(unknown.begin(), unknown.end(), object.class_name) == unknown.end()) {
        unknown.push_back(std::string(object.class_name));
      }
    } else {
      detail.layout_size = layout->size;
      detail.fields = fields_of(object, *layout);
    }

    const std::uint64_t end = static_cast<std::uint64_t>(object.offset) + object.data.size();
    for (const HkxLocalFixup& fixup : data.local_fixups) {
      if (fixup.source < object.offset || fixup.source >= end) continue;
      HkxObjectReference reference;
      reference.source = fixup.source;
      reference.member = member_of(detail.fields, fixup.source - object.offset);
      reference.target_section = static_cast<std::uint32_t>(packfile.data_section);
      reference.target_offset = fixup.destination;
      reference.target_object = object_at(packfile.objects, fixup.destination);
      if (reference.target_object >= 0) {
        reference.target_class = packfile.objects[static_cast<std::size_t>(reference.target_object)].class_name;
      }
      detail.references.push_back(std::move(reference));
    }
    for (const HkxRemoteFixup& fixup : data.global_fixups) {
      if (fixup.source < object.offset || fixup.source >= end) continue;
      HkxObjectReference reference;
      reference.source = fixup.source;
      reference.member = member_of(detail.fields, fixup.source - object.offset);
      reference.target_section = fixup.section;
      reference.target_offset = fixup.destination;
      if (fixup.section == packfile.data_section) {
        reference.target_object = object_at(packfile.objects, fixup.destination);
        if (reference.target_object >= 0) {
          reference.target_class = packfile.objects[static_cast<std::size_t>(reference.target_object)].class_name;
        }
      }
      detail.references.push_back(std::move(reference));
    }
    out.objects.push_back(std::move(detail));
  }

  for (const std::string& name : unknown) {
    const HkxObjectView* sample = nullptr;
    for (const HkxObjectView& object : packfile.objects) {
      if (object.class_name == name) {
        sample = &object;
        break;
      }
    }
    out.diagnostics.push_back("no generated layout for " + name +
                              (sample == nullptr ? std::string{} : " (signature " + hex_signature(sample->signature) + ")"));
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
