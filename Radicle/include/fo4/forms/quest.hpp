#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
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

struct QuestSpecificReferenceFill {
  std::optional<FormKey> reference;
};

struct QuestUniqueActorFill {
  FormKey actor;
};

struct QuestLocationAliasReferenceFill {
  std::int32_t alias{0};
  FormKey reference_type;
};

struct QuestExternalAliasReferenceFill {
  std::optional<FormKey> quest;
  std::int32_t alias{0};
};

using QuestAliasFill = std::variant<
    std::monostate,
    QuestSpecificReferenceFill,
    QuestUniqueActorFill,
    QuestLocationAliasReferenceFill,
    QuestExternalAliasReferenceFill>;

// One alias is kept as an ordered subrecord sequence because reference and
// location aliases are repeatable tagged unions with conditions/arrays inside
// them. Semantic members are added incrementally, but the raw ordered sequence
// is preserved from day one so unknown/legacy members are never collapsed by
// signature.
struct QuestAlias {
  QuestAliasKind kind{QuestAliasKind::reference};
  std::uint32_t id{0};
  std::optional<std::string> name;
  std::optional<std::uint32_t> flags;
  std::optional<std::int32_t> force_into_alias;
  std::optional<std::uint8_t> max_initial_fill_count;
  QuestAliasFill fill;
  std::vector<Subrecord> subrecords;
};

struct QuestAliasCreation {
  QuestAliasKind kind{QuestAliasKind::reference};
  std::uint32_t id{0};
  std::optional<std::string_view> name;
  std::optional<std::uint32_t> flags;
  std::optional<std::int32_t> force_into_alias;
  std::optional<std::uint8_t> max_initial_fill_count;
  QuestAliasFill fill;
};

struct QuestAliases {
  std::uint32_t next_alias_id{0};
  std::vector<QuestAlias> aliases;
};

// Base alias encoding is available without a plugin context only when no
// FormLink-bearing fill is requested.
QuestAlias encode_quest_alias(const QuestAliasCreation& creation);
QuestAlias encode_quest_alias(const Plugin& plugin,
                              const QuestAliasCreation& creation);
QuestAliases decode_quest_aliases(const Plugin& plugin, const FormKey& key);

// Mutates only the xEdit-proven contiguous Alias array immediately following
// ANAM. Add/replace encode through the destination plugin's master table.
// ANAM Next Alias ID grows monotonically when an inserted ID reaches/exceeds it.
void add_quest_alias(Plugin& plugin,
                     const FormKey& quest,
                     const QuestAliasCreation& creation);
void replace_quest_alias(Plugin& plugin,
                         const FormKey& quest,
                         std::uint32_t alias_id,
                         const QuestAliasCreation& creation);
bool remove_quest_alias(Plugin& plugin,
                        const FormKey& quest,
                        std::uint32_t alias_id);

}  // namespace fo4::forms
