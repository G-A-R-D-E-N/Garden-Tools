#include "fo4/hkx/decode.hpp"

#include "hkx_read_internal.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using K = HkxMemberKind;
using detail::in_section;
using detail::local_target;
using detail::value_at;

constexpr std::size_t kArrayCountAt = 8;
constexpr int kMaxDepth = 8;

struct Slot {
  K kind{K::None};
  K element{K::None};
  std::string_view struct_class;
};

Slot slot_of(const HkxMemberType& member) { return {member.kind, member.element, member.struct_class}; }

std::optional<std::size_t> slot_size(const Slot& slot) {
  if (slot.kind == K::Struct) {
    const HkxDecodeClass* entry = find_decode_class(slot.struct_class);
    if (entry == nullptr) return std::nullopt;
    return entry->size;
  }
  const std::uint32_t size = kind_size(slot.kind);
  if (size == 0) return std::nullopt;
  return size;
}

std::size_t real_count(K kind) {
  switch (kind) {
    case K::Vector4:
    case K::Quaternion:
      return 4;
    case K::QsTransform:
      return 12;
    case K::Transform:
      return 16;
    default:
      return 0;
  }
}

bool is_integer(K kind) {
  switch (kind) {
    case K::Int8:
    case K::Int16:
    case K::Int32:
    case K::Int64:
    case K::Uint8:
    case K::Uint16:
    case K::Uint32:
    case K::Uint64:
      return true;
    default:
      return false;
  }
}

bool fits(K kind, std::int64_t value) {
  switch (kind) {
    case K::Int8:
      return value >= -128 && value <= 127;
    case K::Uint8:
      return value >= 0 && value <= 255;
    case K::Int16:
      return value >= -32768 && value <= 32767;
    case K::Uint16:
      return value >= 0 && value <= 65535;
    case K::Int32:
      return value >= INT32_MIN && value <= INT32_MAX;
    case K::Uint32:
      return value >= 0 && value <= static_cast<std::int64_t>(UINT32_MAX);
    default:
      return true;
  }
}

std::uint64_t double_bits(double value) { return std::bit_cast<std::uint64_t>(value); }

bool same(const HkxValue& a, const HkxValue& b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case HkxValue::Kind::Null:
      return true;
    case HkxValue::Kind::Bool:
      return a.boolean == b.boolean;
    case HkxValue::Kind::Integer:
      return a.integer == b.integer;
    case HkxValue::Kind::Real:
      return double_bits(a.real) == double_bits(b.real);
    case HkxValue::Kind::String:
      return a.text == b.text;
    case HkxValue::Kind::Reference:
      return a.target == b.target;
    case HkxValue::Kind::Array:
    case HkxValue::Kind::Struct:
      if (a.names != b.names || a.items.size() != b.items.size()) return false;
      for (std::size_t i = 0; i < a.items.size(); ++i) {
        if (!same(a.items[i], b.items[i])) return false;
      }
      return true;
  }
  return false;
}

bool same_document(const HkxSemanticDocument& a, const HkxSemanticDocument& b) {
  if (a.root != b.root || a.objects.size() != b.objects.size()) return false;
  for (const auto& pair : a.objects) {
    const auto other = b.objects.find(pair.first);
    if (other == b.objects.end() || other->second.type_name != pair.second.type_name ||
        other->second.members.size() != pair.second.members.size()) {
      return false;
    }
    for (const auto& member : pair.second.members) {
      const auto match = other->second.members.find(member.first);
      if (match == other->second.members.end() || !same(member.second, match->second)) return false;
    }
  }
  return true;
}

std::string join(const std::string& path, std::string_view name) {
  return path.empty() ? std::string(name) : path + "." + std::string(name);
}

std::string index_path(const std::string& path, std::size_t index) { return path + "[" + std::to_string(index) + "]"; }

class Codec {
 public:
  Codec(const HkxPackfile& packfile, std::vector<std::byte>* out)
      : packfile_(packfile), data_(packfile.sections[packfile.data_section]), out_(out) {
    for (const HkxRemoteFixup& fixup : data_.global_fixups) global_.emplace(fixup.source, fixup);
    for (const HkxObjectView& object : packfile.objects) starts_.insert(object.offset);
  }

