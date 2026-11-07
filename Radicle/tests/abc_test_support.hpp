#pragma once

#include "fo4/abc/abc.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fo4::test {

inline constexpr std::uint32_t kStringFoo = 1;
inline constexpr std::uint32_t kStringExternalInterface = 2;
inline constexpr std::uint32_t kStringCall = 3;
inline constexpr std::uint32_t kStringTest = 4;
inline constexpr std::uint32_t kStringObject = 5;
inline constexpr std::uint32_t kStringFlashExternal = 6;
inline constexpr std::uint32_t kStringClick = 7;
inline constexpr std::uint32_t kStringAddEventListener = 8;
inline constexpr std::uint32_t kStringHandler = 9;
inline constexpr std::uint32_t kStringVoid = 10;
inline constexpr std::uint32_t kStringOnClick = 11;
inline constexpr std::uint32_t kStringCounter = 12;
inline constexpr std::uint32_t kStringInner = 13;
inline constexpr std::uint32_t kStringHelper = 14;

inline constexpr std::uint32_t kMultinameExternalInterface = 1;
inline constexpr std::uint32_t kMultinameCall = 2;
inline constexpr std::uint32_t kMultinameTest = 3;
inline constexpr std::uint32_t kMultinameObject = 4;
inline constexpr std::uint32_t kMultinameClick = 5;
inline constexpr std::uint32_t kMultinameAddEventListener = 6;
inline constexpr std::uint32_t kMultinameHandler = 7;
inline constexpr std::uint32_t kMultinameVoid = 8;
inline constexpr std::uint32_t kMultinameOnClick = 9;
inline constexpr std::uint32_t kMultinameCounter = 10;
inline constexpr std::uint32_t kMultinameTypeName = 11;
inline constexpr std::uint32_t kMultinameInner = 12;
inline constexpr std::uint32_t kMultinameHelper = 13;
inline constexpr std::uint32_t kMultinameRuntimeName = 14;

enum class AbcBadIndex {
  None = 0,
  InstanceName,
  InstanceNameAtLimit,
  InstanceSuper,
  InstanceInterface,
  InstanceIinit,
  InstanceIinitAtLimit,
  InstanceProtectedNs,
  ClassInit,
  TraitName,
  TraitTypeName,
  TraitMethodIndex,
  TraitClassIndex,
  TraitFunctionIndex,
  MethodReturnType,
  MethodParamType,
  ExceptionType,
  ExceptionVarName,
  ScriptInit,
  BodyMethod,
  MethodNameAtLast,
  MethodNameAtLimit,
  MethodParamNameAtLast,
  MethodParamNameAtLimit,
  MethodOptionIndexAtLast,
  MethodOptionIndexAtLimit,
  MethodOptionIndexZero,
  MetadataNameAtLast,
  MetadataNameAtLimit,
  MetadataItemKeyAtLast,
  MetadataItemKeyAtLimit,
  MetadataItemValueAtLast,
  MetadataItemValueAtLimit,
  TraitValueIndexAtLast,
  TraitValueIndexAtLimit,
  TraitValueIndexStringAtLast,
  TraitValueIndexStringAtLimit,
  TraitValueIndexUIntAtLimit,
  TraitValueIndexDoubleAtLimit,
  TraitValueIndexNamespaceAtLast,
  TraitValueIndexNamespaceAtLimit,
};

inline constexpr std::uint32_t kBadIndex = 99;
inline constexpr std::uint32_t kMethodCount = 3;
inline constexpr std::uint32_t kMultinameCount = 15;
inline constexpr std::uint32_t kStringCount = 15;
inline constexpr std::uint32_t kIntCount = 2;
inline constexpr std::uint32_t kUIntCount = 2;
inline constexpr std::uint32_t kDoubleCount = 2;
inline constexpr std::uint32_t kNamespaceCount = 3;

inline std::uint32_t bad_or(AbcBadIndex bad, AbcBadIndex field, std::uint32_t valid) {
  return bad == field ? kBadIndex : valid;
}

inline std::uint32_t method_name_or(AbcBadIndex bad, std::uint32_t valid) {
  switch (bad) {
    case AbcBadIndex::MethodNameAtLast:
      return kStringCount - 1;
    case AbcBadIndex::MethodNameAtLimit:
      return kStringCount;
    default:
      return valid;
  }
}

