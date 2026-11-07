#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/script_set.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fo4::pex_analysis {

// Invariant: an object name is looked up without case, and a ':' in it stands for the '/' of the script's path.
// Invariant: a winner's payload is parsed on first use, and a payload that cannot be parsed holds no object and counts as unreadable.
// Requires the winners, ordered by name, and the resolver to outlive the library; a library is not thread-safe.
class ScriptLibrary {
public:
  ScriptLibrary(const fo4::archive::AssetResolver& resolver, std::span<const ScriptWinner> winners);

  std::size_t size() const noexcept { return winners_.size(); }
  const ScriptWinner& winner(std::size_t index) const { return winners_[index]; }
  // Invariant: null when no winner provides the object or its payload is unreadable.
  const fo4::pex::PexObject* find_object(std::string_view object_name);
  // Invariant: null when the payload is unreadable, or the index is at or past size().
  const fo4::pex::PexFile* file_at(std::size_t winner_index);

private:
  const fo4::archive::AssetResolver* resolver_;
  std::span<const ScriptWinner> winners_;
  std::unordered_map<std::size_t, std::unique_ptr<fo4::pex::PexFile>> files_;
};

// Invariant: a missing parent, an unreadable payload and a cycle are invalid; an object with no parent is valid; the walk has no depth limit.
bool parent_chain_valid(ScriptLibrary& library, std::string_view object_name);

struct CensusSite {
  std::string script;
  std::string function;
  std::size_t instruction{0};
  bool is_static{false};  // invariant: false for a method call and a parent call
  std::string target;
  std::string called_function;
};

struct ScriptCensus {
  std::size_t objects{0};
  std::vector<std::string> unreadable;  // invariant: set names of the unreadable payloads, ordered as the winners
  std::vector<CensusSite> sites;  // invariant: native call sites ordered by winner, object, state, function name, instruction
  std::size_t script_calls{0};
  std::vector<std::string> unresolved;
};

ScriptCensus collect_script_census(ScriptLibrary& library);

}  // namespace fo4::pex_analysis