  const HkxBlock& block() const { return block_; }

  bool decode_all(const HkxDecodeOptions& options, HkxSemanticDocument& document,
                  std::vector<std::string>& opaque) {
    for (const HkxObjectView& object : packfile_.objects) {
      block_ = HkxBlock{object.offset, std::string(object.class_name), {}, {}};
      if (find_decode_class(object.class_name) == nullptr) {
        if (!options.opaque_unlisted) return fail({}, "no verified member table for this class");
        if (std::find(opaque.begin(), opaque.end(), object.class_name) == opaque.end()) {
          opaque.emplace_back(object.class_name);
        }
        continue;
      }
      HkxSemanticObject decoded;
      if (!decode_object(object, decoded)) return false;
      if (!document.root && object.class_name == "hkRootLevelContainer") document.root = decoded.id;
      document.objects.emplace(decoded.id, std::move(decoded));
    }
    return true;
  }

  bool patch_all(const HkxSemanticDocument& baseline, const HkxSemanticDocument& edited) {
    block_ = HkxBlock{};
    if (baseline.root != edited.root) return fail({}, "root object changed");
    for (const auto& [id, object] : baseline.objects) {
      block_ = HkxBlock{id, object.type_name, {}, {}};
      if (edited.objects.find(id) == edited.objects.end()) return fail({}, "object is missing from the edited document");
    }
    for (const auto& [id, object] : edited.objects) {
      block_ = HkxBlock{id, object.type_name, {}, {}};
      if (baseline.objects.find(id) == baseline.objects.end()) return fail({}, "object is not in the source file");
    }
    for (const auto& [id, object] : baseline.objects) {
      const HkxSemanticObject& want = edited.objects.at(id);
      block_ = HkxBlock{id, object.type_name, {}, {}};
      if (want.type_name != object.type_name) return fail({}, "type name changed");
      const HkxDecodeClass* entry = find_decode_class(object.type_name);
      if (entry == nullptr) return fail({}, "no verified member table for this class");
      for (const auto& pair : want.members) {
        const std::string& extra = pair.first;
        const bool known = std::any_of(entry->members.begin(), entry->members.end(),
                                       [&extra](const HkxMemberType& member) { return member.name == extra; });
        if (!known) return fail(extra, "member is not in the verified table");
      }
      for (const HkxMemberType& member : entry->members) {
        const std::string name(member.name);
        const auto before = object.members.find(name);
        const auto after = want.members.find(name);
        if (before == object.members.end() || after == want.members.end()) {
          return fail(name, "member is missing from the edited document");
        }
        if (!patch(slot_of(member), static_cast<std::size_t>(id) + member.offset, name, before->second, after->second)) {
          return false;
        }
      }
    }
    return true;
  }

 private:
  bool fail(const std::string& path, std::string reason) {
    block_.member = path;
    block_.reason = std::move(reason);
    return false;
  }

  bool decode_object(const HkxObjectView& object, HkxSemanticObject& out) {
    const HkxDecodeClass* entry = find_decode_class(object.class_name);
    if (object.signature != entry->signature) return fail({}, "signature differs from the verified table");
    if (object.data.size() < entry->size) return fail({}, "object is shorter than its class");
    if (!claim(object.offset, entry->size, {})) return false;
    out.id = object.offset;
    out.type_name = std::string(entry->name);
    for (const HkxMemberType& member : entry->members) {
      HkxValue value;
      if (!decode(slot_of(member), static_cast<std::size_t>(object.offset) + member.offset, std::string(member.name), value,
                  0)) {
        return false;
      }
      out.members.emplace(std::string(member.name), std::move(value));
    }
    return true;
  }

  // Invariant: claimed ranges never overlap, so every stored value is decoded from bytes no other array or object owns.
  bool claim(std::size_t begin, std::size_t size, const std::string& path) {
    const std::size_t end = begin + size;
    auto next = claimed_.lower_bound(begin);
    if (next != claimed_.end() && next->first < end) return fail(path, "range overlaps another array or object");
    if (next != claimed_.begin() && std::prev(next)->second > begin) {
      return fail(path, "range overlaps another array or object");
    }
    claimed_.emplace(begin, end);
    return true;
  }

