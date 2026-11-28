#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::abc::detail::text {

std::string quote(std::string_view value);
std::string kind_token(AbcNamespaceKind kind);
std::string kind_token(AbcMultinameKind kind);
std::string kind_token(AbcTraitKind kind);
std::string kind_token(AbcValueKind kind);

/* Reference helpers must append the original index so an unedited round trip restores it. */
std::string string_ref(const AbcFile& file, std::uint32_t index);
std::string namespace_ref(const AbcFile& file, std::uint32_t index);
std::string ns_set_ref(const AbcFile& file, std::uint32_t index);
std::string multiname_ref(const AbcFile& file, std::uint32_t index);
std::string method_ref(const AbcFile& file, std::uint32_t index);
std::string class_ref(const AbcFile& file, std::uint32_t index);
std::string int_ref(const AbcFile& file, std::uint32_t index);
std::string uint_ref(const AbcFile& file, std::uint32_t index);
std::string double_ref(const AbcFile& file, std::uint32_t index);
std::string value_ref(const AbcFile& file, AbcValueKind kind, std::uint32_t index);

std::string render_traits(const AbcFile& file, const std::vector<AbcTrait>& traits);
std::string render_body(const AbcFile& file, const AbcMethodBody& body, std::size_t index);

}  // namespace fo4::abc::detail::text
