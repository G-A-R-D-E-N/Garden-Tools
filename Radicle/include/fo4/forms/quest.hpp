#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <cstdint>
#include <optional>
#include <span>
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
  std::uint32_t level{0};
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

struct QuestCollectionEntry {
  std::uint32_t id{0};
  std::optional<std::uint8_t> max_initial_fill_count;
};

struct QuestAliasItemExtra {
  std::optional<FormKey> owner;
  std::optional<FormKey> global;
  std::optional<std::int32_t> required_rank;
  float item_condition{0.0f};
};

struct QuestAliasItem {
  FormKey item;
  std::int32_t count{1};
  std::optional<QuestAliasItemExtra> extra;
};

struct QuestLinkedAlias {
  FormKey keyword;
  std::int32_t alias{0};
};

// One alias is kept as an ordered subrecord sequence because reference and
// location aliases are repeatable tagged unions with conditions/arrays inside
// them. Semantic members are added incrementally, but the raw ordered sequence
// is preserved from day one so unknown/legacy members are never collapsed by
// signature.
struct QuestAlias {
  QuestAliasKind kind{QuestAliasKind::reference};
  // Reference/location only; unset (0) for collections (see collection_entries).
  std::uint32_t id{0};
  std::optional<std::string> name;
  std::optional<std::uint32_t> flags;
  std::optional<std::int32_t> force_into_alias;
  std::optional<std::int32_t> closest_to_alias;
  QuestAliasFill fill;
  std::vector<QuestCollectionEntry> collection_entries;
  std::vector<FormKey> keywords;
  std::vector<QuestAliasItem> items;
  std::vector<QuestLinkedAlias> linked_aliases;
  std::vector<FormKey> spells;
  std::vector<FormKey> factions;
  std::vector<FormKey> packages;
  std::optional<FormKey> spectator_override;
  std::optional<FormKey> observe_dead_body_override;
  std::optional<FormKey> guard_warn_override;
  std::optional<FormKey> combat_override;
  std::optional<FormKey> display_name;
  std::optional<FormKey> forced_voice;
  std::optional<FormKey> death_item;
  std::optional<FormKey> voice_types;
  // Subrecord signatures in this alias whose FormID names a master index
  // outside the plugin's master table. Real mods ship these dangling links --
  // OWR.esp has 28 ALFR references to index 3 with a single master -- and
  // refusing to decode would make the whole quest unreadable in an editor
  // whose job is to surface exactly that kind of problem.
  //
  // The raw subrecord is preserved verbatim in `subrecords`, so nothing is
  // lost and a rewrite is unaffected; only the semantic fill cannot name a
  // target.
  std::vector<Signature> unresolved_links;
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
  std::vector<FormKey> keywords;
  std::vector<QuestAliasItem> items;
  std::vector<QuestLinkedAlias> linked_aliases;
  std::vector<FormKey> spells;
  std::vector<FormKey> factions;
  std::vector<FormKey> packages;
  std::optional<FormKey> spectator_override;
  std::optional<FormKey> observe_dead_body_override;
  std::optional<FormKey> guard_warn_override;
  std::optional<FormKey> combat_override;
  std::optional<FormKey> display_name;
  std::optional<FormKey> forced_voice;
  std::optional<FormKey> death_item;
  std::optional<FormKey> voice_types;
  std::vector<CtdaCondition> conditions;
  std::vector<QuestCollectionEntry> collection_entries;
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
  // Decoded views of CNAM and NAM2. Read-only: `members` is what gets written.
  std::optional<schema::LocalizedString> log_text;
  std::optional<std::string> note;
  // Every member after QSDT, in file order, CNAM and NAM2 included.
  //
  // This is authoritative on encode. The shipped order is
  // QSDT [CTDA...] [NAM2] [CNAM] -- conditions come first -- so rebuilding
  // from the decoded fields instead would silently reorder the 8,000+ entries
  // that carry both.
  std::vector<Subrecord> members;
};

struct QuestStage {
  std::uint16_t index{0};
  std::uint8_t flags{0};
  // Not always zero in shipped data, so it is preserved rather than rebuilt.
  std::uint8_t unknown{0};
  // 63 shipped stages carry no log entry at all, so this may be empty.
  std::vector<QuestLogEntry> log_entries;
  // Members between INDX and the first QSDT. Bethesda never emits these, but
  // third-party plugins do (a NAM2 ahead of any log entry), and rejecting the
  // record would make the whole quest unreadable. Written back immediately
  // after INDX.
  std::vector<Subrecord> leading_members;
};

// Constraint: only 0x02, 0x04 and 0x08 are named; 0x01 and 0x10-0x80 stay unnamed (no evidence).
enum class QuestStageFlag : std::uint8_t {
  // Constraint: RVA 0xaeceb0 (E:\RE-work\scratch\ck_research\dlg_handlers.txt) writes this from control 0x875, dialog 3240 'Run on start?'.
  RunOnStart = 0x02,
  // Constraint: RVA 0xaeceb0 (E:\RE-work\scratch\ck_research\dlg_handlers.txt) writes this from control 0x876, dialog 3240 'Run on stop?'.
  RunOnStop = 0x04,
  // Constraint: RVA 0xaeceb0 (E:\RE-work\scratch\ck_research\dlg_handlers.txt) writes this from control 0x877, dialog 3240 'Keep Instance Data From Here On'.
  KeepInstanceDataFromHereOn = 0x08,
};

