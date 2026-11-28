#include "fo4/hkx/object_view.hpp"

#include "hkx_read_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {
namespace {

using detail::value_at;

constexpr std::size_t kRepeatShown = 16;
constexpr std::size_t kArrayShown = 8;
constexpr std::size_t kArrayCountAt = 8;
constexpr std::size_t kSummaryStrings = 3;
constexpr std::size_t kSummaryText = 60;
constexpr std::size_t kValueText = 512;
constexpr std::size_t kStructDepth = 4;
constexpr std::size_t kWorkCap = 4000;

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

enum class V : std::uint8_t {
  None,
  Bool,
  Int8,
  Int16,
  Int32,
  Int64,
  Uint8,
  Uint16,
  Uint32,
  Uint64,
  Char,
  Half,
  Real,
  Vector4,
  Quaternion,
  QsTransform,
  Transform,
  Matrix3,
  StringPtr,
  Pointer,
  Array,
  RelArray,
  Struct,
};

// Invariant: each width is stated by the layout tables, commonlibf4-nomad or the decode table's kind_size; a Struct width is its class size.
std::size_t width_of(V kind) {
  switch (kind) {
    case V::Bool:
    case V::Int8:
    case V::Uint8:
    case V::Char:
      return 1;
    case V::Int16:
    case V::Uint16:
    case V::Half:
      return 2;
    case V::Int32:
    case V::Uint32:
    case V::Real:
    case V::RelArray:
      return 4;
    case V::Int64:
    case V::Uint64:
    case V::StringPtr:
    case V::Pointer:
      return 8;
    case V::Vector4:
    case V::Quaternion:
    case V::Array:
      return 16;
    case V::Matrix3:
    case V::QsTransform:
      return 48;
    case V::Transform:
      return 64;
    case V::None:
    case V::Struct:
      return 0;
  }
  return 0;
}

V scalar_kind(std::string_view text) {
  struct Row {
    std::string_view name;
    V kind;
  };
  static constexpr Row kRows[] = {
      {"bool", V::Bool},          {"int8", V::Int8},           {"uint8", V::Uint8},
      {"int16", V::Int16},        {"uint16", V::Uint16},       {"int32", V::Int32},
      {"uint32", V::Uint32},      {"int64", V::Int64},         {"uint64", V::Uint64},
      {"real", V::Real},          {"half", V::Half},           {"char", V::Char},
      {"vector4", V::Vector4},    {"quaternion", V::Quaternion}, {"qstransform", V::QsTransform},
      {"transform", V::Transform}, {"matrix3", V::Matrix3},    {"rotation", V::Matrix3},
      {"stringptr", V::StringPtr}, {"cstring", V::StringPtr},  {"pointer", V::Pointer},
  };
  for (const Row& row : kRows) {
    if (row.name == text) return row.kind;
  }
  return V::None;
}

bool is_integer(V kind) {
  switch (kind) {
    case V::Int8:
    case V::Int16:
    case V::Int32:
    case V::Int64:
    case V::Uint8:
    case V::Uint16:
    case V::Uint32:
    case V::Uint64:
      return true;
    default:
      return false;
  }
}

bool is_group(V kind) {
  return kind == V::Vector4 || kind == V::Quaternion || kind == V::QsTransform || kind == V::Transform ||
         kind == V::Matrix3;
}

bool is_listed(V kind) { return kind == V::Bool || kind == V::Real || kind == V::Half || is_integer(kind) || is_group(kind); }

// Requires the repeat to be a trailing " xN"; any other text is returned whole with repeat 1.
std::string_view strip_repeat(std::string_view text, std::size_t& repeat) {
  repeat = 1;
  const std::size_t space = text.rfind(' ');
  if (space == std::string_view::npos || space + 2 >= text.size() || text[space + 1] != 'x') return text;
  std::size_t count = 0;
  for (const char digit : text.substr(space + 2)) {
    if (digit < '0' || digit > '9') return text;
    count = count * 10 + static_cast<std::size_t>(digit - '0');
    if (count > 4096) return text;
  }
  repeat = count;
  return text.substr(0, space);
}

struct Shape {
  V kind{V::None};
  V element{V::None};
  const HkxClassLayout* record{nullptr};
  std::size_t repeat{1};
  const HkxEnumLayout* names{nullptr};
  bool flags{false};
  std::string why;
};

Shape shape_of(const HkxMemberLayout& member) {
  Shape shape;
  const std::string_view type = member.type;
  if (type.empty()) {
    shape.why = "the layout table has no type for this member";
    return shape;
  }
  const std::string_view base = strip_repeat(type, shape.repeat);
  if (base == "array") {
    shape.kind = V::Array;
  } else if (base.starts_with("array of ") || base.starts_with("relarray of ")) {
    const bool relative = base.starts_with("relarray");
    shape.kind = relative ? V::RelArray : V::Array;
    const std::string_view inner = base.substr(base.find(" of ") + 4);
    if (inner == "struct") {
      const HkxFieldKind wanted = relative ? HkxFieldKind::RelArray : HkxFieldKind::Array;
      if (member.kind == wanted && member.element == HkxFieldKind::Struct) {
        shape.record = find_layout(member.type_class);
        if (shape.record != nullptr) shape.element = V::Struct;
      }
    } else {
      shape.element = scalar_kind(inner);
    }
    if (relative && shape.element == V::None) {
      shape.kind = V::None;
      shape.why = "relative array: the element width is not stated by the layout tables (UNVERIFIED)";
      return shape;
    }
  } else if (base.starts_with("enum of ") || base.starts_with("flags of ")) {
    const V inner = scalar_kind(base.substr(base.find(" of ") + 4));
    if (is_integer(inner)) {
      shape.kind = inner;
      shape.names = member.enumeration;
      shape.flags = base.starts_with("flags");
    }
  } else if (base == "pointer of struct") {
    shape.kind = V::Pointer;
  } else if (base == "struct") {
    if (member.kind == HkxFieldKind::Struct && shape.repeat == 1) shape.record = find_layout(member.type_class);
    if (shape.record != nullptr) shape.kind = V::Struct;
  } else {
    shape.kind = scalar_kind(base);
  }
  if (shape.kind == V::None) {
    if (base != "struct") {
      shape.why = "no established width for type " + std::string(base) + " (UNVERIFIED)";
    } else if (shape.repeat > 1) {
      shape.why = "fixed arrays of structs are not decoded";
    } else {
      shape.why = "struct member: the layout table does not name its class";
    }
  }
  return shape;
}

template <typename Fill>
std::vector<HkxObjectField> rows_of(const HkxObjectView& object, const HkxClassLayout& layout, Fill fill) {
  const std::vector<HkxMemberLayout> members = all_members(layout);
  std::vector<HkxObjectField> rows;
  for (std::size_t at = 0; at < members.size(); ++at) {
    const std::uint32_t begin = members[at].offset;
    const std::uint32_t end = at + 1 < members.size() ? members[at + 1].offset : layout.size;
    if (begin >= layout.size || end < begin) continue;
    HkxObjectField field;
    field.name = std::string(members[at].name);
    field.offset = begin;
    field.size = end - begin;
    field.hex = hex_of(object.data, begin, end - begin);
    field.type = std::string(members[at].type);
    fill(members[at], static_cast<std::size_t>(end - begin), field);
    rows.push_back(std::move(field));
  }
  return rows;
}

std::string real_text(float value) {
  char text[32];
  std::snprintf(text, sizeof text, "%.9g", static_cast<double>(value));
  return text;
}

// Requires the packfile to hold a data section; every read is bounded by the member's room, so a short object never reads past its span.
class Reader {
 public:
  Reader(const HkxPackfile& packfile, const HkxSection& data,
         const std::unordered_map<std::uint32_t, HkxRemoteFixup>& global)
      : packfile_(packfile), data_(data), global_(global) {}

