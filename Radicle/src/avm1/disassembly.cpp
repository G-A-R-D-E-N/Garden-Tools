#include <fo4/avm1/avm1.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>

namespace fo4::avm1 {

namespace {

std::string hex(std::uint64_t value, int digits) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%0*llX", digits, static_cast<unsigned long long>(value));
  return buffer;
}

std::string quote(const std::string& text) {
  return "\"" + text + "\"";
}

std::string describe_value(const Avm1Value& value) {
  switch (value.type) {
    case Avm1ValueType::String:
      return quote(value.text);
    case Avm1ValueType::Float:
      return "float 0x" + hex(value.float_bits, 8);
    case Avm1ValueType::Null:
      return "null";
    case Avm1ValueType::Undefined:
      return "undefined";
    case Avm1ValueType::Register:
      return "register " + std::to_string(value.index);
    case Avm1ValueType::Boolean:
      return value.boolean ? "true" : "false";
    case Avm1ValueType::Double:
      return "double 0x" + hex(value.double_bits, 16);
    case Avm1ValueType::Integer:
      return "int " + std::to_string(value.integer);
    case Avm1ValueType::Constant8:
      return "const8 " + std::to_string(value.index);
    case Avm1ValueType::Constant16:
      return "const16 " + std::to_string(value.index);
  }
  return "?";
}

void describe_payload(std::string&, std::monostate) {}

void describe_payload(std::string& text, const Avm1Raw& value) {
  text += " " + std::to_string(value.bytes.size()) + " raw bytes";
}

void describe_payload(std::string& text, const Avm1Byte& value) {
  text += " " + std::to_string(value.value);
}

void describe_payload(std::string& text, const Avm1Frame& value) {
  text += " " + std::to_string(value.frame);
}

void describe_payload(std::string& text, const Avm1WaitForFrame& value) {
  text += " " + std::to_string(value.frame) + " skip " + std::to_string(value.skip);
}

void describe_payload(std::string& text, const Avm1Text& value) {
  text += " " + quote(value.text);
}

void describe_payload(std::string& text, const Avm1GetUrl& value) {
  text += " " + quote(value.url) + " " + quote(value.target);
}

void describe_payload(std::string& text, const Avm1GotoFrame2& value) {
  text += " flags " + std::to_string(value.flags);
  if (value.scene_bias) {
    text += " bias " + std::to_string(*value.scene_bias);
  }
}

void describe_payload(std::string& text, const Avm1ConstantPool& value) {
  for (const std::string& item : value.strings) {
    text += " " + quote(item);
  }
}

void describe_payload(std::string& text, const Avm1Push& value) {
  for (std::size_t index = 0; index < value.values.size(); ++index) {
    text += index == 0 ? " " : ", ";
    text += describe_value(value.values[index]);
  }
}

void describe_payload(std::string& text, const Avm1Branch& value) {
  text += " -> " + hex(value.target, 4);
}

void describe_payload(std::string& text, const Avm1Function& value) {
  text += " " + quote(value.name) + "(";
  for (std::size_t index = 0; index < value.parameters.size(); ++index) {
    text += index == 0 ? "" : ", ";
    text += value.parameters[index].name;
  }
  text += ")";
  if (value.version2) {
    text += " registers " + std::to_string(value.register_count) + " flags " +
            std::to_string(value.flags);
  }
  text += " body " + std::to_string(value.body_size);
}

void describe_payload(std::string& text, const Avm1With& value) {
  text += " body " + std::to_string(value.body_size);
}

void describe_payload(std::string& text, const Avm1Try& value) {
  text += " try " + std::to_string(value.try_size) + " catch " + std::to_string(value.catch_size) +
          " finally " + std::to_string(value.finally_size);
  if ((value.flags & 4) != 0) {
    text += " catch register " + std::to_string(value.catch_register);
  } else {
    text += " catch " + quote(value.catch_name);
  }
}

std::string describe_action(const Avm1Action& action) {
  const char* name = action_mnemonic(action.code);
  std::string text = hex(action.offset, 4) + "  ";
  text += name == nullptr ? "unknown 0x" + hex(action.code, 2) : std::string(name);
  std::visit([&text](const auto& payload) { describe_payload(text, payload); }, action.payload);
  text += "\n";
  return text;
}

}  // namespace

std::string disassemble_actions(const Avm1Stream& stream) {
  std::string text;
  for (const Avm1Action& action : stream.actions) {
    text += describe_action(action);
  }
  if (!stream.trailing.empty()) {
    text += "trailing " + std::to_string(stream.trailing.size()) + " bytes\n";
  }
  return text;
}

}  // namespace fo4::avm1
