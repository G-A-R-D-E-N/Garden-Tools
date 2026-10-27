#include <fo4/avm2/class.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

void add_constant(Class& klass, const char* name, Value value, std::uint32_t& next_slot) {
  Trait trait;
  trait.multiname = builtin_name(name);
  trait.kind = TraitKind::Const;
  trait.slot_id = next_slot;
  next_slot += 1;
  trait.has_default = true;
  trait.default_value = std::move(value);
  klass.class_traits.push_back(std::move(trait));
}

void add_static_method(Class& klass, const char* name, native::Id id) {
  auto function = std::make_shared<FunctionObject>(name, nullptr, kNoMethod);
  function->set_native_id(static_cast<std::uint32_t>(id));
  function->set_owner_class(&klass);
  Trait trait;
  trait.multiname = builtin_name(name);
  trait.kind = TraitKind::Method;
  trait.function = std::move(function);
  klass.class_traits.push_back(std::move(trait));
}

ExecutionResult enumerate_fonts(Interpreter& interpreter) {
  return native_value(Value::object(make_array_value(interpreter, {})));
}

}  // namespace

RuntimeStatus Runtime::install_system_builtins() {
  Class* object_class = const_cast<Class*>(find_class("Object"));
  Class* event_dispatcher = const_cast<Class*>(find_class("flash.events::EventDispatcher"));
  Class* text_auto_size = add_builtin_class("TextFieldAutoSize", "flash.text", object_class);
  Class* capabilities = add_builtin_class("Capabilities", "flash.system", object_class);
  Class* security = add_builtin_class("Security", "flash.system", object_class);
  Class* security_domain = add_builtin_class("SecurityDomain", "flash.system", object_class);
  Class* font = add_builtin_class("Font", "flash.text", object_class);
  Class* filter_quality = add_builtin_class("BitmapFilterQuality", "flash.filters", object_class);
  Class* event_dispatcher_interface =
      add_builtin_class("IEventDispatcher", "flash.events", nullptr);
  if (object_class == nullptr || event_dispatcher == nullptr || text_auto_size == nullptr ||
      capabilities == nullptr || security == nullptr || security_domain == nullptr ||
      font == nullptr || filter_quality == nullptr || event_dispatcher_interface == nullptr) {
    return RuntimeStatus::InvalidTrait;
  }

  event_dispatcher_interface->is_interface = true;
  event_dispatcher->interfaces.push_back(event_dispatcher_interface);

  std::uint32_t auto_size_slot = 0;
  add_constant(*text_auto_size, "NONE", Value::string("none"), auto_size_slot);
  add_constant(*text_auto_size, "LEFT", Value::string("left"), auto_size_slot);
  add_constant(*text_auto_size, "RIGHT", Value::string("right"), auto_size_slot);
  add_constant(*text_auto_size, "CENTER", Value::string("center"), auto_size_slot);

  std::uint32_t capabilities_slot = 0;
  add_constant(*capabilities, "language", Value::string("en"), capabilities_slot);
  add_constant(*capabilities, "os", Value::string("Windows"), capabilities_slot);
  add_constant(*capabilities, "version", Value::string("WIN 32,0,0,0"), capabilities_slot);
  add_constant(*capabilities, "playerType", Value::string("StandAlone"), capabilities_slot);
  add_constant(*capabilities, "isDebugger", Value::boolean(false), capabilities_slot);

  std::uint32_t security_slot = 0;
  add_constant(*security, "sandboxType", Value::string("application"), security_slot);
  add_constant(*security, "LOCAL_TRUSTED", Value::string("localTrusted"), security_slot);
  add_constant(*security, "LOCAL_WITH_FILE", Value::string("localWithFile"), security_slot);
  add_constant(*security, "LOCAL_WITH_NETWORK", Value::string("localWithNetwork"), security_slot);
  add_constant(*security, "REMOTE", Value::string("remote"), security_slot);
  add_constant(*security, "APPLICATION", Value::string("application"), security_slot);
  add_static_method(*security, "allowDomain", native::Id::compat_undefined);
  add_static_method(*security, "allowInsecureDomain", native::Id::compat_undefined);
  add_static_method(*security, "loadPolicyFile", native::Id::compat_undefined);

  add_static_method(*font, "registerFont", native::Id::compat_undefined);
  add_static_method(*font, "enumerateFonts", native::Id::font_enumerate_fonts);

  std::uint32_t filter_quality_slot = 0;
  add_constant(*filter_quality, "LOW", Value::int32(1), filter_quality_slot);
  add_constant(*filter_quality, "MEDIUM", Value::int32(2), filter_quality_slot);
  add_constant(*filter_quality, "HIGH", Value::int32(3), filter_quality_slot);
  return RuntimeStatus::Ok;
}

bool try_system_native(Interpreter& interpreter, native::Id id, const Value& self,
                       const std::vector<Value>& arguments, std::size_t depth,
                       ExecutionResult& out) {
  (void)self;
  (void)arguments;
  (void)depth;
  if (id != native::Id::font_enumerate_fonts) {
    return false;
  }
  out = enumerate_fonts(interpreter);
  return true;
}

}  // namespace fo4::avm2
