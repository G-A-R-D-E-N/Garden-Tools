#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace fo4::nif {

using EditValue = std::variant<std::string, float, std::vector<float>>;

struct Edit {
  std::uint32_t id{0};
  std::string key;
  EditValue value;
};

std::vector<std::byte> write_nif(const NifFile& nif);
std::vector<std::byte> apply_edits(const NifFile& nif, std::span<const Edit> edits);

}  // namespace fo4::nif