  // Invariant: false leaves value empty and sets reason; true may also set reason when a part of a struct or array was not decoded.
  bool member(std::size_t base, std::size_t limit, const HkxMemberLayout& layout, std::size_t room, std::string& value,
              std::string& reason) {
    work_ = 0;
    const bool decoded = slot(base, limit, layout, room, 0, value, reason);
    const std::size_t before = value.size();
    detail::cap_text(value, kValueText);
    if (value.size() != before) value += "...";
    return decoded;
  }

 private:
  bool slot(std::size_t base, std::size_t limit, const HkxMemberLayout& layout, std::size_t room, std::size_t depth,
            std::string& value, std::string& reason) {
    if (!layout.written) {
      reason = "not stored in files";
      return false;
    }
    const Shape shape = shape_of(layout);
    if (shape.kind == V::None) {
      reason = shape.why;
      return false;
    }
    const std::size_t unit = shape.kind == V::Struct ? static_cast<std::size_t>(shape.record->size) : width_of(shape.kind);
    const std::size_t width = unit * shape.repeat;
    if (width > room) {
      reason = "member is wider than the room before the next member";
      return false;
    }
    const std::size_t at = base + layout.offset;
    if (at > limit || width > limit - at) {
      reason = "object is shorter than this member";
      return false;
    }
    switch (shape.kind) {
      case V::Array:
        return array(shape, at, depth, value, reason);
      case V::RelArray:
        return relative(shape, at, depth, value, reason);
      case V::Struct:
        return record(*shape.record, at, depth, value, reason);
      default:
        break;
    }
    if (shape.repeat > 1) return repeated(shape, at, value, reason);
    if (!scalar(shape.kind, at, value, reason)) return false;
    if (shape.names != nullptr) {
      const auto number = small_integer(shape.kind, at);
      const std::string name = number ? enum_value_name(*shape.names, shape.flags, *number) : std::string{};
      if (!name.empty()) value += " (" + name + ")";
    }
    return true;
  }

