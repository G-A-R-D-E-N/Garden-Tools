#include <fo4/avm2/class.hpp>

#include <algorithm>
#include <deque>
#include <unordered_set>

namespace fo4::avm2 {

const char* trait_kind_name(TraitKind kind) noexcept {
  switch (kind) {
    case TraitKind::Slot:
      return "slot";
    case TraitKind::Method:
      return "method";
    case TraitKind::Getter:
      return "getter";
    case TraitKind::Setter:
      return "setter";
    case TraitKind::Class:
      return "class";
    case TraitKind::Function:
      return "function";
    case TraitKind::Const:
      return "const";
  }
  return "unknown";
}

bool trait_matches(const Trait& trait, const Multiname& access) noexcept {
  if (access.has_fixed_name() && trait.multiname.has_fixed_name() &&
      access.name != trait.multiname.name) {
    return false;
  }
  if (!access.has_fixed_namespace()) {
    return true;
  }
  if (trait.multiname.namespaces.empty()) {
    for (const Namespace& candidate : access.namespaces) {
      if (candidate == Namespace{}) {
        return true;
      }
    }
    return false;
  }
  for (const Namespace& candidate : access.namespaces) {
    for (const Namespace& declared : trait.multiname.namespaces) {
      if (candidate == declared) {
        return true;
      }
    }
  }
  return false;
}

const Trait* Class::find_own_instance_trait(const Multiname& multiname) const {
  for (const Trait& trait : instance_traits) {
    if (trait_matches(trait, multiname)) {
      return &trait;
    }
  }
  return nullptr;
}

const Trait* Class::find_own_class_trait(const Multiname& multiname) const {
  for (const Trait& trait : class_traits) {
    if (trait_matches(trait, multiname)) {
      return &trait;
    }
  }
  return nullptr;
}

const Trait* Class::resolve_instance_trait(const Multiname& multiname) const {
  for (const Class* klass = this; klass != nullptr; klass = klass->super) {
    if (const Trait* trait = klass->find_own_instance_trait(multiname)) {
      return trait;
    }
  }
  return nullptr;
}

const Trait* Class::resolve_class_trait(const Multiname& multiname) const {
  for (const Class* klass = this; klass != nullptr; klass = klass->super) {
    if (const Trait* trait = klass->find_own_class_trait(multiname)) {
      return trait;
    }
  }
  return nullptr;
}

const Trait* Class::find_slot_trait(std::uint32_t slot_id) const {
  for (const Class* klass = this; klass != nullptr; klass = klass->super) {
    for (const Trait& trait : klass->instance_traits) {
      if (trait.is_slot_like() && trait.slot_id == slot_id) {
        return &trait;
      }
    }
  }
  return nullptr;
}

std::size_t Class::compute_slot_count() const {
  std::size_t count = 0;
  for (const Class* klass = this; klass != nullptr; klass = klass->super) {
    for (const Trait& trait : klass->instance_traits) {
      if (trait.is_slot_like()) {
        count = std::max<std::size_t>(count, static_cast<std::size_t>(trait.slot_id) + 1);
      }
    }
  }
  return count;
}

bool Class::is_subclass_of(const Class* other) const {
  for (const Class* klass = this; klass != nullptr; klass = klass->super) {
    if (klass == other) {
      return true;
    }
  }
  return false;
}

bool Class::implements_interface(const Class* other) const {
  for (const Class* klass = this; klass != nullptr; klass = klass->super) {
    std::deque<const Class*> pending(klass->interfaces.begin(), klass->interfaces.end());
    std::unordered_set<const Class*> seen;
    while (!pending.empty()) {
      const Class* candidate = pending.front();
      pending.pop_front();
      if (candidate == nullptr || !seen.insert(candidate).second) {
        continue;
      }
      if (candidate == other) {
        return true;
      }
      pending.insert(pending.end(), candidate->interfaces.begin(), candidate->interfaces.end());
      if (candidate->super != nullptr) {
        pending.push_back(candidate->super);
      }
    }
  }
  return false;
}

std::uint32_t Class::method_index_for(const Trait& trait) const { return trait.method_index; }

void Class::apply_slot_defaults(Object& instance) const {
  instance.resize_slots(slot_count);
  std::vector<const Class*> chain;
  for (const Class* klass = this; klass != nullptr; klass = klass->super) {
    chain.push_back(klass);
  }
  for (auto klass = chain.rbegin(); klass != chain.rend(); ++klass) {
    for (const Trait& trait : (*klass)->instance_traits) {
      if (trait.is_slot_like() && trait.has_default && trait.slot_id < slot_count) {
        instance.slots()[trait.slot_id] = trait.default_value;
      }
    }
  }
}

}  // namespace fo4::avm2
