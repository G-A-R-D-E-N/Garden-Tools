#pragma once

#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

enum class HkxHitKind : std::uint8_t { Object = 0, Class, Event, Variable, Asset, Name, Field };

// Invariant: this is the one spelling of a hit kind for every consumer.
const char* hkx_hit_kind_name(HkxHitKind kind);

// Invariant: text is at most kSearchTextLimit characters; object is -1 when the hit is not tied to one object.
inline constexpr std::size_t kSearchTextLimit = 500;

struct HkxSearchHit {
  HkxHitKind kind{HkxHitKind::Name};
  std::string text;
  std::int32_t object{-1};
};

struct HkxSearchResult {
  std::vector<HkxSearchHit> hits;
  bool truncated{false};
};

// Constraint: an empty query returns no hits; matching ignores ASCII case; a "#N" query also matches object index N; limit 0 means no hits are kept; a value hit reads only members the generated layout types as written scalars, strings and their arrays, and its text is "Class.path = value".
HkxStatus search_symbols(const HkxPackfile& packfile, std::string_view query, std::size_t limit, HkxSearchResult& out);

enum class HkxEventSite : std::uint8_t {
  ClipTrigger = 0,
  StateMachineChange,
  StateMachineReturnToPrevious,
  StateMachineRandomTransition,
  StateMachineNextHigher,
  StateMachineNextLower,
  Transition,
  TriggerEnter,
  TriggerExit,
  InitiateEnter,
  InitiateExit,
  ObjectMember,
};

const char* hkx_event_site_name(HkxEventSite site);

// Invariant: node indexes the model's nodes table; state indexes the states table, -1 for a wildcard or a non-transition site; object and member are set only for an ObjectMember site, where object is the packfile object index and member the "Class.path" of the event slot.
struct HkxEventSiteRow {
  HkxEventSite site{HkxEventSite::ClipTrigger};
  std::int32_t node{-1};
  std::int32_t state{-1};
  std::string owner;
  std::int32_t object{-1};
  std::string member;
};

// Invariant: in_range is false for an id outside the events table, whose name stays empty; an in-range event with no site only has no decoded site.
struct HkxEventUsage {
  std::int32_t event_id{0};
  std::string name;
  bool in_range{true};
  std::vector<HkxEventSiteRow> sites;
};

struct HkxVariableSiteRow {
  HkxGraphOwnerKind owner_kind{HkxGraphOwnerKind::StateMachine};
  std::size_t owner_index{0};
  std::string member_path;
};

struct HkxVariableUsage {
  std::int32_t variable_index{0};
  std::string name;
  bool in_range{true};
  std::vector<HkxVariableSiteRow> sites;
};

// Invariant: incoming counts transitions of the same machine, wildcards included, whose to_state_id equals this state's id.
struct HkxStateUsage {
  std::size_t state{0};
  std::string name;
  std::int32_t state_id{0};
  std::size_t state_machine{0};
  std::size_t incoming{0};
  std::size_t outgoing{0};
  bool is_start{false};
};

struct HkxSymbolUsage {
  std::vector<HkxEventUsage> events;
  std::vector<HkxVariableUsage> variables;
  std::vector<HkxStateUsage> states;
};

// Constraint: covers the event sites read_behavior_model decodes only; the sites collect_object_event_sites adds are separate, so a missing site proves nothing about use.
void collect_symbol_usage(const HkxGraphModel& model, HkxSymbolUsage& out);

// Requires usage from collect_symbol_usage of the model read from this packfile; adds the event slots of objects the model does not decode, found only through the generated layout table (struct members typed hkbEventProperty or hkbEvent, and the listed event id members). A class with no layout, or a slot the table does not type, adds nothing, so the usage stays partial.
void collect_object_event_sites(const HkxPackfile& packfile, HkxSymbolUsage& usage);

// Requires decoded_binding_sets to hold the object indexes of the binding sets the model already decoded, so their rows are not read twice; variables may be null. Reads event slots as collect_object_event_sites does and, for variables, hkbExpressionData.assignmentVariableIndex and the variableIndex of a graph-variable binding (bindingType 0); negative values are dropped.
void collect_object_slots(const HkxPackfile& packfile, std::span<const std::size_t> decoded_binding_sets,
                          std::vector<HkxGraphObjectSlot>& events, std::vector<HkxGraphObjectSlot>* variables);

}  // namespace fo4::hkx
