#include <fo4/avm1/avm1.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace fo4::avm1 {

namespace {

using Bytes = std::vector<std::byte>;

void put8(Bytes& out, std::uint8_t value) {
  out.push_back(static_cast<std::byte>(value));
}

void put16(Bytes& out, std::uint16_t value) {
  put8(out, static_cast<std::uint8_t>(value & 0xFFu));
  put8(out, static_cast<std::uint8_t>(value >> 8));
}

void put32(Bytes& out, std::uint32_t value) {
  put16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  put16(out, static_cast<std::uint16_t>(value >> 16));
}

void put_string(Bytes& out, const std::string& text) {
  for (const char ch : text) {
    put8(out, static_cast<std::uint8_t>(ch));
  }
  put8(out, 0);
}

void write_value(Bytes& out, const Avm1Value& value) {
  put8(out, static_cast<std::uint8_t>(value.type));
  switch (value.type) {
    case Avm1ValueType::String:
      put_string(out, value.text);
      return;
    case Avm1ValueType::Float:
      put32(out, value.float_bits);
      return;
    case Avm1ValueType::Null:
    case Avm1ValueType::Undefined:
      return;
    case Avm1ValueType::Register:
    case Avm1ValueType::Constant8:
      put8(out, static_cast<std::uint8_t>(value.index));
      return;
    case Avm1ValueType::Boolean:
      put8(out, value.boolean ? 1 : 0);
      return;
    case Avm1ValueType::Double:
      put32(out, static_cast<std::uint32_t>(value.double_bits >> 32));
      put32(out, static_cast<std::uint32_t>(value.double_bits & 0xFFFFFFFFu));
      return;
    case Avm1ValueType::Integer:
      put32(out, static_cast<std::uint32_t>(value.integer));
      return;
    case Avm1ValueType::Constant16:
      put16(out, value.index);
      return;
  }
}

void write_payload(Bytes&, std::monostate) {}

void write_payload(Bytes& out, const Avm1Raw& value) {
  out.insert(out.end(), value.bytes.begin(), value.bytes.end());
}

void write_payload(Bytes& out, const Avm1Byte& value) {
  put8(out, value.value);
}

void write_payload(Bytes& out, const Avm1Frame& value) {
  put16(out, value.frame);
}

void write_payload(Bytes& out, const Avm1WaitForFrame& value) {
  put16(out, value.frame);
  put8(out, value.skip);
}

void write_payload(Bytes& out, const Avm1Text& value) {
  put_string(out, value.text);
}

void write_payload(Bytes& out, const Avm1GetUrl& value) {
  put_string(out, value.url);
  put_string(out, value.target);
}

void write_payload(Bytes& out, const Avm1GotoFrame2& value) {
  put8(out, value.flags);
  if (value.scene_bias) {
    put16(out, *value.scene_bias);
  }
}

void write_payload(Bytes& out, const Avm1ConstantPool& value) {
  put16(out, static_cast<std::uint16_t>(value.strings.size()));
  for (const std::string& text : value.strings) {
    put_string(out, text);
  }
}

void write_payload(Bytes& out, const Avm1Push& value) {
  for (const Avm1Value& item : value.values) {
    write_value(out, item);
  }
}

void write_payload(Bytes& out, const Avm1Branch& value) {
  put16(out, static_cast<std::uint16_t>(value.offset));
}

void write_payload(Bytes& out, const Avm1Function& value) {
  put_string(out, value.name);
  put16(out, static_cast<std::uint16_t>(value.parameters.size()));
  if (value.version2) {
    put8(out, value.register_count);
    put16(out, value.flags);
  }
  for (const Avm1Parameter& parameter : value.parameters) {
    if (value.version2) {
      put8(out, parameter.register_index);
    }
    put_string(out, parameter.name);
  }
  put16(out, value.body_size);
}

void write_payload(Bytes& out, const Avm1With& value) {
  put16(out, value.body_size);
}

void write_payload(Bytes& out, const Avm1Try& value) {
  put8(out, value.flags);
  put16(out, value.try_size);
  put16(out, value.catch_size);
  put16(out, value.finally_size);
  if ((value.flags & 4) != 0) {
    put8(out, value.catch_register);
  } else {
    put_string(out, value.catch_name);
  }
}

void write_action(Bytes& out, const Avm1Action& action) {
  put8(out, action.code);
  if (action.code < 0x80) {
    return;
  }
  Bytes payload;
  std::visit([&payload](const auto& value) { write_payload(payload, value); }, action.payload);
  put16(out, static_cast<std::uint16_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

}  // namespace

std::vector<std::byte> encode_actions(const Avm1Stream& stream) {
  Bytes out;
  for (const Avm1Action& action : stream.actions) {
    write_action(out, action);
  }
  out.insert(out.end(), stream.trailing.begin(), stream.trailing.end());
  return out;
}

}  // namespace fo4::avm1
