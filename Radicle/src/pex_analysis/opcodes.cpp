#include <fo4/pex_analysis/opcodes.hpp>

#include <cstdio>

namespace fo4::pex_analysis {

std::string pex_op_name(std::uint8_t op) {
  if (op > static_cast<std::uint8_t>(PexOp::ArrayClear)) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "op_%02X", op);
    return std::string(buffer);
  }
  return fo4::pex::PexFile::kOpCodes[op].name;
}

std::string pex_value_text(const fo4::pex::PexValue& value) {
  char buffer[48];
  switch (value.type) {
    case fo4::pex::PexValueType::Identifier:
      return value.str;
    case fo4::pex::PexValueType::String:
      return "\"" + value.str + "\"";
    case fo4::pex::PexValueType::Integer:
      std::snprintf(buffer, sizeof(buffer), "%d", value.int_);
      return std::string(buffer);
    case fo4::pex::PexValueType::Float:
      std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value.float_));
      return std::string(buffer);
    case fo4::pex::PexValueType::Bool:
      return value.bool_ ? "true" : "false";
    case fo4::pex::PexValueType::None:
      break;
  }
  return "none";
}

}
