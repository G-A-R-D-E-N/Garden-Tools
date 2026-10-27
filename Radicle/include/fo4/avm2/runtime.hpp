#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/host.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/sound.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace fo4::avm2 {

class MovieClip;

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
  /* Requires a linked runtime; links the file added at file_index and registers its scripts. */
  RuntimeLoad link_added_file(std::size_t file_index);
  std::size_t file_count() const noexcept { return files_.size(); }

  bool linked() const noexcept { return linked_; }
  const std::vector<Class*>& classes() const noexcept { return classes_; }
  const std::vector<ScriptEntry>& scripts() const noexcept { return scripts_; }
  Object* global_object() const noexcept { return global_object_.get(); }
  FontRegistry& fonts() noexcept { return fonts_; }
  const FontRegistry& fonts() const noexcept { return fonts_; }
  HostBridge& host() noexcept { return host_; }
  const HostBridge& host() const noexcept { return host_; }
  SoundEventQueue& sound_events() noexcept { return sound_events_; }
  /* The host must feed key state through the movie so Keyboard.isDown can answer. */
  void set_key_pressed(std::uint32_t key_code, bool pressed) {
    if (pressed) {
      pressed_keys_.insert(key_code);
    } else {
      pressed_keys_.erase(key_code);
    }
  }
  bool key_pressed(std::uint32_t key_code) const { return pressed_keys_.count(key_code) != 0; }
  const SoundEventQueue& sound_events() const noexcept { return sound_events_; }
  /* Requires a decoded resource; returns the runtime id the host plays by. */
  std::uint32_t register_sound(SoundResource resource);
  const SoundResource* find_sound(std::uint32_t id) const;
  /* A loaded movie's independent root clip must be advanced by the host frame driver. */
  void register_movie_clip(const std::shared_ptr<MovieClip>& clip);
  const std::vector<std::shared_ptr<MovieClip>>& movie_clips() const noexcept {
    return movie_clips_;
  }

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
    bool scripts_registered{false};
  };

  Class* ensure_external_class(const Multiname& multiname);
  Class* add_builtin_class(std::string name, std::string package, Class* super);
  RuntimeStatus build_class_shells();
  RuntimeStatus build_shells_for_file(const abc::AbcFile& file);
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
  HostBridge host_;
  SoundEventQueue sound_events_;
  std::set<std::uint32_t> pressed_keys_;
  std::map<std::uint32_t, SoundResource> sound_table_;
  std::uint32_t next_sound_id_{1};
  std::vector<std::shared_ptr<MovieClip>> movie_clips_;
  bool linked_{false};
  bool failed_{false};
};

}  // namespace fo4::avm2
