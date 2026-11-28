#pragma once

#include "c_api_papyrus_internal.hpp"
#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/papyrus/runtime.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace radicle_c_papyrus_detail {

// Invariant: the one owner decoding every runtime export uses; null for an empty base key and for a reference owner without a discriminator.
std::optional<fo4::papyrus::OwnerId> ownerFrom(radicle_papyrus_owner_kind kind, const char* base, const char* discriminator);

}  // namespace radicle_c_papyrus_detail

struct radicle_papyrus_runtime {
  radicle_papyrus_runtime_callbacks callbacks{};
  radicle_papyrus_engine_callbacks engine{};
  void* castUser{nullptr};
  int32_t (*castHandler)(void* user, const radicle_papyrus_object* object, const char* target_type, uint64_t* out_instance){nullptr};
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
