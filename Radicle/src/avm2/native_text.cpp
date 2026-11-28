#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/text.hpp>
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

TextFieldObject* text_field_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Display) {
    return nullptr;
  }
  auto* display = static_cast<DisplayObject*>(self.as_object().get());
  return display->is_text_field() ? static_cast<TextFieldObject*>(display) : nullptr;
}

void set_dynamic(Interpreter& interpreter, Object& object, const char* name, Value value) {
  Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  Property& property = object.define_own_property(primary_key(multiname));
  property.kind = PropertyKind::Value;
  property.value = std::move(value);
  (void)interpreter;
}

ExecutionResult text_format_construct(Interpreter& interpreter, const Value& self,
                                      const std::vector<Value>& arguments) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return native_value(Value::null());
  }
  Class* klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  if (klass == nullptr) {
    return native_value(Value::null());
  }
  auto instance = interpreter.make_instance(*klass);
  set_dynamic(interpreter, *instance, "font", argument_at(arguments, 0));
  set_dynamic(interpreter, *instance, "size", argument_at(arguments, 1));
  set_dynamic(interpreter, *instance, "color", argument_at(arguments, 2));
  set_dynamic(interpreter, *instance, "bold", argument_at(arguments, 3));
  set_dynamic(interpreter, *instance, "italic", argument_at(arguments, 4));
  set_dynamic(interpreter, *instance, "underline", argument_at(arguments, 5));
  set_dynamic(interpreter, *instance, "url", argument_at(arguments, 6));
  set_dynamic(interpreter, *instance, "target", argument_at(arguments, 7));
  set_dynamic(interpreter, *instance, "align", argument_at(arguments, 8));
  set_dynamic(interpreter, *instance, "leftMargin", argument_at(arguments, 9));
  set_dynamic(interpreter, *instance, "rightMargin", argument_at(arguments, 10));
  set_dynamic(interpreter, *instance, "indent", argument_at(arguments, 11));
  set_dynamic(interpreter, *instance, "leading", argument_at(arguments, 12));
  return native_value(Value::object(std::move(instance)));
}

ExecutionResult text_field_append_text(Interpreter& interpreter, const Value& self,
                                       const std::vector<Value>& arguments) {
  (void)interpreter;
  TextFieldObject* field = text_field_self(self);
  if (field == nullptr) {
    return native_value(Value::undefined());
  }
  field->set_text(field->text() + to_string(argument_at(arguments, 0)));
  return native_value(Value::undefined());
}

ExecutionResult text_field_get_line_text(Interpreter& interpreter, const Value& self,
                                         const std::vector<Value>& arguments) {
  (void)interpreter;
  TextFieldObject* field = text_field_self(self);
  if (field == nullptr) {
    return native_value(Value::string(std::string{}));
  }
  const std::int32_t index = to_int32(argument_at(arguments, 0));
  if (index < 0) {
    return native_value(Value::string(std::string{}));
  }
  return native_value(Value::string(field->line_text(static_cast<std::size_t>(index))));
}

ExecutionResult text_field_set_text_format(Interpreter& interpreter, const Value& self,
                                           const std::vector<Value>& arguments) {
  (void)interpreter;
  TextFieldObject* field = text_field_self(self);
  if (field == nullptr) {
    return native_value(Value::undefined());
  }
  field->set_format(read_text_format_instance(argument_at(arguments, 0)));
  return native_value(Value::undefined());
}

ExecutionResult text_field_get_text_format(Interpreter& interpreter, const Value& self,
                                           const std::vector<Value>& arguments) {
  TextFieldObject* field = text_field_self(self);
  if (field == nullptr) {
    return native_value(Value::null());
  }
  (void)arguments;
  return native_value(make_text_format_instance(interpreter, field->format()));
}

}  // namespace

bool try_text_native(Interpreter& interpreter, native::Id id, const Value& self,
                     const std::vector<Value>& arguments, std::size_t depth,
                     ExecutionResult& out) {
  (void)depth;
  switch (id) {
    case native::Id::text_format_construct:
      out = text_format_construct(interpreter, self, arguments);
      return true;
    case native::Id::text_field_append_text:
      out = text_field_append_text(interpreter, self, arguments);
      return true;
    case native::Id::text_field_get_line_text:
      out = text_field_get_line_text(interpreter, self, arguments);
      return true;
    case native::Id::text_field_set_text_format:
      out = text_field_set_text_format(interpreter, self, arguments);
      return true;
    case native::Id::text_field_get_text_format:
      out = text_field_get_text_format(interpreter, self, arguments);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