  bool spend(std::string& reason) {
    if (++work_ <= kWorkCap) return true;
    reason = "value is too large to expand";
    return false;
  }

  std::optional<std::int64_t> small_integer(V kind, std::size_t at) const {
    const std::span<const std::byte> bytes(data_.data);
    switch (kind) {
      case V::Int8: return value_at<std::int8_t>(bytes, at);
      case V::Int16: return value_at<std::int16_t>(bytes, at);
      case V::Int32: return value_at<std::int32_t>(bytes, at);
      case V::Uint8: return value_at<std::uint8_t>(bytes, at);
      case V::Uint16: return value_at<std::uint16_t>(bytes, at);
      case V::Uint32: return value_at<std::uint32_t>(bytes, at);
      default: return std::nullopt;
    }
  }

  bool scalar(V kind, std::size_t at, std::string& value, std::string& reason) {
    if (!spend(reason)) return false;
    const std::span<const std::byte> bytes(data_.data);
    switch (kind) {
      case V::Bool: {
        const auto byte = value_at<std::uint8_t>(bytes, at);
        if (byte > 1) {
          reason = "bool byte is not 0 or 1";
          return false;
        }
        value = byte == 1 ? "true" : "false";
        return true;
      }
      case V::Int8:
      case V::Char:
      case V::Int16:
      case V::Int32:
      case V::Uint8:
      case V::Uint16:
      case V::Uint32:
        value = std::to_string(*small_integer(kind == V::Char ? V::Int8 : kind, at));
        return true;
      case V::Int64:
        value = std::to_string(static_cast<long long>(value_at<std::int64_t>(bytes, at)));
        return true;
      case V::Uint64:
        value = std::to_string(static_cast<unsigned long long>(value_at<std::uint64_t>(bytes, at)));
        return true;
      case V::Half: {
        char text[16];
        std::snprintf(text, sizeof text, "half 0x%04x", static_cast<unsigned>(value_at<std::uint16_t>(bytes, at)));
        value = text;
        return true;
      }
      case V::Real:
        value = real_text(value_at<float>(bytes, at));
        return true;
      case V::Vector4:
      case V::Quaternion:
      case V::QsTransform:
      case V::Transform:
      case V::Matrix3: {
        const std::size_t count = width_of(kind) / 4;
        value = "[";
        for (std::size_t i = 0; i < count; ++i) {
          if (i != 0) value += ", ";
          value += real_text(value_at<float>(bytes, at + 4 * i));
        }
        value += "]";
        return true;
      }
      case V::StringPtr: {
        if (global_.count(static_cast<std::uint32_t>(at)) != 0) {
          reason = "string pointer uses a global fixup";
          return false;
        }
        const auto target = detail::local_target(data_, at);
        if (!target) {
          if (value_at<std::uint64_t>(bytes, at) != 0) {
            reason = "string pointer has no fixup but is not zero";
            return false;
          }
          value = "null";
          return true;
        }
        const auto text = cstring_at(bytes, *target);
        if (!text) {
          reason = "string has no terminator within the scan cap";
          return false;
        }
        value = "\"" + detail::printable_text(*text) + "\"";
        return true;
      }
      case V::Pointer: {
        if (detail::local_target(data_, at)) {
          reason = "object pointer uses a local fixup";
          return false;
        }
        const auto found = global_.find(static_cast<std::uint32_t>(at));
        if (found == global_.end()) {
          if (value_at<std::uint64_t>(bytes, at) != 0) {
            reason = "pointer has no fixup but is not zero";
            return false;
          }
          value = "null";
          return true;
        }
        const HkxRemoteFixup& fixup = found->second;
        if (fixup.section != packfile_.data_section) {
          value = "section " + std::to_string(fixup.section) + " at " + std::to_string(fixup.destination);
          return true;
        }
        const auto object = std::lower_bound(packfile_.objects.begin(), packfile_.objects.end(), fixup.destination,
                                             [](const HkxObjectView& view, std::uint32_t offset) {
                                               return view.offset < offset;
                                             });
        if (object == packfile_.objects.end() || object->offset != fixup.destination) {
          value = "offset " + std::to_string(fixup.destination);
          return true;
        }
        value = "object " + std::to_string(object - packfile_.objects.begin()) + " (" +
                detail::printable_text(object->class_name) + ")";
        return true;
      }
      default:
        break;
    }
    reason = "no established width for this member type";
    return false;
  }

