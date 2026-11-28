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
  bool forwards_arguments;
  bool returns_answer;
};

constexpr BgsFunction kBgsFunctions[] = {
    {"registerSound", native::Id::bgs_register_sound, true, true},
    {"playSound", native::Id::bgs_play_sound, true, true},
    {"playRegisteredSound", native::Id::bgs_play_registered_sound, true, true},
    {"stopRegisteredSound", native::Id::bgs_stop_registered_sound, true, true},
    {"pauseRegisteredSound", native::Id::bgs_pause_registered_sound, true, true},
    {"getHighscore", native::Id::bgs_get_highscore, true, false},
    {"setHighscore", native::Id::bgs_set_highscore, true, true},
    {"getSaveData", native::Id::bgs_get_save_data, true, false},
    {"setSaveData", native::Id::bgs_set_save_data, true, true},
    {"closeHolotape", native::Id::bgs_close_holotape, true, true},
    {"showTutorialOverlay", native::Id::bgs_show_tutorial_overlay, true, true},
    {"notifyScripts", native::Id::bgs_notify_scripts, true, true},
    {"getTextReplaceID", native::Id::bgs_get_text_replace_id, true, true},
    {"getTextReplaceValue", native::Id::bgs_get_text_replace_value, true, true},
    {"playActionAnim", native::Id::bgs_play_action_anim, false, true},
    {"playLeftAnim", native::Id::bgs_play_left_anim, false, true},
    {"playRightAnim", native::Id::bgs_play_right_anim, false, true},
    {"executeCommand", native::Id::bgs_execute_command, true, true},
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
    if (entry.forwards_arguments) {
      for (const Value& argument : arguments) {
        forwarded.push_back(to_string(argument));
      }
    }
    const std::optional<std::string> answer = interpreter.runtime().host().call_host(
        std::string{"bgs."} + entry.name, std::move(forwarded));
    const bool returned = entry.returns_answer && answer.has_value();
    out = native_value(returned ? Value::string(*answer) : Value::null());
    return true;
  }
  return false;
}

}  // namespace fo4::avm2
