#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace fo4::patch {

class ListMergeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct ListMergeCounts {
  std::size_t merged{0};
  std::size_t unchanged{0};
};

struct ListMergeResult {
  Plugin patch;
  ListMergeCounts counts;
};

ListMergeResult build_list_merge_patch(const env::Environment& env,
                                       Signature signature,
                                       std::string_view patch_name);

}  // namespace fo4::patch
