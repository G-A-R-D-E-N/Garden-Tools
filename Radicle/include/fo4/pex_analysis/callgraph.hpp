#pragma once

#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/opcodes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::pex_analysis {

enum class PexCallKind : std::uint8_t { Method = 0, Parent = 1, Static = 2 };

enum class PexAccessKind : std::uint8_t { Read = 0, Write = 1 };

enum class PexFunctionSlot : std::uint8_t {
  State = 0,
  PropertyRead = 1,
  PropertyWrite = 2,
};

struct PexFunctionRef {
  std::size_t fileIndex{kNoIndex};
  std::size_t objectIndex{kNoIndex};
  std::size_t stateIndex{kNoIndex};
  std::size_t functionIndex{kNoIndex};
  PexFunctionSlot slot{PexFunctionSlot::State};
  std::string objectName;
  std::string stateName;
  std::string functionName;
};

struct PexCallSite {
  PexFunctionRef caller;
  std::size_t instructionIndex{kNoIndex};
  PexCallKind kind{PexCallKind::Method};
  std::string objectName;
  std::string functionName;
  bool selfTarget{false};
  bool resolved{false};
  bool nativeTarget{false};
  PexFunctionRef target;
};

struct PexVariableAccess {
  PexFunctionRef function;
  std::size_t instructionIndex{kNoIndex};
  std::string name;
  std::string type;
  bool isProperty{false};
  PexAccessKind kind{PexAccessKind::Read};
};

struct PexScriptAnalysis {
  std::size_t fileIndex{kNoIndex};
  std::string sourceFileName;
  std::vector<PexFunctionRef> functions;
  std::vector<PexCallSite> calls;
  std::vector<PexVariableAccess> accesses;
};

struct PexCallGraph {
  std::vector<PexScriptAnalysis> scripts;
  std::vector<PexCallSite> calls;
  std::vector<PexFunctionRef> roots;
  std::size_t unresolvedCalls{0};
  std::size_t nativeCalls{0};
};

const fo4::pex::PexFunction* pex_function_at(const fo4::pex::PexFile& file,
                                             const PexFunctionRef& ref);

bool pex_ref_equal(const PexFunctionRef& left, const PexFunctionRef& right);

bool pex_name_equal(std::string_view left, std::string_view right);

bool pex_find_object(const fo4::pex::PexFile& file, const std::string& name,
                     std::size_t* outObjectIndex);

bool pex_find_function(const fo4::pex::PexObject& object, const std::string& name,
                       std::size_t* outStateIndex, std::size_t* outFunctionIndex);

PexCallGraph build_call_graph(std::span<const fo4::pex::PexFile* const> files);

}
