#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fo4::nif {

// Invariant: a shader-flag edit carries flag names, so no caller passes a bit index.
using EditValue = std::variant<std::string, float, std::vector<float>, std::vector<std::string>>;

struct Edit {
  std::uint32_t id{0};
  std::string key;
  EditValue value;
};

std::vector<std::byte> write_nif(const NifFile& nif);
std::vector<std::byte> apply_edits(const NifFile& nif, std::span<const Edit> edits);

// Requires a JSON array of {id, key, value} objects; value is text, one number, or a list of numbers or flag names.
std::vector<Edit> read_edits_json(std::string_view text);
std::vector<std::byte> apply_edits_json(const NifFile& nif, std::string_view text);

}  // namespace fo4::nif
