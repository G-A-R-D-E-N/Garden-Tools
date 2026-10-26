#include <fo4/pex_analysis/callgraph.hpp>

#include <algorithm>
#include <initializer_list>
#include <string_view>

namespace fo4::pex_analysis {

namespace {

struct OperandAccess {
  std::size_t operand;
  PexAccessKind kind;
};

struct AccessList {
  OperandAccess items[4];
  std::size_t count{0};
};

AccessList make_accesses(std::initializer_list<OperandAccess> items) {
  AccessList list;
  for (const auto& item : items) {
    if (list.count < 4) list.items[list.count++] = item;
  }
  return list;
}

char lowered_char(char ch) {
  return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32) : ch;
}

AccessList operand_accesses(std::uint8_t op) {
  switch (static_cast<PexOp>(op)) {
    case PexOp::Assign:
      return make_accesses({{0, PexAccessKind::Write}, {1, PexAccessKind::Read}});
    case PexOp::Cast:
      return make_accesses({{0, PexAccessKind::Write}, {1, PexAccessKind::Read}});
    case PexOp::StrCat:
      return make_accesses({{0, PexAccessKind::Write},
                            {1, PexAccessKind::Read},
                            {2, PexAccessKind::Read}});
    case PexOp::IAdd:
    case PexOp::FAdd:
    case PexOp::ISub:
    case PexOp::FSub:
    case PexOp::IMul:
    case PexOp::FMul:
    case PexOp::IDiv:
    case PexOp::FDiv:
    case PexOp::IMod:
    case PexOp::CmpEq:
    case PexOp::CmpLt:
    case PexOp::CmpLte:
    case PexOp::CmpGt:
    case PexOp::CmpGte:
      return make_accesses({{0, PexAccessKind::Write},
                            {1, PexAccessKind::Read},
                            {2, PexAccessKind::Read}});
    case PexOp::Not:
    case PexOp::INeg:
    case PexOp::FNeg:
    case PexOp::Is:
    case PexOp::ArrayCreate:
    case PexOp::ArrayLength:
      return make_accesses({{0, PexAccessKind::Write}, {1, PexAccessKind::Read}});
    case PexOp::Return:
    case PexOp::JmpT:
    case PexOp::JmpF:
      return make_accesses({{0, PexAccessKind::Read}});
    case PexOp::PropGet:
      return make_accesses({{0, PexAccessKind::Read},
                            {1, PexAccessKind::Read},
                            {2, PexAccessKind::Write}});
    case PexOp::PropSet:
      return make_accesses({{0, PexAccessKind::Write},
                            {1, PexAccessKind::Read},
                            {2, PexAccessKind::Read}});
    case PexOp::ArrayGetElement:
      return make_accesses({{0, PexAccessKind::Write},
                            {1, PexAccessKind::Read},
                            {2, PexAccessKind::Read}});
    case PexOp::ArrayFindElement:
    case PexOp::ArrayRFindElement:
      return make_accesses({{0, PexAccessKind::Read},
                            {1, PexAccessKind::Write},
                            {2, PexAccessKind::Read},
                            {3, PexAccessKind::Read}});
    case PexOp::ArraySetElement:
    case PexOp::ArrayAdd:
    case PexOp::ArrayInsert:
    case PexOp::ArrayRemove:
      return make_accesses({{0, PexAccessKind::Write},
                            {1, PexAccessKind::Read},
                            {2, PexAccessKind::Read}});
    case PexOp::ArrayRemoveLast:
    case PexOp::ArrayClear:
    case PexOp::StructCreate:
      return make_accesses({{0, PexAccessKind::Write}});
    case PexOp::StructGet:
      return make_accesses({{0, PexAccessKind::Write}, {1, PexAccessKind::Read}});
    case PexOp::StructSet:
      return make_accesses({{0, PexAccessKind::Write}, {2, PexAccessKind::Read}});
    case PexOp::ArrayFindStruct:
    case PexOp::ArrayRFindStruct:
      return make_accesses({{0, PexAccessKind::Read},
                            {1, PexAccessKind::Write},
                            {3, PexAccessKind::Read},
                            {4, PexAccessKind::Read}});
    default:
      return AccessList{};
  }
}

std::size_t call_result_operand(std::uint8_t op) {
  switch (static_cast<PexOp>(op)) {
    case PexOp::CallMethod:
      return 2;
    case PexOp::CallParent:
      return 1;
    case PexOp::CallStatic:
      return 2;
    default:
      return kNoIndex;
  }
}

struct DeclaredName {
  std::string name;
  std::string type;
  bool isProperty;
};

std::vector<DeclaredName> declared_names(const fo4::pex::PexObject& object) {
  std::vector<DeclaredName> names;
  names.reserve(object.variables.size() + object.properties.size());
  for (const auto& variable : object.variables) {
    names.push_back(DeclaredName{variable.name, variable.type, false});
  }
  for (const auto& property : object.properties) {
    names.push_back(DeclaredName{property.name, property.type, true});
  }
  return names;
}

const DeclaredName* find_declared(const std::vector<DeclaredName>& names,
                                  const std::string& name) {
  for (const auto& declared : names) {
    if (pex_name_equal(declared.name, name)) return &declared;
  }
  return nullptr;
}

std::string declared_type_of(const fo4::pex::PexFunction& function, const std::string& name,
                             const std::vector<DeclaredName>& declared) {
  for (const auto& param : function.params) {
    if (pex_name_equal(param.name, name)) return param.type;
  }
  for (const auto& local : function.locals) {
    if (pex_name_equal(local.name, name)) return local.type;
  }
  const auto* match = find_declared(declared, name);
  return match != nullptr ? match->type : std::string{};
}

PexFunctionRef make_ref(std::size_t fileIndex, std::size_t objectIndex,
                        const fo4::pex::PexObject& object, const fo4::pex::PexState& state,
                        std::size_t stateIndex, const fo4::pex::PexFunction& function,
                        std::size_t functionIndex) {
  PexFunctionRef ref;
  ref.fileIndex = fileIndex;
  ref.objectIndex = objectIndex;
  ref.stateIndex = stateIndex;
  ref.functionIndex = functionIndex;
  ref.slot = PexFunctionSlot::State;
  ref.objectName = object.name;
  ref.stateName = state.name;
  ref.functionName = function.name;
  return ref;
}

PexFunctionRef make_handler_ref(std::size_t fileIndex, std::size_t objectIndex,
                                const fo4::pex::PexObject& object,
                                const fo4::pex::PexProperty& property,
                                std::size_t propertyIndex, PexFunctionSlot slot) {
  PexFunctionRef ref;
  ref.fileIndex = fileIndex;
  ref.objectIndex = objectIndex;
  ref.functionIndex = propertyIndex;
  ref.slot = slot;
  ref.objectName = object.name;
  ref.functionName = property.name;
  return ref;
}

bool ref_less(const PexFunctionRef& left, const PexFunctionRef& right) {
  if (left.fileIndex != right.fileIndex) return left.fileIndex < right.fileIndex;
  if (left.objectIndex != right.objectIndex) return left.objectIndex < right.objectIndex;
  if (left.slot != right.slot) return left.slot < right.slot;
  if (left.stateIndex != right.stateIndex) return left.stateIndex < right.stateIndex;
  return left.functionIndex < right.functionIndex;
}

void record_access(std::vector<PexVariableAccess>& out, const PexFunctionRef& function,
                   std::size_t instructionIndex, const DeclaredName& declared,
                   PexAccessKind kind) {
  PexVariableAccess entry;
  entry.function = function;
  entry.instructionIndex = instructionIndex;
  entry.name = declared.name;
  entry.type = declared.type;
  entry.isProperty = declared.isProperty;
  entry.kind = kind;
  out.push_back(entry);
}

void collect_accesses(const fo4::pex::PexFunction& function, const PexFunctionRef& ref,
                      const std::vector<DeclaredName>& declared,
                      std::vector<PexVariableAccess>& out) {
  for (std::size_t i = 0; i < function.instructions.size(); ++i) {
    const auto& instruction = function.instructions[i];
    if (pex_is_call(instruction.opCode)) {
      const auto result = call_result_operand(instruction.opCode);
      if (instruction.opCode == static_cast<std::uint8_t>(PexOp::CallMethod) &&
          instruction.args.size() > 1) {
        const auto& expression = instruction.args[1];
        if (expression.type == fo4::pex::PexValueType::Identifier) {
          const auto* match = find_declared(declared, expression.str);
          if (match != nullptr) {
            record_access(out, ref, i, *match, PexAccessKind::Read);
          }
        }
      }
      for (auto a = result; a < instruction.args.size(); ++a) {
        const auto& argument = instruction.args[a];
        if (argument.type != fo4::pex::PexValueType::Identifier) continue;
        const auto* match = find_declared(declared, argument.str);
        if (match == nullptr) continue;
        record_access(out, ref, i, *match,
                      a == result ? PexAccessKind::Write : PexAccessKind::Read);
      }
      continue;
    }
    const auto accesses = operand_accesses(instruction.opCode);
    for (std::size_t k = 0; k < accesses.count; ++k) {
      const auto& access = accesses.items[k];
      if (access.operand >= instruction.args.size()) continue;
      const auto& argument = instruction.args[access.operand];
      if (argument.type != fo4::pex::PexValueType::Identifier) continue;
      const auto* match = find_declared(declared, argument.str);
      if (match == nullptr) continue;
      record_access(out, ref, i, *match, access.kind);
    }
  }
}

}

