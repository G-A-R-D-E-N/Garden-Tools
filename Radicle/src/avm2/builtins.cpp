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

  Class* event_dispatcher = add_builtin_class("EventDispatcher", "flash.events", object_class);
  Class* event_class = add_builtin_class("Event", "flash.events", object_class);
  if (event_dispatcher == nullptr || event_class == nullptr) {
    return RuntimeStatus::InvalidTrait;
  }
  event_dispatcher->native_constructor = static_cast<std::uint32_t>(native::Id::dispatcher_construct);
  add_method(*event_dispatcher, "addEventListener", native::Id::dispatcher_add_listener, false);
  add_method(*event_dispatcher, "removeEventListener", native::Id::dispatcher_remove_listener,
             false);
  add_method(*event_dispatcher, "dispatchEvent", native::Id::dispatcher_dispatch_event, false);
  add_method(*event_dispatcher, "hasEventListener", native::Id::dispatcher_has_listener, false);
  event_class->native_constructor = static_cast<std::uint32_t>(native::Id::event_construct);
  add_method(*event_class, "preventDefault", native::Id::event_prevent_default, false);
  add_method(*event_class, "stopPropagation", native::Id::event_stop_propagation, false);
  add_method(*event_class, "stopImmediatePropagation", native::Id::event_stop_immediate, false);
  add_method(*event_class, "isDefaultPrevented", native::Id::event_is_default_prevented, false);
  add_method(*event_class, "clone", native::Id::event_clone, false);
  add_method(*event_class, "toString", native::Id::event_to_string, false);
  std::uint32_t event_slot = 0;
  const char* event_constants[] = {
      "ADDED",        "ADDED_TO_STAGE", "REMOVED",      "REMOVED_FROM_STAGE", "ENTER_FRAME",
      "EXIT_FRAME",   "CHANGE",         "RENDER",       "TIMER",              "TIMER_COMPLETE",
      "COMPLETE",     "OPEN",           "PROGRESS",     "IO_ERROR",           "SECURITY_ERROR",
      "CLOSE",        "CONNECT",        "INIT",         "SELECT",             "CANCEL",
      "SCROLL",       "RESIZE",         "FULLSCREEN",   "UNLOAD",             "ACTIVATE",
      "DEACTIVATE",   "MOUSE_UP",       "MOUSE_DOWN",   "MOUSE_MOVE",         "MOUSE_OVER",
      "MOUSE_OUT",    "CLICK",          "DOUBLE_CLICK", "ROLL_OVER",          "ROLL_OUT",
      "MOUSE_WHEEL",  "KEY_DOWN",       "KEY_UP",       "FOCUS_IN",           "FOCUS_OUT",
      "TEXT_INPUT",   "FRAME_CONSTRUCTED"};
  for (const char* constant : event_constants) {
    add_constant(*event_class, constant, Value::string(constant), event_slot);
  }
  const auto add_event_subclass = [&](const char* name) -> Class* {
    Class* subclass = add_builtin_class(name, "flash.events", event_class);
    if (subclass != nullptr) {
      subclass->native_constructor = static_cast<std::uint32_t>(native::Id::event_construct);
      add_method(*subclass, "toString", native::Id::event_to_string, false);
    }
    return subclass;
  };
  Class* keyboard_event = add_event_subclass("KeyboardEvent");
  Class* mouse_event = add_event_subclass("MouseEvent");
  Class* focus_event = add_event_subclass("FocusEvent");
  Class* timer_event = add_event_subclass("TimerEvent");
  add_event_subclass("ProgressEvent");
  add_event_subclass("IOErrorEvent");
  add_event_subclass("SecurityErrorEvent");
  add_event_subclass("ErrorEvent");
  if (keyboard_event != nullptr) {
    std::uint32_t slot = 0;
    add_constant(*keyboard_event, "KEY_DOWN", Value::string("keyDown"), slot);
    add_constant(*keyboard_event, "KEY_UP", Value::string("keyUp"), slot);
  }
  if (mouse_event != nullptr) {
    std::uint32_t slot = 0;
    add_constant(*mouse_event, "CLICK", Value::string("click"), slot);
    add_constant(*mouse_event, "DOUBLE_CLICK", Value::string("doubleClick"), slot);
    add_constant(*mouse_event, "MOUSE_DOWN", Value::string("mouseDown"), slot);
    add_constant(*mouse_event, "MOUSE_UP", Value::string("mouseUp"), slot);
    add_constant(*mouse_event, "MOUSE_MOVE", Value::string("mouseMove"), slot);
    add_constant(*mouse_event, "MOUSE_OVER", Value::string("mouseOver"), slot);
    add_constant(*mouse_event, "MOUSE_OUT", Value::string("mouseOut"), slot);
    add_constant(*mouse_event, "ROLL_OVER", Value::string("rollOver"), slot);
    add_constant(*mouse_event, "ROLL_OUT", Value::string("rollOut"), slot);
  }
  if (focus_event != nullptr) {
    std::uint32_t slot = 0;
    add_constant(*focus_event, "FOCUS_IN", Value::string("focusIn"), slot);
    add_constant(*focus_event, "FOCUS_OUT", Value::string("focusOut"), slot);
    add_constant(*focus_event, "KEY_FOCUS_CHANGE", Value::string("keyFocusChange"), slot);
    add_constant(*focus_event, "MOUSE_FOCUS_CHANGE", Value::string("mouseFocusChange"), slot);
  }
  if (timer_event != nullptr) {
    std::uint32_t slot = 0;
    add_constant(*timer_event, "TIMER", Value::string("timer"), slot);
    add_constant(*timer_event, "TIMER_COMPLETE", Value::string("timerComplete"), slot);
  }
  Class* keyboard_class = add_builtin_class("Keyboard", "flash.ui", object_class);
  Class* timer_class = add_builtin_class("Timer", "flash.utils", object_class);
  if (keyboard_class == nullptr || timer_class == nullptr) {
    return RuntimeStatus::InvalidTrait;
  }
  std::uint32_t keyboard_slot = 0;
  for (int code = 'A'; code <= 'Z'; ++code) {
    const std::string name(1, static_cast<char>(code));
    add_constant(*keyboard_class, name, Value::uint32(static_cast<std::uint32_t>(code)),
                 keyboard_slot);
  }
  const std::pair<const char*, std::uint32_t> keyboard_named[] = {
      {"LEFT", 37},      {"UP", 38},        {"RIGHT", 39},     {"DOWN", 40},
      {"SPACE", 32},     {"ENTER", 13},     {"ESCAPE", 27},    {"TAB", 9},
      {"BACKSPACE", 8},  {"DELETE", 46},    {"HOME", 36},      {"END", 35},
      {"PAGE_UP", 33},   {"PAGE_DOWN", 34}, {"INSERT", 45},    {"SHIFT", 16},
      {"CONTROL", 17},   {"ALT", 18},       {"CAPS_LOCK", 20}};
  for (const auto& [name, code] : keyboard_named) {
    add_constant(*keyboard_class, name, Value::uint32(code), keyboard_slot);
  }
  for (int index = 0; index < 10; ++index) {
    add_constant(*keyboard_class, "NUMBER_" + std::to_string(index),
                 Value::uint32(static_cast<std::uint32_t>('0' + index)), keyboard_slot);
    add_constant(*keyboard_class, "NUMPAD_" + std::to_string(index),
                 Value::uint32(static_cast<std::uint32_t>(96 + index)), keyboard_slot);
  }
  for (int index = 1; index <= 12; ++index) {
    add_constant(*keyboard_class, "F" + std::to_string(index),
                 Value::uint32(static_cast<std::uint32_t>(111 + index)), keyboard_slot);
  }
  timer_class->native_constructor = static_cast<std::uint32_t>(native::Id::timer_construct);
  add_method(*timer_class, "start", native::Id::timer_start, false);
  add_method(*timer_class, "stop", native::Id::timer_stop, false);
  add_method(*timer_class, "reset", native::Id::timer_reset, false);

  Class* display_object = add_builtin_class("DisplayObject", "flash.display", event_dispatcher);
  Class* interactive_object = add_builtin_class("InteractiveObject", "flash.display", display_object);
  Class* display_container =
      add_builtin_class("DisplayObjectContainer", "flash.display", interactive_object);
  Class* sprite = add_builtin_class("Sprite", "flash.display", display_container);
  Class* movie_clip = add_builtin_class("MovieClip", "flash.display", sprite);
  Class* stage = add_builtin_class("Stage", "flash.display", display_container);
  Class* shape = add_builtin_class("Shape", "flash.display", display_object);
  Class* bitmap = add_builtin_class("Bitmap", "flash.display", display_object);
  Class* text_field = add_builtin_class("TextField", "flash.text", interactive_object);
  Class* text_format = add_builtin_class("TextFormat", "flash.text", object_class);
  Class* simple_button = add_builtin_class("SimpleButton", "flash.display", interactive_object);
  if (display_object == nullptr || interactive_object == nullptr || display_container == nullptr ||
      sprite == nullptr || movie_clip == nullptr || stage == nullptr || shape == nullptr ||
      bitmap == nullptr || text_field == nullptr || simple_button == nullptr ||
      text_format == nullptr) {
    return RuntimeStatus::InvalidTrait;
  }
  for (Class* display_class : {display_object, interactive_object, display_container, sprite,
                               movie_clip, stage, shape, bitmap, text_field, simple_button}) {
    display_class->native_constructor = static_cast<std::uint32_t>(native::Id::display_construct);
  }
  add_method(*display_object, "localToGlobal", native::Id::display_local_to_global, false);
  add_method(*display_object, "globalToLocal", native::Id::display_global_to_local, false);
  add_method(*display_container, "addChild", native::Id::display_add_child, false);
  add_method(*display_container, "addChildAt", native::Id::display_add_child_at, false);
  add_method(*display_container, "removeChild", native::Id::display_remove_child, false);
  add_method(*display_container, "removeChildAt", native::Id::display_remove_child_at, false);
  add_method(*display_container, "getChildAt", native::Id::display_get_child_at, false);
  add_method(*display_container, "getChildByName", native::Id::display_get_child_by_name, false);
  add_method(*display_container, "contains", native::Id::display_contains, false);
  add_method(*display_container, "setChildIndex", native::Id::display_set_child_index, false);
  add_method(*display_container, "swapChildren", native::Id::display_swap_children, false);
  add_method(*movie_clip, "gotoAndPlay", native::Id::display_goto_and_play, false);
  add_method(*movie_clip, "gotoAndStop", native::Id::display_goto_and_stop, false);
  add_method(*movie_clip, "stop", native::Id::display_stop, false);
  add_method(*movie_clip, "play", native::Id::display_play, false);
  add_method(*movie_clip, "nextFrame", native::Id::display_next_frame, false);
  add_method(*movie_clip, "prevFrame", native::Id::display_prev_frame, false);
  add_method(*text_field, "appendText", native::Id::text_field_append_text, false);
  add_method(*text_field, "getLineText", native::Id::text_field_get_line_text, false);
  add_method(*text_field, "setTextFormat", native::Id::text_field_set_text_format, false);
  add_method(*text_field, "getTextFormat", native::Id::text_field_get_text_format, false);
  text_format->native_constructor = static_cast<std::uint32_t>(native::Id::text_format_construct);

  Class* point_class = add_builtin_class("Point", "flash.geom", object_class);
  Class* rectangle_class = add_builtin_class("Rectangle", "flash.geom", object_class);
  Class* matrix_class = add_builtin_class("Matrix", "flash.geom", object_class);
  Class* color_transform_class =
      add_builtin_class("ColorTransform", "flash.geom", object_class);
  Class* dictionary_class = add_builtin_class("Dictionary", "flash.utils", object_class);
  if (point_class != nullptr) {
    point_class->native_constructor = static_cast<std::uint32_t>(native::Id::geom_construct);
  }
  if (rectangle_class != nullptr) {
    rectangle_class->native_constructor = static_cast<std::uint32_t>(native::Id::geom_construct);
  }
  if (matrix_class != nullptr) {
    matrix_class->native_constructor = static_cast<std::uint32_t>(native::Id::geom_construct);
  }
  if (color_transform_class != nullptr) {
    color_transform_class->native_constructor =
        static_cast<std::uint32_t>(native::Id::geom_construct);
  }
  if (dictionary_class != nullptr) {
    dictionary_class->native_constructor =
        static_cast<std::uint32_t>(native::Id::dictionary_construct);
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