inline std::uint32_t param_name_or(AbcBadIndex bad, std::uint32_t valid) {
  switch (bad) {
    case AbcBadIndex::MethodParamNameAtLast:
      return kStringCount - 1;
    case AbcBadIndex::MethodParamNameAtLimit:
      return kStringCount;
    default:
      return valid;
  }
}

inline std::uint32_t option_index_or(AbcBadIndex bad, std::uint32_t valid) {
  switch (bad) {
    case AbcBadIndex::MethodOptionIndexAtLast:
      return kIntCount - 1;
    case AbcBadIndex::MethodOptionIndexAtLimit:
      return kIntCount;
    case AbcBadIndex::MethodOptionIndexZero:
      return 0;
    default:
      return valid;
  }
}

inline std::uint32_t metadata_index_or(AbcBadIndex bad, AbcBadIndex at_last,
                                       AbcBadIndex at_limit, std::uint32_t valid) {
  if (bad == at_last) {
    return kStringCount - 1;
  }
  if (bad == at_limit) {
    return kStringCount;
  }
  return valid;
}

inline std::uint32_t trait_value_index_or(AbcBadIndex bad, std::uint8_t& kind) {
  switch (bad) {
    case AbcBadIndex::TraitValueIndexAtLast:
      return kIntCount - 1;
    case AbcBadIndex::TraitValueIndexAtLimit:
      return kIntCount;
    case AbcBadIndex::TraitValueIndexStringAtLast:
      kind = 0x01;
      return kStringCount - 1;
    case AbcBadIndex::TraitValueIndexStringAtLimit:
      kind = 0x01;
      return kStringCount;
    case AbcBadIndex::TraitValueIndexUIntAtLimit:
      kind = 0x04;
      return kUIntCount;
    case AbcBadIndex::TraitValueIndexDoubleAtLimit:
      kind = 0x06;
      return kDoubleCount;
    case AbcBadIndex::TraitValueIndexNamespaceAtLast:
      kind = 0x16;
      return kNamespaceCount - 1;
    case AbcBadIndex::TraitValueIndexNamespaceAtLimit:
      kind = 0x16;
      return kNamespaceCount;
    default:
      return 1;
  }
}

inline void put_u8(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

inline void put_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put_u8(out, value & 0xFFu);
  put_u8(out, (value >> 8) & 0xFFu);
}

inline void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  std::uint32_t remaining = value;
  while (remaining >= 0x80u) {
    out.push_back(static_cast<std::uint8_t>((remaining & 0x7Fu) | 0x80u));
    remaining >>= 7;
  }
  out.push_back(static_cast<std::uint8_t>(remaining));
}

inline void put_u30(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put_u32(out, value & 0x3FFFFFFFu);
}

inline void put_s24(std::vector<std::uint8_t>& out, std::int32_t value) {
  const std::uint32_t raw = static_cast<std::uint32_t>(value) & 0xFFFFFFu;
  put_u8(out, raw & 0xFFu);
  put_u8(out, (raw >> 8) & 0xFFu);
  put_u8(out, (raw >> 16) & 0xFFu);
}

inline void put_double(std::vector<std::uint8_t>& out, double value) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  for (std::size_t i = 0; i < 8; ++i) {
    put_u8(out, static_cast<std::uint32_t>((bits >> (8 * i)) & 0xFFu));
  }
}

inline void put_string(std::vector<std::uint8_t>& out, const std::string& value) {
  put_u30(out, static_cast<std::uint32_t>(value.size()));
  for (const char ch : value) {
    out.push_back(static_cast<std::uint8_t>(ch));
  }
}

inline std::vector<std::uint8_t> tiny_constructor_code() {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 1);
  put_u8(code, 0x47);
  return code;
}

inline std::vector<std::uint8_t> tiny_listener_code() {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameAddEventListener);
  put_u8(code, 0x2C);
  put_u30(code, kStringClick);
  put_u8(code, 0x60);
  put_u30(code, kMultinameHandler);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameAddEventListener);
  put_u30(code, 2);
  put_u8(code, 0x47);
  return code;
}

inline std::vector<std::uint8_t> tiny_helper_code() {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x47);
  return code;
}

