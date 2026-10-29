#pragma once

#include "fo4/pex/pex.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

struct radicle_pex {
  fo4::pex::PexFile value;
  std::unordered_map<std::string, size_t> strings;
};

inline std::unique_ptr<radicle_pex> make_pex_handle(fo4::pex::PexFile value) {
  auto handle = std::make_unique<radicle_pex>();
  handle->value = std::move(value);
  for (size_t index = 0; index < handle->value.stringTable.size(); ++index) {
    handle->strings.emplace(handle->value.stringTable[index], index);
  }
  return handle;
}
