#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace fo4::avm2 {

enum class TraitKind : std::uint8_t {
  Slot = 0,
  Method = 1,
  Getter = 2,
  Setter = 3,
  Class = 4,
  Function = 5,
  Const = 6,
};

const char* trait_kind_name(TraitKind kind) noexcept;

struct Trait {
  Multiname multiname;
  TraitKind kind{TraitKind::Slot};
  bool is_final{false};
  bool is_override{false};
  bool has_metadata{false};
  bool has_default{false};
  Value default_value;
  std::uint32_t slot_id{0};
  std::uint32_t type_multiname_index{0};
  std::string type_name;
  std::uint32_t dispatch_id{0};
  std::uint32_t method_index{0};
  std::uint32_t class_trait_index{0};
  std::uint32_t function_trait_index{0};
  std::shared_ptr<FunctionObject> function;
  std::shared_ptr<ClassObject> nested_class;
  std::vector<std::uint32_t> metadata;
  const abc::AbcFile* file{nullptr};

  bool is_slot_like() const noexcept { return kind == TraitKind::Slot || kind == TraitKind::Const; }
  bool is_method_like() const noexcept {
    return kind == TraitKind::Method || kind == TraitKind::Getter || kind == TraitKind::Setter;
  }
};

bool trait_matches(const Trait& trait, const Multiname& access) noexcept;

enum class ClassInitializationState : std::uint8_t {
  Uninitialized = 0,
  Initializing,
  Initialized,
};

struct Class {
  std::string name;
  Multiname name_multiname;
  const abc::AbcFile* file{nullptr};
  std::uint32_t instance_index{0};
  Class* super{nullptr};
  std::uint32_t super_index{0};
  std::vector<Class*> interfaces;
  std::vector<std::uint32_t> interface_indices;
  std::vector<Trait> instance_traits;
  std::vector<Trait> class_traits;
  std::uint32_t iinit{kNoMethod};
  std::uint32_t cinit{kNoMethod};
  bool is_sealed{false};
  bool is_final{false};
  bool is_interface{false};

  bool external{false};
  ClassInitializationState initialization{ClassInitializationState::Uninitialized};
  std::size_t slot_count{0};
  std::size_t class_slot_count{0};
  std::shared_ptr<Object> prototype_object;
  std::shared_ptr<ClassObject> class_object;
  bool linked{false};

  const Trait* find_own_instance_trait(const Multiname& multiname) const;
  const Trait* find_own_class_trait(const Multiname& multiname) const;
  const Trait* resolve_instance_trait(const Multiname& multiname) const;
  const Trait* resolve_class_trait(const Multiname& multiname) const;

  const Trait* find_slot_trait(std::uint32_t slot_id) const;

  std::size_t compute_slot_count() const;

  void apply_slot_defaults(Object& instance) const;

  std::size_t compute_class_slot_count() const;

  void apply_class_slot_defaults(Object& class_object) const;

  bool is_subclass_of(const Class* other) const;
  bool implements_interface(const Class* other) const;

  std::uint32_t method_index_for(const Trait& trait) const;
};

}  // namespace fo4::avm2