inline void put_constant_pool(std::vector<std::uint8_t>& out) {
  const std::vector<std::string> strings{
      "Foo",   "ExternalInterface", "call",   "Test",   "Object",
      "flash.external", "click", "addEventListener", "handler", "void",
      "onClick", "counter", "Inner", "helper"};
  put_u30(out, 2);
  put_u30(out, 7);
  put_u30(out, 2);
  put_u30(out, 9);
  put_u30(out, 2);
  put_double(out, 1.5);
  put_u30(out, static_cast<std::uint32_t>(strings.size()) + 1);
  for (const std::string& value : strings) {
    put_string(out, value);
  }
  put_u30(out, 3);
  put_u8(out, 0x16);
  put_u30(out, kStringFlashExternal);
  put_u8(out, 0x16);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, kMultinameCount);
  put_u8(out, 0x07);
  put_u30(out, 1);
  put_u30(out, kStringExternalInterface);
  put_u8(out, 0x07);
  put_u30(out, 1);
  put_u30(out, kStringCall);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringTest);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringObject);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringClick);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringAddEventListener);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringHandler);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringVoid);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringOnClick);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringCounter);
  put_u8(out, 0x1D);
  put_u30(out, kMultinameObject);
  put_u30(out, 1);
  put_u30(out, kMultinameTest);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringInner);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringHelper);
  put_u8(out, 0x1B);
  put_u30(out, 0);
}

inline void put_method(std::vector<std::uint8_t>& out, std::uint32_t name,
                       AbcBadIndex bad = AbcBadIndex::None) {
  put_u30(out, 0);
  put_u30(out, bad_or(bad, AbcBadIndex::MethodReturnType, kMultinameVoid));
  put_u30(out, method_name_or(bad, name));
  put_u8(out, 0);
}

inline void put_method_with_options(std::vector<std::uint8_t>& out, std::uint32_t name,
                                    AbcBadIndex bad = AbcBadIndex::None) {
  put_u30(out, 1);
  put_u30(out, kMultinameVoid);
  put_u30(out, bad_or(bad, AbcBadIndex::MethodParamType, kMultinameVoid));
  put_u30(out, method_name_or(bad, name));
  put_u8(out, 0x88);
  put_u30(out, 1);
  put_u30(out, option_index_or(bad, 1));
  put_u8(out, 0x03);
  put_u30(out, param_name_or(bad, kStringClick));
}

inline void put_instance_traits(std::vector<std::uint8_t>& out,
                                AbcBadIndex bad = AbcBadIndex::None) {
  put_u30(out, 5);
  put_u30(out, bad_or(bad, AbcBadIndex::TraitName, kMultinameOnClick));
  put_u8(out, 0x41);
  put_u30(out, 0);
  put_u30(out, bad_or(bad, AbcBadIndex::TraitMethodIndex, 1));
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, kMultinameCounter);
  put_u8(out, 0x40);
  put_u30(out, 1);
  put_u30(out, bad_or(bad, AbcBadIndex::TraitTypeName, kMultinameVoid));
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, kMultinameCounter);
  put_u8(out, 0x06);
  put_u30(out, 2);
  put_u30(out, kMultinameVoid);
  std::uint8_t value_kind = 0x03;
  put_u30(out, trait_value_index_or(bad, value_kind));
  put_u8(out, value_kind);
  put_u30(out, kMultinameInner);
  put_u8(out, 0x04);
  put_u30(out, 0);
  put_u30(out, bad_or(bad, AbcBadIndex::TraitClassIndex, 0));
  put_u30(out, kMultinameHelper);
  put_u8(out, 0x45);
  put_u30(out, 3);
  put_u30(out, bad_or(bad, AbcBadIndex::TraitFunctionIndex, 2));
  put_u30(out, 1);
  put_u30(out, 0);
}

inline void put_body(std::vector<std::uint8_t>& out, std::uint32_t method,
              const std::vector<std::uint8_t>& code, bool with_exception,
              AbcBadIndex bad = AbcBadIndex::None, std::uint32_t local_count = 1) {
  put_u30(out, method);
  put_u30(out, 4);
  put_u30(out, local_count);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, static_cast<std::uint32_t>(code.size()));
  out.insert(out.end(), code.begin(), code.end());
  put_u30(out, with_exception ? 1 : 0);
  if (with_exception) {
    put_u30(out, 0);
    put_u30(out, static_cast<std::uint32_t>(code.size()));
    put_u30(out, 0);
    put_u30(out, bad_or(bad, AbcBadIndex::ExceptionType, kMultinameTest));
    put_u30(out, bad_or(bad, AbcBadIndex::ExceptionVarName, kMultinameHelper));
  }
  put_u30(out, 0);
}

