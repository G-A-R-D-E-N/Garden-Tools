#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/host.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/net.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/sound.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include "native_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

LoaderObject::LoaderObject(Class* display_class) : DisplayObjectContainer(display_class) {}

SoundObject::SoundObject(Class* sound_class) : Object(ObjectKind::Object) {
  if (sound_class != nullptr) {
    set_instance_class(sound_class);
    if (sound_class->prototype_object != nullptr) {
      set_prototype(sound_class->prototype_object);
    }
  }
  set_dynamic(true);
}

SoundChannelObject::SoundChannelObject(Class* channel_class) : Object(ObjectKind::Object) {
  if (channel_class != nullptr) {
    set_instance_class(channel_class);
    if (channel_class->prototype_object != nullptr) {
      set_prototype(channel_class->prototype_object);
    }
  }
  set_dynamic(true);
}

namespace {

Class* class_from_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return nullptr;
  }
  return static_cast<const ClassObject&>(*self.as_object()).represented_class();
}

std::string dynamic_string(const Object& object, std::string_view name) {
  Multiname multiname;
  multiname.name = std::string{name};
  multiname.namespaces.push_back(Namespace{});
  Value value;
  if (!object.read_value(multiname, value) || !value.is_string()) {
    return {};
  }
  return value.as_string();
}

void define_dynamic_string(Interpreter& interpreter, Object& object, const char* name,
                           std::string text) {
  (void)interpreter;
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  Property& property = object.define_own_property(primary_key(multiname));
  property.kind = PropertyKind::Value;
  property.value = Value::string(std::move(text));
}

std::shared_ptr<Object> value_object(const Value& value) {
  return value.is_object() ? value.as_object() : nullptr;
}

bool is_instance_of_class(const Object& object, std::string_view class_name) {
  const Class* klass = object.instance_class();
  return klass != nullptr && klass->name == class_name;
}

SoundObject* sound_self(const Value& self) {
  if (!self.is_object() || !is_instance_of_class(*self.as_object(), "flash.media::Sound")) {
    return nullptr;
  }
  return static_cast<SoundObject*>(self.as_object().get());
}

SoundChannelObject* channel_self(const Value& self) {
  if (!self.is_object() ||
      !is_instance_of_class(*self.as_object(), "flash.media::SoundChannel")) {
    return nullptr;
  }
  return static_cast<SoundChannelObject*>(self.as_object().get());
}

ExecutionResult instance_construct(Interpreter& interpreter, const Value& self) {
  Class* klass = class_from_self(self);
  if (klass == nullptr) {
    return native_value(Value::null());
  }
  return native_value(Value::object(interpreter.make_instance(*klass)));
}

ApplicationDomainObject* domain_self(const Value& self) {
  if (!self.is_object() ||
      !is_instance_of_class(*self.as_object(), "flash.system::ApplicationDomain")) {
    return nullptr;
  }
  return dynamic_cast<ApplicationDomainObject*>(self.as_object().get());
}

Value dynamic_value(const Object& object, std::string_view name) {
  Multiname multiname;
  multiname.name = std::string{name};
  multiname.namespaces.push_back(Namespace{});
  Value value;
  if (!object.read_value(multiname, value)) {
    return Value::undefined();
  }
  return value;
}

std::shared_ptr<ApplicationDomainObject> context_domain(const Value& context) {
  const std::shared_ptr<Object> object = value_object(context);
  if (object == nullptr) {
    return nullptr;
  }
  return std::dynamic_pointer_cast<ApplicationDomainObject>(
      value_object(dynamic_value(*object, "applicationDomain")));
}

ExecutionResult app_domain_construct(Interpreter& interpreter, const Value& self,
                                     const std::vector<Value>& arguments) {
  Class* klass = class_from_self(self);
  if (klass == nullptr) {
    return native_value(Value::null());
  }
  std::shared_ptr<ApplicationDomainObject> parent =
      std::dynamic_pointer_cast<ApplicationDomainObject>(value_object(argument_at(arguments, 0)));
  if (parent == nullptr) {
    parent = interpreter.runtime().current_domain();
  }
  return native_value(
      Value::object(std::make_shared<ApplicationDomainObject>(klass, std::move(parent))));
}

