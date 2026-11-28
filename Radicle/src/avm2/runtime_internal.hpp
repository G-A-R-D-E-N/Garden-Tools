#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/value.hpp>

#include <cstdint>
#include <vector>

namespace fo4::avm2 {

TraitKind map_trait_kind(abc::AbcTraitKind kind);
Value trait_default_value(const abc::AbcFile& file, const abc::AbcTrait& trait);
void number_auto_slots(std::vector<Trait>& traits, std::uint32_t first);

}  // namespace fo4::avm2
