#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

enum class QuestAliasKind {
  reference,
  location,
  collection,
};

class QuestError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// One alias is kept as an ordered subrecord sequence because reference and
// location aliases are repeatable tagged unions with conditions/arrays inside
// them. Semantic members are added incrementally, but the raw ordered sequence
// is preserved from day one so unknown/legacy members are never collapsed by
// signature.
struct QuestAlias {
  QuestAliasKind kind{QuestAliasKind::reference};
  std::uint32_t id{0};
  std::vector<Subrecord> subrecords;
};

struct QuestAliases {
  std::uint32_t next_alias_id{0};
  std::vector<QuestAlias> aliases;
};

QuestAliases decode_quest_aliases(const Plugin& plugin, const FormKey& key);

}  // namespace fo4::forms
