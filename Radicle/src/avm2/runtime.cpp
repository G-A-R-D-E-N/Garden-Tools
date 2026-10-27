#include <fo4/avm2/runtime.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

NamespaceKind map_namespace_kind(abc::AbcNamespaceKind kind) {
  switch (kind) {
    case abc::AbcNamespaceKind::Private:
      return NamespaceKind::Private;
    case abc::AbcNamespaceKind::Namespace:
      return NamespaceKind::Namespace;
    case abc::AbcNamespaceKind::Package:
      return NamespaceKind::Package;
    case abc::AbcNamespaceKind::PackageInternal:
      return NamespaceKind::PackageInternal;
    case abc::AbcNamespaceKind::Protected:
      return NamespaceKind::Protected;
    case abc::AbcNamespaceKind::Explicit:
      return NamespaceKind::Explicit;
    case abc::AbcNamespaceKind::StaticProtected:
      return NamespaceKind::StaticProtected;
  }
  return NamespaceKind::Namespace;
}

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

std::string class_key(NamespaceKind kind, std::string_view name_space, std::string_view local_name) {
  std::string key;
  key += static_cast<char>(static_cast<std::uint8_t>(kind));
  key += '\x1f';
  key += name_space;
  key += '\x1f';
  key += local_name;
  return key;
}

std::string key_for_multiname(const Multiname& multiname) {
  if (multiname.namespaces.empty() || !multiname.has_fixed_name()) {
    return {};
  }
  const Namespace& name_space = multiname.namespaces.front();
  return class_key(name_space.kind, name_space.name, multiname.name);
}

std::string instance_key(const abc::AbcFile& file, std::uint32_t instance_index) {
  std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(&file));
  key += '#';
  key += std::to_string(instance_index);
  return key;
}

Value trait_default_value(const abc::AbcFile& file, const abc::AbcTrait& trait) {
  if (trait.value_index == 0) {
    return Value::undefined();
  }
  switch (trait.value_kind) {
    case abc::AbcValueKind::Utf8:
      return Value::string(file.string_at(trait.value_index));
    case abc::AbcValueKind::Int:
      if (trait.value_index < file.pool.ints.size()) {
        return Value::int32(file.pool.ints[trait.value_index]);
      }
      return Value::undefined();
    case abc::AbcValueKind::UInt:
      if (trait.value_index < file.pool.uints.size()) {
        return Value::uint32(file.pool.uints[trait.value_index]);
      }
      return Value::undefined();
    case abc::AbcValueKind::Double:
      if (trait.value_index < file.pool.doubles.size()) {
        return Value::number(file.pool.doubles[trait.value_index]);
      }
      return Value::undefined();
    case abc::AbcValueKind::True:
      return Value::boolean(true);
    case abc::AbcValueKind::False:
      return Value::boolean(false);
    case abc::AbcValueKind::Null:
      return Value::null();
    default:
      return Value::undefined();
  }
}

}  // namespace

const char* runtime_status_name(RuntimeStatus status) noexcept {
  switch (status) {
    case RuntimeStatus::Ok:
      return "ok";
    case RuntimeStatus::InvalidArgument:
      return "invalid argument";
    case RuntimeStatus::InvalidMultiname:
      return "invalid multiname";
    case RuntimeStatus::DuplicateClass:
      return "duplicate class";
    case RuntimeStatus::UnresolvedClass:
      return "unresolved class";
    case RuntimeStatus::ClassCycle:
      return "class cycle";
    case RuntimeStatus::InvalidTrait:
      return "invalid trait";
    case RuntimeStatus::AlreadyLinked:
      return "already linked";
    case RuntimeStatus::NotLinked:
      return "not linked";
  }
  return "unknown";
}

