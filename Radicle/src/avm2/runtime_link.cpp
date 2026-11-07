#include <fo4/avm2/runtime.hpp>

#include "runtime_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace fo4::avm2 {

namespace {

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

}  // namespace

RuntimeStatus Runtime::build_shells_for_file(const abc::AbcFile& file,
                                              const std::shared_ptr<ApplicationDomainObject>& domain) {
  if (file.instances.size() != file.classes.size()) {
    return RuntimeStatus::InvalidTrait;
  }
  // A file without a domain must record its classes on the root, keeping them parent-visible.
  const std::shared_ptr<ApplicationDomainObject> table =
      domain != nullptr ? domain : current_domain();
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
        // A loaded sibling movie must not fail because it repeats a shared class such as the Scaleform
        // constants, so the first definition must win and this file's references must alias it.
        const auto existing = class_by_key_.find(key);
        if (existing != class_by_key_.end()) {
          class_by_instance_[instance_key(file, index)] = existing->second;
          table->define_local_definition(owned->name, existing->second);
          continue;
        }
        class_by_key_.emplace(key, klass);
      }
      if (name_multiname.has_fixed_name()) {
        class_by_local_name_.emplace(name_multiname.name, klass);
      }
    class_by_instance_[instance_key(file, index)] = klass;
    classes_.push_back(klass);
    table->define_local_definition(owned->name, klass);
    owned_classes_.push_back(std::move(owned));
  }
  return RuntimeStatus::Ok;
}

RuntimeStatus Runtime::build_class_shells() {
  for (const FileState& state : files_) {
    if (build_shells_for_file(*state.file, state.domain) != RuntimeStatus::Ok) {
      return RuntimeStatus::InvalidTrait;
    }
  }
  return RuntimeStatus::Ok;
}

RuntimeLoad Runtime::link_added_file(std::size_t file_index) {
  RuntimeLoad result;
  if (!linked_) {
    result.status = RuntimeStatus::NotLinked;
    result.message = "link_added_file before link";
    return result;
  }
  if (file_index >= files_.size()) {
    result.status = RuntimeStatus::InvalidArgument;
    result.message = "file index is out of range";
    return result;
  }
  FileState& state = files_[file_index];
  if (state.scripts_registered) {
    result.status = RuntimeStatus::AlreadyLinked;
    result.message = "file is already linked";
    return result;
  }
  if (build_shells_for_file(*state.file, state.domain) != RuntimeStatus::Ok) {
    result.status = RuntimeStatus::DuplicateClass;
    result.message = "a class name is defined twice";
    return result;
  }
  if (resolve_hierarchy() != RuntimeStatus::Ok) {
    result.status = RuntimeStatus::ClassCycle;
    result.message = "a class hierarchy does not terminate";
    return result;
  }
  build_prototypes();
  if (build_traits() != RuntimeStatus::Ok) {
    result.status = RuntimeStatus::InvalidTrait;
    result.message = "a trait does not resolve";
    return result;
  }
  apply_class_slots();
  if (build_script_globals() != RuntimeStatus::Ok) {
    result.status = RuntimeStatus::InvalidTrait;
    result.message = "a script trait does not resolve";
    return result;
  }
  return result;
}

RuntimeStatus Runtime::build_script_globals() {
  for (FileState& state : files_) {
    if (state.scripts_registered) {
      continue;
    }
    state.scripts_registered = true;
    const abc::AbcFile& file = *state.file;
    for (std::uint32_t script_index = 0; script_index < file.scripts.size(); ++script_index) {
      const abc::AbcScriptInfo& script = file.scripts[script_index];
      owned_classes_.push_back(std::make_unique<Class>());
      Class* scope_class = owned_classes_.back().get();
      std::shared_ptr<Object> global = std::make_shared<Object>();
      global->set_instance_class(scope_class);
      std::vector<Trait> traits;
      for (const abc::AbcTrait& source : script.traits) {
        const TraitKind kind = map_trait_kind(source.kind);
        if (kind != TraitKind::Slot && kind != TraitKind::Const && kind != TraitKind::Class &&
            kind != TraitKind::Method && kind != TraitKind::Function) {
          continue;
        }
        Trait trait;
        if (resolve_multiname(file, source.name, trait.multiname) != RuntimeStatus::Ok) {
          return RuntimeStatus::InvalidMultiname;
        }
        if (source.slot_id > kMaxSlotIndex) {
          return RuntimeStatus::InvalidTrait;
        }
        trait.file = &file;
        trait.kind = kind == TraitKind::Slot ? TraitKind::Slot : TraitKind::Const;
        trait.slot_id = source.slot_id;
        trait.has_default = true;
        switch (kind) {
          case TraitKind::Class: {
            Class* nested = class_for_instance(file, source.class_index);
            if (nested == nullptr || nested->class_object == nullptr) {
              return RuntimeStatus::InvalidTrait;
            }
            if (nested->script_global == nullptr) {
              nested->script_global = global;
            }
            trait.default_value = Value::object(nested->class_object);
            break;
          }
          case TraitKind::Method:
          case TraitKind::Function: {
            const std::uint32_t method_index =
                kind == TraitKind::Method ? source.method_index : source.class_index;
            std::shared_ptr<FunctionObject> function =
                function_for_method(file, method_index, file.method_name(method_index));
            if (function == nullptr) {
              return RuntimeStatus::InvalidTrait;
            }
            function->captured_scope().push_back(global);
            script_functions_.push_back(function);
            trait.default_value = Value::object(function);
            if (trait.multiname.has_fixed_name() && !trait.multiname.namespaces.empty()) {
              Property& property =
                  global_object_->define_own_property(primary_key(trait.multiname));
              property.kind = PropertyKind::Value;
              property.value = trait.default_value;
            }
            break;
          }
          default:
            trait.default_value = trait_default_value(file, source);
            break;
        }
        if (!trait.multiname.has_fixed_name() || trait.multiname.namespaces.empty()) {
          continue;
        }
        traits.push_back(std::move(trait));
      }
      number_auto_slots(traits, 0);
      scope_class->instance_traits = std::move(traits);
      scope_class->slot_count = scope_class->compute_slot_count();
      scope_class->apply_slot_defaults(*global);
      scripts_.push_back(ScriptEntry{&file, script.init, script_index, global});
    }
  }
  for (Class* klass : classes_) {
    register_class_global(*klass);
    klass->slot_count = klass->compute_slot_count();
  }
  return RuntimeStatus::Ok;
}

}  // namespace fo4::avm2
