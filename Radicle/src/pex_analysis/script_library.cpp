#include <fo4/pex_analysis/script_library.hpp>

#include <fo4/pex_analysis/callgraph.hpp>
#include <fo4/pex_analysis/opcodes.hpp>

#include <algorithm>
#include <exception>
#include <optional>
#include <unordered_set>

namespace fo4::pex_analysis {

namespace {

std::string fold(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
  }
  return out;
}

std::string set_name_of(std::string_view object_name) {
  std::string name = fold(object_name);
  std::replace(name.begin(), name.end(), ':', '/');
  return name;
}

bool is_name_operand(const fo4::pex::PexValue& value) {
  return value.type == fo4::pex::PexValueType::Identifier || value.type == fo4::pex::PexValueType::String;
}

bool is_blank(std::string_view text) { return text.find_first_not_of(" \t\r\n") == std::string_view::npos; }

const fo4::pex::PexFunction* find_default_state_function(const fo4::pex::PexObject& object, std::string_view name) {
  for (const auto& state : object.states) {
    if (!state.name.empty()) continue;
    for (const auto& function : state.functions) {
      if (pex_name_equal(function.name, name)) return &function;
    }
  }
  return nullptr;
}

struct FunctionEntry {
  const std::string* state;
  std::string name;
  const fo4::pex::PexFunction* function;
};

std::vector<FunctionEntry> ordered_functions(const fo4::pex::PexObject& object) {
  std::vector<FunctionEntry> entries;
  for (const auto& state : object.states) {
    for (const auto& function : state.functions) entries.push_back({&state.name, function.name, &function});
  }
  for (const auto& property : object.properties) {
    if (property.readHandler) entries.push_back({nullptr, property.name + ".get", &*property.readHandler});
    if (property.writeHandler) entries.push_back({nullptr, property.name + ".set", &*property.writeHandler});
  }
  std::stable_sort(entries.begin(), entries.end(), [](const FunctionEntry& left, const FunctionEntry& right) {
    if ((left.state == nullptr) != (right.state == nullptr)) return left.state == nullptr;
    if (left.state != nullptr && *left.state != *right.state) return *left.state < *right.state;
    return left.name < right.name;
  });
  return entries;
}

std::optional<std::string> receiver_type(const fo4::pex::PexObject& object, const fo4::pex::PexFunction& function,
                                         const fo4::pex::PexValue& receiver) {
  if (receiver.type != fo4::pex::PexValueType::Identifier) return std::nullopt;
  if (pex_name_equal(receiver.str, "self")) return object.name;
  for (const auto& param : function.params) {
    if (pex_name_equal(param.name, receiver.str)) return param.type;
  }
  for (const auto& local : function.locals) {
    if (pex_name_equal(local.name, receiver.str)) return local.type;
  }
  for (const auto& variable : object.variables) {
    if (pex_name_equal(variable.name, receiver.str)) return variable.type;
  }
  return std::nullopt;
}

void collect_object(ScriptLibrary& library, const fo4::pex::PexObject& object, ScriptCensus& census) {
  constexpr auto method_op = static_cast<std::uint8_t>(PexOp::CallMethod);
  constexpr auto parent_op = static_cast<std::uint8_t>(PexOp::CallParent);
  constexpr auto static_op = static_cast<std::uint8_t>(PexOp::CallStatic);
  for (const auto& entry : ordered_functions(object)) {
    const auto& instructions = entry.function->instructions;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
      const auto& instruction = instructions[index];
      if (!pex_is_call(instruction.opCode)) continue;
      const auto name_at = pex_call_operand_index(instruction.opCode, PexOperandRole::FunctionName);
      if (instruction.args.size() <= name_at || !is_name_operand(instruction.args[name_at])) continue;
      const std::string& called = instruction.args[name_at].str;
      std::optional<std::string> target;
      if (instruction.opCode == parent_op) {
        if (!object.parentClassName.empty()) target = object.parentClassName;
      } else if (instruction.opCode == static_op) {
        const auto script_at = pex_call_operand_index(instruction.opCode, PexOperandRole::OwningScript);
        if (instruction.args.size() <= script_at || !is_name_operand(instruction.args[script_at])) continue;
        target = instruction.args[script_at].str;
      } else {
        const auto receiver_at = pex_call_operand_index(instruction.opCode, PexOperandRole::Receiver);
        if (instruction.args.size() <= receiver_at) continue;
        target = receiver_type(object, *entry.function, instruction.args[receiver_at]);
      }

      std::unordered_set<std::string> visited;
      const fo4::pex::PexFunction* declared = nullptr;
      const fo4::pex::PexFunction* native_declaration = nullptr;
      std::string current = target.value_or(std::string());
      while (!is_blank(current)) {
        const auto* owner = library.find_object(current);
        if (owner == nullptr) break;
        if (!visited.insert(fold(current)).second) {
          declared = native_declaration = nullptr;
          break;
        }
        declared = find_default_state_function(*owner, called);
        if (instruction.opCode != method_op || (declared != nullptr && !declared->isNative)) break;
        if (native_declaration == nullptr) native_declaration = declared;
        current = owner->parentClassName;
      }
      if (declared == nullptr) declared = native_declaration;
      if (declared == nullptr) {
        census.unresolved.push_back(object.name + "[" + (entry.state != nullptr ? *entry.state : "<property>") + "]." +
                                    entry.name + "+" + std::to_string(index) + ": no resolved declaration for " +
                                    (target ? *target : "<unknown>") + "." + called);
        continue;
      }
      if (!declared->isNative) {
        ++census.script_calls;
        continue;
      }
      census.sites.push_back({object.name, entry.name, index, instruction.opCode == static_op, *target, called});
    }
  }
}

}  // namespace