ExecutionResult ext_available(Interpreter& interpreter, const Value& self) {
  (void)self;
  return native_value(Value::boolean(interpreter.runtime().host().available()));
}

ExecutionResult ext_call(Interpreter& interpreter, const Value& self,
                         const std::vector<Value>& arguments) {
  (void)self;
  std::string method = arguments.empty() ? std::string{} : to_string(arguments[0]);
  std::vector<std::string> forwarded;
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    forwarded.push_back(to_string(arguments[index]));
  }
  const std::optional<std::string> answer =
      interpreter.runtime().host().call_host(method, std::move(forwarded));
  return native_value(answer.has_value() ? Value::string(*answer) : Value::null());
}

ExecutionResult ext_add_callback(Interpreter& interpreter, const Value& self,
                                 const std::vector<Value>& arguments) {
  (void)self;
  const std::shared_ptr<Object> function = value_object(argument_at(arguments, 1));
  if (function != nullptr && function->is_callable()) {
    interpreter.runtime().set_external_callback(to_string(argument_at(arguments, 0)), function);
  }
  return native_value(Value::undefined());
}

ExecutionResult definition_by_name(Interpreter& interpreter, const Value& self,
                                   const std::vector<Value>& arguments) {
  (void)self;
  const std::string name = to_string(argument_at(arguments, 0));
  const Class* klass = interpreter.runtime().find_class(name);
  if (klass == nullptr) {
    return native_value(Value::null());
  }
  return native_value(Value::object(klass->class_object));
}

std::string qualified_name(const Value& value, bool superclass) {
  Class* klass = nullptr;
  if (value.is_object()) {
    if (value.as_object()->kind() == ObjectKind::Class) {
      klass = static_cast<const ClassObject&>(*value.as_object()).represented_class();
    } else {
      klass = value.as_object()->instance_class();
    }
  }
  if (klass == nullptr) {
    return {};
  }
  if (superclass) {
    return klass->super == nullptr ? std::string{} : klass->super->name;
  }
  return klass->name;
}

ExecutionResult qualified_class_name(const Value& value, bool superclass) {
  return native_value(Value::string(qualified_name(value, superclass)));
}

ExecutionResult app_domain_current(Interpreter& interpreter, const Value& self) {
  (void)self;
  return native_value(Value::object(interpreter.runtime().current_domain()));
}

ExecutionResult app_domain_get_definition(Interpreter& interpreter, const Value& self,
                                          const std::vector<Value>& arguments) {
  const ApplicationDomainObject* domain = domain_self(self);
  if (domain == nullptr) {
    return native_value(Value::null());
  }
  const std::string name = to_string(argument_at(arguments, 0));
  const Class* klass = interpreter.runtime().find_domain_definition(*domain, name);
  if (klass == nullptr) {
    return native_value(Value::null());
  }
  return native_value(Value::object(klass->class_object));
}

ExecutionResult app_domain_has_definition(Interpreter& interpreter, const Value& self,
                                          const std::vector<Value>& arguments) {
  const ApplicationDomainObject* domain = domain_self(self);
  if (domain == nullptr) {
    return native_value(Value::boolean(false));
  }
  const std::string name = to_string(argument_at(arguments, 0));
  return native_value(
      Value::boolean(interpreter.runtime().find_domain_definition(*domain, name) != nullptr));
}

std::string request_url(const Value& request) {
  const std::shared_ptr<Object> object = value_object(request);
  return object == nullptr ? std::string{} : dynamic_string(*object, "url");
}

void dispatch_type(Interpreter& interpreter, const Value& target, const char* type,
                   std::size_t depth) {
  const std::shared_ptr<Object> object = value_object(target);
  if (object == nullptr) {
    return;
  }
  dispatch_event(interpreter, object, type, false, depth);
}

