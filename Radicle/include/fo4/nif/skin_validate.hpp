#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {

enum class SkinFindingLevel : std::uint8_t { error, warning };

struct SkinFinding {
  SkinFindingLevel level{SkinFindingLevel::error};
  std::string where;
  std::string what;
};

struct SkinReport {
  std::uint32_t block{0};
  std::size_t bones{0};
  std::size_t bind_transforms{0};
  std::size_t used_bones{0};
  // Invariant: the findings come back errors first, in the order BGS SkinValidator.Check emits them.
  std::vector<SkinFinding> findings;
  // Invariant: only an error makes the skin unusable, so ok() ignores warnings.
  bool ok() const noexcept;
  std::size_t errors() const noexcept;
  std::size_t warnings() const noexcept;
};

// Requires a tri-shape block and the shapes already read from nif.
SkinReport validate_skin(const NifFile& nif, const Shape& shape);

}  // namespace fo4::nif
