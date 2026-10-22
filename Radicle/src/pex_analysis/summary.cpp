#include <fo4/pex_analysis/summary.hpp>

#include <algorithm>
#include <string_view>

namespace fo4::pex_analysis {

namespace {

bool has_name(const std::vector<std::string>& names, std::string_view name) {
  return std::any_of(names.begin(), names.end(),
                     [&](const std::string& entry) { return pex_name_equal(entry, name); });
}

void add_unique(std::vector<std::string>& names, const std::string& name) {
  if (!has_name(names, name)) names.push_back(name);
}

const fo4::pex::PexFunction* handler_function(const fo4::pex::PexObject& object,
                                              const PexFunctionRef& ref) {
  if (ref.slot == PexFunctionSlot::State) return nullptr;
  if (ref.functionIndex >= object.properties.size()) return nullptr;
  const auto& property = object.properties[ref.functionIndex];
  if (ref.slot == PexFunctionSlot::PropertyRead) {
    return property.readHandler ? &*property.readHandler : nullptr;
  }
  return property.writeHandler ? &*property.writeHandler : nullptr;
}

const fo4::pex::PexFunction* state_function(const fo4::pex::PexObject& object,
                                            const PexFunctionRef& ref) {
  if (ref.slot != PexFunctionSlot::State) return nullptr;
  if (ref.stateIndex >= object.states.size()) return nullptr;
  const auto& state = object.states[ref.stateIndex];
  if (ref.functionIndex >= state.functions.size()) return nullptr;
  return &state.functions[ref.functionIndex];
}

void collect_transitions(const fo4::pex::PexObject& object, const PexFunctionRef& ref,
                         const fo4::pex::PexFunction& function,
                         std::vector<PexGoToState>& out) {
  for (const auto& instruction : function.instructions) {
    if (instruction.opCode != static_cast<std::uint8_t>(PexOp::CallMethod)) continue;
    if (instruction.args.size() < 4) continue;
    if (instruction.args[0].type != fo4::pex::PexValueType::Identifier) continue;
    if (!pex_name_equal(instruction.args[0].str, "GoToState")) continue;
    if (instruction.args[1].type != fo4::pex::PexValueType::Identifier) continue;
    if (!pex_name_equal(instruction.args[1].str, "self")) continue;

    PexGoToState transition;
    transition.function = ref;
    const auto& stateArgument = instruction.args[3];
    if (stateArgument.type == fo4::pex::PexValueType::String ||
        stateArgument.type == fo4::pex::PexValueType::Identifier) {
      transition.literal = stateArgument.type == fo4::pex::PexValueType::String;
      transition.targetState = stateArgument.str;
      for (const auto& state : object.states) {
        if (pex_name_equal(state.name, stateArgument.str)) {
          transition.knownState = true;
          break;
        }
      }
    }
    out.push_back(transition);
  }
}

}

PexScriptSummary summarize_script(const PexCallGraph& graph, std::size_t fileIndex,
                                  const fo4::pex::PexFile& file) {
  PexScriptSummary summary;
  summary.sourceFileName = file.sourceFileName;
  if (fileIndex >= graph.scripts.size()) return summary;
  const auto& script = graph.scripts[fileIndex];

  for (std::size_t o = 0; o < file.objects.size(); ++o) {
    const auto& object = file.objects[o];
    PexObjectSummary objectSummary;
    objectSummary.name = object.name;
    objectSummary.parentClassName = object.parentClassName;
    objectSummary.autoStateName = object.autoStateName;

    for (const auto& root : graph.roots) {
      if (root.fileIndex != fileIndex || root.objectIndex != o) continue;
      const fo4::pex::PexFunction* function = state_function(object, root);
      if (function == nullptr) function = handler_function(object, root);
      if (function == nullptr) continue;

      PexEventSummary event;
      event.function = root;
      for (const auto& site : script.calls) {
        if (!pex_ref_equal(site.caller, root)) continue;
        add_unique(event.calls, site.functionName);
      }
      collect_transitions(object, root, *function, objectSummary.transitions);
      objectSummary.events.push_back(event);
    }

    for (const auto& access : script.accesses) {
      if (access.function.objectIndex != o) continue;
      if (access.isProperty) {
        add_unique(objectSummary.referencedProperties, access.name);
      } else if (access.kind == PexAccessKind::Write) {
        add_unique(objectSummary.writtenVariables, access.name);
      }
    }

    summary.objects.push_back(objectSummary);
  }

  return summary;
}

}