RuntimeStatus resolve_multiname(const abc::AbcFile& file, std::uint32_t index, Multiname& out) {
  out = Multiname{};
  if (index >= file.pool.multinames.size()) {
    return RuntimeStatus::InvalidMultiname;
  }
  const abc::AbcMultiname& multiname = file.pool.multinames[index];
  const auto resolve_namespace = [&file](std::uint32_t namespace_index, Namespace& name_space) {
    if (namespace_index >= file.pool.namespaces.size()) {
      return false;
    }
    const abc::AbcNamespace& entry = file.pool.namespaces[namespace_index];
    name_space.kind = map_namespace_kind(entry.kind);
    name_space.name = file.namespace_at(namespace_index);
    return true;
  };
  const auto resolve_set = [&file, &resolve_namespace](std::uint32_t set_index,
                                                       std::vector<Namespace>& out_namespaces) {
    if (set_index >= file.pool.ns_sets.size()) {
      return false;
    }
    for (const std::uint32_t namespace_index : file.pool.ns_sets[set_index].namespaces) {
      Namespace name_space;
      if (!resolve_namespace(namespace_index, name_space)) {
        return false;
      }
      out_namespaces.push_back(std::move(name_space));
    }
    return true;
  };
  switch (multiname.kind) {
    case abc::AbcMultinameKind::QName:
    case abc::AbcMultinameKind::QNameA: {
      out.kind = MultinameKind::QName;
      out.name = file.string_at(multiname.name);
      out.attribute = multiname.kind == abc::AbcMultinameKind::QNameA;
      Namespace name_space;
      if (!resolve_namespace(multiname.ns, name_space)) {
        return RuntimeStatus::InvalidMultiname;
      }
      out.namespaces.push_back(std::move(name_space));
      return RuntimeStatus::Ok;
    }
    case abc::AbcMultinameKind::RTQName:
    case abc::AbcMultinameKind::RTQNameA:
      out.kind = MultinameKind::RuntimeNamespace;
      out.name = file.string_at(multiname.name);
      out.attribute = multiname.kind == abc::AbcMultinameKind::RTQNameA;
      return RuntimeStatus::Ok;
    case abc::AbcMultinameKind::RTQNameL:
    case abc::AbcMultinameKind::RTQNameLA:
      out.kind = MultinameKind::Dynamic;
      return RuntimeStatus::Ok;
    case abc::AbcMultinameKind::Multiname:
    case abc::AbcMultinameKind::MultinameA:
      out.kind = MultinameKind::QNameSet;
      out.name = file.string_at(multiname.name);
      out.attribute = multiname.kind == abc::AbcMultinameKind::MultinameA;
      if (!resolve_set(multiname.ns_set, out.namespaces)) {
        return RuntimeStatus::InvalidMultiname;
      }
      return RuntimeStatus::Ok;
    case abc::AbcMultinameKind::MultinameL:
    case abc::AbcMultinameKind::MultinameLA:
      out.kind = MultinameKind::RuntimeName;
      out.attribute = multiname.kind == abc::AbcMultinameKind::MultinameLA;
      if (!resolve_set(multiname.ns_set, out.namespaces)) {
        return RuntimeStatus::InvalidMultiname;
      }
      return RuntimeStatus::Ok;
    case abc::AbcMultinameKind::TypeName:
      out.kind = MultinameKind::TypeName;
      out.name = file.multiname_at(multiname.name);
      for (const std::uint32_t parameter : multiname.params) {
        out.type_parameters.push_back(file.multiname_at(parameter));
      }
      return RuntimeStatus::Ok;
  }
  return RuntimeStatus::InvalidMultiname;
}

Runtime::Runtime() : global_object_(std::make_shared<Object>()) {}

RuntimeLoad Runtime::add_file(const abc::AbcFile& file) {
  RuntimeLoad result;
  if (linked_ || failed_) {
    result.status = RuntimeStatus::AlreadyLinked;
    result.message = "add_file after link";
    return result;
  }
  files_.push_back(FileState{&file});
  return result;
}

RuntimeStatus Runtime::build_class_shells() {
  for (const FileState& state : files_) {
    const abc::AbcFile& file = *state.file;
    if (file.instances.size() != file.classes.size()) {
      return RuntimeStatus::InvalidTrait;
    }
    for (std::uint32_t index = 0; index < file.instances.size(); ++index) {
      Multiname name_multiname;
      if (resolve_multiname(file, file.instances[index].name, name_multiname) !=
          RuntimeStatus::Ok) {
        return RuntimeStatus::InvalidMultiname;
      }
      auto owned = std::make_unique<Class>();
      owned->name_multiname = name_multiname;
      owned->name = render_multiname(name_multiname);
      owned->file = &file;
      owned->instance_index = index;
      owned->super_index = file.instances[index].super_index;
      owned->interface_indices = file.instances[index].interfaces;
      owned->iinit = file.instances[index].iinit;
      owned->cinit = file.classes[index].cinit;
      owned->is_sealed = file.instances[index].is_sealed();
      owned->is_final = file.instances[index].is_final();
      owned->is_interface = file.instances[index].is_interface();
      Class* klass = owned.get();
      const std::string key = key_for_multiname(name_multiname);
      if (!key.empty()) {
        const auto inserted = class_by_key_.emplace(key, klass);
        if (!inserted.second) {
          return RuntimeStatus::DuplicateClass;
        }
      }
      if (name_multiname.has_fixed_name()) {
        class_by_local_name_.emplace(name_multiname.name, klass);
      }
      class_by_instance_[instance_key(file, index)] = klass;
      classes_.push_back(klass);
      owned_classes_.push_back(std::move(owned));
    }
  }
  return RuntimeStatus::Ok;
}

