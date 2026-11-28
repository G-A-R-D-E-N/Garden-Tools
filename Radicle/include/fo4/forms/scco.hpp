#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace fo4::forms {

class SccoError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Constraint: a Scene Layout entry is one SNAM scene link plus its 8-byte XNAM coordinate,
// Constraint: so this adds both at the end of the leading pair run or changes no bytes at all. Returns false when the SCCO record is missing.
bool add_scco_scene(Plugin& plugin,
                    const FormKey& scco,
                    const FormKey& scene,
                    std::int32_t x,
                    std::int32_t y);

// Constraint: scene_index counts SNAM/XNAM pairs in file order from the run start, so
// Constraint: removing one pair leaves the VNAM markers and Original Coordinates tail untouched. Returns false when the SCCO record or scene pair is missing.
bool remove_scco_scene(Plugin& plugin, const FormKey& scco, std::size_t scene_index);

}  // namespace fo4::forms