struct QuestStages {
  std::vector<QuestStage> stages;
};

QuestStages decode_quest_stages(const Plugin& plugin, const FormKey& quest);
QuestStages decode_quest_stages(const RecordIndex& index, const FormKey& quest);

bool lossless_quest_log_entry_conditions(std::span<const Subrecord> members,
                                         std::vector<CtdaCondition>& conditions,
                                         std::size_t& consumed);

/*
 * Quest objectives, measured on Fallout4.esm (1,198 objectives, 1,500
 * targets). The array runs from the first QOBJ up to ANAM:
 *
 *   QOBJ (u16 objective index)
 *     FNAM (u32 objective flags)    -- exactly one per objective
 *     NNAM (display text, localized) -- exactly one; 4 bytes in all 1,198
 *     QSTA (12 bytes; 8 before form version 82) -- one target; 0..19 of them
 *       CTDA / CIS1 / CIS2 per-target conditions
 */
struct QuestObjectiveTarget {
  // QSTA: int32 alias id at 0, uint32 flags at 4, optional keyword FormID at
  // 8. The keyword is zero in 1,498 of the 1,500 shipped targets.
  std::int32_t alias{0};
  std::uint32_t flags{0};
  std::optional<FormKey> keyword;
  bool keyword_unresolved{false};
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

// Constraint: only 0x01 and 0x02 are named; bits 2-31 stay unnamed (no evidence).
enum class QuestObjectiveFlag : std::uint32_t {
  // Constraint: RVA 0xa0dbc0 (E:\RE-work\scratch\ck_research\dlg_handlers.txt) reads this, dialog 3245 'ORed With Previous'.
  ORedWithPrevious = 0x01,
  // Constraint: RVA 0xa0dbc0 (E:\RE-work\scratch\ck_research\dlg_handlers.txt) reads this, dialog 3245 'No Stats Tracking'.
  NoStatsTracking = 0x02,
};

struct QuestObjectives {
  std::vector<QuestObjective> objectives;
};

QuestObjectives decode_quest_objectives(const Plugin& plugin, const FormKey& quest);
QuestObjectives decode_quest_objectives(const RecordIndex& index, const FormKey& quest);

/*
 * Authoring.
 *
 * Each writer rebuilds only its own contiguous array and splices it back into
 * the record, so stages, objectives and aliases never disturb one another:
 * the stage array is bounded by the first INDX and the first QOBJ/ANAM, the
 * objective array by the first QOBJ and ANAM.
 *
 * Round-tripping an unmodified quest reproduces its exact subrecord sequence.
 */
void write_quest_stages(Plugin& plugin, const FormKey& quest, const QuestStages& stages);
void write_quest_objectives(Plugin& plugin,
                            const FormKey& quest,
                            const QuestObjectives& objectives);

// Convenience mutations over the decode/write pair. Each preserves everything
// outside the array it touches.
void add_quest_stage(Plugin& plugin, const FormKey& quest, const QuestStage& stage);
bool remove_quest_stage(Plugin& plugin, const FormKey& quest, std::uint16_t index);
// Moves the stage with `index` to position `to` in the array order.
bool reorder_quest_stage(Plugin& plugin,
                         const FormKey& quest,
                         std::uint16_t index,
                         std::size_t to);

void add_quest_objective(Plugin& plugin,
                         const FormKey& quest,
                         const QuestObjective& objective);
bool remove_quest_objective(Plugin& plugin, const FormKey& quest, std::uint16_t index);
bool reorder_quest_objective(Plugin& plugin,
                             const FormKey& quest,
                             std::uint16_t index,
                             std::size_t to);

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
QuestAliases decode_quest_aliases(const RecordIndex& index, const FormKey& key);
// The lookup must type every COED owner it is given; it may throw QuestError to refuse one.
QuestAliases decode_quest_aliases(const Plugin& plugin,
                                  const FormKey& key,
                                  const FormLinkTypeLookup& owner_types);

void sync_quest_vmad_aliases(Record& record,
                             const QuestAliases& previous,
                             std::span<const QuestAliasCreation> next);
void sync_quest_vmad_stages(Record& record,
                            const QuestStages& previous,
                            const QuestStages& next);

// Mutates only the xEdit-proven contiguous Alias array immediately following
// ANAM. Add/replace encode through the destination plugin's master table.
// ANAM Next Alias ID grows monotonically when an inserted ID reaches/exceeds it.
void write_quest_aliases(Plugin& plugin,
                         const FormKey& quest,
                         std::span<const QuestAliasCreation> creations);
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

// Moves the alias with `alias_id` to position `to` in the array order. The
// alias's whole contiguous subrecord run moves with it, so ALCS/ALMI prefixes
// and per-alias conditions stay attached.
bool reorder_quest_alias(Plugin& plugin,
                         const FormKey& quest,
                         std::uint32_t alias_id,
                         std::size_t to);

}  // namespace fo4::forms