bool pex_ref_equal(const PexFunctionRef& left, const PexFunctionRef& right) {
  return left.fileIndex == right.fileIndex && left.objectIndex == right.objectIndex &&
         left.slot == right.slot && left.stateIndex == right.stateIndex &&
         left.functionIndex == right.functionIndex;
}

bool pex_name_equal(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (lowered_char(left[i]) != lowered_char(right[i])) return false;
  }
  return true;
}

bool pex_find_object(const fo4::pex::PexFile& file, const std::string& name,
                     std::size_t* outObjectIndex) {
  for (std::size_t o = 0; o < file.objects.size(); ++o) {
    if (pex_name_equal(file.objects[o].name, name)) {
      if (outObjectIndex != nullptr) *outObjectIndex = o;
      return true;
    }
  }
  return false;
}

bool pex_find_function(const fo4::pex::PexObject& object, const std::string& name,
                       std::size_t* outStateIndex, std::size_t* outFunctionIndex) {
  for (std::size_t s = 0; s < object.states.size(); ++s) {
    const auto& state = object.states[s];
    for (std::size_t f = 0; f < state.functions.size(); ++f) {
      if (pex_name_equal(state.functions[f].name, name)) {
        if (outStateIndex != nullptr) *outStateIndex = s;
        if (outFunctionIndex != nullptr) *outFunctionIndex = f;
        return true;
      }
    }
  }
  return false;
}

