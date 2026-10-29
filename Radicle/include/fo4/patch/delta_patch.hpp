#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin_key.hpp"

#include <cstddef>
#include <stdexcept>

namespace fo4::patch {

class DeltaPatchError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct DeltaPatchCounts {
  std::size_t kept{0};
  std::size_t removed{0};
};

struct DeltaPatchResult {
  Plugin patch;
  DeltaPatchCounts counts;
};

DeltaPatchResult build_delta_patch(const env::Environment& env,
                                   const PluginKey& older,
                                   const PluginKey& newer);

}  // namespace fo4::patch
