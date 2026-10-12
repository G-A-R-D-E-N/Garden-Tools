#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace fo4::text {

std::string windows1252_to_utf8(std::string_view bytes);
std::optional<std::string> utf8_to_windows1252(std::string_view utf8);
bool is_valid_utf8(std::string_view utf8) noexcept;

std::string fold(std::string_view utf8);
int compare_folded(std::string_view a, std::string_view b) noexcept;
bool equals_folded(std::string_view a, std::string_view b) noexcept;
std::size_t hash_folded(std::string_view text) noexcept;

}