ScriptLibrary::ScriptLibrary(const fo4::archive::AssetResolver& resolver, std::span<const ScriptWinner> winners)
    : resolver_(&resolver), winners_(winners) {}

const fo4::pex::PexFile* ScriptLibrary::file_at(std::size_t winner_index) {
  if (winner_index >= winners_.size()) return nullptr;
  const auto cached = files_.find(winner_index);
  if (cached != files_.end()) return cached->second.get();
  std::unique_ptr<fo4::pex::PexFile> parsed;
  try {
    const auto bytes = resolver_->read(winners_[winner_index].winner.location);
    std::vector<std::uint8_t> raw(bytes.size());
    std::transform(bytes.begin(), bytes.end(), raw.begin(),
                   [](std::byte value) { return static_cast<std::uint8_t>(value); });
    parsed = std::make_unique<fo4::pex::PexFile>(fo4::pex::PexFile::fromBytes(raw));
  } catch (const std::exception&) {
    parsed.reset();
  }
  return files_.emplace(winner_index, std::move(parsed)).first->second.get();
}

const fo4::pex::PexObject* ScriptLibrary::find_object(std::string_view object_name) {
  const auto wanted = set_name_of(object_name);
  const auto it = std::lower_bound(winners_.begin(), winners_.end(), wanted,
                                   [](const ScriptWinner& winner, const std::string& name) {
                                     return winner.winner.name < name;
                                   });
  if (it == winners_.end() || it->winner.name != wanted) return nullptr;
  const auto* file = file_at(static_cast<std::size_t>(it - winners_.begin()));
  if (file == nullptr) return nullptr;
  for (const auto& object : file->objects) {
    if (pex_name_equal(object.name, object_name)) return &object;
  }
  return nullptr;
}

bool parent_chain_valid(ScriptLibrary& library, std::string_view object_name) {
  std::unordered_set<std::string> visited;
  std::string name(object_name);
  while (const auto* object = library.find_object(name)) {
    if (!visited.insert(fold(object->name)).second) return false;
    if (object->parentClassName.empty()) return true;
    name = object->parentClassName;
  }
  return false;
}

ScriptCensus collect_script_census(ScriptLibrary& library) {
  ScriptCensus census;
  for (std::size_t i = 0; i < library.size(); ++i) {
    const auto* file = library.file_at(i);
    if (file == nullptr) {
      census.unreadable.push_back(library.winner(i).winner.name);
      continue;
    }
    for (const auto& object : file->objects) {
      ++census.objects;
      collect_object(library, object, census);
    }
  }
  return census;
}

}  // namespace fo4::pex_analysis