const Class* Runtime::find_class(const Multiname& multiname) const {
  if (!multiname.has_fixed_name()) {
    return nullptr;
  }
  if (multiname.has_fixed_namespace()) {
    for (const Namespace& name_space : multiname.namespaces) {
      const auto found = class_by_key_.find(class_key(name_space.kind, name_space.name,
                                                      multiname.name));
      if (found != class_by_key_.end()) {
        return found->second;
      }
    }
    return nullptr;
  }
  const auto found = class_by_local_name_.find(multiname.name);
  return found == class_by_local_name_.end() ? nullptr : found->second;
}

const Class* Runtime::class_at(const abc::AbcFile& file, std::uint32_t instance_index) const {
  return class_for_instance(file, instance_index);
}

Class* Runtime::resolve_or_create_global_class(const Multiname& multiname) {
  if (!multiname.has_fixed_name()) {
    return nullptr;
  }
  Class* klass = nullptr;
  if (const Class* found = find_class(multiname)) {
    klass = const_cast<Class*>(found);
  } else {
    klass = ensure_external_class(multiname);
  }
  if (klass == nullptr) {
    return nullptr;
  }
  if (klass->class_object == nullptr) {
    klass->prototype_object = std::make_shared<Object>();
    klass->class_object = std::make_shared<ClassObject>(klass);
    klass->slot_count = klass->compute_slot_count();
    klass->linked = true;
  }
  register_class_global(*klass);
  return klass;
}

void Runtime::apply_class_slots() {
  for (Class* klass : classes_) {
    klass->class_slot_count = klass->compute_class_slot_count();
    if (klass->class_object != nullptr) {
      klass->apply_class_slot_defaults(*klass->class_object);
    }
  }
}

const Class* Runtime::find_class(std::string_view rendered_name) const {
  for (const Class* klass : classes_) {
    if (std::string_view{klass->name} == rendered_name) {
      return klass;
    }
  }
  return nullptr;
}

Class* Runtime::ensure_external_class(const Multiname& multiname) {
  if (multiname.has_fixed_name()) {
    if (multiname.has_fixed_namespace()) {
      for (const Namespace& name_space : multiname.namespaces) {
        const auto found =
            class_by_key_.find(class_key(name_space.kind, name_space.name, multiname.name));
        if (found != class_by_key_.end()) {
          return found->second;
        }
      }
    } else {
      const auto found = class_by_local_name_.find(multiname.name);
      if (found != class_by_local_name_.end()) {
        return found->second;
      }
    }
  }
  auto owned = std::make_unique<Class>();
  owned->name_multiname = multiname;
  owned->name = render_multiname(multiname);
  owned->external = true;
  owned->linked = true;
  owned->is_sealed = false;
  owned->is_final = false;
  owned->is_interface = false;
  Class* klass = owned.get();
  const std::string key = key_for_multiname(multiname);
  if (!key.empty()) {
    class_by_key_.emplace(key, klass);
  }
  if (multiname.has_fixed_name()) {
    class_by_local_name_.emplace(multiname.name, klass);
  }
  classes_.push_back(klass);
  owned_classes_.push_back(std::move(owned));
  return klass;
}