  bool decode_real(std::size_t at, const std::string& path, HkxValue& out) {
    const auto bits = value_at<std::uint32_t>(data_.data, at);
    const double widened = static_cast<double>(std::bit_cast<float>(bits));
    if (std::bit_cast<std::uint32_t>(static_cast<float>(widened)) != bits) {
      return fail(path, "float does not survive a round trip through double");
    }
    out.kind = HkxValue::Kind::Real;
    out.real = widened;
    return true;
  }

  static void set_integer(HkxValue& out, std::int64_t value) {
    out.kind = HkxValue::Kind::Integer;
    out.integer = value;
  }

  bool decode(const Slot& slot, std::size_t at, const std::string& path, HkxValue& out, int depth) {
    out = HkxValue{};
    const auto size = slot_size(slot);
    if (!size) return fail(path, "member has no verified size");
    if (!in_section(data_, at, *size)) return fail(path, "member runs past the data section");
    if (++leaves_ > data_.data.size()) return fail(path, "decoded values outnumber the bytes of the data section");
    if (depth > kMaxDepth) return fail(path, "members nest too deeply");
    switch (slot.kind) {
      case K::Bool: {
        const auto byte = value_at<std::uint8_t>(data_.data, at);
        if (byte > 1) return fail(path, "bool byte is not 0 or 1");
        out.kind = HkxValue::Kind::Bool;
        out.boolean = byte == 1;
        return true;
      }
      case K::Int8:
        set_integer(out, static_cast<std::int8_t>(value_at<std::uint8_t>(data_.data, at)));
        return true;
      case K::Int16:
        set_integer(out, value_at<std::int16_t>(data_.data, at));
        return true;
      case K::Int32:
        set_integer(out, value_at<std::int32_t>(data_.data, at));
        return true;
      case K::Int64:
        set_integer(out, value_at<std::int64_t>(data_.data, at));
        return true;
      case K::Uint8:
        set_integer(out, value_at<std::uint8_t>(data_.data, at));
        return true;
      case K::Uint16:
        set_integer(out, value_at<std::uint16_t>(data_.data, at));
        return true;
      case K::Uint32:
        set_integer(out, value_at<std::uint32_t>(data_.data, at));
        return true;
      case K::Uint64:
        set_integer(out, static_cast<std::int64_t>(value_at<std::uint64_t>(data_.data, at)));
        return true;
      case K::Real:
        return decode_real(at, path, out);
      case K::Vector4:
      case K::Quaternion:
      case K::QsTransform:
      case K::Transform: {
        out.kind = HkxValue::Kind::Array;
        out.items.resize(real_count(slot.kind));
        for (std::size_t i = 0; i < out.items.size(); ++i) {
          if (!decode_real(at + 4 * i, index_path(path, i), out.items[i])) return false;
        }
        return true;
      }
      case K::StringPtr:
        return decode_string(at, path, out);
      case K::Pointer:
        return decode_pointer(at, path, out);
      case K::Array:
        return decode_array(slot, at, path, out, depth);
      case K::Struct: {
        const HkxDecodeClass* entry = find_decode_class(slot.struct_class);
        out.kind = HkxValue::Kind::Struct;
        for (const HkxMemberType& member : entry->members) {
          HkxValue item;
          if (!decode(slot_of(member), at + member.offset, join(path, member.name), item, depth + 1)) return false;
          out.names.emplace_back(member.name);
          out.items.push_back(std::move(item));
        }
        return true;
      }
      case K::None:
        break;
    }
    return fail(path, "member has no verified kind");
  }

  bool decode_string(std::size_t at, const std::string& path, HkxValue& out) {
    if (global_.count(static_cast<std::uint32_t>(at)) != 0) return fail(path, "string pointer uses a global fixup");
    const auto target = local_target(data_, at);
    if (!target) {
      if (value_at<std::uint64_t>(data_.data, at) != 0) return fail(path, "pointer has no fixup but is not zero");
      return true;
    }
    const auto text = cstring_at(data_.data, *target);
    if (!text) return fail(path, "string has no terminator within the scan cap");
    out.kind = HkxValue::Kind::String;
    out.text.assign(text->begin(), text->end());
    return true;
  }

