#pragma once

#include "fo4/pex/pex.hpp"

#include <string>
#include <unordered_map>

struct radicle_pex {
  fo4::pex::PexFile value;
  std::unordered_map<std::string, size_t> strings;
};
