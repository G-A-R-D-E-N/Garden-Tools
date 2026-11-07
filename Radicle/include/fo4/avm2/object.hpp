#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fo4::avm2 {

inline constexpr std::uint32_t kMaxSlotIndex = 65535;

struct Class;
class FunctionObject;
struct Trait;

enum class ObjectKind : std::uint8_t {
  Object = 0,
  Function,
  Class,
  Array,
  Error,
  Namespace,
  Display,
  Event,
  ByteArray,
  Xml,
};

const char* object_kind_name(ObjectKind kind) noexcept;

enum class PropertyKind : std::uint8_t {
  Value = 0,
  Accessor,
};

struct Property {
  PropertyKind kind{PropertyKind::Value};
  Value value;
  std::shared_ptr<FunctionObject> getter;
  std::shared_ptr<FunctionObject> setter;

  bool has_getter() const noexcept { return getter != nullptr; }
  bool has_setter() const noexcept { return setter != nullptr; }
};

enum class LookupKind : std::uint8_t {
  Missing = 0,
  Dynamic,
  Slot,
  Method,
  Accessor,
};

struct PropertyLookup {
  LookupKind kind{LookupKind::Missing};
  Class* owner{nullptr};
  const Trait* trait{nullptr};
  const Property* property{nullptr};
  std::size_t slot{0};
  Value value;
  std::shared_ptr<FunctionObject> getter;
  std::shared_ptr<FunctionObject> setter;

  bool found() const noexcept { return kind != LookupKind::Missing; }
};

struct EventListener {
  std::string type;
  std::shared_ptr<FunctionObject> function;
  bool use_capture{false};
  std::int32_t priority{0};
};

class Object : public std::enable_shared_from_this<Object> {
 public:
  Object() = default;
  explicit Object(ObjectKind kind);
  virtual ~Object() = default;

  Object(const Object&) = delete;
  Object& operator=(const Object&) = delete;

  ObjectKind kind() const noexcept { return kind_; }
  bool is_dynamic() const noexcept { return dynamic_; }
  void set_dynamic(bool dynamic) noexcept { dynamic_ = dynamic; }

  Class* instance_class() const noexcept { return instance_class_; }
  void set_instance_class(Class* instance_class) noexcept { instance_class_ = instance_class; }

  Class* class_scope() const noexcept { return class_scope_; }
  void set_class_scope(Class* class_scope) noexcept { class_scope_ = class_scope; }

  bool has_primitive() const noexcept { return has_primitive_; }
  const Value& primitive() const noexcept { return primitive_; }
  void set_primitive(Value value);

  const std::shared_ptr<Object>& prototype() const noexcept { return prototype_; }
  void set_prototype(std::shared_ptr<Object> prototype);

  std::vector<EventListener>& event_listeners() noexcept { return event_listeners_; }
  const std::vector<EventListener>& event_listeners() const noexcept { return event_listeners_; }

  std::unordered_map<const FunctionObject*, std::weak_ptr<FunctionObject>>&
  bound_methods() noexcept {
    return bound_methods_;
  }

  const std::vector<Value>& slots() const noexcept { return slots_; }
  std::vector<Value>& slots() noexcept { return slots_; }
  void resize_slots(std::size_t count);

  const std::unordered_map<Key, Property, KeyHash>& own_properties() const noexcept {
    return properties_;
  }
  const Property* find_own_property(const Key& key) const;
  Property* find_own_property(const Key& key);
  Property& define_own_property(const Key& key);
  bool erase_own_property(const Key& key);

  PropertyLookup lookup(const Multiname& multiname) const;
  bool has_property(const Multiname& multiname) const;

  bool read_value(const Multiname& multiname, Value& out) const;
  bool write_value(const Multiname& multiname, const Value& value);
  bool delete_property(const Multiname& multiname);

  bool is_callable() const noexcept { return kind_ == ObjectKind::Function; }
  bool is_constructor() const noexcept { return kind_ == ObjectKind::Class; }

 private:
  ObjectKind kind_{ObjectKind::Object};
  bool dynamic_{true};
  Class* instance_class_{nullptr};
  Class* class_scope_{nullptr};
  bool has_primitive_{false};
  Value primitive_;
  std::shared_ptr<Object> prototype_;
  std::vector<Value> slots_;
  std::vector<EventListener> event_listeners_;
  std::unordered_map<const FunctionObject*, std::weak_ptr<FunctionObject>> bound_methods_;
  std::unordered_map<Key, Property, KeyHash> properties_;
};

inline constexpr std::uint32_t kNoMethod = 0xFFFFFFFFu;
inline constexpr std::uint32_t kNoNative = 0xFFFFFFFFu;

class FunctionObject : public Object {
 public:
  FunctionObject(std::string name, const abc::AbcFile* file, std::uint32_t method_index);

  const std::string& name() const noexcept { return name_; }
  const abc::AbcFile* file() const noexcept { return file_; }
  std::uint32_t method_index() const noexcept { return method_index_; }
  const abc::AbcMethodBody* body() const;
  const abc::AbcMethodInfo* method_info() const;

  bool is_native() const noexcept { return body() == nullptr; }

  std::uint32_t param_count() const;
  std::string return_type_name() const;

  std::uint32_t native_id() const noexcept { return native_id_; }
  void set_native_id(std::uint32_t native_id) noexcept { native_id_ = native_id; }

  Class* owner_class() const noexcept { return owner_class_; }
  void set_owner_class(Class* owner_class) noexcept { owner_class_ = owner_class; }

  const std::vector<std::shared_ptr<Object>>& captured_scope() const noexcept {
    return captured_scope_;
  }
  std::vector<std::shared_ptr<Object>>& captured_scope() noexcept { return captured_scope_; }

  const std::shared_ptr<Object>& bound_receiver() const noexcept { return bound_receiver_; }
  void bind_receiver(std::shared_ptr<Object> receiver) { bound_receiver_ = std::move(receiver); }

 private:
  std::string name_;
  const abc::AbcFile* file_{nullptr};
  std::uint32_t method_index_{kNoMethod};
  std::uint32_t native_id_{kNoNative};
  Class* owner_class_{nullptr};
  std::vector<std::shared_ptr<Object>> captured_scope_;
  std::shared_ptr<Object> bound_receiver_;
};

class ClassObject : public Object {
 public:
  explicit ClassObject(Class* represented_class);

  Class* represented_class() const noexcept { return represented_class_; }

 private:
  Class* represented_class_{nullptr};
};

class ArrayObject : public Object {
 public:
  ArrayObject();

  const std::vector<Value>& elements() const noexcept { return elements_; }
  std::vector<Value>& elements() noexcept { return elements_; }

  Class* element_class() const noexcept { return element_class_; }
  void set_element_class(Class* element_class) noexcept { element_class_ = element_class; }

 private:
  std::vector<Value> elements_;
  Class* element_class_{nullptr};
};

class ErrorObject : public Object {
 public:
  ErrorObject(std::string error_name, std::string message);

  const std::string& error_name() const noexcept { return error_name_; }
  const std::string& message() const noexcept { return message_; }

 private:
  std::string error_name_;
  std::string message_;
};

}  // namespace fo4::avm2