const fo4::pex::PexFunction* pex_function_at(const fo4::pex::PexFile& file,
                                             const PexFunctionRef& ref) {
  if (ref.objectIndex >= file.objects.size()) return nullptr;
  const auto& object = file.objects[ref.objectIndex];
  if (ref.slot == PexFunctionSlot::State) {
    if (ref.stateIndex >= object.states.size()) return nullptr;
    const auto& state = object.states[ref.stateIndex];
    if (ref.functionIndex >= state.functions.size()) return nullptr;
    return &state.functions[ref.functionIndex];
  }
  if (ref.functionIndex >= object.properties.size()) return nullptr;
  const auto& property = object.properties[ref.functionIndex];
  if (ref.slot == PexFunctionSlot::PropertyRead) {
    return property.readHandler ? &*property.readHandler : nullptr;
  }
  return property.writeHandler ? &*property.writeHandler : nullptr;
}

PexCallGraph build_call_graph(std::span<const fo4::pex::PexFile* const> files) {
  PexCallGraph graph;
  graph.scripts.resize(files.size());

  for (std::size_t f = 0; f < files.size(); ++f) {
    const auto* file = files[f];
    auto& script = graph.scripts[f];
    script.fileIndex = f;
    if (file == nullptr) continue;
    script.sourceFileName = file->sourceFileName;
    for (std::size_t o = 0; o < file->objects.size(); ++o) {
      const auto& object = file->objects[o];
      for (std::size_t s = 0; s < object.states.size(); ++s) {
        const auto& state = object.states[s];
        for (std::size_t k = 0; k < state.functions.size(); ++k) {
          script.functions.push_back(make_ref(f, o, object, state, s, state.functions[k], k));
        }
      }
      for (std::size_t p = 0; p < object.properties.size(); ++p) {
        const auto& property = object.properties[p];
        if (property.readHandler) {
          script.functions.push_back(
              make_handler_ref(f, o, object, property, p, PexFunctionSlot::PropertyRead));
        }
        if (property.writeHandler) {
          script.functions.push_back(
              make_handler_ref(f, o, object, property, p, PexFunctionSlot::PropertyWrite));
        }
      }
    }
  }

  for (std::size_t f = 0; f < files.size(); ++f) {
    const auto* file = files[f];
    if (file == nullptr) continue;
    auto& script = graph.scripts[f];
    for (std::size_t o = 0; o < file->objects.size(); ++o) {
      const auto& object = file->objects[o];
      const auto declared = declared_names(object);
      const auto collect_calls = [&](const fo4::pex::PexFunction& function,
                                     const PexFunctionRef& caller) {
        for (std::size_t i = 0; i < function.instructions.size(); ++i) {
          const auto& instruction = function.instructions[i];
          if (!pex_is_call(instruction.opCode)) continue;

          PexCallSite site;
          site.caller = caller;
          site.instructionIndex = i;
          site.kind = instruction.opCode == static_cast<std::uint8_t>(PexOp::CallMethod)
                          ? PexCallKind::Method
                          : (instruction.opCode ==
                                     static_cast<std::uint8_t>(PexOp::CallParent)
                                 ? PexCallKind::Parent
                                 : PexCallKind::Static);

          std::string targetObject;
          std::size_t nameOperand = 0;
          if (site.kind == PexCallKind::Static) {
            if (!instruction.args.empty() &&
                instruction.args[0].type == fo4::pex::PexValueType::Identifier) {
              targetObject = instruction.args[0].str;
            }
            nameOperand = 1;
          } else if (site.kind == PexCallKind::Parent) {
            targetObject = object.parentClassName;
          } else if (instruction.args.size() > 1 &&
                     instruction.args[1].type == fo4::pex::PexValueType::Identifier) {
            const auto& expression = instruction.args[1];
            if (pex_name_equal(expression.str, "self")) {
              targetObject = object.name;
              site.selfTarget = true;
            } else {
              targetObject = declared_type_of(function, expression.str, declared);
              if (targetObject.empty()) targetObject = expression.str;
            }
          }
          if (instruction.args.size() > nameOperand &&
              instruction.args[nameOperand].type == fo4::pex::PexValueType::Identifier) {
            site.functionName = instruction.args[nameOperand].str;
          }
          site.objectName = targetObject;

          if (!targetObject.empty() && !site.functionName.empty()) {
            for (std::size_t g = 0; g < files.size() && !site.resolved; ++g) {
              if (files[g] == nullptr) continue;
              std::size_t targetObjectIndex = kNoIndex;
              if (!pex_find_object(*files[g], targetObject, &targetObjectIndex)) continue;
              const auto& target = files[g]->objects[targetObjectIndex];
              std::size_t stateIndex = kNoIndex;
              std::size_t functionIndex = kNoIndex;
              if (!pex_find_function(target, site.functionName, &stateIndex, &functionIndex)) {
                continue;
              }
              const auto& targetState = target.states[stateIndex];
              const auto& targetFunction = targetState.functions[functionIndex];
              site.target = make_ref(g, targetObjectIndex, target, targetState, stateIndex,
                                     targetFunction, functionIndex);
              site.resolved = true;
              site.nativeTarget = targetFunction.isNative;
            }
          }
          if (site.resolved) {
            if (site.nativeTarget) ++graph.nativeCalls;
          } else {
            ++graph.unresolvedCalls;
          }
          script.calls.push_back(site);
          graph.calls.push_back(site);
        }
      };
      for (std::size_t s = 0; s < object.states.size(); ++s) {
        const auto& state = object.states[s];
        for (std::size_t k = 0; k < state.functions.size(); ++k) {
          const auto& function = state.functions[k];
          const auto caller = make_ref(f, o, object, state, s, function, k);
          collect_accesses(function, caller, declared, script.accesses);
          collect_calls(function, caller);
        }
      }
      // Invariant: property handlers hold call sites too, so they are scanned like state functions.
      for (std::size_t p = 0; p < object.properties.size(); ++p) {
        const auto& property = object.properties[p];
        if (property.readHandler) {
          const auto ref =
              make_handler_ref(f, o, object, property, p, PexFunctionSlot::PropertyRead);
          collect_accesses(*property.readHandler, ref, declared, script.accesses);
          collect_calls(*property.readHandler, ref);
        }
        if (property.writeHandler) {
          const auto ref =
              make_handler_ref(f, o, object, property, p, PexFunctionSlot::PropertyWrite);
          collect_accesses(*property.writeHandler, ref, declared, script.accesses);
          collect_calls(*property.writeHandler, ref);
        }
      }
    }
  }

  for (const auto& script : graph.scripts) {
    for (const auto& function : script.functions) {
      const bool called =
          std::any_of(graph.calls.begin(), graph.calls.end(), [&](const PexCallSite& site) {
            return site.resolved && pex_ref_equal(site.target, function);
          });
      if (!called) graph.roots.push_back(function);
    }
  }
  std::sort(graph.roots.begin(), graph.roots.end(), ref_less);

  return graph;
}

}
