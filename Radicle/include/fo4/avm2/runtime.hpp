#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/domain.hpp>
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
#include <string_view>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4::avm2 {

class DisplayObject;
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
Value constant_value(const abc::AbcFile& file, abc::AbcValueKind kind, std::uint32_t index);

struct ScriptEntry {
  const abc::AbcFile* file{nullptr};
  std::uint32_t init_method{0};
  std::uint32_t script_index{0};
  std::shared_ptr<Object> global;
};

class Runtime {
 public:
  Runtime();
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  /* A null domain must mean the current domain, so a plain add_file keeps its classes visible from root. */
  RuntimeLoad add_file(const abc::AbcFile& file,
                       std::shared_ptr<ApplicationDomainObject> domain = nullptr);
  RuntimeLoad link();
  /* Requires a linked runtime; links the file added at file_index and registers its scripts. */
  RuntimeLoad link_added_file(std::size_t file_index);
  std::size_t file_count() const noexcept { return files_.size(); }
  std::size_t class_count() const noexcept { return classes_.size(); }

  bool linked() const noexcept { return linked_; }
  const std::vector<Class*>& classes() const noexcept { return classes_; }
  const std::vector<ScriptEntry>& scripts() const noexcept { return scripts_; }
  Object* global_object() const noexcept { return global_object_.get(); }
  const std::shared_ptr<Object>& global_object_shared() const noexcept { return global_object_; }
  FontRegistry& fonts() noexcept { return *fonts_; }
  const FontRegistry& fonts() const noexcept { return *fonts_; }
  const std::shared_ptr<FontRegistry>& font_registry() const noexcept { return fonts_; }
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
  void track_frame_listener(const std::shared_ptr<DisplayObject>& object);
  void untrack_frame_listener(const DisplayObject* object);
  const std::vector<std::shared_ptr<DisplayObject>>& frame_listener_objects() const noexcept {
    return frame_listener_objects_;
  }

  const Class* find_class(const Multiname& multiname) const;
  const Class* find_class(std::string_view rendered_name) const;
  const Class* class_at(const abc::AbcFile& file, std::uint32_t instance_index) const;

  /* Requires a linked runtime; the root domain is created on first use and owns the builtins. */
  std::shared_ptr<ApplicationDomainObject> current_domain();
  /* Requires a domain of this runtime; a definition loaded into an unrelated domain stays hidden. */
  const Class* find_domain_definition(const ApplicationDomainObject& domain,
                                      std::string_view name) const;

  /* Requires a callable object; addCallback stores it so the host can invoke it by name. */
  void set_external_callback(std::string name, std::shared_ptr<Object> function) {
    external_callbacks_[std::move(name)] = std::move(function);
  }
  std::shared_ptr<Object> external_callback(std::string_view name) const {
    const auto found = external_callbacks_.find(std::string{name});
    return found == external_callbacks_.end() ? std::shared_ptr<Object>{} : found->second;
  }

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
    std::shared_ptr<ApplicationDomainObject> domain;
  };

  Class* ensure_external_class(const Multiname& multiname);
  Class* add_builtin_class(std::string name, std::string package, Class* super);
  RuntimeStatus build_class_shells();
  RuntimeStatus build_shells_for_file(const abc::AbcFile& file,
                                      const std::shared_ptr<ApplicationDomainObject>& domain);
  RuntimeStatus resolve_hierarchy();
  RuntimeStatus build_traits();
  RuntimeStatus install_builtins();
  RuntimeStatus install_system_builtins();
  RuntimeStatus install_bytearray_builtins();
  RuntimeStatus install_xml_builtins();
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
  std::vector<std::shared_ptr<FunctionObject>> script_functions_;
  std::shared_ptr<Object> global_object_;
  std::shared_ptr<FontRegistry> fonts_{std::make_shared<FontRegistry>()};
  HostBridge host_;
  SoundEventQueue sound_events_;
  std::set<std::uint32_t> pressed_keys_;
  std::map<std::uint32_t, SoundResource> sound_table_;
  std::uint32_t next_sound_id_{1};
  std::vector<std::shared_ptr<MovieClip>> movie_clips_;
  std::vector<std::shared_ptr<DisplayObject>> frame_listener_objects_;
  std::shared_ptr<ApplicationDomainObject> current_domain_;
  std::map<std::string, std::shared_ptr<Object>> external_callbacks_;
  bool linked_{false};
  bool failed_{false};
};

}  // namespace fo4::avm2