RuntimeStatus Runtime::resolve_hierarchy() {
  for (std::size_t index = 0; index < classes_.size(); ++index) {
    Class* klass = classes_[index];
    if (klass->external) {
      continue;
    }
    const abc::AbcFile& file = *klass->file;
    Multiname super_multiname;
    if (resolve_multiname(file, klass->super_index, super_multiname) != RuntimeStatus::Ok) {
      return RuntimeStatus::InvalidMultiname;
    }
    klass->super = ensure_external_class(super_multiname);
    klass->interfaces.clear();
    for (const std::uint32_t interface_index : klass->interface_indices) {
      Multiname interface_multiname;
      if (resolve_multiname(file, interface_index, interface_multiname) != RuntimeStatus::Ok) {
        return RuntimeStatus::InvalidMultiname;
      }
      klass->interfaces.push_back(ensure_external_class(interface_multiname));
    }
  }
  for (Class* klass : classes_) {
    std::vector<const Class*> walk;
    const Class* current = klass;
    std::size_t guard = 0;
    while (current != nullptr) {
      if (std::find(walk.begin(), walk.end(), current) != walk.end()) {
        return RuntimeStatus::ClassCycle;
      }
      walk.push_back(current);
      if (walk.size() > classes_.size() + 1) {
        return RuntimeStatus::ClassCycle;
      }
      current = current->super;
      ++guard;
    }
  }
  return RuntimeStatus::Ok;
}

void Runtime::build_prototypes() {
  for (Class* klass : classes_) {
    if (klass->prototype_object == nullptr) {
      klass->prototype_object = std::make_shared<Object>();
    }
    if (klass->class_object == nullptr) {
      klass->class_object = std::make_shared<ClassObject>(klass);
    }
    klass->slot_count = klass->compute_slot_count();
  }
  for (Class* klass : classes_) {
    if (klass->super == nullptr) {
      continue;
    }
    klass->prototype_object->set_prototype(klass->super->prototype_object);
    klass->class_object->set_prototype(klass->super->class_object);
  }
  for (Class* klass : classes_) {
    klass->linked = true;
  }
}

std::shared_ptr<FunctionObject> Runtime::function_for_method(const abc::AbcFile& file,
                                                             std::uint32_t method_index,
                                                             std::string name) {
  if (method_index >= file.methods.size()) {
    return nullptr;
  }
  return std::make_shared<FunctionObject>(std::move(name), &file, method_index);
}

RuntimeStatus Runtime::build_traits() {
  for (Class* klass : classes_) {
    if (klass->external) {
      continue;
    }
    const abc::AbcFile& file = *klass->file;
    const auto build_trait = [this, &file, klass](const abc::AbcTrait& source, Trait& out) {
      if (resolve_multiname(file, source.name, out.multiname) != RuntimeStatus::Ok) {
        return RuntimeStatus::InvalidMultiname;
      }
      out.kind = map_trait_kind(source.kind);
      out.is_final = source.is_final();
      out.is_override = source.is_override();
      out.has_metadata = source.has_metadata();
      out.metadata = source.metadata;
      out.file = &file;
      switch (out.kind) {
        case TraitKind::Slot:
        case TraitKind::Const:
          out.slot_id = source.slot_id;
          out.type_multiname_index = source.type_name;
          out.type_name = file.multiname_at(source.type_name);
          out.has_default = source.value_index != 0;
          if (out.has_default) {
            out.default_value = trait_default_value(file, source);
          }
          break;
        case TraitKind::Class: {
          out.class_trait_index = source.class_index;
          const Class* nested = class_for_instance(file, source.class_index);
          if (nested == nullptr || nested->class_object == nullptr) {
            return RuntimeStatus::InvalidTrait;
          }
          out.nested_class = nested->class_object;
          break;
        }
        case TraitKind::Function:
          out.function_trait_index = source.class_index;
          out.function = function_for_method(file, source.class_index,
                                             file.method_name(source.class_index));
          if (out.function == nullptr) {
            return RuntimeStatus::InvalidTrait;
          }
          break;
        case TraitKind::Method:
        case TraitKind::Getter:
        case TraitKind::Setter:
          out.dispatch_id = source.disp_id;
          out.method_index = source.method_index;
          out.function = function_for_method(file, source.method_index,
                                             file.method_name(source.method_index));
          if (out.function == nullptr) {
            return RuntimeStatus::InvalidTrait;
          }
          out.function->set_owner_class(klass);
          break;
      }
      return RuntimeStatus::Ok;
    };
    const auto& instance = file.instances[klass->instance_index];
    for (const abc::AbcTrait& source : instance.traits) {
      Trait trait;
      const RuntimeStatus status = build_trait(source, trait);
      if (status != RuntimeStatus::Ok) {
        return status;
      }
      klass->instance_traits.push_back(std::move(trait));
    }
    for (const abc::AbcTrait& source : file.classes[klass->instance_index].traits) {
      Trait trait;
      const RuntimeStatus status = build_trait(source, trait);
      if (status != RuntimeStatus::Ok) {
        return status;
      }
      klass->class_traits.push_back(std::move(trait));
    }
  }
  return RuntimeStatus::Ok;
}

