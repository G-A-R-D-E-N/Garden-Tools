#include <fo4/avm2/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace fo4::avm2 {

RuntimeStatus Runtime::build_shells_for_file(const abc::AbcFile& file) {
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
  return RuntimeStatus::Ok;
}

RuntimeStatus Runtime::build_class_shells() {
  for (const FileState& state : files_) {
    if (build_shells_for_file(*state.file) != RuntimeStatus::Ok) {
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
  if (build_shells_for_file(*state.file) != RuntimeStatus::Ok) {
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

}  // namespace fo4::avm2
