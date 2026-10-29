#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

struct BgsFunction {
  const char* name;
  native::Id id;
};

constexpr BgsFunction kBgsFunctions[] = {
    {"registerSound", native::Id::bgs_register_sound},
    {"playSound", native::Id::bgs_play_sound},
    {"playRegisteredSound", native::Id::bgs_play_registered_sound},
    {"stopRegisteredSound", native::Id::bgs_stop_registered_sound},
    {"pauseRegisteredSound", native::Id::bgs_pause_registered_sound},
    {"getHighscore", native::Id::bgs_get_highscore},
    {"setHighscore", native::Id::bgs_set_highscore},
    {"getSaveData", native::Id::bgs_get_save_data},
    {"setSaveData", native::Id::bgs_set_save_data},
    {"closeHolotape", native::Id::bgs_close_holotape},
    {"showTutorialOverlay", native::Id::bgs_show_tutorial_overlay},
};

}  // namespace

void install_bgs_code_object(Runtime& runtime, Object& root) {
  Value code_object;
  if (!root.read_value(builtin_name("BGSCodeObj"), code_object) || !code_object.is_object() ||
      code_object.as_object()->kind() != ObjectKind::Object) {
    return;
  }
  Object& target = *code_object.as_object();
  for (const BgsFunction& entry : kBgsFunctions) {
    auto function = std::make_shared<FunctionObject>(entry.name, nullptr, kNoMethod);
    function->set_native_id(static_cast<std::uint32_t>(entry.id));
    runtime.bind_function_class(*function);
    Property& property = target.define_own_property(primary_key(builtin_name(entry.name)));
    property.kind = PropertyKind::Value;
    property.value = Value::object(std::move(function));
  }
}

bool try_bgs_native(Interpreter& interpreter, native::Id id, const std::vector<Value>& arguments,
                    ExecutionResult& out) {
  for (const BgsFunction& entry : kBgsFunctions) {
    if (entry.id != id) {
      continue;
    }
    std::vector<std::string> forwarded;
    for (const Value& argument : arguments) {
      forwarded.push_back(to_string(argument));
    }
    const std::optional<std::string> answer = interpreter.runtime().host().call_host(
        std::string{"bgs."} + entry.name, std::move(forwarded));
    out = native_value(answer.has_value() ? Value::string(*answer) : Value::null());
    return true;
  }
  return false;
}

}  // namespace fo4::avm2
