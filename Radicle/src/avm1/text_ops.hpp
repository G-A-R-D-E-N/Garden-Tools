#pragma once

#include <fo4/avm1/interpreter.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fo4::avm1::text_ops {

inline constexpr std::uint8_t kCodePointVersion = 6;

std::uint32_t first_code_point(std::string_view text);
std::string encode_code_unit(std::uint32_t unit);
std::string to_text(const Avm1Data& data, std::uint8_t swf_version);
std::size_t unit_count(std::string_view text, std::uint8_t swf_version);
std::size_t unit_offset(std::string_view text, std::size_t units, std::uint8_t swf_version);

}  // namespace fo4::avm1::text_ops