  bool decode_pointer(std::size_t at, const std::string& path, HkxValue& out) {
    if (local_target(data_, at)) return fail(path, "object pointer uses a local fixup");
    const auto found = global_.find(static_cast<std::uint32_t>(at));
    if (found == global_.end()) {
      if (value_at<std::uint64_t>(data_.data, at) != 0) return fail(path, "pointer has no fixup but is not zero");
      return true;
    }
    if (found->second.section != packfile_.data_section) return fail(path, "pointer targets another section");
    if (starts_.count(found->second.destination) == 0) return fail(path, "pointer does not land on the start of an object");
    out.kind = HkxValue::Kind::Reference;
    out.target = found->second.destination;
    return true;
  }

  bool array_elements(const Slot& slot, std::size_t at, const std::string& path, Slot& element,
                      std::size_t& stride, std::size_t& count, std::size_t& first) {
    if (slot.element == K::Array || slot.element == K::None) return fail(path, "array element kind is not supported");
    element = Slot{slot.element, K::None, slot.struct_class};
    const auto size = slot_size(element);
    if (!size) return fail(path, "array element has no verified size");
    stride = *size;
    const auto stored = value_at<std::int32_t>(data_.data, at + kArrayCountAt);
    if (stored < 0) return fail(path, "array count is negative");
    count = static_cast<std::size_t>(stored);
    first = 0;
    if (count == 0) return true;
    const auto target = local_target(data_, at);
    if (!target) return fail(path, "array has no fixup");
    if (static_cast<std::uint64_t>(*target) + static_cast<std::uint64_t>(count) * stride > data_.data.size()) {
      return fail(path, "array runs past the data section");
    }
    first = *target;
    return true;
  }

  bool decode_array(const Slot& slot, std::size_t at, const std::string& path, HkxValue& out, int depth) {
    Slot element;
    std::size_t stride = 0;
    std::size_t count = 0;
    std::size_t first = 0;
    if (!array_elements(slot, at, path, element, stride, count, first)) return false;
    if (count != 0 && !claim(first, count * stride, path)) return false;
    out.kind = HkxValue::Kind::Array;
    out.items.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
      if (!decode(element, first + i * stride, index_path(path, i), out.items[i], depth + 1)) return false;
    }
    return true;
  }

  void store(std::size_t at, const void* bytes, std::size_t size) {
    std::memcpy(out_->data() + data_.file_offset + at, bytes, size);
  }

  bool patch_real(std::size_t at, const std::string& path, const HkxValue& want) {
    if (want.kind != HkxValue::Kind::Real) return fail(path, "edited value is not a real");
    const float narrowed = static_cast<float>(want.real);
    if (double_bits(static_cast<double>(narrowed)) != double_bits(want.real)) {
      return fail(path, "edited value is not representable as a float");
    }
    const auto bits = std::bit_cast<std::uint32_t>(narrowed);
    store(at, &bits, sizeof bits);
    return true;
  }

  bool patch_integer(K kind, std::size_t at, const std::string& path, const HkxValue& want) {
    if (want.kind != HkxValue::Kind::Integer) return fail(path, "edited value is not an integer");
    if (!fits(kind, want.integer)) return fail(path, "edited integer is out of range for the member");
    const std::size_t width = kind_size(kind);
    const auto bits = static_cast<std::uint64_t>(want.integer);
    store(at, &bits, width);
    return true;
  }

  bool patch(const Slot& slot, std::size_t at, const std::string& path, const HkxValue& before,
             const HkxValue& want) {
    if (same(before, want)) return true;
    if (is_integer(slot.kind)) return patch_integer(slot.kind, at, path, want);
    switch (slot.kind) {
      case K::Bool: {
        if (want.kind != HkxValue::Kind::Bool) return fail(path, "edited value is not a bool");
        const std::uint8_t byte = want.boolean ? 1 : 0;
        store(at, &byte, 1);
        return true;
      }
      case K::Real:
        return patch_real(at, path, want);
      case K::Vector4:
      case K::Quaternion:
      case K::QsTransform:
      case K::Transform: {
        if (want.kind != HkxValue::Kind::Array || want.items.size() != before.items.size()) {
          return fail(path, "edited vector has a different length");
        }
        for (std::size_t i = 0; i < want.items.size(); ++i) {
          if (!patch_real(at + 4 * i, index_path(path, i), want.items[i])) return false;
        }
        return true;
      }
      case K::StringPtr:
        return fail(path, "string text cannot change without moving bytes");
      case K::Pointer:
        return fail(path, "reference target cannot change without moving bytes");
      case K::Array:
        return patch_array(slot, at, path, before, want);
      case K::Struct:
        return patch_struct(slot, at, path, before, want);
      default:
        break;
    }
    return fail(path, "member has no verified kind");
  }

  bool patch_array(const Slot& slot, std::size_t at, const std::string& path, const HkxValue& before,
                   const HkxValue& want) {
    if (want.kind != HkxValue::Kind::Array || want.items.size() != before.items.size()) {
      return fail(path, "array length change would move bytes");
    }
    Slot element;
    std::size_t stride = 0;
    std::size_t count = 0;
    std::size_t first = 0;
    if (!array_elements(slot, at, path, element, stride, count, first)) return false;
    for (std::size_t i = 0; i < count; ++i) {
      if (!patch(element, first + i * stride, index_path(path, i), before.items[i], want.items[i])) return false;
    }
    return true;
  }

  bool patch_struct(const Slot& slot, std::size_t at, const std::string& path, const HkxValue& before,
                    const HkxValue& want) {
    if (want.kind != HkxValue::Kind::Struct || want.names != before.names || want.items.size() != before.items.size()) {
      return fail(path, "edited struct has different members");
    }
    const HkxDecodeClass* entry = find_decode_class(slot.struct_class);
    for (std::size_t i = 0; i < entry->members.size(); ++i) {
      const HkxMemberType& member = entry->members[i];
      if (!patch(slot_of(member), at + member.offset, join(path, member.name), before.items[i], want.items[i])) {
        return false;
      }
    }
    return true;
  }

  const HkxPackfile& packfile_;
  const HkxSection& data_;
  std::vector<std::byte>* out_;
  std::map<std::uint32_t, HkxRemoteFixup> global_;
  std::set<std::uint32_t> starts_;
  std::map<std::size_t, std::size_t> claimed_;
  std::size_t leaves_{0};
  HkxBlock block_;
};

}  // namespace

