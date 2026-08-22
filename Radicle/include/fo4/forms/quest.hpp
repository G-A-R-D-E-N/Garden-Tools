#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/localized_fields.hpp"

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

struct QuestCreateReferenceFill {
  std::optional<FormKey> object;
  std::int16_t alias{0};
  std::uint16_t create_mode{0};
};

struct QuestMatchingReferenceNearAlias {
  std::int32_t alias{0};
  std::uint32_t type{0};
};

struct QuestMatchingReferenceEvent {
  std::uint32_t event{0};
  std::uint32_t data{0};
};

struct QuestMatchingReferenceClosest {
  std::int32_t alias{0};
};

using QuestMatchingReferenceSelector = std::variant<
    QuestMatchingReferenceNearAlias,
    QuestMatchingReferenceEvent,
    QuestMatchingReferenceClosest>;

struct QuestFindMatchingReferenceFill {
  QuestMatchingReferenceSelector selector;
};

struct QuestSpecificLocationFill {
  FormKey location;
};

struct QuestReferenceAliasLocationFill {
  std::int32_t alias{0};
  std::optional<FormKey> keyword;
};

struct QuestExternalAliasLocationFill {
  std::optional<FormKey> quest;
  std::optional<std::int32_t> alias;
};

struct QuestFindMatchingLocationFill {
  std::uint32_t event{0};
  std::uint32_t data{0};
};

using QuestAliasFill = std::variant<
    std::monostate,
    QuestSpecificReferenceFill,
    QuestUniqueActorFill,
    QuestLocationAliasReferenceFill,
    QuestExternalAliasReferenceFill,
    QuestCreateReferenceFill,
    QuestFindMatchingReferenceFill,
    QuestSpecificLocationFill,
    QuestReferenceAliasLocationFill,
    QuestExternalAliasLocationFill,
    QuestFindMatchingLocationFill>;

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
  std::optional<std::int32_t> closest_to_alias;
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
  std::optional<std::int32_t> closest_to_alias;
  QuestAliasFill fill;
};

/*
 * Quest stages, measured on Fallout4.esm's 1,336 QUST records (8,680 stages,
 * 9,058 log entries).
 *
 * The array runs from the first INDX up to the first QOBJ or ANAM:
 *
 *   INDX (4 bytes: u16 index, u8 flags, u8 unknown)
 *     QSDT (1 byte flags)          -- one log entry; 0..9 of them per stage
 *       CNAM  log text, localized  -- 4 bytes in all 921 shipped instances
 *       NAM2  note text, inline    -- never localized
 *       CTDA/CIS2 per-entry conditions
 *
 * NAM0 does not occur in FO4 quest stages at all (0 of 8,680) despite being a
 * documented Skyrim stage field, so it is not modelled.
 */
struct QuestLogEntry {
  // QSDT: bit 0 completes the quest, bit 1 fails it. Observed values 0, 1, 2.
  std::uint8_t flags{0};
  std::optional<schema::LocalizedString> log_text;
  std::optional<std::string> note;
  // Ordered remainder (conditions and anything unmodelled) kept verbatim so a
  // rewrite never collapses members by signature.
  std::vector<Subrecord> subrecords;
};

struct QuestStage {
  std::uint16_t index{0};
  std::uint8_t flags{0};
  // Not always zero in shipped data, so it is preserved rather than rebuilt.
  std::uint8_t unknown{0};
  // 63 shipped stages carry no log entry at all, so this may be empty.
  std::vector<QuestLogEntry> log_entries;
};

struct QuestStages {
  std::vector<QuestStage> stages;
};

QuestStages decode_quest_stages(const Plugin& plugin, const FormKey& quest);

/*
 * Quest objectives, measured on Fallout4.esm (1,198 objectives, 1,500
 * targets). The array runs from the first QOBJ up to ANAM:
 *
 *   QOBJ (u16 objective index)
 *     FNAM (u32 objective flags)    -- exactly one per objective
 *     NNAM (display text, localized) -- exactly one; 4 bytes in all 1,198
 *     QSTA (12 bytes)               -- one target; 0..19 of them
 *       CTDA / CIS1 / CIS2 per-target conditions
 */
struct QuestObjectiveTarget {
  // QSTA: int32 alias id at 0, uint32 flags at 4, optional keyword FormID at
  // 8. The keyword is zero in 1,498 of the 1,500 shipped targets.
  std::int32_t alias{0};
  std::uint32_t flags{0};
  std::optional<FormKey> keyword;
  // Per-target conditions and anything unmodelled, kept in order.
  std::vector<Subrecord> subrecords;
};

struct QuestObjective {
  std::uint16_t index{0};
  std::uint32_t flags{0};
  std::optional<schema::LocalizedString> display_text;
  // 66 shipped objectives carry no target at all.
  std::vector<QuestObjectiveTarget> targets;
};

struct QuestObjectives {
  std::vector<QuestObjective> objectives;
};

QuestObjectives decode_quest_objectives(const Plugin& plugin, const FormKey& quest);

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
