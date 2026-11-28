#include <fo4/abc/abc_asm.hpp>

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include "abc_asm_internal.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::abc::detail::text {

std::string trim(std::string_view line) {
  std::size_t begin = 0;
  while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) {
    ++begin;
  }
  std::size_t end = line.size();
  while (end > begin && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r')) {
    end -= 1;
  }
  return std::string{line.substr(begin, end - begin)};
}

std::vector<std::string> split_tokens(std::string_view line) {
  std::vector<std::string> tokens;
  std::size_t index = 0;
  while (index < line.size()) {
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) {
      ++index;
    }
    if (index >= line.size()) {
      break;
    }
    if (line[index] == '"') {
      const std::size_t start = index;
      ++index;
      while (index < line.size()) {
        if (line[index] == '\\' && index + 1 < line.size()) {
          index += 2;
          continue;
        }
        if (line[index] == '"') {
          ++index;
          break;
        }
        ++index;
      }
      tokens.emplace_back(line.substr(start, index - start));
      continue;
    }
    const std::size_t start = index;
    while (index < line.size() && line[index] != ' ' && line[index] != '\t') {
      ++index;
    }
    tokens.emplace_back(line.substr(start, index - start));
  }
  return tokens;
}

bool unquote(std::string_view token, std::string& out) {
  out.clear();
  if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
    return false;
  }
  for (std::size_t index = 1; index + 1 < token.size(); ++index) {
    const char character = token[index];
    if (character != '\\') {
      out.push_back(character);
      continue;
    }
    if (index + 2 > token.size()) {
      return false;
    }
    const char escape = token[++index];
    switch (escape) {
      case '"':
        out.push_back('"');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'x': {
        if (index + 2 >= token.size()) {
          return false;
        }
        unsigned value = 0;
        for (int digit = 0; digit < 2; ++digit) {
          const char hex = token[++index];
          value *= 16;
          if (hex >= '0' && hex <= '9') {
            value += static_cast<unsigned>(hex - '0');
          } else if (hex >= 'a' && hex <= 'f') {
            value += static_cast<unsigned>(hex - 'a' + 10);
          } else if (hex >= 'A' && hex <= 'F') {
            value += static_cast<unsigned>(hex - 'A' + 10);
          } else {
            return false;
          }
        }
        out.push_back(static_cast<char>(value));
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

bool parse_u32(std::string_view token, std::uint32_t& out) {
  if (token.empty()) {
    return false;
  }
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

bool parse_i32(std::string_view token, std::int32_t& out) {
  if (token.empty()) {
    return false;
  }
  const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
  return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

bool parse_double_bits(std::string_view token, double& out) {
  if (token.size() < 3 || token[0] != '0' || token[1] != 'x') {
    return false;
  }
  std::uint64_t bits = 0;
  const auto result =
      std::from_chars(token.data() + 2, token.data() + token.size(), bits, 16);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
    return false;
  }
  std::memcpy(&out, &bits, sizeof(out));
  return true;
}

bool parse_ref(std::string_view token, std::uint32_t& out) {
  if (token.size() < 2 || token[0] != '@') {
    return false;
  }
  return parse_u32(token.substr(1), out);
}


bool namespace_kind_from(std::string_view token, AbcNamespaceKind& out) {
  constexpr std::array<const char*, 7> names{{"private", "namespace", "package",
                                              "package_internal", "protected", "explicit",
                                              "static_protected"}};
  constexpr std::array<AbcNamespaceKind, 7> kinds{
      {AbcNamespaceKind::Private, AbcNamespaceKind::Namespace, AbcNamespaceKind::Package,
       AbcNamespaceKind::PackageInternal, AbcNamespaceKind::Protected, AbcNamespaceKind::Explicit,
       AbcNamespaceKind::StaticProtected}};
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (std::string_view{names[index]} == token) {
      out = kinds[index];
      return true;
    }
  }
  return false;
}

bool multiname_kind_from(std::string_view token, AbcMultinameKind& out) {
  constexpr std::array<const char*, 11> names{
      {"qname", "qname_a", "rtqname", "rtqname_a", "rtqname_l", "rtqname_la", "multiname",
       "multiname_a", "multinamel", "multinamela", "typename"}};
  constexpr std::array<AbcMultinameKind, 11> kinds{
      {AbcMultinameKind::QName, AbcMultinameKind::QNameA, AbcMultinameKind::RTQName,
       AbcMultinameKind::RTQNameA, AbcMultinameKind::RTQNameL, AbcMultinameKind::RTQNameLA,
       AbcMultinameKind::Multiname, AbcMultinameKind::MultinameA, AbcMultinameKind::MultinameL,
       AbcMultinameKind::MultinameLA, AbcMultinameKind::TypeName}};
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (std::string_view{names[index]} == token) {
      out = kinds[index];
      return true;
    }
  }
  return false;
}

bool trait_kind_from(std::string_view token, AbcTraitKind& out) {
  constexpr std::array<const char*, 7> names{
      {"slot", "method", "getter", "setter", "class", "function", "const"}};
  constexpr std::array<AbcTraitKind, 7> kinds{
      {AbcTraitKind::Slot, AbcTraitKind::Method, AbcTraitKind::Getter, AbcTraitKind::Setter,
       AbcTraitKind::Class, AbcTraitKind::Function, AbcTraitKind::Const}};
  std::size_t match = names.size();
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (std::string_view{names[index]} == token) {
      match = index;
    }
  }
  if (match == names.size()) {
    return false;
  }
  out = kinds[match];
  return true;
}

bool value_kind_from(std::string_view token, AbcValueKind& out) {
  constexpr std::array<const char*, 15> names{
      {"undefined", "utf8", "int", "uint", "private_ns", "double", "namespace", "false", "true",
       "null", "package_namespace", "package_internal_ns", "protected_namespace",
       "explicit_namespace", "static_protected_ns"}};
  constexpr std::array<AbcValueKind, 15> kinds{
      {AbcValueKind::Undefined, AbcValueKind::Utf8, AbcValueKind::Int, AbcValueKind::UInt,
       AbcValueKind::PrivateNs, AbcValueKind::Double, AbcValueKind::Namespace, AbcValueKind::False,
       AbcValueKind::True, AbcValueKind::Null, AbcValueKind::PackageNamespace,
       AbcValueKind::PackageInternalNs, AbcValueKind::ProtectedNamespace,
       AbcValueKind::ExplicitNamespace, AbcValueKind::StaticProtectedNs}};
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (std::string_view{names[index]} == token) {
      out = kinds[index];
      return true;
    }
  }
  return false;
}