HkxDecodeResult decode_document(std::span<const std::byte> bytes, const HkxDecodeOptions& options) {
  HkxDecodeResult result;
  HkxPackfile packfile;
  const HkxStatus status = read_packfile(bytes, packfile);
  if (status != HkxStatus::Ok) {
    result.status = HkxDecodeStatus::Unreadable;
    result.packfile_status = status;
    return result;
  }
  Codec codec(packfile, nullptr);
  if (!codec.decode_all(options, result.document, result.opaque_classes)) {
    result.status = HkxDecodeStatus::Refused;
    result.block = codec.block();
    result.document = HkxSemanticDocument{};
    result.opaque_classes.clear();
  }
  return result;
}

HkxEmitResult emit_document(std::span<const std::byte> source, const HkxSemanticDocument& edited,
                            const HkxDecodeOptions& options) {
  HkxEmitResult result;
  HkxPackfile packfile;
  const HkxStatus status = read_packfile(source, packfile);
  if (status != HkxStatus::Ok) {
    result.status = HkxDecodeStatus::Unreadable;
    result.packfile_status = status;
    return result;
  }
  std::vector<std::byte> out(source.begin(), source.end());
  Codec codec(packfile, &out);
  HkxSemanticDocument baseline;
  std::vector<std::string> opaque;
  if (!codec.decode_all(options, baseline, opaque) || !codec.patch_all(baseline, edited)) {
    result.status = HkxDecodeStatus::Refused;
    result.block = codec.block();
    return result;
  }
  const HkxDecodeResult check = decode_document(out, options);
  if (check.status != HkxDecodeStatus::Ok || !same_document(check.document, edited)) {
    result.status = HkxDecodeStatus::Refused;
    result.block = check.status == HkxDecodeStatus::Ok ? HkxBlock{} : check.block;
    result.block.reason = "emitted bytes do not decode back to the edited document";
    return result;
  }
  result.bytes = std::move(out);
  return result;
}

}  // namespace fo4::hkx
