#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include "interpreter_internal.hpp"
#include "native_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

ArrayObject* array_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Array) {
    return nullptr;
  }
  return static_cast<ArrayObject*>(self.as_object().get());
}

std::size_t start_index(const std::vector<Value>& arguments, std::size_t size,
                        std::size_t argument_index) {
  const Value value = argument_at(arguments, argument_index);
  if (value.is_undefined()) {
    return 0;
  }
  const std::int32_t start = to_int32(value);
  if (start < 0) {
    const std::int64_t adjusted = static_cast<std::int64_t>(size) + start;
    return adjusted < 0 ? std::size_t{0} : static_cast<std::size_t>(adjusted);
  }
  return std::min(static_cast<std::size_t>(start), size);
}

std::size_t end_index(const std::vector<Value>& arguments, std::size_t size,
                      std::size_t argument_index) {
  const Value value = argument_at(arguments, argument_index);
  if (value.is_undefined()) {
    return size;
  }
  const std::int32_t end = to_int32(value);
  if (end < 0) {
    const std::int64_t adjusted = static_cast<std::int64_t>(size) + end;
    return adjusted < 0 ? std::size_t{0} : static_cast<std::size_t>(adjusted);
  }
  return std::min(static_cast<std::size_t>(end), size);
}

ExecutionResult array_construct(Interpreter& interpreter, const Value& self,
                                  const std::vector<Value>& arguments) {
  std::vector<Value> elements;
  if (arguments.size() == 1 && arguments[0].is_number_like()) {
    const double length = to_number(arguments[0]);
    if (std::isfinite(length) && length >= 0.0 && std::floor(length) == length) {
      elements.resize(static_cast<std::size_t>(length));
      return native_value(Value::object(interpreter.make_array(std::move(elements))));
    }
  }
  elements = arguments;
  auto array = interpreter.make_array(std::move(elements));
  Class* klass = nullptr;
  if (self.is_object() && self.as_object()->kind() == ObjectKind::Class) {
    klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  }
  if (klass != nullptr && klass->vector_element != nullptr) {
    array->set_instance_class(klass);
    array->set_element_class(klass->vector_element);
  }
  return native_value(Value::object(std::move(array)));
}

ExecutionResult array_push(Interpreter& interpreter, const Value& self,
                            const std::vector<Value>& arguments) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(Value::number(0.0));
  }
  for (const Value& argument : arguments) {
    Value element = argument;
    if (array->element_class() != nullptr) {
      std::string message;
      ExecutionResult coerced = interpreter.coerce_to_class(*array->element_class(), element, message);
      if (!coerced.ok()) {
        return coerced;
      }
    }
    array->elements().push_back(std::move(element));
  }
  return native_value(Value::number(static_cast<double>(array->elements().size())));
}

ExecutionResult array_pop(const Value& self) {
  ArrayObject* array = array_self(self);
  if (array == nullptr || array->elements().empty()) {
    return native_value(Value::undefined());
  }
  Value value = array->elements().back();
  array->elements().pop_back();
  return native_value(std::move(value));
}

ExecutionResult array_shift(const Value& self) {
  ArrayObject* array = array_self(self);
  if (array == nullptr || array->elements().empty()) {
    return native_value(Value::undefined());
  }
  Value value = array->elements().front();
  array->elements().erase(array->elements().begin());
  return native_value(std::move(value));
}

ExecutionResult array_unshift(Interpreter& interpreter, const Value& self,
                               const std::vector<Value>& arguments) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(Value::number(0.0));
  }
  std::vector<Value> elements;
  for (const Value& argument : arguments) {
    Value element = argument;
    if (array->element_class() != nullptr) {
      std::string message;
      ExecutionResult coerced = interpreter.coerce_to_class(*array->element_class(), element, message);
      if (!coerced.ok()) {
        return coerced;
      }
    }
    elements.push_back(std::move(element));
  }
  array->elements().insert(array->elements().begin(), elements.begin(), elements.end());
  return native_value(Value::number(static_cast<double>(array->elements().size())));
}

ExecutionResult array_slice(Interpreter& interpreter, const Value& self,
                            const std::vector<Value>& arguments) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(Value::object(interpreter.make_array({})));
  }
  const std::size_t begin = start_index(arguments, array->elements().size(), 0);
  const std::size_t end = end_index(arguments, array->elements().size(), 1);
  std::vector<Value> result;
  for (std::size_t index = begin; index < end; ++index) {
    result.push_back(array->elements()[index]);
  }
  return native_value(Value::object(interpreter.make_array(std::move(result))));
}