inline std::vector<std::uint8_t> build_abc(const std::vector<std::uint8_t>& constructor_code,
                                    const std::vector<std::uint8_t>& listener_code,
                                    AbcBadIndex bad = AbcBadIndex::None) {
  std::vector<std::uint8_t> out;
  put_u16(out, fo4::abc::kAbcMinorVersion);
  put_u16(out, fo4::abc::kAbcMajorVersion);
  put_constant_pool(out);
  put_u30(out, kMethodCount);
  put_method(out, kStringTest, bad);
  put_method_with_options(out, kStringOnClick, bad);
  put_method(out, kStringHelper);
  put_u30(out, 1);
  put_u30(out, metadata_index_or(bad, AbcBadIndex::MetadataNameAtLast,
                                 AbcBadIndex::MetadataNameAtLimit, kStringFoo));
  put_u30(out, 1);
  put_u30(out, metadata_index_or(bad, AbcBadIndex::MetadataItemKeyAtLast,
                                 AbcBadIndex::MetadataItemKeyAtLimit,
                                 kStringExternalInterface));
  put_u30(out, metadata_index_or(bad, AbcBadIndex::MetadataItemValueAtLast,
                                 AbcBadIndex::MetadataItemValueAtLimit, kStringCall));
  put_u30(out, 1);
  const bool name_at_limit = bad == AbcBadIndex::InstanceNameAtLimit;
  put_u30(out, name_at_limit ? kMultinameCount
                             : bad_or(bad, AbcBadIndex::InstanceName, kMultinameTest));
  put_u30(out, bad_or(bad, AbcBadIndex::InstanceSuper, kMultinameObject));
  const bool bad_protected_ns = bad == AbcBadIndex::InstanceProtectedNs;
  put_u8(out, bad_protected_ns ? 0x0D : 0x05);
  if (bad_protected_ns) {
    put_u30(out, kBadIndex);
  }
  put_u30(out, 1);
  put_u30(out, bad_or(bad, AbcBadIndex::InstanceInterface, kMultinameClick));
  const bool iinit_at_limit = bad == AbcBadIndex::InstanceIinitAtLimit;
  put_u30(out, iinit_at_limit ? kMethodCount : bad_or(bad, AbcBadIndex::InstanceIinit, 0));
  put_instance_traits(out, bad);
  put_u30(out, bad_or(bad, AbcBadIndex::ClassInit, 0));
  put_u30(out, 0);
  put_u30(out, bad == AbcBadIndex::ScriptInit ? 1 : 0);
  if (bad == AbcBadIndex::ScriptInit) {
    put_u30(out, kBadIndex);
    put_u30(out, 0);
  }
  put_u30(out, 3);
  put_body(out, bad_or(bad, AbcBadIndex::BodyMethod, 0), constructor_code, false, bad);
  // Constraint: the listener method takes a parameter, so its body needs two locals.
  put_body(out, 1, listener_code, false, bad, 2);
  put_body(out, 2, tiny_helper_code(), true, bad);
  return out;
}

inline std::vector<std::uint8_t> build_abc() {
  return build_abc(tiny_constructor_code(), tiny_listener_code());
}

inline std::vector<std::uint8_t> abc_header() {
  std::vector<std::uint8_t> out;
  put_u16(out, fo4::abc::kAbcMinorVersion);
  put_u16(out, fo4::abc::kAbcMajorVersion);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 1);
  return out;
}

inline std::vector<std::uint8_t> minimal_instance_file(std::uint8_t trait_tag,
                                               std::uint32_t metadata_index,
                                                std::uint8_t flags = 0) {
  std::vector<std::uint8_t> out = abc_header();
  put_u30(out, 2);
  put_u8(out, 0x16);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u8(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u8(out, flags);
  if ((flags & 0x08u) != 0u) {
    put_u30(out, 1);
  }
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u8(out, trait_tag);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, metadata_index);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  return out;
}

inline std::vector<std::uint8_t> multiname_file(std::uint32_t count,
                                         const std::vector<std::uint8_t>& entries) {
  std::vector<std::uint8_t> out = abc_header();
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, count);
  out.insert(out.end(), entries.begin(), entries.end());
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  return out;
}

}  // namespace fo4::test
