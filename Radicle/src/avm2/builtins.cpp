#include <fo4/abc/abc.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "native_ids.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

constexpr double kNumberMaxValue = 1.7976931348623157e308;
constexpr double kNumberMinValue = 5e-324;

Multiname package_name(std::string_view name, std::string_view package = {}) {
  Multiname multiname;
  multiname.kind = MultinameKind::QName;
  multiname.name = std::string{name};
  multiname.namespaces.push_back(Namespace{NamespaceKind::Package, std::string{package}});
  return multiname;
}

}  // namespace

RuntimeStatus Runtime::install_builtins() {
  const auto add_method = [](Class& klass, std::string name, native::Id id, bool static_trait) {
    auto function = std::make_shared<FunctionObject>(name, nullptr, kNoMethod);
    function->set_native_id(static_cast<std::uint32_t>(id));
    function->set_owner_class(&klass);
    Trait trait;
    trait.multiname = package_name(name);
    trait.kind = TraitKind::Method;
    trait.function = std::move(function);
    if (static_trait) {
      klass.class_traits.push_back(std::move(trait));
    } else {
      klass.instance_traits.push_back(std::move(trait));
    }
  };
  const auto add_constant = [](Class& klass, std::string name, Value value,
                               std::uint32_t& next_slot) {
    Trait trait;
    trait.multiname = package_name(name);
    trait.kind = TraitKind::Const;
    trait.slot_id = next_slot;
    next_slot += 1;
    trait.has_default = true;
    trait.default_value = std::move(value);
    klass.class_traits.push_back(std::move(trait));
  };

  Class* object_class = add_builtin_class("Object", "", nullptr);
  Class* function_class = add_builtin_class("Function", "", object_class);
  Class* class_class = add_builtin_class("Class", "", object_class);
  Class* number_class = add_builtin_class("Number", "", object_class);
  Class* int_class = add_builtin_class("int", "", object_class);
  Class* uint_class = add_builtin_class("uint", "", object_class);
  Class* boolean_class = add_builtin_class("Boolean", "", object_class);
  Class* string_class = add_builtin_class("String", "", object_class);
  Class* math_class = add_builtin_class("Math", "", object_class);
  Class* array_class = add_builtin_class("Array", "", object_class);
  Class* vector_class = add_builtin_class("Vector", "__AS3__.vec", object_class);
  if (object_class == nullptr || function_class == nullptr || class_class == nullptr ||
      number_class == nullptr || int_class == nullptr || uint_class == nullptr ||
      boolean_class == nullptr || string_class == nullptr || math_class == nullptr ||
      array_class == nullptr || vector_class == nullptr) {
    return RuntimeStatus::InvalidTrait;
  }

  add_method(*object_class, "toString", native::Id::object_to_string, false);
  add_method(*object_class, "valueOf", native::Id::object_value_of, false);
  add_method(*object_class, "hasOwnProperty", native::Id::object_has_own_property, false);
  add_method(*object_class, "isPrototypeOf", native::Id::object_is_prototype_of, false);
  add_method(*object_class, "propertyIsEnumerable", native::Id::object_property_is_enumerable,
             false);
  object_class->native_constructor = static_cast<std::uint32_t>(native::Id::object_construct);

  add_method(*function_class, "call", native::Id::function_call, false);
  add_method(*function_class, "apply", native::Id::function_apply, false);
  add_method(*function_class, "toString", native::Id::function_to_string, false);

  add_method(*number_class, "toString", native::Id::number_to_string, false);
  add_method(*number_class, "toFixed", native::Id::number_to_fixed, false);
  add_method(*number_class, "toExponential", native::Id::number_to_exponential, false);
  add_method(*number_class, "toPrecision", native::Id::number_to_precision, false);
  add_method(*number_class, "valueOf", native::Id::number_value_of, false);
  number_class->native_constructor = static_cast<std::uint32_t>(native::Id::number_construct);

  add_method(*int_class, "toString", native::Id::int_to_string, false);
  add_method(*int_class, "valueOf", native::Id::int_value_of, false);
  int_class->native_constructor = static_cast<std::uint32_t>(native::Id::int_construct);

  add_method(*uint_class, "toString", native::Id::uint_to_string, false);
  add_method(*uint_class, "valueOf", native::Id::uint_value_of, false);
  uint_class->native_constructor = static_cast<std::uint32_t>(native::Id::uint_construct);

  add_method(*boolean_class, "toString", native::Id::boolean_to_string, false);
  add_method(*boolean_class, "valueOf", native::Id::boolean_value_of, false);
  boolean_class->native_constructor = static_cast<std::uint32_t>(native::Id::boolean_construct);
  string_class->native_constructor = static_cast<std::uint32_t>(native::Id::to_string_value);

  add_method(*string_class, "charAt", native::Id::string_char_at, false);
  add_method(*string_class, "charCodeAt", native::Id::string_char_code_at, false);
  add_method(*string_class, "indexOf", native::Id::string_index_of, false);
  add_method(*string_class, "lastIndexOf", native::Id::string_last_index_of, false);
  add_method(*string_class, "substr", native::Id::string_substr, false);
  add_method(*string_class, "substring", native::Id::string_substring, false);
  add_method(*string_class, "slice", native::Id::string_slice, false);
  add_method(*string_class, "split", native::Id::string_split, false);
  add_method(*string_class, "toUpperCase", native::Id::string_to_upper, false);
  add_method(*string_class, "toLowerCase", native::Id::string_to_lower, false);
  add_method(*string_class, "replace", native::Id::string_replace, false);
  add_method(*string_class, "concat", native::Id::string_concat, false);
  add_method(*string_class, "trim", native::Id::string_trim, false);
  add_method(*string_class, "toString", native::Id::string_to_string, false);
  add_method(*string_class, "valueOf", native::Id::string_value_of, false);
  add_method(*string_class, "localeCompare", native::Id::string_locale_compare, false);
  add_method(*string_class, "fromCharCode", native::Id::string_from_char_code, true);

  add_method(*array_class, "push", native::Id::array_push, false);
  add_method(*array_class, "pop", native::Id::array_pop, false);
  add_method(*array_class, "shift", native::Id::array_shift, false);
  add_method(*array_class, "unshift", native::Id::array_unshift, false);
  add_method(*array_class, "slice", native::Id::array_slice, false);
  add_method(*array_class, "splice", native::Id::array_splice, false);
  add_method(*array_class, "join", native::Id::array_join, false);
  add_method(*array_class, "concat", native::Id::array_concat, false);
  add_method(*array_class, "indexOf", native::Id::array_index_of, false);
  add_method(*array_class, "lastIndexOf", native::Id::array_last_index_of, false);
  add_method(*array_class, "reverse", native::Id::array_reverse, false);
  add_method(*array_class, "sort", native::Id::array_sort, false);
  add_method(*array_class, "forEach", native::Id::array_for_each, false);
  add_method(*array_class, "map", native::Id::array_map, false);
  add_method(*array_class, "filter", native::Id::array_filter, false);
  add_method(*array_class, "every", native::Id::array_every, false);
  add_method(*array_class, "some", native::Id::array_some, false);
  add_method(*array_class, "reduce", native::Id::array_reduce, false);
  add_method(*array_class, "toString", native::Id::array_to_string, false);
  array_class->native_constructor = static_cast<std::uint32_t>(native::Id::array_construct);

  add_method(*vector_class, "push", native::Id::array_push, false);
  add_method(*vector_class, "pop", native::Id::array_pop, false);
  add_method(*vector_class, "shift", native::Id::array_shift, false);
  add_method(*vector_class, "unshift", native::Id::array_unshift, false);
  add_method(*vector_class, "slice", native::Id::array_slice, false);
  add_method(*vector_class, "splice", native::Id::array_splice, false);
  add_method(*vector_class, "join", native::Id::array_join, false);
  add_method(*vector_class, "concat", native::Id::array_concat, false);
  add_method(*vector_class, "indexOf", native::Id::array_index_of, false);
  add_method(*vector_class, "lastIndexOf", native::Id::array_last_index_of, false);
  add_method(*vector_class, "reverse", native::Id::array_reverse, false);
  add_method(*vector_class, "sort", native::Id::array_sort, false);
  add_method(*vector_class, "forEach", native::Id::array_for_each, false);
  add_method(*vector_class, "map", native::Id::array_map, false);
  add_method(*vector_class, "filter", native::Id::array_filter, false);
  add_method(*vector_class, "every", native::Id::array_every, false);
  add_method(*vector_class, "some", native::Id::array_some, false);
  add_method(*vector_class, "reduce", native::Id::array_reduce, false);
  add_method(*vector_class, "toString", native::Id::array_to_string, false);
  vector_class->native_constructor = static_cast<std::uint32_t>(native::Id::array_construct);

  add_method(*math_class, "abs", native::Id::math_abs, true);
  add_method(*math_class, "min", native::Id::math_min, true);
  add_method(*math_class, "max", native::Id::math_max, true);
  add_method(*math_class, "floor", native::Id::math_floor, true);
  add_method(*math_class, "ceil", native::Id::math_ceil, true);
  add_method(*math_class, "round", native::Id::math_round, true);
  add_method(*math_class, "sqrt", native::Id::math_sqrt, true);
  add_method(*math_class, "pow", native::Id::math_pow, true);
  add_method(*math_class, "random", native::Id::math_random, true);
  add_method(*math_class, "sin", native::Id::math_sin, true);
  add_method(*math_class, "cos", native::Id::math_cos, true);
  add_method(*math_class, "tan", native::Id::math_tan, true);
  add_method(*math_class, "atan2", native::Id::math_atan2, true);
  add_method(*math_class, "log", native::Id::math_log, true);
  add_method(*math_class, "exp", native::Id::math_exp, true);
  add_method(*math_class, "acos", native::Id::math_acos, true);
  add_method(*math_class, "asin", native::Id::math_asin, true);
  add_method(*math_class, "atan", native::Id::math_atan, true);

  std::uint32_t number_slot = 0;
  add_constant(*number_class, "MAX_VALUE", Value::number(kNumberMaxValue), number_slot);
  add_constant(*number_class, "MIN_VALUE", Value::number(kNumberMinValue), number_slot);
  add_constant(*number_class, "NaN", Value::number(std::numeric_limits<double>::quiet_NaN()),
               number_slot);
  add_constant(*number_class, "POSITIVE_INFINITY",
               Value::number(std::numeric_limits<double>::infinity()), number_slot);
  add_constant(*number_class, "NEGATIVE_INFINITY",
               Value::number(-std::numeric_limits<double>::infinity()), number_slot);

  std::uint32_t int_slot = 0;
  add_constant(*int_class, "MAX_VALUE",
               Value::int32(std::numeric_limits<std::int32_t>::max()), int_slot);
  add_constant(*int_class, "MIN_VALUE",
               Value::int32(std::numeric_limits<std::int32_t>::min()), int_slot);

  std::uint32_t uint_slot = 0;
  add_constant(*uint_class, "MAX_VALUE",
               Value::uint32(std::numeric_limits<std::uint32_t>::max()), uint_slot);
  add_constant(*uint_class, "MIN_VALUE", Value::uint32(0), uint_slot);

  std::uint32_t math_slot = 0;
  add_constant(*math_class, "PI", Value::number(3.14159265358979323846), math_slot);
  add_constant(*math_class, "E", Value::number(2.71828182845904523536), math_slot);
  add_constant(*math_class, "LN2", Value::number(0.69314718055994530942), math_slot);
  add_constant(*math_class, "LN10", Value::number(2.30258509299404568402), math_slot);
  add_constant(*math_class, "LOG2E", Value::number(1.44269504088896340736), math_slot);
  add_constant(*math_class, "LOG10E", Value::number(0.43429448190325182765), math_slot);
  add_constant(*math_class, "SQRT1_2", Value::number(0.70710678118654752440), math_slot);
  add_constant(*math_class, "SQRT2", Value::number(1.41421356237309504880), math_slot);

  Class* error_class = add_builtin_class("Error", "", object_class);
  if (error_class == nullptr) {
    return RuntimeStatus::InvalidTrait;
  }
  add_method(*error_class, "toString", native::Id::error_to_string, false);
  error_class->native_constructor = static_cast<std::uint32_t>(native::Id::error_construct);
  const char* error_names[] = {"TypeError",       "ReferenceError", "RangeError",
                               "SyntaxError",     "URIError",       "SecurityError",
                               "ArgumentError",   "EvalError",      "DefinitionError",
                               "VerifyError",     "UninitializedError"};
  for (const char* error_name : error_names) {
    Class* subclass = add_builtin_class(error_name, "", error_class);
    if (subclass == nullptr) {
      return RuntimeStatus::InvalidTrait;
    }
    add_method(*subclass, "toString", native::Id::error_to_string, false);
    subclass->native_constructor = static_cast<std::uint32_t>(native::Id::error_construct);
  }

  const auto add_global_function = [this](std::string name, native::Id id) {
    auto function = std::make_shared<FunctionObject>(name, nullptr, kNoMethod);
    function->set_native_id(static_cast<std::uint32_t>(id));
    bind_function_class(*function);
    Property& property = global_object_->define_own_property(primary_key(package_name(name)));
    property.kind = PropertyKind::Value;
    property.value = Value::object(std::move(function));
  };
  add_global_function("trace", native::Id::trace);
  add_global_function("isNaN", native::Id::is_nan);
  add_global_function("isFinite", native::Id::is_finite);
  add_global_function("parseInt", native::Id::parse_int);
  add_global_function("parseFloat", native::Id::parse_float);
  add_global_function("String", native::Id::to_string_value);
  add_global_function("Number", native::Id::to_number_value);
  add_global_function("Boolean", native::Id::to_boolean_value);
  add_global_function("int", native::Id::to_int_value);
  add_global_function("uint", native::Id::to_uint_value);
  add_global_function("Object", native::Id::to_object_value);
  add_global_function("getTimer", native::Id::get_timer);

  const auto add_global_value = [this](std::string name, Value value) {
    Property& property = global_object_->define_own_property(primary_key(package_name(name)));
    property.kind = PropertyKind::Value;
    property.value = std::move(value);
  };
  add_global_value("NaN", Value::number(std::numeric_limits<double>::quiet_NaN()));
  add_global_value("Infinity", Value::number(std::numeric_limits<double>::infinity()));
  add_global_value("undefined", Value::undefined());
  return RuntimeStatus::Ok;
}

}  // namespace fo4::avm2