std::uint8_t opcode_for(std::string_view mnemonic) {
  for (std::uint32_t opcode = 0; opcode < 256; ++opcode) {
    const AbcOpcodeInfo* info = opcode_info(static_cast<std::uint8_t>(opcode));
    if (info != nullptr && mnemonic == info->mnemonic) {
      return static_cast<std::uint8_t>(opcode);
    }
  }
  return 0xFF;
}

bool Cursor::take(std::string_view expected) {
  if (empty() || peek() != expected) {
    return false;
  }
  ++position_;
  return true;
}

bool Cursor::skip(std::string_view keyword) noexcept {
  if (!empty() && peek() == keyword) {
    ++position_;
    return true;
  }
  return false;
}

bool Cursor::u32(std::uint32_t& out) {
  if (empty() || !parse_u32(peek(), out)) {
    return false;
  }
  ++position_;
  return true;
}

bool Cursor::i32(std::int32_t& out) {
  if (empty() || !parse_i32(peek(), out)) {
    return false;
  }
  ++position_;
  return true;
}

bool Cursor::ref(std::uint32_t& out) {
  if (empty() || !parse_ref(peek(), out)) {
    return false;
  }
  ++position_;
  return true;
}

bool Cursor::quoted(std::string& out) {
  if (empty() || !unquote(peek(), out)) {
    return false;
  }
  ++position_;
  return true;
}

bool Cursor::text(std::string_view keyword, std::uint32_t& out) {
  return take(keyword) && ref(out);
}

bool Cursor::invalid(std::string_view message) {
  if (error_.empty()) {
    error_ = std::string{message};
  }
  return false;
}

AbcStatus parse_trait(Cursor& cursor, AbcTrait& out) {
  if (!cursor.skip("trait")) {
    return AbcStatus::InconsistentModel;
  }
  AbcTraitKind kind{};
  if (cursor.empty() || !trait_kind_from(cursor.peek(), kind)) {
    cursor.invalid("trait kind expected");
    return AbcStatus::InconsistentModel;
  }
  cursor.skip(cursor.peek());
  out.kind = kind;
  std::uint32_t value = 0;
  if (!cursor.text("name", out.name) || !cursor.skip("attrs") || !cursor.u32(value)) {
    cursor.invalid("trait name/attrs expected");
    return AbcStatus::InconsistentModel;
  }
  out.attrs = static_cast<std::uint8_t>(value);
  switch (out.kind) {
    case AbcTraitKind::Slot:
    case AbcTraitKind::Const: {
      if (!cursor.skip("slot") || !cursor.u32(out.slot_id) ||
          !cursor.text("type", out.type_name)) {
        cursor.invalid("slot trait needs slot/type");
        return AbcStatus::InconsistentModel;
      }
      AbcValueKind stored_kind = AbcValueKind::Undefined;
      if (!cursor.empty() && cursor.skip("value")) {
        if (cursor.empty()) {
          cursor.invalid("trait value kind expected");
          return AbcStatus::InconsistentModel;
        }
        if (cursor.peek() == "none") {
          cursor.skip("none");
          if (!cursor.ref(out.value_index)) {
            cursor.invalid("trait value reference expected");
            return AbcStatus::InconsistentModel;
          }
        } else {
          if (!value_kind_from(cursor.peek(), stored_kind)) {
            cursor.invalid("unknown trait value kind");
            return AbcStatus::InconsistentModel;
          }
          cursor.skip(cursor.peek());
          if (!cursor.ref(out.value_index)) {
            cursor.invalid("trait value reference expected");
            return AbcStatus::InconsistentModel;
          }
          out.value_kind = stored_kind;
        }
      }
      break;
    }
    case AbcTraitKind::Class:
    case AbcTraitKind::Function:
      if (!cursor.skip("slot") || !cursor.u32(out.slot_id) ||
          !cursor.text("class", out.class_index)) {
        cursor.invalid("class trait needs slot/class");
        return AbcStatus::InconsistentModel;
      }
      break;
    case AbcTraitKind::Method:
    case AbcTraitKind::Getter:
    case AbcTraitKind::Setter:
      if (!cursor.skip("disp") || !cursor.u32(out.disp_id) ||
          !cursor.text("method", out.method_index)) {
        cursor.invalid("method trait needs disp/method");
        return AbcStatus::InconsistentModel;
      }
      break;
  }
  if (cursor.skip("metadata")) {
    while (!cursor.empty() && cursor.peek().front() == '@') {
      std::uint32_t metadata = 0;
      if (!cursor.ref(metadata)) {
        break;
      }
      out.metadata.push_back(metadata);
    }
  }
  return AbcStatus::Ok;
}

}  // namespace fo4::abc::detail::text