ExecutionResult loader_load(Interpreter& interpreter, const Value& self,
                            const std::vector<Value>& arguments, std::size_t depth) {
  if (!self.is_object()) {
    return native_value(Value::undefined());
  }
  auto* loader = self.as_object()->kind() == ObjectKind::Display ? dynamic_cast<LoaderObject*>(
                      static_cast<DisplayObject*>(self.as_object().get()))
                                                                  : nullptr;
  if (loader == nullptr) {
    return native_value(Value::undefined());
  }
  const std::string url = request_url(argument_at(arguments, 0));
  const std::optional<std::vector<std::uint8_t>> bytes =
      interpreter.runtime().host().resolve_asset(url);
  if (!bytes.has_value()) {
    dispatch_type(interpreter, self, "ioError", depth);
    return native_value(Value::undefined());
  }
  loader->bytes = *bytes;
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(loader->bytes.data()), loader->bytes.size()));
  if (!read.ok()) {
    dispatch_type(interpreter, self, "ioError", depth);
    return native_value(Value::undefined());
  }
  loader->movie = std::make_shared<fo4::swf::SwfFile>(read.file);
  const std::shared_ptr<ApplicationDomainObject> domain = context_domain(argument_at(arguments, 1));
  loader->child_runtime =
      SwfRuntime::create(interpreter.runtime(), interpreter, *loader->movie, {}, domain);
  const std::shared_ptr<SwfRuntime>& child = loader->child_runtime;
  if (child == nullptr) {
    dispatch_type(interpreter, self, "ioError", depth);
    return native_value(Value::undefined());
  }
  loader->content = child->root();
  if (loader->content != nullptr) {
    loader->add_child(loader->content);
    interpreter.runtime().register_movie_clip(loader->content);
  }
  loader->loaded = true;
  dispatch_type(interpreter, self, "complete", depth);
  return native_value(Value::undefined());
}

ExecutionResult url_loader_load(Interpreter& interpreter, const Value& self,
                                const std::vector<Value>& arguments, std::size_t depth) {
  if (!self.is_object()) {
    return native_value(Value::undefined());
  }
  const std::string url = request_url(argument_at(arguments, 0));
  const std::optional<std::vector<std::uint8_t>> bytes =
      interpreter.runtime().host().resolve_asset(url);
  if (!bytes.has_value()) {
    define_dynamic_string(interpreter, *self.as_object(), "data", std::string{});
    dispatch_type(interpreter, self, "ioError", depth);
    return native_value(Value::undefined());
  }
  const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
  define_dynamic_string(interpreter, *self.as_object(), "data", text);
  dispatch_type(interpreter, self, "complete", depth);
  return native_value(Value::undefined());
}

ExecutionResult sound_construct(Interpreter& interpreter, const Value& self) {
  (void)interpreter;
  Class* klass = class_from_self(self);
  if (klass == nullptr) {
    return native_value(Value::null());
  }
  return native_value(Value::object(std::make_shared<SoundObject>(klass)));
}

ExecutionResult sound_load(Interpreter& interpreter, const Value& self,
                           const std::vector<Value>& arguments) {
  SoundObject* sound = sound_self(self);
  if (sound == nullptr) {
    return native_value(Value::undefined());
  }
  const std::string url = request_url(argument_at(arguments, 0));
  const std::optional<std::vector<std::uint8_t>> bytes =
      interpreter.runtime().host().resolve_asset(url);
  if (!bytes.has_value()) {
    return native_value(Value::undefined());
  }
  SoundResource resource;
  resource.payload = *bytes;
  const fo4::swf::SwfReadResult read = fo4::swf::read_swf(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(bytes->data()), bytes->size()));
  if (read.ok()) {
    for (const fo4::swf::SwfTag& tag : read.file.tags) {
      if (tag.sound.has_value()) {
        resource.source_id = tag.sound->sound_id;
        resource.format = tag.sound->format;
        resource.rate = tag.sound->rate;
        resource.sample_16bit = tag.sound->sample_16bit;
        resource.stereo = tag.sound->stereo;
        resource.sample_count = tag.sound->sample_count;
        resource.payload.clear();
        for (const std::byte raw : tag.payload) {
          resource.payload.push_back(static_cast<std::uint8_t>(raw));
        }
        break;
      }
    }
  }
  sound->sound_id = interpreter.runtime().register_sound(std::move(resource));
  sound->loaded = true;
  return native_value(Value::undefined());
}