ExecutionResult array_splice(Interpreter& interpreter, const Value& self,
                             const std::vector<Value>& arguments) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(Value::object(interpreter.make_array({})));
  }
  const std::size_t begin = start_index(arguments, array->elements().size(), 0);
  const std::int32_t requested =
      argument_at(arguments, 1).is_undefined()
          ? static_cast<std::int32_t>(array->elements().size() - begin)
          : to_int32(argument_at(arguments, 1));
  const std::size_t remove =
      requested < 0
          ? 0
          : std::min(static_cast<std::size_t>(requested), array->elements().size() - begin);
  std::vector<Value> removed;
  for (std::size_t index = 0; index < remove; ++index) {
    removed.push_back(array->elements()[begin + index]);
  }
  std::vector<Value> inserted;
  for (std::size_t index = 2; index < arguments.size(); ++index) {
    inserted.push_back(arguments[index]);
  }
  array->elements().erase(array->elements().begin() + static_cast<std::ptrdiff_t>(begin),
                          array->elements().begin() +
                              static_cast<std::ptrdiff_t>(begin + remove));
  array->elements().insert(
      array->elements().begin() + static_cast<std::ptrdiff_t>(begin), inserted.begin(),
      inserted.end());
  return native_value(Value::object(interpreter.make_array(std::move(removed))));
}

ExecutionResult array_join(const Value& self, const std::vector<Value>& arguments) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(Value::string(std::string{}));
  }
  const std::string separator =
      argument_at(arguments, 0).is_undefined() ? std::string{","}
                                               : to_string(argument_at(arguments, 0));
  std::string text;
  for (std::size_t index = 0; index < array->elements().size(); ++index) {
    if (index != 0) {
      text += separator;
    }
    const Value& element = array->elements()[index];
    if (!element.is_nullish()) {
      text += to_string(element);
    }
  }
  return native_value(Value::string(std::move(text)));
}

ExecutionResult array_concat(Interpreter& interpreter, const Value& self,
                             const std::vector<Value>& arguments) {
  std::vector<Value> result;
  ArrayObject* array = array_self(self);
  if (array != nullptr) {
    result = array->elements();
  }
  for (const Value& argument : arguments) {
    if (argument.is_object() && argument.as_object()->kind() == ObjectKind::Array) {
      const auto& elements = static_cast<const ArrayObject&>(*argument.as_object()).elements();
      result.insert(result.end(), elements.begin(), elements.end());
    } else {
      result.push_back(argument);
    }
  }
  return native_value(Value::object(interpreter.make_array(std::move(result))));
}

ExecutionResult array_index_of(const Value& self, const std::vector<Value>& arguments) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(Value::int32(-1));
  }
  const std::size_t begin = start_index(arguments, array->elements().size(), 1);
  const Value needle = argument_at(arguments, 0);
  for (std::size_t index = begin; index < array->elements().size(); ++index) {
    if (strict_equals(array->elements()[index], needle)) {
      return native_value(Value::int32(static_cast<std::int32_t>(index)));
    }
  }
  return native_value(Value::int32(-1));
}

ExecutionResult array_last_index_of(const Value& self, const std::vector<Value>& arguments) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(Value::int32(-1));
  }
  const Value needle = argument_at(arguments, 0);
  for (std::size_t index = array->elements().size(); index > 0; --index) {
    if (strict_equals(array->elements()[index - 1], needle)) {
      return native_value(Value::int32(static_cast<std::int32_t>(index - 1)));
    }
  }
  return native_value(Value::int32(-1));
}

ExecutionResult array_reverse(const Value& self) {
  ArrayObject* array = array_self(self);
  if (array != nullptr) {
    std::reverse(array->elements().begin(), array->elements().end());
    return native_value(self);
  }
  return native_value(Value::undefined());
}

ExecutionResult array_sort(Interpreter& interpreter, const Value& self,
                           const std::vector<Value>& arguments, std::size_t depth) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(self);
  }
  const Value comparator = argument_at(arguments, 0);
  const bool compare = comparator.is_object() && comparator.as_object()->kind() == ObjectKind::Function;
  const auto compare_values = [&](const Value& left, const Value& right) {
    if (compare) {
      ExecutionResult called = interpreter.call_function(
          static_cast<const FunctionObject&>(*comparator.as_object()), Value::undefined(),
          {left, right}, depth + 1);
      if (called.ok() && called.value.is_number_like()) {
        const double order = to_number(called.value);
        if (order < 0.0) {
          return -1;
        }
        return order > 0.0 ? 1 : 0;
      }
    }
    const std::string left_text = to_string(left);
    const std::string right_text = to_string(right);
    if (left_text < right_text) {
      return -1;
    }
    return left_text > right_text ? 1 : 0;
  };
  std::stable_sort(array->elements().begin(), array->elements().end(),
                   [&compare_values](const Value& left, const Value& right) {
                     return compare_values(left, right) < 0;
                   });
  return native_value(self);
}