  bool repeated(const Shape& shape, std::size_t at, std::string& value, std::string& reason) {
    if (shape.kind == V::Char) {
      std::size_t length = 0;
      while (length < shape.repeat && value_at<std::uint8_t>(std::span<const std::byte>(data_.data), at + length) != 0) {
        ++length;
      }
      const auto* chars = reinterpret_cast<const char*>(data_.data.data() + at);
      value = "\"" + detail::printable_text(std::string_view(chars, length)) + "\"";
      return true;
    }
    if (!is_listed(shape.kind)) {
      reason = "repeated string and pointer members are not decoded";
      return false;
    }
    const std::size_t stride = width_of(shape.kind);
    value = "[";
    for (std::size_t i = 0; i < shape.repeat && i < kRepeatShown; ++i) {
      std::string item;
      if (!scalar(shape.kind, at + i * stride, item, reason)) return false;
      value += (i == 0 ? "" : ", ") + item;
    }
    value += shape.repeat > kRepeatShown ? ", ...]" : "]";
    return true;
  }

  bool record(const HkxClassLayout& layout, std::size_t at, std::size_t depth, std::string& value, std::string& reason) {
    if (depth >= kStructDepth) {
      reason = "struct nesting is deeper than " + std::to_string(kStructDepth);
      return false;
    }
    const std::vector<HkxMemberLayout> members = all_members(layout);
    const std::size_t limit = at + layout.size;
    value = "{";
    bool first = true;
    for (std::size_t index = 0; index < members.size(); ++index) {
      const HkxMemberLayout& child = members[index];
      if (!child.written) continue;
      const std::uint32_t next = index + 1 < members.size() ? members[index + 1].offset : layout.size;
      if (child.offset >= layout.size || next < child.offset) continue;
      std::string text;
      std::string why;
      const bool decoded = spend(why) && slot(at, limit, child, next - child.offset, depth + 1, text, why);
      value += (first ? "" : ", ") + std::string(child.name) + "=" + (decoded ? text : "?");
      first = false;
      if (!why.empty() && reason.empty()) reason = std::string(child.name) + ": " + why;
    }
    value += "}";
    return true;
  }

