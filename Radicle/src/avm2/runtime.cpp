#include <fo4/avm2/runtime.hpp>

#include "runtime_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

constexpr std::string_view kAs3BuiltinNamespace = "http://adobe.com/AS3/2006/builtin";

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

void assign_auto_slot_ids(Class& klass, std::unordered_set<Class*>& pending) {
  if (pending.erase(&klass) == 0) {
    return;
  }
  if (klass.super != nullptr) {
    assign_auto_slot_ids(*klass.super, pending);
  }
  const std::uint32_t inherited =
      klass.super == nullptr ? 0u : static_cast<std::uint32_t>(klass.super->compute_slot_count());
  number_auto_slots(klass.instance_traits, inherited);
  number_auto_slots(klass.class_traits, 0u);
}

}  // namespace

Value constant_value(const abc::AbcFile& file, abc::AbcValueKind kind, std::uint32_t index) {
  switch (kind) {
    case abc::AbcValueKind::Utf8:
      return Value::string(file.string_at(index));
    case abc::AbcValueKind::Int:
      if (index < file.pool.ints.size()) {
        return Value::int32(file.pool.ints[index]);
      }
      return Value::undefined();
    case abc::AbcValueKind::UInt:
      if (index < file.pool.uints.size()) {
        return Value::uint32(file.pool.uints[index]);
      }
      return Value::undefined();
    case abc::AbcValueKind::Double:
      if (index < file.pool.doubles.size()) {
        return Value::number(file.pool.doubles[index]);
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
    if (entry.kind == abc::AbcNamespaceKind::Namespace && name_space.name == kAs3BuiltinNamespace) {
      name_space = Namespace{};
    }
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
      out.attribute = multiname.kind == abc::AbcMultinameKind::RTQNameLA;
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

Runtime::~Runtime() {
  frame_listener_objects_.clear();
  for (const std::shared_ptr<FunctionObject>& function : script_functions_) {
    function->captured_scope().clear();
  }
}

std::uint32_t Runtime::register_sound(SoundResource resource) {
  const std::uint32_t id = next_sound_id_++;
  sound_table_.emplace(id, std::move(resource));
  return id;
}

const SoundResource* Runtime::find_sound(std::uint32_t id) const {
  const auto found = sound_table_.find(id);
  return found == sound_table_.end() ? nullptr : &found->second;
}

void Runtime::register_movie_clip(const std::shared_ptr<MovieClip>& clip) {
  if (clip != nullptr) {
    movie_clips_.push_back(clip);
  }
}

void Runtime::track_frame_listener(const std::shared_ptr<DisplayObject>& object) {
  if (object == nullptr) {
    return;
  }
  for (const std::shared_ptr<DisplayObject>& tracked : frame_listener_objects_) {
    if (tracked == object) {
      return;
    }
  }
  frame_listener_objects_.push_back(object);
}

void Runtime::untrack_frame_listener(const DisplayObject* object) {
  frame_listener_objects_.erase(
      std::remove_if(frame_listener_objects_.begin(), frame_listener_objects_.end(),
                     [object](const std::shared_ptr<DisplayObject>& tracked) {
                       return tracked.get() == object;
                     }),
      frame_listener_objects_.end());
}

RuntimeLoad Runtime::add_file(const abc::AbcFile& file,
                              std::shared_ptr<ApplicationDomainObject> domain) {
  RuntimeLoad result;
  if (failed_) {
    result.status = RuntimeStatus::AlreadyLinked;
    result.message = "add_file after a failed link";
    return result;
  }
  files_.push_back(FileState{&file, false, std::move(domain)});
  return result;
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

Class* Runtime::add_builtin_class(std::string name, std::string package, Class* super) {
  auto owned = std::make_unique<Class>();
  owned->name = package.empty() ? name : package + "::" + name;
  owned->name_multiname.kind = MultinameKind::QName;
  owned->name_multiname.name = name;
  owned->name_multiname.namespaces.push_back(Namespace{NamespaceKind::Package, package});
  owned->super = super;
  owned->external = false;
  owned->linked = true;
  Class* klass = owned.get();
  class_by_key_.emplace(class_key(NamespaceKind::Package, package, name), klass);
  class_by_local_name_.emplace(name, klass);
  owned_classes_.push_back(std::move(owned));
  classes_.push_back(klass);
  return klass;
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
    if (klass->external || klass->file == nullptr) {
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
  auto function = std::make_shared<FunctionObject>(std::move(name), &file, method_index);
  bind_function_class(*function);
  return function;
}

Class* Runtime::resolve_vector_class(Class* element) {
  const auto cached = vector_classes_.find(element);
  if (cached != vector_classes_.end()) {
    return cached->second;
  }
  Class* base = nullptr;
  if (const Class* found = find_class("__AS3__.vec::Vector")) {
    base = const_cast<Class*>(found);
  }
  if (base == nullptr) {
    return nullptr;
  }
  auto owned = std::make_unique<Class>();
  owned->name = element == nullptr ? base->name + ".<*>" : base->name + ".<" + element->name + ">";
  owned->name_multiname = base->name_multiname;
  owned->super = base;
  owned->external = false;
  owned->linked = true;
  owned->vector_element = element;
  Class* klass = owned.get();
  klass->prototype_object = std::make_shared<Object>();
  klass->prototype_object->set_prototype(base->prototype_object);
  klass->class_object = std::make_shared<ClassObject>(klass);
  klass->class_object->set_prototype(base->class_object);
  klass->slot_count = klass->compute_slot_count();
  vector_classes_.emplace(element, klass);
  owned_classes_.push_back(std::move(owned));
  return klass;
}

void Runtime::bind_function_class(FunctionObject& function) {
  const Class* function_class = find_class("Function");
  if (function_class != nullptr) {
    function.set_instance_class(const_cast<Class*>(function_class));
  }
}

RuntimeStatus Runtime::build_traits() {
  std::unordered_set<Class*> pending;
  for (Class* klass : classes_) {
    if (klass->external || klass->file == nullptr || klass->traits_built) {
      continue;
    }
    klass->traits_built = true;
    pending.insert(klass);
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
          if (source.slot_id > kMaxSlotIndex) {
            return RuntimeStatus::InvalidTrait;
          }
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
  for (Class* klass : classes_) {
    assign_auto_slot_ids(*klass, pending);
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
  if (install_builtins() != RuntimeStatus::Ok) {
    return fail(RuntimeStatus::InvalidTrait, "the builtin class table does not install");
  }
  if (install_bytearray_builtins() != RuntimeStatus::Ok) {
    return fail(RuntimeStatus::InvalidTrait, "the flash.utils classes do not install");
  }
  if (install_xml_builtins() != RuntimeStatus::Ok) {
    return fail(RuntimeStatus::InvalidTrait, "the XML classes do not install");
  }
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
