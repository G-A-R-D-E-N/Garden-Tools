#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fo4::avm2 {

enum class RuntimeStatus : std::uint8_t {
  Ok = 0,
  InvalidArgument,
  InvalidMultiname,
  DuplicateClass,
  UnresolvedClass,
  ClassCycle,
  InvalidTrait,
  AlreadyLinked,
  NotLinked,
};

const char* runtime_status_name(RuntimeStatus status) noexcept;

struct RuntimeLoad {
  RuntimeStatus status{RuntimeStatus::Ok};
  std::string message;

  bool ok() const noexcept { return status == RuntimeStatus::Ok; }
};

RuntimeStatus resolve_multiname(const abc::AbcFile& file, std::uint32_t index, Multiname& out);

struct ScriptEntry {
  const abc::AbcFile* file{nullptr};
  std::uint32_t init_method{0};
  std::uint32_t script_index{0};
};

class Runtime {
 public:
  Runtime();
  ~Runtime() = default;

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  RuntimeLoad add_file(const abc::AbcFile& file);
  RuntimeLoad link();

  bool linked() const noexcept { return linked_; }
  const std::vector<Class*>& classes() const noexcept { return classes_; }
  const std::vector<ScriptEntry>& scripts() const noexcept { return scripts_; }
  Object* global_object() const noexcept { return global_object_.get(); }
  FontRegistry& fonts() noexcept { return fonts_; }
  const FontRegistry& fonts() const noexcept { return fonts_; }

  const Class* find_class(const Multiname& multiname) const;
  const Class* find_class(std::string_view rendered_name) const;
  const Class* class_at(const abc::AbcFile& file, std::uint32_t instance_index) const;

  /* A linked runtime must create and register an external class on first unresolved use. */
  Class* resolve_or_create_global_class(const Multiname& multiname);

  /* Requires a linked runtime; gives a function object the Function builtin class so its methods
     resolve. A function created after link must call this explicitly. */
  void bind_function_class(FunctionObject& function);

  /* A linked runtime must create and cache the concrete Vector.<T> class on first use. */
  Class* resolve_vector_class(Class* element);

 private:
  struct FileState {
    const abc::AbcFile* file{nullptr};
  };

  Class* ensure_external_class(const Multiname& multiname);
  Class* add_builtin_class(std::string name, std::string package, Class* super);
  RuntimeStatus build_class_shells();
  RuntimeStatus resolve_hierarchy();
  RuntimeStatus build_traits();
  RuntimeStatus install_builtins();
  RuntimeStatus build_script_globals();
  void apply_class_slots();
  void build_prototypes();
  Class* class_for_instance(const abc::AbcFile& file, std::uint32_t instance_index) const;
  std::shared_ptr<FunctionObject> function_for_method(const abc::AbcFile& file,
                                                      std::uint32_t method_index,
                                                      std::string name);
  void register_class_global(Class& klass);

  std::vector<FileState> files_;
  std::vector<std::unique_ptr<Class>> owned_classes_;
  std::vector<Class*> classes_;
  std::unordered_map<std::string, Class*> class_by_key_;
  std::unordered_map<std::string, Class*> class_by_local_name_;
  std::unordered_map<std::string, Class*> class_by_instance_;
  std::unordered_map<const Class*, Class*> vector_classes_;
  std::vector<ScriptEntry> scripts_;
  std::shared_ptr<Object> global_object_;
  FontRegistry fonts_;
  bool linked_{false};
  bool failed_{false};
};

}  // namespace fo4::avm2