void Runtime::register_class_global(Class& klass) {
  if (klass.name_multiname.has_fixed_name() && !klass.name_multiname.namespaces.empty()) {
    Property& property = global_object_->define_own_property(primary_key(klass.name_multiname));
    property.kind = PropertyKind::Value;
    property.value = Value::object(klass.class_object);
  }
}

RuntimeStatus Runtime::build_script_globals() {
  for (const FileState& state : files_) {
    const abc::AbcFile& file = *state.file;
    for (std::uint32_t script_index = 0; script_index < file.scripts.size(); ++script_index) {
      const abc::AbcScriptInfo& script = file.scripts[script_index];
      scripts_.push_back(ScriptEntry{&file, script.init, script_index});
      for (const abc::AbcTrait& source : script.traits) {
        Trait trait;
        if (resolve_multiname(file, source.name, trait.multiname) != RuntimeStatus::Ok) {
          return RuntimeStatus::InvalidMultiname;
        }
        trait.kind = map_trait_kind(source.kind);
        Value value = Value::undefined();
        switch (trait.kind) {
          case TraitKind::Class: {
            const Class* nested = class_for_instance(file, source.class_index);
            if (nested == nullptr) {
              return RuntimeStatus::InvalidTrait;
            }
            value = Value::object(nested->class_object);
            break;
          }
          case TraitKind::Function: {
            std::shared_ptr<FunctionObject> function =
                function_for_method(file, source.class_index,
                                    file.method_name(source.class_index));
            if (function == nullptr) {
              return RuntimeStatus::InvalidTrait;
            }
            value = Value::object(function);
            break;
          }
          case TraitKind::Slot:
          case TraitKind::Const:
            value = trait_default_value(file, source);
            break;
          default:
            break;
        }
        if (!trait.multiname.has_fixed_name() || trait.multiname.namespaces.empty()) {
          continue;
        }
        Property& property = global_object_->define_own_property(primary_key(trait.multiname));
        property.kind = PropertyKind::Value;
        property.value = value;
      }
    }
  }
  for (Class* klass : classes_) {
    register_class_global(*klass);
  }
  return RuntimeStatus::Ok;
}

Class* Runtime::class_for_instance(const abc::AbcFile& file, std::uint32_t instance_index) const {
  const auto found = class_by_instance_.find(instance_key(file, instance_index));
  return found == class_by_instance_.end() ? nullptr : found->second;
}

RuntimeLoad Runtime::link() {
  RuntimeLoad result;
  if (linked_ || failed_) {
    result.status = RuntimeStatus::AlreadyLinked;
    result.message = "link called twice";
    return result;
  }
  const auto fail = [this, &result](RuntimeStatus status, std::string message) {
    result.status = status;
    result.message = std::move(message);
    owned_classes_.clear();
    classes_.clear();
    class_by_key_.clear();
    class_by_local_name_.clear();
    class_by_instance_.clear();
    scripts_.clear();
    global_object_ = std::make_shared<Object>();
    failed_ = true;
    return result;
  };
  if (build_class_shells() != RuntimeStatus::Ok) {
    return fail(RuntimeStatus::DuplicateClass, "a class name is defined twice");
  }
  if (resolve_hierarchy() != RuntimeStatus::Ok) {
    return fail(RuntimeStatus::ClassCycle, "a class hierarchy does not terminate");
  }
  build_prototypes();
  if (build_traits() != RuntimeStatus::Ok) {
    return fail(RuntimeStatus::InvalidTrait, "a trait does not resolve");
  }
  apply_class_slots();
  if (build_script_globals() != RuntimeStatus::Ok) {
    return fail(RuntimeStatus::InvalidTrait, "a script trait does not resolve");
  }
  linked_ = true;
  return result;
}

}  // namespace fo4::avm2
