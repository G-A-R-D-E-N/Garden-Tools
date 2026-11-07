#pragma once

#include "c_api_papyrus_internal.hpp"
#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/papyrus/runtime.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct radicle_papyrus_runtime {
  radicle_papyrus_runtime_callbacks callbacks{};
  radicle_papyrus_engine_callbacks engine{};
  std::vector<const radicle_pex*> pex_files;
  fo4::papyrus::Runtime runtime;

  radicle_papyrus_runtime() : runtime(std::vector<const fo4::pex::PexObject*>{}) {}
};

struct radicle_papyrus_native_call {
  radicle_papyrus_runtime* runtime{nullptr};
  uint32_t flags{0};
  std::string script;
  std::string function;
  std::vector<radicle_papyrus_value> arguments;
  std::optional<radicle_papyrus_object> self;
  std::optional<uint64_t> token;
  std::optional<radicle_papyrus_value> result;
  radicle_papyrus_status failure{RADICLE_PAPYRUS_STATUS_OK};
};
