#include <fo4/avm2/amf.hpp>

#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

constexpr std::size_t kMaxDepth = 64;
constexpr std::int64_t kIntMin = -268435456;
constexpr std::int64_t kIntMax = 268435455;

class Writer {
 public:
  void byte(std::uint8_t value) { bytes_.push_back(value); }
  void u29(std::uint32_t value) {
    value &= 0x1FFFFFFFu;
    if (value < 0x80u) {
      byte(static_cast<std::uint8_t>(value));
      return;
    }
    byte(static_cast<std::uint8_t>((value >> 22u) | 0x80u));
    byte(static_cast<std::uint8_t>((value >> 15u) & 0x7Fu | 0x80u));
    byte(static_cast<std::uint8_t>((value >> 8u) & 0x7Fu | 0x80u));
    byte(static_cast<std::uint8_t>(value & 0xFFu));
  }
  void double_bits(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 56; shift >= 0; shift -= 8) {
      byte(static_cast<std::uint8_t>((bits >> static_cast<unsigned>(shift)) & 0xFFu));
    }
  }
  void string(std::string_view value) {
    u29(static_cast<std::uint32_t>(value.size() << 1u));
    for (const char character : value) {
      byte(static_cast<std::uint8_t>(character));
    }
  }
  std::vector<std::uint8_t>& bytes() noexcept { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
};

class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  bool ok() const noexcept { return ok_; }
  void fail() noexcept { ok_ = false; }
  bool done() const noexcept { return position_ >= bytes_.size(); }

  std::uint8_t byte() {
    if (position_ >= bytes_.size()) {
      ok_ = false;
      return 0;
    }
    return bytes_[position_++];
  }
  std::uint32_t u29() {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
      const std::uint8_t part = byte();
      if (!ok_) return 0;
      value = (value << 7u) | (part & 0x7Fu);
      if ((part & 0x80u) == 0) return value;
    }
    ok_ = false;
    return 0;
  }
  double double_bits() {
    std::uint64_t bits = 0;
    for (int index = 0; index < 8; ++index) {
      bits = (bits << 8u) | byte();
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
  std::string string() {
    const std::uint32_t header = u29();
    if (!ok_ || (header & 1u) != 0) {
      return {};
    }
    const std::size_t size = header >> 1u;
    if (size > bytes_.size() - position_) {
      ok_ = false;
      return {};
    }
    std::string text(reinterpret_cast<const char*>(bytes_.data() + position_), size);
    position_ += size;
    return text;
  }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_{0};
  bool ok_{true};
};

Value decode_value(Reader& reader, std::size_t depth) {
  if (depth > kMaxDepth) {
    reader.fail();
    return Value::null();
  }
  const std::uint8_t marker = reader.byte();
  switch (marker) {
    case 0x00:
      return Value::undefined();
    case 0x01:
      return Value::null();
    case 0x02:
      return Value::boolean(false);
    case 0x03:
      return Value::boolean(true);
    case 0x04: {
      const std::uint32_t raw = reader.u29();
      const std::int32_t value = (raw & 0x10000000u) != 0
                                     ? static_cast<std::int32_t>(raw | 0xE0000000u)
                                     : static_cast<std::int32_t>(raw);
      return Value::int32(value);
    }
    case 0x05:
      return Value::number(reader.double_bits());
    case 0x06:
      return Value::string(reader.string());
    case 0x09: {
      const std::uint32_t header = reader.u29();
      if (!reader.ok()) return Value::null();
      if ((header & 1u) != 0) {
        const std::uint32_t associative = header >> 1u;
        if (associative != 0) {
          reader.fail();
          return Value::null();
        }
      }
      const std::uint32_t count = header & 1u ? reader.u29() : header >> 1u;
      auto array = std::make_shared<ArrayObject>();
      for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        array->elements().push_back(decode_value(reader, depth + 1));
      }
      return Value::object(std::move(array));
    }
    case 0x0A: {
      const std::uint32_t header = reader.u29();
      if (!reader.ok() || (header & 1u) == 0) {
        return Value::null();
      }
      const std::uint32_t traits = reader.u29();
      if (!reader.ok() || traits != 0) {
        reader.fail();
        return Value::null();
      }
      auto object = std::make_shared<Object>(ObjectKind::Object);
      while (reader.ok()) {
        const std::string key = reader.string();
        if (!reader.ok() || key.empty()) break;
        Multiname multiname;
        multiname.name = key;
        multiname.namespaces.push_back(Namespace{});
        object->write_value(multiname, decode_value(reader, depth + 1));
      }
      return Value::object(std::move(object));
    }
    default:
      reader.fail();
      return Value::null();
  }
}

void encode_value(Writer& writer, const Value& value, std::size_t depth) {
  if (depth > kMaxDepth || value.is_undefined()) {
    writer.byte(0x00);
    return;
  }
  if (value.is_null()) {
    writer.byte(0x01);
    return;
  }
  if (value.is_boolean()) {
    writer.byte(value.as_boolean() ? 0x03 : 0x02);
    return;
  }
  if (value.is_int32() || value.is_uint32()) {
    const std::int64_t integer = value.is_int32() ? value.as_int32()
                                                  : static_cast<std::int64_t>(value.as_uint32());
    if (integer >= kIntMin && integer <= kIntMax) {
      writer.byte(0x04);
      writer.u29(static_cast<std::uint32_t>(integer) & 0x1FFFFFFFu);
      return;
    }
    writer.byte(0x05);
    writer.double_bits(static_cast<double>(integer));
    return;
  }
  if (value.is_number_like()) {
    writer.byte(0x05);
    writer.double_bits(value.as_number());
    return;
  }
  if (value.is_string()) {
    writer.byte(0x06);
    writer.string(value.as_string());
    return;
  }
  if (!value.is_object()) {
    writer.byte(0x01);
    return;
  }
  const Object& object = *value.as_object();
  if (object.kind() == ObjectKind::Array) {
    const auto& array = static_cast<const ArrayObject&>(object);
    writer.byte(0x09);
    writer.u29(static_cast<std::uint32_t>(array.elements().size() << 1u));
    for (const Value& element : array.elements()) {
      encode_value(writer, element, depth + 1);
    }
    return;
  }
  writer.byte(0x0A);
  writer.u29(1u);
  writer.u29(0u);
  std::vector<std::pair<std::string, Value>> entries;
  for (const auto& [key, property] : object.own_properties()) {
    if (property.kind != PropertyKind::Value) continue;
    entries.emplace_back(key.name, property.value);
  }
  std::sort(entries.begin(), entries.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
  for (const auto& [name, property] : entries) {
    if (name.empty()) continue;
    writer.string(name);
    encode_value(writer, property, depth + 1);
  }
  writer.string(std::string_view{});
}

}  // namespace

std::vector<std::uint8_t> encode_amf3(const Value& value) {
  Writer writer;
  encode_value(writer, value, 0);
  return std::move(writer.bytes());
}

bool decode_amf3(std::span<const std::uint8_t> bytes, Value& out) {
  out = Value::undefined();
  if (bytes.empty()) return false;
  Reader reader(bytes);
  out = decode_value(reader, 0);
  return reader.ok() && reader.done();
}

bool shared_object_name_ok(std::string_view name) {
  if (name.empty() || name.size() > 255) return false;
  for (const char character : name) {
    if (character == '\0' || character == '/' || character == '\\') return false;
  }
  return true;
}

}  // namespace fo4::avm2