ExecutionResult sound_play(Interpreter& interpreter, const Value& self) {
  Class* channel_class = const_cast<Class*>(
      interpreter.runtime().find_class("flash.media::SoundChannel"));
  if (channel_class == nullptr) {
    return native_value(Value::null());
  }
  auto channel = std::make_shared<SoundChannelObject>(channel_class);
  SoundObject* sound = sound_self(self);
  if (sound == nullptr || !sound->loaded) {
    return native_value(Value::null());
  }
  channel->sound_id = sound->sound_id;
  SnapshotSoundEvent event;
  event.kind = SnapshotSoundEvent::Kind::Play;
  event.sound_id = static_cast<std::uint16_t>(sound->sound_id);
  event.volume = channel->volume;
  interpreter.runtime().sound_events().push(event);
  return native_value(Value::object(std::move(channel)));
}

ExecutionResult sound_close(Interpreter& interpreter, const Value& self) {
  if (SoundObject* sound = sound_self(self)) {
    SnapshotSoundEvent event;
    event.kind = SnapshotSoundEvent::Kind::Stop;
    event.sound_id = static_cast<std::uint16_t>(sound->sound_id);
    interpreter.runtime().sound_events().push(event);
  }
  return native_value(Value::undefined());
}

ExecutionResult sound_channel_stop(Interpreter& interpreter, const Value& self) {
  if (SoundChannelObject* channel = channel_self(self)) {
    SnapshotSoundEvent event;
    event.kind = SnapshotSoundEvent::Kind::Stop;
    event.sound_id = static_cast<std::uint16_t>(channel->sound_id);
    interpreter.runtime().sound_events().push(event);
  }
  return native_value(Value::undefined());
}

}  // namespace

bool try_net_native(Interpreter& interpreter, native::Id id, const Value& self,
                    const std::vector<Value>& arguments, std::size_t depth,
                    ExecutionResult& out) {
  switch (id) {
    case native::Id::instance_construct:
      out = instance_construct(interpreter, self);
      return true;
    case native::Id::ext_available:
      out = ext_available(interpreter, self);
      return true;
    case native::Id::ext_call:
      out = ext_call(interpreter, self, arguments);
      return true;
    case native::Id::ext_add_callback:
      out = ext_add_callback(interpreter, self, arguments);
      return true;
    case native::Id::app_domain_construct:
      out = app_domain_construct(interpreter, self, arguments);
      return true;
    case native::Id::app_domain_has_definition:
      out = app_domain_has_definition(interpreter, self, arguments);
      return true;
    case native::Id::definition_by_name:
      out = definition_by_name(interpreter, self, arguments);
      return true;
    case native::Id::qualified_class_name:
      out = qualified_class_name(argument_at(arguments, 0), false);
      return true;
    case native::Id::qualified_superclass_name:
      out = qualified_class_name(argument_at(arguments, 0), true);
      return true;
    case native::Id::app_domain_current:
      out = app_domain_current(interpreter, self);
      return true;
    case native::Id::app_domain_get_definition:
      out = app_domain_get_definition(interpreter, self, arguments);
      return true;
    case native::Id::loader_load:
      out = loader_load(interpreter, self, arguments, depth);
      return true;
    case native::Id::url_loader_construct:
      out = instance_construct(interpreter, self);
      return true;
    case native::Id::url_loader_load:
      out = url_loader_load(interpreter, self, arguments, depth);
      return true;
    case native::Id::sound_construct:
      out = sound_construct(interpreter, self);
      return true;
    case native::Id::sound_load:
      out = sound_load(interpreter, self, arguments);
      return true;
    case native::Id::sound_play:
      out = sound_play(interpreter, self);
      return true;
    case native::Id::sound_close:
      out = sound_close(interpreter, self);
      return true;
    case native::Id::sound_channel_stop:
      out = sound_channel_stop(interpreter, self);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