ExecutionResult array_for_each(Interpreter& interpreter, const Value& self,
                               const std::vector<Value>& arguments, std::size_t depth) {
  ArrayObject* array = array_self(self);
  const Value callback = argument_at(arguments, 0);
  const Value this_arg = argument_at(arguments, 1);
  if (array == nullptr || !callback.is_object() ||
      callback.as_object()->kind() != ObjectKind::Function) {
    return native_value(Value::undefined());
  }
  const auto& function = static_cast<const FunctionObject&>(*callback.as_object());
  for (std::size_t index = 0; index < array->elements().size(); ++index) {
    ExecutionResult called = interpreter.call_function(
        function, this_arg, {array->elements()[index], Value::int32(static_cast<std::int32_t>(index)),
                             detail::borrowed_object(*array)},
        depth + 1);
    if (!called.ok()) {
      return called;
    }
  }
  return native_value(Value::undefined());
}

ExecutionResult array_map(Interpreter& interpreter, const Value& self,
                          const std::vector<Value>& arguments, std::size_t depth) {
  ArrayObject* array = array_self(self);
  const Value callback = argument_at(arguments, 0);
  if (array == nullptr || !callback.is_object() ||
      callback.as_object()->kind() != ObjectKind::Function) {
    return native_value(Value::object(interpreter.make_array({})));
  }
  const auto& function = static_cast<const FunctionObject&>(*callback.as_object());
  std::vector<Value> mapped;
  for (std::size_t index = 0; index < array->elements().size(); ++index) {
    ExecutionResult called = interpreter.call_function(
        function, Value::undefined(),
        {array->elements()[index], Value::int32(static_cast<std::int32_t>(index)),
         detail::borrowed_object(*array)},
        depth + 1);
    if (!called.ok()) {
      return called;
    }
    mapped.push_back(called.value);
  }
  return native_value(Value::object(interpreter.make_array(std::move(mapped))));
}

ExecutionResult array_filter(Interpreter& interpreter, const Value& self,
                             const std::vector<Value>& arguments, std::size_t depth) {
  ArrayObject* array = array_self(self);
  const Value callback = argument_at(arguments, 0);
  if (array == nullptr || !callback.is_object() ||
      callback.as_object()->kind() != ObjectKind::Function) {
    return native_value(Value::object(interpreter.make_array({})));
  }
  const auto& function = static_cast<const FunctionObject&>(*callback.as_object());
  std::vector<Value> kept;
  for (std::size_t index = 0; index < array->elements().size(); ++index) {
    ExecutionResult called = interpreter.call_function(
        function, Value::undefined(),
        {array->elements()[index], Value::int32(static_cast<std::int32_t>(index)),
         detail::borrowed_object(*array)},
        depth + 1);
    if (!called.ok()) {
      return called;
    }
    if (to_boolean(called.value)) {
      kept.push_back(array->elements()[index]);
    }
  }
  return native_value(Value::object(interpreter.make_array(std::move(kept))));
}

ExecutionResult array_every(Interpreter& interpreter, const Value& self,
                            const std::vector<Value>& arguments, std::size_t depth) {
  ArrayObject* array = array_self(self);
  const Value callback = argument_at(arguments, 0);
  if (array == nullptr || !callback.is_object() ||
      callback.as_object()->kind() != ObjectKind::Function) {
    return native_value(Value::boolean(true));
  }
  const auto& function = static_cast<const FunctionObject&>(*callback.as_object());
  const Value this_arg = argument_at(arguments, 1);
  for (std::size_t index = 0; index < array->elements().size(); ++index) {
    ExecutionResult called = interpreter.call_function(
        function, this_arg,
        {array->elements()[index], Value::int32(static_cast<std::int32_t>(index)),
         detail::borrowed_object(*array)},
        depth + 1);
    if (!called.ok()) {
      return called;
    }
    if (!to_boolean(called.value)) {
      return native_value(Value::boolean(false));
    }
  }
  return native_value(Value::boolean(true));
}

