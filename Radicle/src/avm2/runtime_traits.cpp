#include "runtime_internal.hpp"

#include <fo4/avm2/runtime.hpp>

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace fo4::avm2 {

TraitKind map_trait_kind(abc::AbcTraitKind kind) {
  switch (kind) {
    case abc::AbcTraitKind::Slot:
      return TraitKind::Slot;
    case abc::AbcTraitKind::Method:
      return TraitKind::Method;
    case abc::AbcTraitKind::Getter:
      return TraitKind::Getter;
    case abc::AbcTraitKind::Setter:
      return TraitKind::Setter;
    case abc::AbcTraitKind::Class:
      return TraitKind::Class;
    case abc::AbcTraitKind::Function:
      return TraitKind::Function;
    case abc::AbcTraitKind::Const:
      return TraitKind::Const;
  }
  return TraitKind::Slot;
}

Value trait_default_value(const abc::AbcFile& file, const abc::AbcTrait& trait) {
  if (trait.value_index == 0) {
    return Value::undefined();
  }
  return constant_value(file, trait.value_kind, trait.value_index);
}

void number_auto_slots(std::vector<Trait>& traits, std::uint32_t first) {
  std::unordered_set<std::uint32_t> taken;
  for (const Trait& trait : traits) {
    if (trait.is_slot_like() && trait.slot_id != 0) {
      taken.insert(trait.slot_id);
    }
  }
  std::uint32_t next = first;
  for (Trait& trait : traits) {
    if (!trait.is_slot_like() || trait.slot_id != 0) {
      continue;
    }
    while (taken.count(next) != 0) {
      ++next;
    }
    trait.slot_id = next;
    taken.insert(next);
    ++next;
  }
}

}  // namespace fo4::avm2
