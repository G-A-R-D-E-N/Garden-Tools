#include <fo4/avm2/object.hpp>

#include <fo4/avm2/class.hpp>

#include <algorithm>
#include <utility>

namespace fo4::avm2 {

namespace {

const Trait* find_getter_or_setter(const Class& owner, const Trait& trait, bool getter) {
  const TraitKind wanted = getter ? TraitKind::Getter : TraitKind::Setter;
  for (const Trait& candidate : owner.instance_traits) {
    if (candidate.kind != wanted) {
      continue;
    }
    if (candidate.multiname.name == trait.multiname.name &&
        candidate.multiname.namespaces == trait.multiname.namespaces) {
      return &candidate;
    }
  }
  return nullptr;
}

}  // namespace

const char* object_kind_name(ObjectKind kind) noexcept {
  switch (kind) {
    case ObjectKind::Object:
      return "object";
    case ObjectKind::Function:
      return "function";
    case ObjectKind::Class:
      return "class";
    case ObjectKind::Array:
      return "array";
    case ObjectKind::Error:
      return "error";
    case ObjectKind::Namespace:
      return "namespace";
  }
  return "unknown";
}

Object::Object() = default;

Object::Object(ObjectKind kind) : kind_(kind) {}

void Object::set_prototype(std::shared_ptr<Object> prototype) {
  prototype_ = std::move(prototype);
}

void Object::resize_slots(std::size_t count) { slots_.resize(count); }

const Property* Object::find_own_property(const Key& key) const {
  const auto found = dynamic_.find(key);
  return found == dynamic_.end() ? nullptr : &found->second;
}

Property* Object::find_own_property(const Key& key) {
  const auto found = dynamic_.find(key);
  return found == dynamic_.end() ? nullptr : &found->second;
}

Property& Object::define_own_property(const Key& key) { return dynamic_[key]; }

bool Object::erase_own_property(const Key& key) { return dynamic_.erase(key) != 0; }

PropertyLookup Object::lookup(const Multiname& multiname) const {
  PropertyLookup result;
  for (const Object* current = this; current != nullptr; current = current->prototype_.get()) {
    for (const auto& [key, property] : current->dynamic_) {
      if (!key_matches(multiname, key)) {
        continue;
      }
      result.property = &property;
      if (property.kind == PropertyKind::Accessor) {
        result.kind = LookupKind::Accessor;
        result.getter = property.getter;
        result.setter = property.setter;
      } else {
        result.kind = LookupKind::Dynamic;
        result.value = property.value;
      }
      return result;
    }
    Class* owning_class = current->instance_class();
    if (owning_class == nullptr) {
      continue;
    }
    for (Class* klass = owning_class; klass != nullptr; klass = klass->super) {
      for (const Trait& trait : klass->instance_traits) {
        if (!trait_matches(trait, multiname)) {
          continue;
        }
        result.owner = klass;
        result.trait = &trait;
        if (trait.is_slot_like()) {
          result.kind = LookupKind::Slot;
          result.slot = trait.slot_id;
          if (trait.slot_id < current->slots_.size()) {
            result.value = current->slots_[trait.slot_id];
          }
          return result;
        }
        if (trait.kind == TraitKind::Method) {
          result.kind = LookupKind::Method;
          return result;
        }
        if (trait.kind == TraitKind::Getter || trait.kind == TraitKind::Setter) {
          result.kind = LookupKind::Accessor;
          const Trait* getter = trait.kind == TraitKind::Getter
                                    ? &trait
                                    : find_getter_or_setter(*klass, trait, true);
          const Trait* setter = trait.kind == TraitKind::Setter
                                    ? &trait
                                    : find_getter_or_setter(*klass, trait, false);
          if (getter != nullptr) {
            result.getter = getter->function;
          }
          if (setter != nullptr) {
            result.setter = setter->function;
          }
          return result;
        }
        if (trait.kind == TraitKind::Class || trait.kind == TraitKind::Function) {
          result.kind = LookupKind::Slot;
          result.slot = trait.slot_id;
          if (trait.nested_class != nullptr) {
            result.value = Value::object(trait.nested_class);
          } else if (trait.function != nullptr) {
            result.value = Value::object(trait.function);
          }
          return result;
        }
      }
    }
  }
  return result;
}

bool Object::has_property(const Multiname& multiname) const { return lookup(multiname).found(); }

bool Object::read_value(const Multiname& multiname, Value& out) const {
  const PropertyLookup result = lookup(multiname);
  if (result.kind == LookupKind::Dynamic || result.kind == LookupKind::Slot) {
    out = result.value;
    return true;
  }
  return false;
}

bool Object::write_value(const Multiname& multiname, const Value& value) {
  for (Object* current = this; current != nullptr; current = current->prototype_.get()) {
    for (auto& [key, property] : current->dynamic_) {
      if (!key_matches(multiname, key)) {
        continue;
      }
      if (property.kind != PropertyKind::Value) {
        return false;
      }
      property.value = value;
      return true;
    }
  }
  const PropertyLookup result = lookup(multiname);
  if (result.kind == LookupKind::Slot) {
    if (result.trait != nullptr && result.trait->kind == TraitKind::Const) {
      return false;
    }
    if (result.slot < slots_.size()) {
      slots_[result.slot] = value;
      return true;
    }
    return false;
  }
  if (result.kind == LookupKind::Accessor || result.kind == LookupKind::Method) {
    return false;
  }
  if (!is_dynamic() || !multiname.has_fixed_name()) {
    return false;
  }
  Property& property = define_own_property(primary_key(multiname));
  property.kind = PropertyKind::Value;
  property.value = value;
  return true;
}

bool Object::delete_property(const Multiname& multiname) {
  const PropertyLookup result = lookup(multiname);
  if (result.kind == LookupKind::Dynamic && result.property != nullptr) {
    for (auto entry = dynamic_.begin(); entry != dynamic_.end(); ++entry) {
      if (&entry->second == result.property) {
        dynamic_.erase(entry);
        return true;
      }
    }
    return false;
  }
  if (result.kind == LookupKind::Missing) {
    return true;
  }
  return false;
}

FunctionObject::FunctionObject(std::string name, const abc::AbcFile* file,
                               std::uint32_t method_index)
    : Object(ObjectKind::Function), name_(std::move(name)), file_(file), method_index_(method_index) {
  set_dynamic(true);
}

const abc::AbcMethodBody* FunctionObject::body() const {
  if (file_ == nullptr || method_index_ == kNoMethod) {
    return nullptr;
  }
  return file_->body_for(method_index_);
}

const abc::AbcMethodInfo* FunctionObject::method_info() const {
  if (file_ == nullptr || method_index_ >= file_->methods.size()) {
    return nullptr;
  }
  return &file_->methods[method_index_];
}

std::uint32_t FunctionObject::param_count() const {
  const abc::AbcMethodInfo* info = method_info();
  return info == nullptr ? 0 : info->param_count;
}

std::string FunctionObject::return_type_name() const {
  const abc::AbcMethodInfo* info = method_info();
  if (info == nullptr || file_ == nullptr) {
    return {};
  }
  return file_->multiname_at(info->return_type);
}

ClassObject::ClassObject(Class* represented_class)
    : Object(ObjectKind::Class), represented_class_(represented_class) {
  set_dynamic(true);
}

ArrayObject::ArrayObject() : Object(ObjectKind::Array) { set_dynamic(true); }

ErrorObject::ErrorObject(std::string error_name, std::string message)
    : Object(ObjectKind::Error), error_name_(std::move(error_name)), message_(std::move(message)) {
  set_dynamic(true);
}

}  // namespace fo4::avm2
