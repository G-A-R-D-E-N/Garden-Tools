#include <fo4/pex_analysis/fragments.hpp>

#include <utility>

namespace fo4::pex_analysis {

std::vector<PexFragmentTarget> resolve_fragments(
    const fo4::pex::PexFile& file, std::span<const fo4::QuestVmadFragment> fragments) {
  std::vector<PexFragmentTarget> targets;
  targets.reserve(fragments.size());

  for (const auto& fragment : fragments) {
    PexFragmentTarget target;
    target.stage = fragment.stage;
    target.stageIndex = fragment.stage_index;
    target.script = fragment.script;
    target.function = fragment.function;

    std::size_t objectIndex = kNoIndex;
    if (!fragment.script.empty() &&
        pex_find_object(file, fragment.script, &objectIndex)) {
      target.scriptFound = true;
      std::size_t stateIndex = kNoIndex;
      std::size_t functionIndex = kNoIndex;
      if (!fragment.function.empty() &&
          pex_find_function(file.objects[objectIndex], fragment.function, &stateIndex,
                            &functionIndex)) {
        target.functionFound = true;
        const auto& object = file.objects[objectIndex];
        const auto& state = object.states[stateIndex];
        target.target.fileIndex = 0;
        target.target.objectIndex = objectIndex;
        target.target.stateIndex = stateIndex;
        target.target.functionIndex = functionIndex;
        target.target.slot = PexFunctionSlot::State;
        target.target.objectName = object.name;
        target.target.stateName = state.name;
        target.target.functionName = state.functions[functionIndex].name;
        target.nativeTarget = state.functions[functionIndex].isNative;
      }
    }
    targets.push_back(std::move(target));
  }

  return targets;
}

}