ExecutionResult array_some(Interpreter& interpreter, const Value& self,
                           const std::vector<Value>& arguments, std::size_t depth) {
  ArrayObject* array = array_self(self);
  const Value callback = argument_at(arguments, 0);
  if (array == nullptr || !callback.is_object() ||
      callback.as_object()->kind() != ObjectKind::Function) {
    return native_value(Value::boolean(false));
  }
  const auto& function = static_cast<const FunctionObject&>(*callback.as_object());
  const Value this_arg = argument_at(arguments, 1);
  for (std::size_t index = 0; index < array->elements().size(); ++index) {
    ExecutionResult called = interpreter.call_function(
        function, this_arg,
        {array->elements()[index], Value::int32(static_cast<std::int32_t>(index)),
         detail::borrowed_object(*array)},
        depth + 1);
    if (!called.ok()) {
      return called;
    }
    if (to_boolean(called.value)) {
      return native_value(Value::boolean(true));
    }
  }
  return native_value(Value::boolean(false));
}

ExecutionResult array_reduce(Interpreter& interpreter, const Value& self,
                             const std::vector<Value>& arguments, std::size_t depth) {
  ArrayObject* array = array_self(self);
  const Value callback = argument_at(arguments, 0);
  if (array == nullptr || !callback.is_object() ||
      callback.as_object()->kind() != ObjectKind::Function || array->elements().empty()) {
    return native_value(argument_at(arguments, 1));
  }
  const auto& function = static_cast<const FunctionObject&>(*callback.as_object());
  const bool has_initial = arguments.size() > 1;
  Value accumulator = has_initial ? arguments[1] : array->elements()[0];
  const std::size_t begin = has_initial ? 0 : 1;
  for (std::size_t index = begin; index < array->elements().size(); ++index) {
    ExecutionResult called = interpreter.call_function(
        function, Value::undefined(),
        {accumulator, array->elements()[index], Value::int32(static_cast<std::int32_t>(index)),
         detail::borrowed_object(*array)},
        depth + 1);
    if (!called.ok()) {
      return called;
    }
    accumulator = called.value;
  }
  return native_value(accumulator);
}

ExecutionResult array_to_string(const Value& self) {
  ArrayObject* array = array_self(self);
  if (array == nullptr) {
    return native_value(Value::string(std::string{}));
  }
  std::string text;
  for (std::size_t index = 0; index < array->elements().size(); ++index) {
    if (index != 0) {
      text.push_back(',');
    }
    if (!array->elements()[index].is_nullish()) {
      text += to_string(array->elements()[index]);
    }
  }
  return native_value(Value::string(std::move(text)));
}

}  // namespace

bool try_array_native(Interpreter& interpreter, native::Id id, const Value& self,
                      const std::vector<Value>& arguments, std::size_t depth,
                      ExecutionResult& out) {
  switch (id) {
    case native::Id::array_construct:
      out = array_construct(interpreter, self, arguments);
      return true;
    case native::Id::array_push:
      out = array_push(interpreter, self, arguments);
      return true;
    case native::Id::array_pop:
      out = array_pop(self);
      return true;
    case native::Id::array_shift:
      out = array_shift(self);
      return true;
    case native::Id::array_unshift:
      out = array_unshift(interpreter, self, arguments);
      return true;
    case native::Id::array_slice:
      out = array_slice(interpreter, self, arguments);
      return true;
    case native::Id::array_splice:
      out = array_splice(interpreter, self, arguments);
      return true;
    case native::Id::array_join:
      out = array_join(self, arguments);
      return true;
    case native::Id::array_concat:
      out = array_concat(interpreter, self, arguments);
      return true;
    case native::Id::array_index_of:
      out = array_index_of(self, arguments);
      return true;
    case native::Id::array_last_index_of:
      out = array_last_index_of(self, arguments);
      return true;
    case native::Id::array_reverse:
      out = array_reverse(self);
      return true;
    case native::Id::array_sort:
      out = array_sort(interpreter, self, arguments, depth);
      return true;
    case native::Id::array_for_each:
      out = array_for_each(interpreter, self, arguments, depth);
      return true;
    case native::Id::array_map:
      out = array_map(interpreter, self, arguments, depth);
      return true;
    case native::Id::array_filter:
      out = array_filter(interpreter, self, arguments, depth);
      return true;
    case native::Id::array_every:
      out = array_every(interpreter, self, arguments, depth);
      return true;
    case native::Id::array_some:
      out = array_some(interpreter, self, arguments, depth);
      return true;
    case native::Id::array_reduce:
      out = array_reduce(interpreter, self, arguments, depth);
      return true;
    case native::Id::array_to_string:
      out = array_to_string(self);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
