#pragma once

#include "fo4/as3/lexer.hpp"

#include <string_view>

namespace fo4::as3::detail {

bool is_assignment_operator(std::string_view text);
int binary_precedence(const Token& token);
bool is_unary_operator(const Token& token);
bool is_property_name(const Token& token);
bool is_literal_key(const Token& token);
bool is_keyword_modifier(const Token& token);
bool is_contextual_modifier(const Token& token);

}  // namespace fo4::as3::detail