  bool array(const Shape& shape, std::size_t at, std::size_t depth, std::string& value, std::string& reason) {
    const auto stored = value_at<std::int32_t>(std::span<const std::byte>(data_.data), at + kArrayCountAt);
    if (stored < 0) {
      reason = "array count is negative";
      return false;
    }
    const auto count = static_cast<std::size_t>(stored);
    value = std::to_string(count) + (count == 1 ? " item" : " items");
    if (count == 0) return true;
    const auto first = detail::local_target(data_, at);
    if (!first) {
      reason = "array has no fixup";
      return false;
    }
    return elements(shape, *first, count, "array", depth, value, reason);
  }

  bool relative(const Shape& shape, std::size_t at, std::size_t depth, std::string& value, std::string& reason) {
    const std::span<const std::byte> bytes(data_.data);
    const std::size_t count = value_at<std::uint16_t>(bytes, at);
    const std::size_t offset = value_at<std::uint16_t>(bytes, at + 2);
    value = std::to_string(count) + (count == 1 ? " item" : " items");
    if (count == 0) return true;
    if (offset == 0) {
      reason = "relative array offset is zero";
      return false;
    }
    return elements(shape, at + offset, count, "relative array", depth, value, reason);
  }

  bool elements(const Shape& shape, std::size_t first, std::size_t count, const char* what, std::size_t depth,
                std::string& value, std::string& reason) {
    const std::size_t stride = shape.element == V::Struct ? static_cast<std::size_t>(shape.record->size) : width_of(shape.element);
    if (stride == 0) return true;
    if (static_cast<std::uint64_t>(first) + static_cast<std::uint64_t>(count) * stride > data_.data.size()) {
      reason = std::string(what) + " runs past the data section";
      return false;
    }
    if (shape.element != V::Struct && !is_listed(shape.element)) return true;
    std::string items;
    for (std::size_t i = 0; i < count && i < kArrayShown; ++i) {
      std::string item;
      if (shape.element == V::Struct) {
        std::string why;
        if (!record(*shape.record, first + i * stride, depth + 1, item, why)) {
          reason = why;
          return false;
        }
        if (!why.empty() && reason.empty()) reason = why;
      } else if (!scalar(shape.element, first + i * stride, item, reason)) {
        return false;
      }
      items += (i == 0 ? "" : ", ") + item;
    }
    value += " [" + items + (count > kArrayShown ? ", ..." : "") + "]";
    return true;
  }

  const HkxPackfile& packfile_;
  const HkxSection& data_;
  const std::unordered_map<std::uint32_t, HkxRemoteFixup>& global_;
  std::size_t work_{0};
};

}  // namespace

HkxFieldDecoder::HkxFieldDecoder(const HkxPackfile& packfile)
    : packfile_(packfile), data_(packfile.sections[packfile.data_section]) {
  for (const HkxRemoteFixup& fixup : data_.global_fixups) global_.emplace(fixup.source, fixup);
}

std::vector<HkxObjectField> HkxFieldDecoder::fields(const HkxObjectView& object, const HkxClassLayout& layout) const {
  Reader reader(packfile_, data_, global_);
  const std::size_t limit = static_cast<std::size_t>(object.offset) + object.data.size();
  return rows_of(object, layout, [&](const HkxMemberLayout& entry, std::size_t room, HkxObjectField& field) {
    if (!reader.member(object.offset, limit, entry, room, field.value, field.reason)) field.value.clear();
  });
}

