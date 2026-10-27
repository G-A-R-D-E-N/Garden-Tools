#pragma once

#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/papyrus/runtime.hpp"

#include <vector>

struct radicle_papyrus_runtime {
  radicle_papyrus_runtime_callbacks callbacks{};
  std::vector<const radicle_pex*> pex_files;
  fo4::papyrus::Runtime runtime;

  radicle_papyrus_runtime() : runtime(std::vector<const fo4::pex::PexObject*>{}) {}
};
