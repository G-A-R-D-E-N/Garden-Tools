#pragma once

#include "fo4/papyrus_source/token.hpp"

#include <string>

namespace fo4::psc {

LexResult lex(const std::string& source);

}  // namespace fo4::psc