std::vector<HkxObjectField> HkxFieldDecoder::labels(const HkxObjectView& object, const HkxClassLayout& layout) {
  return rows_of(object, layout, [](const HkxMemberLayout&, std::size_t, HkxObjectField&) {});
}

std::string HkxFieldDecoder::summary(const HkxObjectView& object, const HkxClassLayout& layout) const {
  Reader reader(packfile_, data_, global_);
  const std::size_t limit = static_cast<std::size_t>(object.offset) + object.data.size();
  const std::vector<HkxMemberLayout> members = all_members(layout);
  std::string text;
  std::size_t shown = 0;
  for (std::size_t at = 0; at < members.size() && shown < kSummaryStrings; ++at) {
    const HkxMemberLayout& entry = members[at];
    if (!entry.written || (entry.type != "stringptr" && entry.type != "cstring")) continue;
    const std::uint32_t next = at + 1 < members.size() ? members[at + 1].offset : layout.size;
    const std::size_t room = next > entry.offset ? next - entry.offset : std::size_t{0};
    std::string value;
    std::string reason;
    if (!reader.member(object.offset, limit, entry, room, value, reason) || value == "null") continue;
    if (value.size() > kSummaryText + 2) {
      detail::cap_text(value, kSummaryText);
      value += "...\"";
    }
    if (!text.empty()) text.push_back(' ');
    text += std::string(entry.name) + "=" + value;
    ++shown;
  }
  return text;
}

HkxStatus inspect_objects(const HkxPackfile& packfile, const HkxObjectQuery& query, HkxObjectReport& out) {
  out = HkxObjectReport{};
  if (packfile.sections.empty() || packfile.data_section >= packfile.sections.size()) {
    return HkxStatus::MissingSection;
  }
  const HkxSection& data = packfile.sections[packfile.data_section];
  std::optional<HkxFieldDecoder> decoder;
  if (query.decode_values) decoder.emplace(packfile);
  out.object_count = packfile.objects.size();

  std::vector<std::string> unknown;
  for (const HkxObjectView& object : packfile.objects) {
    const std::string shown = detail::printable_text(object.class_name);
    if (std::find(out.classes.begin(), out.classes.end(), shown) == out.classes.end()) out.classes.push_back(shown);
  }

  const std::size_t limit = query.limit == 0 ? packfile.objects.size() : query.limit;
  for (std::size_t index = 0; index < packfile.objects.size(); ++index) {
    const HkxObjectView& object = packfile.objects[index];
    if (query.object_index && *query.object_index != index) continue;
    if (!query.class_prefix.empty() && object.class_name.rfind(query.class_prefix, 0) != 0) continue;
    const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
    if (query.known_layout_only && layout == nullptr) continue;
    ++out.matched;
    if (out.objects.size() >= limit) continue;

    HkxObjectDetail detail;
    detail.index = index;
    detail.offset = object.offset;
    detail.signature = object.signature;
    detail.class_name = fo4::hkx::detail::printable_text(object.class_name);
    detail.size = object.data.size();
    detail.layout_known = layout != nullptr;
    if (layout == nullptr) {
      if (std::find(unknown.begin(), unknown.end(), object.class_name) == unknown.end()) {
        unknown.push_back(std::string(object.class_name));
      }
    } else {
      detail.layout_size = layout->size;
      detail.fields = decoder ? decoder->fields(object, *layout) : HkxFieldDecoder::labels(object, *layout);
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
        reference.target_class = fo4::hkx::detail::printable_text(packfile.objects[static_cast<std::size_t>(reference.target_object)].class_name);
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
          reference.target_class = fo4::hkx::detail::printable_text(packfile.objects[static_cast<std::size_t>(reference.target_object)].class_name);
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
    out.diagnostics.push_back("no generated layout for " + fo4::hkx::detail::printable_text(name) +
                              (sample == nullptr ? std::string{} : " (signature " + hex_signature(sample->signature) + ")"));
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
