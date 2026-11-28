#include "fo4/hkx/behavior_edit.hpp"

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using detail::elements_at;
using detail::Elements;
using detail::in_section;
using detail::pointer_target;
using detail::printable_text;
using detail::value_at;

constexpr std::size_t k_pointer_size = 8;
constexpr std::size_t k_array_align = 16;
constexpr std::size_t k_name_limit = 65536;
constexpr std::size_t k_section_limit = 0x7fffffff;
constexpr std::uint32_t k_capacity_flags = 0xC0000000u;
// Offset: hkArray header is pointer(8) + size(i32 at +8) + capacityAndFlags(u32 at +12), as write.cpp lays arrays out.
constexpr std::size_t k_array_size_at = 8;
constexpr std::size_t k_array_capacity_at = 12;
constexpr std::size_t k_array_header_size = 16;
constexpr std::size_t k_index_limit = 32767;
constexpr std::string_view k_array_class = "hkbStateMachineTransitionInfoArray";

struct Fields {
  bool ok{true};

  std::size_t member(std::string_view class_name, std::string_view name) {
    const HkxClassLayout* layout = find_layout(class_name);
    const auto offset = layout ? find_member_offset(*layout, name) : std::nullopt;
    if (!offset) {
      ok = false;
      return 0;
    }
    return *offset;
  }

  std::size_t size(std::string_view class_name) {
    const HkxClassLayout* layout = find_layout(class_name);
    if (layout == nullptr) {
      ok = false;
      return 0;
    }
    return layout->size;
  }

  std::size_t graph_data{member("hkbBehaviorGraph", "data")};
  std::size_t data_event_infos{member("hkbBehaviorGraphData", "eventInfos")};
  std::size_t data_strings{member("hkbBehaviorGraphData", "stringData")};
  std::size_t strings_events{member("hkbBehaviorGraphStringData", "eventNames")};
  std::size_t event_info_size{size("hkbEventInfo")};
  std::size_t event_flags{member("hkbEventInfo", "flags")};
  std::size_t machine_wildcards{member("hkbStateMachine", "wildcardTransitions")};
  std::size_t state_transitions{member("hkbStateMachineStateInfo", "transitions")};
  std::size_t transition_array{member("hkbStateMachineTransitionInfoArray", "transitions")};
  std::size_t transition_size{size("hkbStateMachineTransitionInfo")};
  std::size_t transition_trigger{member("hkbStateMachineTransitionInfo", "triggerInterval")};
  std::size_t transition_initiate{member("hkbStateMachineTransitionInfo", "initiateInterval")};
  std::size_t transition_effect{member("hkbStateMachineTransitionInfo", "transition")};
  std::size_t transition_event{member("hkbStateMachineTransitionInfo", "eventId")};
  std::size_t transition_to{member("hkbStateMachineTransitionInfo", "toStateId")};
  std::size_t transition_from_nested{member("hkbStateMachineTransitionInfo", "fromNestedStateId")};
  std::size_t transition_to_nested{member("hkbStateMachineTransitionInfo", "toNestedStateId")};
  std::size_t transition_priority{member("hkbStateMachineTransitionInfo", "priority")};
  std::size_t transition_flags{member("hkbStateMachineTransitionInfo", "flags")};
  std::size_t interval_enter_event{member("hkbStateMachineTimeInterval", "enterEventId")};
  std::size_t interval_exit_event{member("hkbStateMachineTimeInterval", "exitEventId")};
  std::size_t interval_enter_time{member("hkbStateMachineTimeInterval", "enterTime")};
  std::size_t interval_exit_time{member("hkbStateMachineTimeInterval", "exitTime")};
  std::size_t data_variable_infos{member("hkbBehaviorGraphData", "variableInfos")};
  std::size_t data_variable_bounds{member("hkbBehaviorGraphData", "variableBounds")};
  std::size_t data_initial_values{member("hkbBehaviorGraphData", "variableInitialValues")};
  std::size_t strings_variables{member("hkbBehaviorGraphStringData", "variableNames")};
  std::size_t variable_info_size{size("hkbVariableInfo")};
  std::size_t variable_type{member("hkbVariableInfo", "type")};
  std::size_t bounds_size{size("hkbVariableBounds")};
  std::size_t values_words{member("hkbVariableValueSet", "wordVariableValues")};
  std::size_t clip_animation{member("hkbClipGenerator", "animationName")};
  std::size_t machine_start_state{member("hkbStateMachine", "startStateId")};
  std::size_t machine_states{member("hkbStateMachine", "states")};
  std::size_t state_listeners{member("hkbStateMachineStateInfo", "listeners")};
  std::size_t state_generator{member("hkbStateMachineStateInfo", "generator")};
  std::size_t state_name{member("hkbStateMachineStateInfo", "name")};
  std::size_t state_id{member("hkbStateMachineStateInfo", "stateId")};
  std::size_t state_probability{member("hkbStateMachineStateInfo", "probability")};
  std::size_t state_enable{member("hkbStateMachineStateInfo", "enable")};
  std::size_t bindable_cached{member("hkbBindable", "cachedBindables")};
  std::size_t bindable_set{member("hkbBindable", "variableBindingSet")};
  std::size_t binding_array{member("hkbVariableBindingSet", "bindings")};
  std::size_t binding_size{size("hkbVariableBindingSetBinding")};
  std::size_t binding_path{member("hkbVariableBindingSetBinding", "memberPath")};
  std::size_t binding_variable{member("hkbVariableBindingSetBinding", "variableIndex")};
  std::size_t binding_bit{member("hkbVariableBindingSetBinding", "bitIndex")};
  std::size_t blender_children{member("hkbBlenderGenerator", "children")};
  std::size_t blender_internal_states{member("hkbBlenderGenerator", "childrenInternalStates")};
  std::size_t blender_sorted{member("hkbBlenderGenerator", "sortedChildren")};
  std::size_t blender_sync_master{member("hkbBlenderGenerator", "indexOfSyncMasterChild")};
  std::size_t blender_active{member("hkbBlenderGenerator", "numActiveChildren")};
  std::size_t blender_begin{member("hkbBlenderGenerator", "beginIntervalIndex")};
  std::size_t blender_end{member("hkbBlenderGenerator", "endIntervalIndex")};
  std::size_t child_generator{member("hkbBlenderGeneratorChild", "generator")};
  std::size_t child_weight{member("hkbBlenderGeneratorChild", "weight")};
  std::size_t child_world_weight{member("hkbBlenderGeneratorChild", "worldFromModelWeight")};
  std::size_t selector_generators{member("hkbManualSelectorGenerator", "generators")};
  std::size_t selector_selected{member("hkbManualSelectorGenerator", "selectedGeneratorIndex")};
  std::size_t selector_index_selector{member("hkbManualSelectorGenerator", "indexSelector")};
  std::size_t selector_current{member("hkbManualSelectorGenerator", "currentGeneratorIndex")};
  std::size_t selector_at_activate{member("hkbManualSelectorGenerator", "generatorIndexAtActivate")};
  std::size_t selector_active{member("hkbManualSelectorGenerator", "activeTransitions")};
};

bool refuse(std::string& refusal, std::string text) {
  refusal = std::move(text);
  return false;
}

void drop_pointer(HkxSection& section, std::size_t source) {
  std::erase_if(section.local_fixups, [&](const HkxLocalFixup& fixup) { return fixup.source == source; });
  std::erase_if(section.global_fixups, [&](const HkxRemoteFixup& fixup) { return fixup.source == source; });
}

void set_local(HkxSection& section, std::size_t source, std::size_t destination) {
  drop_pointer(section, source);
  section.local_fixups.push_back({static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(destination)});
}

void set_global(HkxSection& section, std::size_t data_section, std::size_t source, std::size_t destination) {
  drop_pointer(section, source);
  section.global_fixups.push_back({static_cast<std::uint32_t>(source), static_cast<std::uint32_t>(data_section),
                                   static_cast<std::uint32_t>(destination)});
}

std::uint32_t moved(std::uint32_t source, std::int64_t delta) {
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(source) + delta);
}

void shift_fixups(HkxSection& section, std::size_t begin, std::size_t end, std::int64_t delta) {
  for (HkxLocalFixup& fixup : section.local_fixups) {
    if (fixup.source >= begin && fixup.source < end) fixup.source = moved(fixup.source, delta);
  }
  for (HkxRemoteFixup& fixup : section.global_fixups) {
    if (fixup.source >= begin && fixup.source < end) fixup.source = moved(fixup.source, delta);
  }
}

void drop_range(HkxSection& section, std::size_t begin, std::size_t end) {
  std::erase_if(section.local_fixups, [&](const HkxLocalFixup& fixup) { return fixup.source >= begin && fixup.source < end; });
  std::erase_if(section.global_fixups, [&](const HkxRemoteFixup& fixup) { return fixup.source >= begin && fixup.source < end; });
}

// Constraint: only pointers into the data section count, and the one pointer at except_source is the owner's own.
bool pointed_into(const HkxSection& section, std::size_t data_section, std::size_t begin, std::size_t end,
                  std::size_t except_source) {
  for (const HkxLocalFixup& fixup : section.local_fixups) {
    if (fixup.source != except_source && fixup.destination >= begin && fixup.destination < end) return true;
  }
  for (const HkxRemoteFixup& fixup : section.global_fixups) {
    if (fixup.section == data_section && fixup.source != except_source && fixup.destination >= begin &&
        fixup.destination < end) {
      return true;
    }
  }
  return false;
}

void sort_fixups(HkxSection& section) {
  std::sort(section.local_fixups.begin(), section.local_fixups.end(),
            [](const HkxLocalFixup& a, const HkxLocalFixup& b) { return a.source < b.source; });
  std::sort(section.global_fixups.begin(), section.global_fixups.end(),
            [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) { return a.source < b.source; });
}

struct Work {
  HkxPackfile pf;
  HkxGraphModel model;
  Fields f;
  bool failed{false};

  HkxSection& data() { return pf.sections[pf.data_section]; }
  const HkxSection& data() const { return pf.sections[pf.data_section]; }

  template <typename T>
  T get(std::size_t at) const {
    return value_at<T>(data().data, at);
  }

  template <typename T>
  void put(std::size_t at, T value) {
    HkxSection& section = data();
    if (!in_section(section, at, sizeof value)) {
      failed = true;
      return;
    }
    std::memcpy(section.data.data() + at, &value, sizeof value);
  }

  std::size_t grow(std::size_t bytes, std::size_t align) {
    HkxSection& section = data();
    const std::size_t at = (section.data.size() + align - 1) / align * align;
    if (at > k_section_limit || bytes > k_section_limit - at) {
      failed = true;
      return 0;
    }
    section.data.resize(at + bytes, std::byte{0});
    return at;
  }

  std::size_t add_string(const std::string& text) {
    HkxSection& section = data();
    const std::size_t at = section.data.size();
    if (at > k_section_limit || text.size() + 1 > k_section_limit - at) {
      failed = true;
      return 0;
    }
    section.data.resize(at + text.size() + 1, std::byte{0});
    std::memcpy(section.data.data() + at, text.data(), text.size());
    return at;
  }

  void copy_bytes(std::size_t to, std::size_t from, std::size_t count) {
    HkxSection& section = data();
    if (count == 0) return;
    if (!in_section(section, to, count) || !in_section(section, from, count)) {
      failed = true;
      return;
    }
    std::memmove(section.data.data() + to, section.data.data() + from, count);
  }

  void zero_bytes(std::size_t at, std::size_t count) {
    HkxSection& section = data();
    if (count == 0) return;
    if (!in_section(section, at, count)) {
      failed = true;
      return;
    }
    std::fill_n(section.data.begin() + static_cast<std::ptrdiff_t>(at), count, std::byte{0});
  }
};

bool load(std::span<const std::byte> bytes, Work& w, std::string& refusal) {
  w.pf = HkxPackfile{};
  w.model = HkxGraphModel{};
  if (const auto status = read_packfile(bytes, w.pf); status != HkxStatus::Ok) {
    return refuse(refusal, "the bytes are not a readable packfile (status " + std::to_string(static_cast<int>(status)) + ")");
  }
  if (const auto status = read_behavior_model(w.pf, w.model); status != HkxStatus::Ok) {
    return refuse(refusal, "the packfile is not a readable behavior graph (status " + std::to_string(static_cast<int>(status)) + ")");
  }
  if (!w.f.ok) return refuse(refusal, "the layout table lacks a behavior class this editor needs");
  if (w.pf.pointer_size != k_pointer_size) return refuse(refusal, "only 8-byte pointer packfiles are edited");
  return true;
}

std::optional<std::size_t> find_object(const HkxPackfile& pf, std::size_t offset) {
  const auto after = std::upper_bound(pf.objects.begin(), pf.objects.end(), offset,
                                      [](std::size_t value, const HkxObjectView& object) { return value < object.offset; });
  if (after == pf.objects.begin()) return std::nullopt;
  const auto found = after - 1;
  if (found->offset != offset) return std::nullopt;
  return static_cast<std::size_t>(found - pf.objects.begin());
}

std::optional<std::size_t> typed_object(const Work& w, std::size_t field, std::string_view class_name) {
  if (!in_section(w.data(), field, k_pointer_size)) return std::nullopt;
  const auto target = pointer_target(w.pf, w.data(), field);
  if (!target) return std::nullopt;
  const auto index = find_object(w.pf, *target);
  if (!index || w.pf.objects[*index].class_name != class_name) return std::nullopt;
  return index;
}

bool finite_interval(const HkxGraphTimeInterval& interval) {
  return std::isfinite(interval.enter_time) && std::isfinite(interval.exit_time);
}

bool same_interval(const HkxGraphTimeInterval& a, const HkxGraphTimeInterval& b) {
  return a.enter_event_id == b.enter_event_id && a.exit_event_id == b.exit_event_id && a.enter_time == b.enter_time &&
         a.exit_time == b.exit_time;
}

bool same_row(const HkxGraphTransition& a, const HkxGraphTransition& b) {
  return a.event_id == b.event_id && a.to_state_id == b.to_state_id && a.from_nested_state_id == b.from_nested_state_id &&
         a.to_nested_state_id == b.to_nested_state_id && a.priority == b.priority && a.flags == b.flags &&
         a.effect_node == b.effect_node && same_interval(a.trigger_interval, b.trigger_interval) &&
         same_interval(a.initiate_interval, b.initiate_interval);
}

struct Run {
  std::size_t field{0};
  std::size_t first{0};
  std::size_t count{0};
  std::optional<std::size_t> array;
  std::size_t head{0};
  Elements rows;
};

bool resolve_run(const Work& w, const HkxBehaviorEdit& e, Run& run, std::string& refusal) {
  const HkxGraphModel& model = w.model;
  if (e.state_machine >= model.state_machines.size()) return refuse(refusal, "the state machine index is outside the graph");
  const HkxGraphStateMachine& machine = model.state_machines[e.state_machine];
  if (e.state < 0) {
    run.field = w.pf.objects[model.nodes[machine.node].object].offset + w.f.machine_wildcards;
    run.first = machine.first_wildcard;
    run.count = machine.wildcard_count;
  } else {
    if (static_cast<std::size_t>(e.state) >= model.states.size() ||
        model.states[static_cast<std::size_t>(e.state)].state_machine != e.state_machine) {
      return refuse(refusal, "the state is not a state of that state machine");
    }
    const HkxGraphState& state = model.states[static_cast<std::size_t>(e.state)];
    run.field = w.pf.objects[state.object].offset + w.f.state_transitions;
    run.first = state.first_transition;
    run.count = state.transition_count;
  }
  if (!in_section(w.data(), run.field, k_pointer_size)) return refuse(refusal, "the transition array pointer is outside the data section");
  const auto target = pointer_target(w.pf, w.data(), run.field);
  if (!target) {
    if (run.count != 0) return refuse(refusal, "the transition run has rows but no array");
    return true;
  }
  const auto index = find_object(w.pf, *target);
  if (!index || w.pf.objects[*index].class_name != k_array_class) {
    return refuse(refusal, "the transition array pointer does not land on a " + std::string(k_array_class));
  }
  run.array = w.pf.objects[*index].offset;
  run.head = *run.array + w.f.transition_array;
  if (elements_at(w.data(), run.head, k_pointer_size, w.f.transition_size, run.rows) != HkxStatus::Ok) {
    return refuse(refusal, "the transition array is not readable");
  }
  if (run.rows.count != run.count) return refuse(refusal, "the transition array and the decoded run disagree");
  if (run.count != 0) {
    const bool inside = run.first + run.count <= model.transitions.size();
    if (!inside || model.transitions[run.first].array_object != *index || model.transitions[run.first].offset != run.rows.first) {
      return refuse(refusal, "the decoded rows are not the rows of the located transition array");
    }
  }
  if (pointed_into(w.data(), w.pf.data_section, *run.array, *run.array + 1, run.field)) {
    return refuse(refusal, "the transition array is shared by more than one owner");
  }
  if (run.rows.count != 0 && pointed_into(w.data(), w.pf.data_section, run.rows.first,
                                          run.rows.first + run.rows.count * w.f.transition_size, run.head)) {
    return refuse(refusal, "a transition row is pointed at from outside its array");
  }
  return true;
}

bool payload_ok(const Work& w, const HkxGraphTransition& t, std::optional<std::size_t>& effect, std::string& refusal) {
  if (!finite_interval(t.trigger_interval) || !finite_interval(t.initiate_interval)) {
    return refuse(refusal, "an interval time is not finite");
  }
  effect.reset();
  if (t.effect_node == -1) return true;
  if (t.effect_node < 0 || static_cast<std::size_t>(t.effect_node) >= w.model.nodes.size()) {
    return refuse(refusal, "the effect node is outside the nodes table");
  }
  const auto is_effect = [&](const auto& e) { return static_cast<std::int32_t>(e.node) == t.effect_node; };
  if (std::none_of(w.model.effects.begin(), w.model.effects.end(), is_effect)) {
    return refuse(refusal, "the effect node is not a transition effect");
  }
  effect = w.pf.objects[w.model.nodes[static_cast<std::size_t>(t.effect_node)].object].offset;
  return true;
}

void write_interval(Work& w, std::size_t base, const HkxGraphTimeInterval& interval) {
  w.put<std::int32_t>(base + w.f.interval_enter_event, interval.enter_event_id);
  w.put<std::int32_t>(base + w.f.interval_exit_event, interval.exit_event_id);
  w.put<float>(base + w.f.interval_enter_time, interval.enter_time);
  w.put<float>(base + w.f.interval_exit_time, interval.exit_time);
}

void write_row(Work& w, std::size_t at, const HkxGraphTransition& t, std::optional<std::size_t> effect) {
  w.put<std::int32_t>(at + w.f.transition_event, t.event_id);
  w.put<std::int32_t>(at + w.f.transition_to, t.to_state_id);
  w.put<std::int32_t>(at + w.f.transition_from_nested, t.from_nested_state_id);
  w.put<std::int32_t>(at + w.f.transition_to_nested, t.to_nested_state_id);
  w.put<std::int16_t>(at + w.f.transition_priority, t.priority);
  w.put<std::uint16_t>(at + w.f.transition_flags, t.flags);
  write_interval(w, at + w.f.transition_trigger, t.trigger_interval);
  write_interval(w, at + w.f.transition_initiate, t.initiate_interval);
  if (effect) {
    set_global(w.data(), w.pf.data_section, at + w.f.transition_effect, *effect);
  } else {
    drop_pointer(w.data(), at + w.f.transition_effect);
  }
}

// Constraint: the array is rebuilt at the end of the data section with a zeroed element at gap, because the capacity of a packfile array cannot be trusted; the old elements are zeroed and their fixups move with them.
std::size_t relocate(Work& w, std::size_t head, const Elements& old, std::size_t stride, std::size_t gap) {
  const std::uint32_t capacity = w.get<std::uint32_t>(head + k_array_capacity_at);
  const std::size_t first = w.grow((old.count + 1) * stride, k_array_align);
  if (old.count != 0) {
    w.copy_bytes(first, old.first, gap * stride);
    w.copy_bytes(first + (gap + 1) * stride, old.first + gap * stride, (old.count - gap) * stride);
    const auto low = static_cast<std::int64_t>(first) - static_cast<std::int64_t>(old.first);
    const std::size_t middle = old.first + gap * stride;
    shift_fixups(w.data(), old.first, middle, low);
    shift_fixups(w.data(), middle, old.first + old.count * stride, low + static_cast<std::int64_t>(stride));
    w.zero_bytes(old.first, old.count * stride);
  }
  set_local(w.data(), head, first);
  w.put<std::int32_t>(head + k_array_size_at, static_cast<std::int32_t>(old.count + 1));
  w.put<std::uint32_t>(head + k_array_capacity_at, (capacity & k_capacity_flags) | static_cast<std::uint32_t>(old.count + 1));
  return first;
}

bool add_transition(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  Run run;
  if (!resolve_run(w, e, run, refusal)) return false;
  if (!run.array) return refuse(refusal, "the run has no transition array");
  if (e.index > run.count) return refuse(refusal, "the insert position is past the end of the run");
  std::optional<std::size_t> effect;
  if (!payload_ok(w, e.transition, effect, refusal)) return false;
  const std::size_t first = relocate(w, run.head, run.rows, w.f.transition_size, e.index);
  write_row(w, first + e.index * w.f.transition_size, e.transition, effect);
  return true;
}

bool set_transition(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  Run run;
  if (!resolve_run(w, e, run, refusal)) return false;
  if (e.index >= run.count) return refuse(refusal, "the transition index is outside the run");
  std::optional<std::size_t> effect;
  if (!payload_ok(w, e.transition, effect, refusal)) return false;
  write_row(w, run.rows.first + e.index * w.f.transition_size, e.transition, effect);
  return true;
}

bool remove_transition(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  Run run;
  if (!resolve_run(w, e, run, refusal)) return false;
  if (e.index >= run.count) return refuse(refusal, "the transition index is outside the run");
  const std::size_t stride = w.f.transition_size;
  const std::size_t row = run.rows.first + e.index * stride;
  const std::size_t end = run.rows.first + run.count * stride;
  drop_range(w.data(), row, row + stride);
  shift_fixups(w.data(), row + stride, end, -static_cast<std::int64_t>(stride));
  w.copy_bytes(row, row + stride, end - row - stride);
  w.zero_bytes(end - stride, stride);
  w.put<std::int32_t>(run.head + k_array_size_at, static_cast<std::int32_t>(run.count - 1));
  if (run.count == 1) drop_pointer(w.data(), run.head);
  return true;
}

struct EventTables {
  std::size_t names_head{0};
  std::size_t infos_head{0};
  Elements names;
  Elements infos;
};

bool locate_events(const Work& w, EventTables& t, std::string& refusal) {
  std::optional<std::size_t> graph;
  for (std::size_t i = 0; i < w.pf.objects.size(); ++i) {
    if (w.pf.objects[i].class_name == "hkbBehaviorGraph") {
      graph = i;
      break;
    }
  }
  if (!graph) return refuse(refusal, "the file has no hkbBehaviorGraph");
  const auto data = typed_object(w, w.pf.objects[*graph].offset + w.f.graph_data, "hkbBehaviorGraphData");
  if (!data) return refuse(refusal, "the graph has no hkbBehaviorGraphData");
  const std::size_t data_offset = w.pf.objects[*data].offset;
  const auto strings = typed_object(w, data_offset + w.f.data_strings, "hkbBehaviorGraphStringData");
  if (!strings) return refuse(refusal, "the graph data has no string data");
  t.names_head = w.pf.objects[*strings].offset + w.f.strings_events;
  t.infos_head = data_offset + w.f.data_event_infos;
  if (elements_at(w.data(), t.names_head, k_pointer_size, k_pointer_size, t.names) != HkxStatus::Ok ||
      elements_at(w.data(), t.infos_head, k_pointer_size, w.f.event_info_size, t.infos) != HkxStatus::Ok) {
    return refuse(refusal, "the event tables are not readable");
  }
  if (t.names.count != t.infos.count || t.names.count != w.model.events.size()) {
    return refuse(refusal, "the event name and event info tables differ in length");
  }
  if ((t.names.count != 0 && pointed_into(w.data(), w.pf.data_section, t.names.first,
                                          t.names.first + t.names.count * k_pointer_size, t.names_head)) ||
      (t.infos.count != 0 && pointed_into(w.data(), w.pf.data_section, t.infos.first,
                                          t.infos.first + t.infos.count * w.f.event_info_size, t.infos_head))) {
    return refuse(refusal, "an event table is pointed at from outside its owner");
  }
  return true;
}

bool valid_name(const std::string& name) {
  return !name.empty() && name.size() <= k_name_limit - 1 && name.find('\0') == std::string::npos;
}

bool add_event(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  EventTables t;
  if (!locate_events(w, t, refusal)) return false;
  if (!valid_name(e.name)) return refuse(refusal, "an event name must be non-empty, free of NUL and at most 65535 bytes");
  const std::size_t count = t.names.count;
  const std::size_t names = relocate(w, t.names_head, t.names, k_pointer_size, count);
  const std::size_t text = w.add_string(e.name);
  set_local(w.data(), names + count * k_pointer_size, text);
  const std::size_t infos = relocate(w, t.infos_head, t.infos, w.f.event_info_size, count);
  w.put<std::uint32_t>(infos + count * w.f.event_info_size + w.f.event_flags, e.flags);
  return true;
}

bool rename_event(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  EventTables t;
  if (!locate_events(w, t, refusal)) return false;
  if (e.index >= t.names.count) return refuse(refusal, "the event index is outside the events table");
  if (!valid_name(e.name)) return refuse(refusal, "an event name must be non-empty, free of NUL and at most 65535 bytes");
  const std::size_t text = w.add_string(e.name);
  set_local(w.data(), t.names.first + e.index * k_pointer_size, text);
  return true;
}

bool set_event_flags(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  EventTables t;
  if (!locate_events(w, t, refusal)) return false;
  if (e.index >= t.infos.count) return refuse(refusal, "the event index is outside the events table");
  w.put<std::uint32_t>(t.infos.first + e.index * w.f.event_info_size + w.f.event_flags, e.flags);
  return true;
}

constexpr std::uint32_t k_last_authored_variable_type = 4;

struct VariableTables {
  std::size_t names_head{0};
  std::size_t infos_head{0};
  std::size_t words_head{0};
  std::size_t bounds_head{0};
  Elements names;
  Elements infos;
  Elements words;
  Elements bounds;
};

bool locate_variables(const Work& w, VariableTables& t, std::string& refusal) {
  std::optional<std::size_t> graph;
  for (std::size_t i = 0; i < w.pf.objects.size(); ++i) {
    if (w.pf.objects[i].class_name == "hkbBehaviorGraph") {
      graph = i;
      break;
    }
  }
  if (!graph) return refuse(refusal, "the file has no hkbBehaviorGraph");
  const auto data = typed_object(w, w.pf.objects[*graph].offset + w.f.graph_data, "hkbBehaviorGraphData");
  if (!data) return refuse(refusal, "the graph has no hkbBehaviorGraphData");
  const std::size_t data_offset = w.pf.objects[*data].offset;
  const auto strings = typed_object(w, data_offset + w.f.data_strings, "hkbBehaviorGraphStringData");
  if (!strings) return refuse(refusal, "the graph data has no string data");
  const auto values = typed_object(w, data_offset + w.f.data_initial_values, "hkbVariableValueSet");
  if (!values) return refuse(refusal, "the graph data has no initial variable value set");
  t.names_head = w.pf.objects[*strings].offset + w.f.strings_variables;
  t.infos_head = data_offset + w.f.data_variable_infos;
  t.words_head = w.pf.objects[*values].offset + w.f.values_words;
  t.bounds_head = data_offset + w.f.data_variable_bounds;
  if (elements_at(w.data(), t.names_head, k_pointer_size, k_pointer_size, t.names) != HkxStatus::Ok ||
      elements_at(w.data(), t.infos_head, k_pointer_size, w.f.variable_info_size, t.infos) != HkxStatus::Ok ||
      elements_at(w.data(), t.words_head, k_pointer_size, 4, t.words) != HkxStatus::Ok ||
      elements_at(w.data(), t.bounds_head, k_pointer_size, w.f.bounds_size, t.bounds) != HkxStatus::Ok) {
    return refuse(refusal, "the variable tables are not readable");
  }
  const std::size_t count = w.model.variables.size();
  if (t.names.count != count || t.infos.count != count || t.words.count != count) {
    return refuse(refusal, "the variable name, info and value tables differ in length");
  }
  if (t.bounds.count != 0 && t.bounds.count != count) return refuse(refusal, "the variable bounds table differs in length from the variables");
  const auto shared = [&](const Elements& rows, std::size_t stride, std::size_t head) {
    return rows.count != 0 && pointed_into(w.data(), w.pf.data_section, rows.first, rows.first + rows.count * stride, head);
  };
  if (shared(t.names, k_pointer_size, t.names_head) || shared(t.infos, w.f.variable_info_size, t.infos_head) ||
      shared(t.words, 4, t.words_head) || shared(t.bounds, w.f.bounds_size, t.bounds_head)) {
    return refuse(refusal, "a variable table is pointed at from outside its owner");
  }
  return true;
}

bool name_taken(const HkxGraphModel& model, const std::string& name, std::optional<std::size_t> except) {
  for (std::size_t i = 0; i < model.variables.size(); ++i) {
    if (i != except && model.variables[i].name == name) return true;
  }
  return false;
}

bool is_name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Constraint: only strings some pointer targets count, so an orphaned old name left by an earlier rename does not block the next one; the table's own pointer at slot is excluded.
bool name_unreferenced(const Work& w, const std::string& name, std::size_t slot, std::string& refusal) {
  if (name.empty()) return true;
  const std::vector<std::byte>& bytes = w.data().data;
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::vector<std::uint32_t> targets;
  for (const HkxLocalFixup& fixup : w.data().local_fixups) {
    if (fixup.source != slot) targets.push_back(fixup.destination);
  }
  for (const HkxRemoteFixup& fixup : w.data().global_fixups) {
    if (fixup.section == w.pf.data_section && fixup.source != slot) targets.push_back(fixup.destination);
  }
  std::sort(targets.begin(), targets.end());
  for (std::size_t at = text.find(name); at != std::string_view::npos; at = text.find(name, at + 1)) {
    const std::size_t end = at + name.size();
    if ((at != 0 && is_name_char(text[at - 1])) || (end < text.size() && is_name_char(text[end]))) continue;
    const std::size_t nul = text.rfind('\0', at);
    const std::size_t start = nul == std::string_view::npos ? 0 : nul + 1;
    if (std::binary_search(targets.begin(), targets.end(), static_cast<std::uint32_t>(start))) {
      return refuse(refusal, "another string in the file mentions '" + name + "', and this editor does not rewrite expression text");
    }
  }
  return true;
}

bool add_variable(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  VariableTables t;
  if (!locate_variables(w, t, refusal)) return false;
  if (!valid_name(e.name)) return refuse(refusal, "a variable name must be non-empty, free of NUL and at most 65535 bytes");
  if (e.flags > k_last_authored_variable_type) {
    return refuse(refusal, "only bool, int8, int16, int32 and real variables can be added");
  }
  if (name_taken(w.model, e.name, std::nullopt)) return refuse(refusal, "a variable named '" + e.name + "' already exists");
  const std::size_t count = t.names.count;
  const std::size_t names = relocate(w, t.names_head, t.names, k_pointer_size, count);
  const std::size_t text = w.add_string(e.name);
  set_local(w.data(), names + count * k_pointer_size, text);
  const std::size_t infos = relocate(w, t.infos_head, t.infos, w.f.variable_info_size, count);
  w.put<std::int8_t>(infos + count * w.f.variable_info_size + w.f.variable_type, static_cast<std::int8_t>(e.flags));
  relocate(w, t.words_head, t.words, 4, count);
  if (t.bounds.count != 0) relocate(w, t.bounds_head, t.bounds, w.f.bounds_size, count);
  return true;
}

bool rename_variable(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  VariableTables t;
  if (!locate_variables(w, t, refusal)) return false;
  if (e.index >= t.names.count) return refuse(refusal, "the variable index is outside the variables table");
  if (!valid_name(e.name)) return refuse(refusal, "a variable name must be non-empty, free of NUL and at most 65535 bytes");
  if (name_taken(w.model, e.name, e.index)) return refuse(refusal, "a variable named '" + e.name + "' already exists");
  const std::size_t slot = t.names.first + e.index * k_pointer_size;
  if (!name_unreferenced(w, w.model.variables[e.index].name, slot, refusal)) return false;
  set_local(w.data(), slot, w.add_string(e.name));
  return true;
}

bool set_clip_animation(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  if (e.index >= w.model.clips.size()) return refuse(refusal, "the clip index is outside the clips table");
  if (!valid_name(e.name)) return refuse(refusal, "an animation name must be non-empty, free of NUL and at most 65535 bytes");
  const std::size_t field = w.pf.objects[w.model.nodes[w.model.clips[e.index].node].object].offset + w.f.clip_animation;
  if (!in_section(w.data(), field, k_pointer_size)) return refuse(refusal, "the animation name pointer is outside the data section");
  set_local(w.data(), field, w.add_string(e.name));
  return true;
}

enum class FieldWidth : std::uint8_t { Real, Int8, UInt8 };

// Invariant: widths and ranges are the ones read_clip and read_blender decode and HavokClassTypes.json lists (PlaybackMode 0 to 3, clip ClipFlags 1 to 32).
struct FieldSpec {
  HkxGraphOwnerKind owner;
  std::string_view class_name;
  std::string_view member;
  FieldWidth width;
  std::int32_t low;
  std::int32_t high;
};

constexpr std::array<FieldSpec, 10> k_fields{{
    {HkxGraphOwnerKind::Clip, "hkbClipGenerator", "playbackSpeed", FieldWidth::Real, 0, 0},
    {HkxGraphOwnerKind::Clip, "hkbClipGenerator", "startTime", FieldWidth::Real, 0, 0},
    {HkxGraphOwnerKind::Clip, "hkbClipGenerator", "cropStartAmountLocalTime", FieldWidth::Real, 0, 0},
    {HkxGraphOwnerKind::Clip, "hkbClipGenerator", "cropEndAmountLocalTime", FieldWidth::Real, 0, 0},
    {HkxGraphOwnerKind::Clip, "hkbClipGenerator", "enforcedDuration", FieldWidth::Real, 0, 0},
    {HkxGraphOwnerKind::Clip, "hkbClipGenerator", "mode", FieldWidth::Int8, 0, 3},
    {HkxGraphOwnerKind::Clip, "hkbClipGenerator", "flags", FieldWidth::UInt8, 0, 0x3F},
    {HkxGraphOwnerKind::Blender, "hkbBlenderGenerator", "blendParameter", FieldWidth::Real, 0, 0},
    {HkxGraphOwnerKind::Blender, "hkbBlenderGenerator", "minCyclicBlendParameter", FieldWidth::Real, 0, 0},
    {HkxGraphOwnerKind::Blender, "hkbBlenderGenerator", "maxCyclicBlendParameter", FieldWidth::Real, 0, 0},
}};

constexpr std::array<FieldSpec, 2> k_child_fields{{
    {HkxGraphOwnerKind::BlendChild, "hkbBlenderGeneratorChild", "weight", FieldWidth::Real, 0, 0},
    {HkxGraphOwnerKind::BlendChild, "hkbBlenderGeneratorChild", "worldFromModelWeight", FieldWidth::Real, 0, 0},
}};

const FieldSpec* find_field(std::uint32_t owner, std::string_view member) {
  for (const FieldSpec& spec : k_fields) {
    if (static_cast<std::uint32_t>(spec.owner) == owner && spec.member == member) return &spec;
  }
  for (const FieldSpec& spec : k_child_fields) {
    if (static_cast<std::uint32_t>(spec.owner) == owner && spec.member == member) return &spec;
  }
  return nullptr;
}

std::size_t pointer_count(const HkxSection& section, std::size_t data_section, std::size_t object) {
  std::size_t count = 0;
  for (const HkxLocalFixup& fixup : section.local_fixups) count += fixup.destination == object ? 1 : 0;
  for (const HkxRemoteFixup& fixup : section.global_fixups) {
    count += fixup.section == data_section && fixup.destination == object ? 1 : 0;
  }
  return count;
}

bool set_generator_field(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  if (e.flags != static_cast<std::uint32_t>(HkxGraphOwnerKind::Blender) &&
      e.flags != static_cast<std::uint32_t>(HkxGraphOwnerKind::BlendChild) &&
      e.flags != static_cast<std::uint32_t>(HkxGraphOwnerKind::Clip)) {
    return refuse(refusal, "the owner kind is not a blender, blend child or clip");
  }
  const FieldSpec* spec = find_field(e.flags, e.name);
  if (spec == nullptr) return refuse(refusal, "this editor does not set '" + e.name + "' of that owner");
  std::size_t object = 0;
  if (spec->owner == HkxGraphOwnerKind::Clip) {
    if (e.index >= w.model.clips.size()) return refuse(refusal, "the clip index is outside the clips table");
    object = w.model.nodes[w.model.clips[e.index].node].object;
  } else if (spec->owner == HkxGraphOwnerKind::Blender) {
    if (e.index >= w.model.blenders.size()) return refuse(refusal, "the blender index is outside the blenders table");
    object = w.model.nodes[w.model.blenders[e.index].node].object;
  } else {
    if (e.index >= w.model.blend_children.size()) return refuse(refusal, "the blend child index is outside the blend children table");
    object = w.model.blend_children[e.index].object;
  }
  if (object >= w.pf.objects.size() || w.pf.objects[object].class_name != spec->class_name) {
    return refuse(refusal, "the decoded object is not a " + std::string(spec->class_name));
  }
  if (spec->width == FieldWidth::Real) {
    if (!std::isfinite(e.real)) return refuse(refusal, "'" + e.name + "' takes a finite real number in real");
  } else if (e.value < spec->low || e.value > spec->high) {
    return refuse(refusal, "'" + e.name + "' must be within " + std::to_string(spec->low) + " to " + std::to_string(spec->high));
  }
  for (const HkxGraphBinding& row : w.model.bindings) {
    if (row.owner_kind == spec->owner && row.owner_index == e.index && row.member_path.ends_with(spec->member)) {
      return refuse(refusal, "a binding drives '" + e.name + "', so its stored value is not played; " +
                                 (spec->owner == HkxGraphOwnerKind::BlendChild
                                      ? "this editor does not edit bindings of a blend child"
                                      : "retarget or remove the binding instead"));
    }
  }
  const std::size_t offset = w.pf.objects[object].offset;
  if (spec->owner == HkxGraphOwnerKind::BlendChild && pointer_count(w.data(), w.pf.data_section, offset) != 1) {
    return refuse(refusal, "the blend child info is pointed at by more than one owner");
  }
  const HkxClassLayout* layout = find_layout(spec->class_name);
  const auto member = layout ? find_member_offset(*layout, spec->member) : std::nullopt;
  if (!member) return refuse(refusal, "the layout table lacks '" + e.name + "'");
  const std::size_t at = offset + *member;
  switch (spec->width) {
    case FieldWidth::Real:
      w.put<float>(at, e.real);
      break;
    case FieldWidth::Int8:
      w.put<std::int8_t>(at, static_cast<std::int8_t>(e.value));
      break;
    case FieldWidth::UInt8:
      w.put<std::uint8_t>(at, static_cast<std::uint8_t>(e.value));
      break;
  }
  return true;
}

bool set_start_state(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  if (e.state_machine >= w.model.state_machines.size()) return refuse(refusal, "the state machine index is outside the graph");
  const HkxGraphStateMachine& machine = w.model.state_machines[e.state_machine];
  bool found = false;
  for (std::size_t i = machine.first_state; i < machine.first_state + machine.state_count; ++i) {
    found = found || w.model.states[i].state_id == e.value;
  }
  if (!found) return refuse(refusal, "the start state id names no state of that state machine");
  w.put<std::int32_t>(w.pf.objects[w.model.nodes[machine.node].object].offset + w.f.machine_start_state, e.value);
  return true;
}

constexpr std::string_view k_state_class = "hkbStateMachineStateInfo";
// Invariant: the base-class header copied unchanged from the sample is the hkReferencedObject size in HavokClassLayouts.json.
constexpr std::size_t k_object_header = 16;
// Invariant: TransitionFlags FROM_NESTED_STATE_ID_IS_VALID and TO_NESTED_STATE_ID_IS_VALID in HavokClassTypes.json.
constexpr std::uint16_t k_from_nested_flag = 0x1000;
constexpr std::uint16_t k_to_nested_flag = 0x2000;

bool reaches(const HkxGraphModel& model, std::size_t from, std::size_t target) {
  std::vector<std::vector<std::size_t>> next(model.nodes.size());
  const auto link = [&](std::size_t parent, std::int32_t child) {
    if (parent < next.size() && child >= 0 && static_cast<std::size_t>(child) < next.size()) {
      next[parent].push_back(static_cast<std::size_t>(child));
    }
  };
  for (const HkxGraphEdge& edge : model.edges) link(edge.parent, edge.child);
  for (const HkxGraphState& state : model.states) {
    if (state.state_machine < model.state_machines.size()) link(model.state_machines[state.state_machine].node, state.generator_node);
  }
  for (const HkxGraphBlendChild& child : model.blend_children) {
    if (child.blender < model.blenders.size()) link(model.blenders[child.blender].node, child.generator_node);
  }
  std::vector<bool> seen(next.size(), false);
  std::vector<std::size_t> queue{from};
  seen[from] = true;
  while (!queue.empty()) {
    const std::size_t at = queue.back();
    queue.pop_back();
    if (at == target) return true;
    for (const std::size_t to : next[at]) {
      if (seen[to]) continue;
      seen[to] = true;
      queue.push_back(to);
    }
  }
  return false;
}

bool generator_target_ok(const HkxGraphModel& model, std::int32_t node, std::size_t container, std::string_view parent,
                         std::string& refusal) {
  if (node < 0 || static_cast<std::size_t>(node) >= model.nodes.size()) {
    return refuse(refusal, "the generator node is outside the nodes table");
  }
  const std::string& class_name = model.nodes[static_cast<std::size_t>(node)].class_name;
  if (class_name != "hkbStateMachine" && !class_name.ends_with("Generator")) {
    return refuse(refusal, "the node is not a generator");
  }
  if (reaches(model, static_cast<std::size_t>(node), container)) {
    return refuse(refusal, "the generator would contain its own " + std::string(parent));
  }
  return true;
}

bool generator_node_ok(const HkxGraphModel& model, std::int32_t node, std::size_t machine, std::string& refusal) {
  return generator_target_ok(model, node, model.state_machines[machine].node, "state machine", refusal);
}

bool locate_states(const Work& w, const HkxGraphStateMachine& machine, std::size_t& head, Elements& states,
                   std::string& refusal) {
  head = w.pf.objects[w.model.nodes[machine.node].object].offset + w.f.machine_states;
  if (elements_at(w.data(), head, k_pointer_size, k_pointer_size, states) != HkxStatus::Ok) {
    return refuse(refusal, "the state array is not readable");
  }
  if (states.count != machine.state_count) return refuse(refusal, "the state array and the decoded states disagree");
  if (states.count != 0 && pointed_into(w.data(), w.pf.data_section, states.first, states.first + states.count * k_pointer_size, head)) {
    return refuse(refusal, "the state array is shared by more than one owner");
  }
  return true;
}

bool remove_state(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  if (e.state_machine >= w.model.state_machines.size()) return refuse(refusal, "the state machine index is outside the graph");
  const HkxGraphStateMachine& machine = w.model.state_machines[e.state_machine];
  if (e.state < 0 || static_cast<std::size_t>(e.state) < machine.first_state ||
      static_cast<std::size_t>(e.state) >= machine.first_state + machine.state_count) {
    return refuse(refusal, "the state is not a state of that state machine");
  }
  if (machine.state_count < 2) return refuse(refusal, "a state machine keeps at least one state");
  const HkxGraphState& state = w.model.states[static_cast<std::size_t>(e.state)];
  for (const HkxGraphTransition& t : w.model.transitions) {
    const bool to_nested = (t.flags & k_to_nested_flag) != 0 && t.to_nested_state_id == state.state_id;
    const bool from_nested = (t.flags & k_from_nested_flag) != 0 && t.from_nested_state_id == state.state_id;
    if (to_nested || from_nested) return refuse(refusal, "a nested-state transition names this state id, and nested ids are not followed");
  }
  if (machine.start_state_id == state.state_id) return refuse(refusal, "the state machine's start state id names this state; set another start state first");
  std::size_t head = 0;
  Elements states;
  if (!locate_states(w, machine, head, states, refusal)) return false;
  const std::size_t slot = states.first + (static_cast<std::size_t>(e.state) - machine.first_state) * k_pointer_size;
  const std::size_t object = w.pf.objects[state.object].offset;
  const auto target = pointer_target(w.pf, w.data(), slot);
  if (!target || *target != object) return refuse(refusal, "the state array slot does not point at the decoded state");
  if (pointed_into(w.data(), w.pf.data_section, object, object + 1, slot)) {
    return refuse(refusal, "something other than the state array points at the state");
  }
  const std::size_t end = states.first + states.count * k_pointer_size;
  drop_range(w.data(), slot, slot + k_pointer_size);
  shift_fixups(w.data(), slot + k_pointer_size, end, -static_cast<std::int64_t>(k_pointer_size));
  w.copy_bytes(slot, slot + k_pointer_size, end - slot - k_pointer_size);
  w.zero_bytes(end - k_pointer_size, k_pointer_size);
  w.put<std::int32_t>(head + k_array_size_at, static_cast<std::int32_t>(states.count - 1));
  return true;
}

constexpr std::string_view k_blender_class = "hkbBlenderGenerator";
constexpr std::string_view k_child_class = "hkbBlenderGeneratorChild";
constexpr std::string_view k_selector_class = "hkbManualSelectorGenerator";

bool driven(const Work& w, HkxGraphOwnerKind kind, std::size_t index, std::string_view member) {
  return std::any_of(w.model.bindings.begin(), w.model.bindings.end(), [&](const HkxGraphBinding& row) {
    return row.owner_kind == kind && row.owner_index == index && row.member_path.ends_with(member);
  });
}

bool read_index(const Work& w, std::size_t at, std::int16_t& out, std::string& refusal) {
  if (!in_section(w.data(), at, sizeof out)) return refuse(refusal, "an index member is outside the data section");
  out = w.get<std::int16_t>(at);
  return true;
}

// Constraint: runtime arrays must be empty because what the engine rebuilds from them on activate is not verified.
bool runtime_array_empty(const Work& w, std::size_t head, std::string_view what, std::string& refusal) {
  if (!in_section(w.data(), head, k_array_header_size)) return refuse(refusal, "an array header is outside the data section");
  if (w.get<std::int32_t>(head + k_array_size_at) != 0) {
    return refuse(refusal, std::string(what) + " is not empty, so the engine state it mirrors is not known");
  }
  return true;
}

template <typename T>
bool runtime_zero(const Work& w, std::size_t at, std::string_view what, std::string& refusal) {
  if (!in_section(w.data(), at, sizeof(T))) return refuse(refusal, "a runtime member is outside the data section");
  if (w.get<T>(at) != 0) return refuse(refusal, std::string(what) + " is not 0, so the engine state it mirrors is not known");
  return true;
}

bool locate_slots(const Work& w, std::size_t head, std::size_t decoded, Elements& slots, std::string& refusal) {
  if (elements_at(w.data(), head, k_pointer_size, k_pointer_size, slots) != HkxStatus::Ok) {
    return refuse(refusal, "the child array is not readable");
  }
  if (slots.count != decoded) return refuse(refusal, "the child array holds null or foreign slots the decoded graph skips");
  if (slots.count >= k_index_limit) return refuse(refusal, "the child count does not fit the 16-bit index the engine stores");
  if (slots.count != 0 && pointed_into(w.data(), w.pf.data_section, slots.first, slots.first + slots.count * k_pointer_size, head)) {
    return refuse(refusal, "the child array is shared by more than one owner");
  }
  return true;
}

void remove_slot(Work& w, const Elements& slots, std::size_t head, std::size_t position) {
  const std::size_t slot = slots.first + position * k_pointer_size;
  const std::size_t end = slots.first + slots.count * k_pointer_size;
  drop_range(w.data(), slot, slot + k_pointer_size);
  shift_fixups(w.data(), slot + k_pointer_size, end, -static_cast<std::int64_t>(k_pointer_size));
  w.copy_bytes(slot, slot + k_pointer_size, end - slot - k_pointer_size);
  w.zero_bytes(end - k_pointer_size, k_pointer_size);
  w.put<std::int32_t>(head + k_array_size_at, static_cast<std::int32_t>(slots.count - 1));
}

struct BlenderRef {
  std::size_t object{0};
  std::size_t head{0};
  Elements slots;
  std::int16_t sync{-1};
};

// Constraint: a blender is edited only when its child array and stored indexes match the decoded graph exactly.
bool resolve_blender(const Work& w, std::size_t index, BlenderRef& b, std::string& refusal) {
  if (index >= w.model.blenders.size()) return refuse(refusal, "the blender index is outside the blenders table");
  const HkxGraphBlender& blender = w.model.blenders[index];
  const HkxObjectView& view = w.pf.objects[w.model.nodes[blender.node].object];
  if (view.class_name != k_blender_class) return refuse(refusal, "the decoded object is not a hkbBlenderGenerator");
  if (blender.subtract_last_child) {
    return refuse(refusal, "subtractLastChild is set, so the last child takes a special weight path and adding or removing a child is not verified");
  }
  b.object = view.offset;
  b.head = b.object + w.f.blender_children;
  if (!locate_slots(w, b.head, blender.child_count, b.slots, refusal)) return false;
  if (!read_index(w, b.object + w.f.blender_sync_master, b.sync, refusal)) return false;
  if (b.sync < -1 || (b.sync >= 0 && static_cast<std::size_t>(b.sync) >= b.slots.count)) {
    return refuse(refusal, "the sync master index is already outside the children, and the engine does not bounds check it");
  }
  if (!runtime_array_empty(w, b.object + w.f.blender_internal_states, "childrenInternalStates", refusal)) return false;
  if (!runtime_array_empty(w, b.object + w.f.blender_sorted, "sortedChildren", refusal)) return false;
  return runtime_zero<std::int32_t>(w, b.object + w.f.blender_active, "numActiveChildren", refusal) &&
         runtime_zero<std::int16_t>(w, b.object + w.f.blender_begin, "beginIntervalIndex", refusal) &&
         runtime_zero<std::int16_t>(w, b.object + w.f.blender_end, "endIntervalIndex", refusal);
}

bool remove_blend_child(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  if (e.index >= w.model.blend_children.size()) return refuse(refusal, "the blend child index is outside the blend children table");
  const HkxGraphBlendChild& child = w.model.blend_children[e.index];
  BlenderRef b;
  if (!resolve_blender(w, child.blender, b, refusal)) return false;
  const HkxGraphBlender& blender = w.model.blenders[child.blender];
  if (blender.child_count < 2) return refuse(refusal, "a blender keeps at least one child");
  const std::size_t position = e.index - blender.first_child;
  if (driven(w, HkxGraphOwnerKind::Blender, child.blender, "indexOfSyncMasterChild")) {
    return refuse(refusal, "a binding drives the sync master index, so removing a child would change which child it picks");
  }
  if (b.sync == static_cast<std::int16_t>(position)) {
    return refuse(refusal, "this child is the sync master; the engine behaviour of a removed sync master is not verified");
  }
  const HkxObjectView& view = w.pf.objects[child.object];
  if (view.class_name != k_child_class) return refuse(refusal, "the decoded object is not a hkbBlenderGeneratorChild");
  const std::size_t slot = b.slots.first + position * k_pointer_size;
  const auto target = pointer_target(w.pf, w.data(), slot);
  if (!target || *target != view.offset) return refuse(refusal, "the children array slot does not point at the decoded blend child");
  if (pointed_into(w.data(), w.pf.data_section, view.offset, view.offset + 1, slot)) {
    return refuse(refusal, "something other than the children array points at the blend child");
  }
  remove_slot(w, b.slots, b.head, position);
  if (b.sync > static_cast<std::int16_t>(position)) {
    w.put<std::int16_t>(b.object + w.f.blender_sync_master, static_cast<std::int16_t>(b.sync - 1));
  }
  drop_pointer(w.data(), view.offset + w.f.child_generator);
  const std::size_t request[] = {child.object};
  HkxRemoval removal;
  if (remove_objects(w.pf, request, true, removal) != HkxStatus::Ok) return refuse(refusal, removal.refusal);
  return true;
}

struct SelectorRef {
  std::size_t object{0};
  std::size_t head{0};
  Elements slots;
  std::int16_t selected{0};
  std::int16_t current{0};
  std::int16_t at_activate{0};
};

// Constraint: a selector is edited only when its selected index is in range and its runtime members are in the shipped state.
bool resolve_selector(const Work& w, std::int32_t node, SelectorRef& s, std::string& refusal) {
  if (node < 0 || static_cast<std::size_t>(node) >= w.model.nodes.size()) return refuse(refusal, "the selector node is outside the nodes table");
  const HkxGraphNode& selector = w.model.nodes[static_cast<std::size_t>(node)];
  if (selector.kind != HkxGraphNodeKind::Selector || selector.class_name != k_selector_class) {
    return refuse(refusal, "the node is not a hkbManualSelectorGenerator");
  }
  const HkxObjectView& view = w.pf.objects[selector.object];
  const HkxClassLayout* layout = find_layout(view.class_name, view.signature);
  if (layout == nullptr || !detail::holds_layout(view, *layout)) return refuse(refusal, "the selector object does not hold its layout");
  const std::size_t decoded = static_cast<std::size_t>(std::count_if(w.model.edges.begin(), w.model.edges.end(), [&](const HkxGraphEdge& edge) {
    return edge.parent == static_cast<std::size_t>(node) && edge.role == HkxGraphEdgeRole::Selected;
  }));
  s.object = view.offset;
  s.head = s.object + w.f.selector_generators;
  if (!locate_slots(w, s.head, decoded, s.slots, refusal)) return false;
  if (!read_index(w, s.object + w.f.selector_selected, s.selected, refusal) ||
      !read_index(w, s.object + w.f.selector_current, s.current, refusal) ||
      !read_index(w, s.object + w.f.selector_at_activate, s.at_activate, refusal)) {
    return false;
  }
  if (s.selected < 0 || static_cast<std::size_t>(s.selected) >= s.slots.count) {
    return refuse(refusal, "the selected generator index is already outside the generators, which the engine reports as invalid");
  }
  return runtime_array_empty(w, s.object + w.f.selector_active, "activeTransitions", refusal);
}

bool add_selector_child(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  SelectorRef s;
  if (!resolve_selector(w, e.node, s, refusal)) return false;
  if (!generator_target_ok(w.model, e.value, static_cast<std::size_t>(e.node), "selector", refusal)) return false;
  const std::size_t generator = w.pf.objects[w.model.nodes[static_cast<std::size_t>(e.value)].object].offset;
  const std::size_t first = relocate(w, s.head, s.slots, k_pointer_size, s.slots.count);
  set_global(w.data(), w.pf.data_section, first + s.slots.count * k_pointer_size, generator);
  return true;
}

bool remove_selector_child(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  if (e.index >= w.model.edges.size()) return refuse(refusal, "the edge index is outside the edges table");
  const HkxGraphEdge& edge = w.model.edges[e.index];
  if (edge.role != HkxGraphEdgeRole::Selected) return refuse(refusal, "the edge is not a selector child");
  SelectorRef s;
  if (!resolve_selector(w, static_cast<std::int32_t>(edge.parent), s, refusal)) return false;
  if (s.slots.count < 2) return refuse(refusal, "a selector keeps at least one child");
  if (edge.slot < 0 || static_cast<std::size_t>(edge.slot) >= s.slots.count) return refuse(refusal, "the edge slot is outside the generators array");
  const auto position = static_cast<std::size_t>(edge.slot);
  if (pointer_target(w.pf, w.data(), s.object + w.f.bindable_set) || pointer_target(w.pf, w.data(), s.object + w.f.selector_index_selector)) {
    return refuse(refusal, "a binding set or index selector may drive the selected index, and selector bindings are not decoded, so removing a child could change which child it picks");
  }
  if (s.selected == static_cast<std::int16_t>(position)) {
    return refuse(refusal, "this child is the selected generator; the engine behaviour of a removed selection is not verified");
  }
  if (s.selected > static_cast<std::int16_t>(position)) {
    if (s.current != s.selected || s.at_activate != s.selected) {
      return refuse(refusal, "currentGeneratorIndex or generatorIndexAtActivate differs from the selected index, so they cannot be renumbered together");
    }
    const auto renumbered = static_cast<std::int16_t>(s.selected - 1);
    w.put<std::int16_t>(s.object + w.f.selector_selected, renumbered);
    w.put<std::int16_t>(s.object + w.f.selector_current, renumbered);
    w.put<std::int16_t>(s.object + w.f.selector_at_activate, renumbered);
  }
  remove_slot(w, s.slots, s.head, position);
  return true;
}

std::optional<std::pair<std::size_t, std::size_t>> run_of(const HkxGraphModel& model, std::size_t machine,
                                                          std::int32_t state) {
  if (machine >= model.state_machines.size()) return std::nullopt;
  if (state < 0) {
    return std::make_pair(model.state_machines[machine].first_wildcard, model.state_machines[machine].wildcard_count);
  }
  if (static_cast<std::size_t>(state) >= model.states.size()) return std::nullopt;
  const HkxGraphState& s = model.states[static_cast<std::size_t>(state)];
  return std::make_pair(s.first_transition, s.transition_count);
}

struct BindingSet {
  std::size_t owner_field{0};
  std::size_t head{0};
  Elements rows;
};

bool owner_object(const Work& w, int kind, std::size_t index, std::size_t& object, std::string& refusal) {
  switch (kind) {
    case static_cast<int>(HkxGraphOwnerKind::StateMachine):
      if (index >= w.model.state_machines.size()) return refuse(refusal, "the owner index is outside the state machines table");
      object = w.model.nodes[w.model.state_machines[index].node].object;
      return true;
    case static_cast<int>(HkxGraphOwnerKind::State):
      if (index >= w.model.states.size()) return refuse(refusal, "the owner index is outside the states table");
      object = w.model.states[index].object;
      return true;
    case static_cast<int>(HkxGraphOwnerKind::Blender):
      if (index >= w.model.blenders.size()) return refuse(refusal, "the owner index is outside the blenders table");
      object = w.model.nodes[w.model.blenders[index].node].object;
      return true;
    case static_cast<int>(HkxGraphOwnerKind::Clip):
      if (index >= w.model.clips.size()) return refuse(refusal, "the owner index is outside the clips table");
      object = w.model.nodes[w.model.clips[index].node].object;
      return true;
    case static_cast<int>(HkxGraphOwnerKind::BlendChild):
      return refuse(refusal, "this editor does not edit bindings of a blend child");
    default:
      return refuse(refusal, "the binding owner kind is not a state machine, state, blender or clip");
  }
}

bool locate_binding_set(const Work& w, int kind, std::size_t index, BindingSet& s, std::string& refusal) {
  std::size_t object = 0;
  if (!owner_object(w, kind, index, object, refusal)) return false;
  s.owner_field = w.pf.objects[object].offset + w.f.bindable_set;
  const auto set = typed_object(w, s.owner_field, "hkbVariableBindingSet");
  if (!set) return refuse(refusal, "the owner has no binding set, and this editor does not create one");
  const std::size_t set_offset = w.pf.objects[*set].offset;
  if (pointed_into(w.data(), w.pf.data_section, set_offset, set_offset + 1, s.owner_field)) {
    return refuse(refusal, "the binding set is pointed at by another object");
  }
  s.head = set_offset + w.f.binding_array;
  if (elements_at(w.data(), s.head, k_pointer_size, w.f.binding_size, s.rows) != HkxStatus::Ok) {
    return refuse(refusal, "the binding rows are not readable");
  }
  if (s.rows.count != 0 &&
      pointed_into(w.data(), w.pf.data_section, s.rows.first, s.rows.first + s.rows.count * w.f.binding_size, s.head)) {
    return refuse(refusal, "the binding rows are pointed at from outside their set");
  }
  return true;
}

bool same_owner(const HkxGraphBinding& a, const HkxGraphBinding& b) {
  return a.owner_kind == b.owner_kind && a.owner_index == b.owner_index;
}

// Invariant: the rows of one owner are contiguous in the bindings table, so a row's position in its set is its index minus the first row of that owner.
bool locate_binding_row(const Work& w, std::size_t index, BindingSet& s, std::size_t& position, std::string& refusal) {
  if (index >= w.model.bindings.size()) return refuse(refusal, "the binding index is outside the bindings table");
  const HkxGraphBinding& row = w.model.bindings[index];
  if (!locate_binding_set(w, static_cast<int>(row.owner_kind), row.owner_index, s, refusal)) return false;
  std::size_t first = index;
  while (first > 0 && same_owner(w.model.bindings[first - 1], row)) --first;
  std::size_t count = 0;
  while (first + count < w.model.bindings.size() && same_owner(w.model.bindings[first + count], row)) ++count;
  if (count != s.rows.count) return refuse(refusal, "the binding set holds a different number of rows than the model decoded");
  position = index - first;
  return true;
}

bool set_binding(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  BindingSet s;
  std::size_t position = 0;
  if (!locate_binding_row(w, e.index, s, position, refusal)) return false;
  if (w.model.bindings[e.index].binding_type != 0) {
    return refuse(refusal, "only variable bindings can be retargeted; this row binds a character property");
  }
  if (e.value < 0 || static_cast<std::size_t>(e.value) >= w.model.variables.size()) {
    return refuse(refusal, "the variable index is outside the variables table");
  }
  w.put<std::int32_t>(s.rows.first + position * w.f.binding_size + w.f.binding_variable, e.value);
  return true;
}

bool add_binding(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  BindingSet s;
  if (!locate_binding_set(w, static_cast<int>(e.flags), e.index, s, refusal)) return false;
  if (!valid_name(e.name)) return refuse(refusal, "a member path must be non-empty, free of NUL and at most 65535 bytes");
  if (e.value < 0 || static_cast<std::size_t>(e.value) >= w.model.variables.size()) {
    return refuse(refusal, "the variable index is outside the variables table");
  }
  for (const HkxGraphBinding& row : w.model.bindings) {
    if (row.owner_kind == static_cast<HkxGraphOwnerKind>(e.flags) && row.owner_index == e.index && row.member_path == e.name) {
      return refuse(refusal, "the owner already binds '" + e.name + "'");
    }
  }
  std::size_t owned = 0;
  for (const HkxGraphBinding& row : w.model.bindings) {
    if (row.owner_kind == static_cast<HkxGraphOwnerKind>(e.flags) && row.owner_index == e.index) ++owned;
  }
  if (owned != s.rows.count) return refuse(refusal, "the binding set holds a different number of rows than the model decoded");
  const std::size_t first = relocate(w, s.head, s.rows, w.f.binding_size, s.rows.count);
  const std::size_t row = first + s.rows.count * w.f.binding_size;
  set_local(w.data(), row + w.f.binding_path, w.add_string(e.name));
  w.put<std::int32_t>(row + w.f.binding_variable, e.value);
  w.put<std::int8_t>(row + w.f.binding_bit, -1);
  return true;
}

bool remove_binding(Work& w, const HkxBehaviorEdit& e, std::string& refusal) {
  BindingSet s;
  std::size_t position = 0;
  if (!locate_binding_row(w, e.index, s, position, refusal)) return false;
  const std::size_t stride = w.f.binding_size;
  const std::size_t row = s.rows.first + position * stride;
  const std::size_t end = s.rows.first + s.rows.count * stride;
  drop_range(w.data(), row, row + stride);
  shift_fixups(w.data(), row + stride, end, -static_cast<std::int64_t>(stride));
  w.copy_bytes(row, row + stride, end - row - stride);
  w.zero_bytes(end - stride, stride);
  w.put<std::int32_t>(s.head + k_array_size_at, static_cast<std::int32_t>(s.rows.count - 1));
  if (s.rows.count == 1) drop_pointer(w.data(), s.head);
  return true;
}

bool binding_edit(HkxBehaviorEditKind kind) {
  return kind == HkxBehaviorEditKind::SetBinding || kind == HkxBehaviorEditKind::AddBinding ||
         kind == HkxBehaviorEditKind::RemoveBinding;
}

bool graph_edit(HkxBehaviorEditKind kind) {
  return kind == HkxBehaviorEditKind::AddVariable || kind == HkxBehaviorEditKind::RenameVariable ||
         kind == HkxBehaviorEditKind::SetClipAnimation || kind == HkxBehaviorEditKind::SetStartState ||
         kind == HkxBehaviorEditKind::AddState || kind == HkxBehaviorEditKind::RemoveState ||
         kind == HkxBehaviorEditKind::SetGeneratorField || kind == HkxBehaviorEditKind::AddBlendChild ||
         kind == HkxBehaviorEditKind::RemoveBlendChild || kind == HkxBehaviorEditKind::AddSelectorChild ||
         kind == HkxBehaviorEditKind::RemoveSelectorChild || kind == HkxBehaviorEditKind::RemoveSubtree || binding_edit(kind);
}

bool same_variables(const HkxGraphModel& before, const HkxGraphModel& after, std::optional<std::size_t> except) {
  for (std::size_t i = 0; i < before.variables.size(); ++i) {
    if (i == except) continue;
    const HkxGraphVariable& a = before.variables[i];
    const HkxGraphVariable& b = after.variables[i];
    if (a.name != b.name || a.type != b.type || a.word != b.word) return false;
  }
  return true;
}

bool confirm_states(const HkxGraphModel& before, const HkxGraphModel& after, const HkxBehaviorEdit& e, std::string& refusal) {
  const char* mismatch = "the written file does not read back as the edit asked";
  const bool adding = e.kind == HkxBehaviorEditKind::AddState;
  if (e.state_machine >= before.state_machines.size() || after.state_machines.size() != before.state_machines.size() ||
      after.nodes.size() != before.nodes.size() || after.variables.size() != before.variables.size() ||
      after.events.size() != before.events.size() || after.clips.size() != before.clips.size()) {
    return refuse(refusal, mismatch);
  }
  const HkxGraphStateMachine& machine = before.state_machines[e.state_machine];
  if (!adding && (e.state < 0 || static_cast<std::size_t>(e.state) >= before.states.size())) return refuse(refusal, mismatch);
  const std::size_t at = adding ? machine.first_state + machine.state_count : static_cast<std::size_t>(e.state);
  const std::size_t expected = adding ? before.states.size() + 1 : before.states.size() - 1;
  const std::size_t expected_run = adding ? machine.state_count + 1 : machine.state_count - 1;
  if (after.states.size() != expected || after.state_machines[e.state_machine].state_count != expected_run) {
    return refuse(refusal, mismatch);
  }
  for (std::size_t i = 0; i < after.states.size(); ++i) {
    if (adding && i == at) {
      const HkxGraphState& added = after.states[i];
      if (added.name != e.name || added.state_id != e.value || added.generator_node != e.node || !added.enable ||
          added.probability != 1.0f || added.transition_count != 0 || added.state_machine != e.state_machine) {
        return refuse(refusal, mismatch);
      }
      continue;
    }
    std::size_t source = i;
    if (adding && i > at) source = i - 1;
    if (!adding && i >= at) source = i + 1;
    if (source >= before.states.size()) return refuse(refusal, mismatch);
    const HkxGraphState& a = before.states[source];
    const HkxGraphState& b = after.states[i];
    if (a.state_id != b.state_id || a.name != b.name || a.generator_node != b.generator_node ||
        a.transition_count != b.transition_count) {
      return refuse(refusal, mismatch);
    }
  }
  const std::size_t dropped = adding ? 0 : before.states[at].transition_count;
  if (after.transitions.size() + dropped != before.transitions.size()) return refuse(refusal, mismatch);
  return true;
}

bool same_binding(const HkxGraphBinding& a, const HkxGraphBinding& b) {
  return same_owner(a, b) && a.member_path == b.member_path && a.variable_index == b.variable_index &&
         a.bit_index == b.bit_index && a.binding_type == b.binding_type;
}

bool confirm_bindings(const HkxGraphModel& before, const HkxGraphModel& after, const HkxBehaviorEdit& e, std::string& refusal) {
  const char* mismatch = "the written file does not read back as the edit asked";
  if (after.states.size() != before.states.size() || after.nodes.size() != before.nodes.size() ||
      after.state_machines.size() != before.state_machines.size() || after.transitions.size() != before.transitions.size() ||
      after.events.size() != before.events.size() || after.clips.size() != before.clips.size() ||
      after.blend_children.size() != before.blend_children.size() || after.variables.size() != before.variables.size() ||
      !same_variables(before, after, std::nullopt)) {
    return refuse(refusal, mismatch);
  }
  std::vector<HkxGraphBinding> expected = before.bindings;
  if (e.kind == HkxBehaviorEditKind::AddBinding) {
    HkxGraphBinding added;
    added.owner_kind = static_cast<HkxGraphOwnerKind>(e.flags);
    added.owner_index = e.index;
    added.member_path = e.name;
    added.variable_index = e.value;
    added.bit_index = -1;
    added.binding_type = 0;
    std::size_t at = after.bindings.size();
    for (std::size_t i = 0; i < after.bindings.size(); ++i) {
      if (same_binding(after.bindings[i], added)) {
        at = i;
        break;
      }
    }
    if (at > expected.size()) return refuse(refusal, mismatch);
    expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(at), added);
  } else {
    if (e.index >= expected.size()) return refuse(refusal, mismatch);
    if (e.kind == HkxBehaviorEditKind::SetBinding) {
      expected[e.index].variable_index = e.value;
    } else {
      expected.erase(expected.begin() + static_cast<std::ptrdiff_t>(e.index));
    }
  }
  if (after.bindings.size() != expected.size()) return refuse(refusal, mismatch);
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (!same_binding(after.bindings[i], expected[i])) return refuse(refusal, mismatch);
  }
  return true;
}

void expect_field(const HkxBehaviorEdit& e, HkxGraphModel& expected) {
  const std::string_view m = e.name;
  if (e.flags == static_cast<std::uint32_t>(HkxGraphOwnerKind::Clip)) {
    HkxGraphClip& c = expected.clips[e.index];
    if (m == "playbackSpeed") c.playback_speed = e.real;
    if (m == "startTime") c.start_time = e.real;
    if (m == "cropStartAmountLocalTime") c.crop_start = e.real;
    if (m == "cropEndAmountLocalTime") c.crop_end = e.real;
    if (m == "enforcedDuration") c.enforced_duration = e.real;
    if (m == "mode") c.mode = static_cast<std::int8_t>(e.value);
    if (m == "flags") c.flags = static_cast<std::uint8_t>(e.value);
  } else if (e.flags == static_cast<std::uint32_t>(HkxGraphOwnerKind::Blender)) {
    HkxGraphBlender& b = expected.blenders[e.index];
    if (m == "blendParameter") b.blend_parameter = e.real;
    if (m == "minCyclicBlendParameter") b.min_cyclic_blend_parameter = e.real;
    if (m == "maxCyclicBlendParameter") b.max_cyclic_blend_parameter = e.real;
  } else {
    HkxGraphBlendChild& c = expected.blend_children[e.index];
    if (m == "weight") c.weight = e.real;
    if (m == "worldFromModelWeight") c.world_from_model_weight = e.real;
  }
}

// Invariant: reals compare by bit pattern, so a NaN already stored in an untouched row does not read as a changed row.
bool same_real(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

bool same_clip(const HkxGraphClip& a, const HkxGraphClip& b) {
  return a.node == b.node && a.animation == b.animation && a.animation_bundle == b.animation_bundle && a.mode == b.mode &&
         a.flags == b.flags && same_real(a.start_time, b.start_time) && same_real(a.playback_speed, b.playback_speed) &&
         same_real(a.crop_start, b.crop_start) && same_real(a.crop_end, b.crop_end) &&
         same_real(a.enforced_duration, b.enforced_duration) && a.triggers.size() == b.triggers.size();
}

bool same_blender(const HkxGraphBlender& a, const HkxGraphBlender& b) {
  return a.node == b.node && same_real(a.blend_parameter, b.blend_parameter) &&
         same_real(a.min_cyclic_blend_parameter, b.min_cyclic_blend_parameter) &&
         same_real(a.max_cyclic_blend_parameter, b.max_cyclic_blend_parameter) &&
         a.index_of_sync_master_child == b.index_of_sync_master_child && a.flags == b.flags &&
         a.first_child == b.first_child && a.child_count == b.child_count;
}

bool same_blend_child(const HkxGraphBlendChild& a, const HkxGraphBlendChild& b) {
  return a.blender == b.blender && a.generator_node == b.generator_node && same_real(a.weight, b.weight) &&
         same_real(a.world_from_model_weight, b.world_from_model_weight) && a.has_bone_weights == b.has_bone_weights &&
         a.weight_variable == b.weight_variable;
}

bool confirm_field(const HkxGraphModel& before, const HkxGraphModel& after, const HkxBehaviorEdit& e, std::string& refusal) {
  const char* mismatch = "the written file does not read back as the edit asked";
  if (after.states.size() != before.states.size() || after.nodes.size() != before.nodes.size() ||
      after.state_machines.size() != before.state_machines.size() || after.transitions.size() != before.transitions.size() ||
      after.events.size() != before.events.size() || after.variables.size() != before.variables.size() ||
      after.bindings.size() != before.bindings.size() || after.clips.size() != before.clips.size() ||
      after.blenders.size() != before.blenders.size() || after.blend_children.size() != before.blend_children.size() ||
      after.edges.size() != before.edges.size()) {
    return refuse(refusal, mismatch);
  }
  HkxGraphModel expected;
  expected.clips = before.clips;
  expected.blenders = before.blenders;
  expected.blend_children = before.blend_children;
  expect_field(e, expected);
  for (std::size_t i = 0; i < expected.clips.size(); ++i) {
    if (!same_clip(expected.clips[i], after.clips[i])) return refuse(refusal, mismatch);
  }
  for (std::size_t i = 0; i < expected.blenders.size(); ++i) {
    if (!same_blender(expected.blenders[i], after.blenders[i])) return refuse(refusal, mismatch);
  }
  for (std::size_t i = 0; i < expected.blend_children.size(); ++i) {
    if (!same_blend_child(expected.blend_children[i], after.blend_children[i])) return refuse(refusal, mismatch);
  }
  return same_variables(before, after, std::nullopt) ? true : refuse(refusal, mismatch);
}

bool same_edge(const HkxGraphEdge& a, const HkxGraphEdge& b) {
  return a.parent == b.parent && a.child == b.child && a.role == b.role && a.slot == b.slot &&
         same_real(a.weight, b.weight) && a.has_bone_weights == b.has_bone_weights;
}

bool same_edges(const std::vector<HkxGraphEdge>& a, const std::vector<HkxGraphEdge>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), same_edge);
}

bool confirm_blend_children(const HkxGraphModel& before, const HkxGraphModel& after, const HkxBehaviorEdit& e, std::string& refusal) {
  const char* mismatch = "the written file does not read back as the edit asked";
  const bool adding = e.kind == HkxBehaviorEditKind::AddBlendChild;
  if (after.states.size() != before.states.size() || after.nodes.size() != before.nodes.size() ||
      after.state_machines.size() != before.state_machines.size() || after.transitions.size() != before.transitions.size() ||
      after.events.size() != before.events.size() || after.variables.size() != before.variables.size() ||
      after.clips.size() != before.clips.size() || after.blenders.size() != before.blenders.size() ||
      after.edges.size() != before.edges.size()) {
    return refuse(refusal, mismatch);
  }
  if (!adding && e.index >= before.blend_children.size()) return refuse(refusal, mismatch);
  const std::size_t blender = adding ? e.index : before.blend_children[e.index].blender;
  if (blender >= before.blenders.size()) return refuse(refusal, mismatch);
  const HkxGraphBlender& old = before.blenders[blender];
  const std::size_t at = adding ? old.first_child + old.child_count : e.index;
  std::vector<HkxGraphBlendChild> children = before.blend_children;
  std::vector<HkxGraphBinding> bindings = before.bindings;
  std::vector<HkxGraphBlender> blenders = before.blenders;
  const auto is_child_row = [](const HkxGraphBinding& row) { return row.owner_kind == HkxGraphOwnerKind::BlendChild; };
  if (adding) {
    HkxGraphBlendChild added;
    added.blender = blender;
    added.generator_node = e.node;
    added.weight = e.real;
    added.world_from_model_weight = 0.0f;
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(at), added);
    for (HkxGraphBinding& row : bindings) {
      if (is_child_row(row) && row.owner_index >= at) row.owner_index += 1;
    }
    blenders[blender].child_count += 1;
    for (std::size_t i = blender + 1; i < blenders.size(); ++i) blenders[i].first_child += 1;
  } else {
    const std::size_t position = at - old.first_child;
    children.erase(children.begin() + static_cast<std::ptrdiff_t>(at));
    std::erase_if(bindings, [&](const HkxGraphBinding& row) { return is_child_row(row) && row.owner_index == at; });
    for (HkxGraphBinding& row : bindings) {
      if (is_child_row(row) && row.owner_index > at) row.owner_index -= 1;
    }
    blenders[blender].child_count -= 1;
    for (std::size_t i = blender + 1; i < blenders.size(); ++i) blenders[i].first_child -= 1;
    if (old.index_of_sync_master_child > static_cast<std::int16_t>(position)) {
      blenders[blender].index_of_sync_master_child = static_cast<std::int16_t>(old.index_of_sync_master_child - 1);
    }
  }
  if (after.blend_children.size() != children.size() || after.bindings.size() != bindings.size()) return refuse(refusal, mismatch);
  for (std::size_t i = 0; i < blenders.size(); ++i) {
    if (!same_blender(blenders[i], after.blenders[i])) return refuse(refusal, mismatch);
  }
  for (std::size_t i = 0; i < children.size(); ++i) {
    if (!same_blend_child(children[i], after.blend_children[i])) return refuse(refusal, mismatch);
  }
  for (std::size_t i = 0; i < bindings.size(); ++i) {
    if (!same_binding(bindings[i], after.bindings[i])) return refuse(refusal, mismatch);
  }
  return same_variables(before, after, std::nullopt) ? true : refuse(refusal, mismatch);
}

bool confirm_selector_children(const HkxGraphModel& before, const HkxGraphModel& after, const HkxBehaviorEdit& e, std::string& refusal) {
  const char* mismatch = "the written file does not read back as the edit asked";
  const bool adding = e.kind == HkxBehaviorEditKind::AddSelectorChild;
  if (after.states.size() != before.states.size() || after.nodes.size() != before.nodes.size() ||
      after.state_machines.size() != before.state_machines.size() || after.transitions.size() != before.transitions.size() ||
      after.events.size() != before.events.size() || after.variables.size() != before.variables.size() ||
      after.clips.size() != before.clips.size() || after.blenders.size() != before.blenders.size() ||
      after.blend_children.size() != before.blend_children.size() || after.bindings.size() != before.bindings.size()) {
    return refuse(refusal, mismatch);
  }
  if (!adding && e.index >= before.edges.size()) return refuse(refusal, mismatch);
  if (adding && e.node < 0) return refuse(refusal, mismatch);
  const std::size_t node = adding ? static_cast<std::size_t>(e.node) : before.edges[e.index].parent;
  std::vector<HkxGraphEdge> own;
  std::vector<HkxGraphEdge> own_after;
  std::vector<HkxGraphEdge> others;
  std::vector<HkxGraphEdge> others_after;
  for (const HkxGraphEdge& edge : before.edges) (edge.parent == node ? own : others).push_back(edge);
  for (const HkxGraphEdge& edge : after.edges) (edge.parent == node ? own_after : others_after).push_back(edge);
  if (adding) {
    own.push_back({node, e.value, HkxGraphEdgeRole::Selected, static_cast<std::int32_t>(own.size()), 0.0f, false});
  } else {
    const std::int32_t removed = before.edges[e.index].slot;
    std::erase_if(own, [&](const HkxGraphEdge& edge) { return edge.slot == removed; });
    for (HkxGraphEdge& edge : own) {
      if (edge.slot > removed) edge.slot -= 1;
    }
  }
  if (!same_edges(own, own_after) || !same_edges(others, others_after)) return refuse(refusal, mismatch);
  if (before.selectors.size() != after.selectors.size()) return refuse(refusal, mismatch);
  const std::int32_t removed_slot = adding ? -1 : before.edges[e.index].slot;
  for (std::size_t i = 0; i < before.selectors.size(); ++i) {
    const HkxGraphSelector& a = before.selectors[i];
    const HkxGraphSelector& b = after.selectors[i];
    std::size_t count = a.generator_count;
    std::int16_t selected = a.selected_generator_index;
    if (a.node == node) {
      count = adding ? count + 1 : count - 1;
      if (!adding && selected > removed_slot) selected = static_cast<std::int16_t>(selected - 1);
    }
    if (a.node != b.node || b.generator_count != count || b.selected_generator_index != selected ||
        b.null_generator_count != a.null_generator_count) {
      return refuse(refusal, mismatch);
    }
  }
  return same_variables(before, after, std::nullopt) ? true : refuse(refusal, mismatch);
}

bool confirm_subtree(const HkxGraphModel& before, const HkxGraphModel& after, const std::vector<bool>& dead, std::string& refusal) {
  const char* mismatch = "the written file does not read back as the edit asked";
  const std::size_t n = before.nodes.size();
  std::vector<std::int32_t> map(n, -1);
  std::int32_t kept = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (!dead[i]) map[i] = kept++;
  }
  const auto alive = [&](std::size_t node) { return node < n && !dead[node]; };
  const auto mapped = [&](std::int32_t node) { return node < 0 || static_cast<std::size_t>(node) >= n ? node : map[static_cast<std::size_t>(node)]; };
  if (after.nodes.size() != static_cast<std::size_t>(kept) || mapped(before.root_node) != after.root_node ||
      after.events.size() != before.events.size() || after.variables.size() != before.variables.size() ||
      !same_variables(before, after, std::nullopt)) {
    return refuse(refusal, mismatch);
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (dead[i]) continue;
    const HkxGraphNode& a = before.nodes[i];
    const HkxGraphNode& b = after.nodes[static_cast<std::size_t>(map[i])];
    if (a.class_name != b.class_name || a.name != b.name || a.kind != b.kind) return refuse(refusal, mismatch);
  }
  std::vector<const HkxGraphState*> states;
  for (const HkxGraphState& state : before.states) {
    if (alive(before.state_machines[state.state_machine].node)) states.push_back(&state);
  }
  std::size_t transitions = 0;
  for (const HkxGraphTransition& t : before.transitions) transitions += alive(before.state_machines[t.state_machine].node) ? 1 : 0;
  std::vector<const HkxGraphBlendChild*> children;
  for (const HkxGraphBlendChild& child : before.blend_children) {
    if (alive(before.blenders[child.blender].node)) children.push_back(&child);
  }
  std::vector<const HkxGraphEdge*> edges;
  for (const HkxGraphEdge& edge : before.edges) {
    if (alive(edge.parent)) edges.push_back(&edge);
  }
  const auto count_alive = [&](const auto& rows) {
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [&](const auto& row) { return alive(row.node); }));
  };
  const auto owner_alive = [&](const HkxGraphBinding& row) {
    switch (row.owner_kind) {
      case HkxGraphOwnerKind::StateMachine:
        return row.owner_index < before.state_machines.size() && alive(before.state_machines[row.owner_index].node);
      case HkxGraphOwnerKind::State:
        return row.owner_index < before.states.size() && alive(before.state_machines[before.states[row.owner_index].state_machine].node);
      case HkxGraphOwnerKind::Blender:
        return row.owner_index < before.blenders.size() && alive(before.blenders[row.owner_index].node);
      case HkxGraphOwnerKind::BlendChild:
        return row.owner_index < before.blend_children.size() && alive(before.blenders[before.blend_children[row.owner_index].blender].node);
      case HkxGraphOwnerKind::Clip:
        return row.owner_index < before.clips.size() && alive(before.clips[row.owner_index].node);
    }
    return true;
  };
  const std::size_t bindings = static_cast<std::size_t>(std::count_if(before.bindings.begin(), before.bindings.end(), owner_alive));
  if (after.states.size() != states.size() || after.transitions.size() != transitions || after.blend_children.size() != children.size() ||
      after.edges.size() != edges.size() || after.clips.size() != count_alive(before.clips) ||
      after.blenders.size() != count_alive(before.blenders) || after.effects.size() != count_alive(before.effects) ||
      after.bindings.size() != bindings) {
    return refuse(refusal, mismatch);
  }
  for (std::size_t i = 0; i < states.size(); ++i) {
    const HkxGraphState& a = *states[i];
    const HkxGraphState& b = after.states[i];
    if (a.state_id != b.state_id || a.name != b.name || mapped(a.generator_node) != b.generator_node ||
        a.transition_count != b.transition_count) {
      return refuse(refusal, mismatch);
    }
  }
  for (std::size_t i = 0; i < children.size(); ++i) {
    const HkxGraphBlendChild& a = *children[i];
    const HkxGraphBlendChild& b = after.blend_children[i];
    if (mapped(a.generator_node) != b.generator_node || !same_real(a.weight, b.weight) ||
        !same_real(a.world_from_model_weight, b.world_from_model_weight) || a.has_bone_weights != b.has_bone_weights ||
        a.weight_variable != b.weight_variable) {
      return refuse(refusal, mismatch);
    }
  }
  for (std::size_t i = 0; i < edges.size(); ++i) {
    const HkxGraphEdge& a = *edges[i];
    const HkxGraphEdge& b = after.edges[i];
    if (mapped(static_cast<std::int32_t>(a.parent)) != static_cast<std::int32_t>(b.parent) || mapped(a.child) != b.child ||
        a.role != b.role || a.slot != b.slot) {
      return refuse(refusal, mismatch);
    }
  }
  std::size_t clip_at = 0;
  for (const HkxGraphClip& a : before.clips) {
    if (!alive(a.node)) continue;
    const HkxGraphClip& b = after.clips[clip_at++];
    if (mapped(static_cast<std::int32_t>(a.node)) != static_cast<std::int32_t>(b.node) || a.animation != b.animation) {
      return refuse(refusal, mismatch);
    }
  }
  std::size_t blender_at = 0;
  for (const HkxGraphBlender& a : before.blenders) {
    if (!alive(a.node)) continue;
    const HkxGraphBlender& b = after.blenders[blender_at++];
    if (mapped(static_cast<std::int32_t>(a.node)) != static_cast<std::int32_t>(b.node) || a.child_count != b.child_count ||
        a.index_of_sync_master_child != b.index_of_sync_master_child || a.flags != b.flags) {
      return refuse(refusal, mismatch);
    }
  }
  std::size_t binding_at = 0;
  for (const HkxGraphBinding& a : before.bindings) {
    if (!owner_alive(a)) continue;
    const HkxGraphBinding& b = after.bindings[binding_at++];
    if (a.owner_kind != b.owner_kind || a.member_path != b.member_path || a.variable_index != b.variable_index ||
        a.bit_index != b.bit_index) {
      return refuse(refusal, mismatch);
    }
  }
  return true;
}

bool confirm_graph(const HkxGraphModel& before, const HkxGraphModel& after, const HkxBehaviorEdit& e, std::string& refusal) {
  const char* mismatch = "the written file does not read back as the edit asked";
  if (e.kind == HkxBehaviorEditKind::SetGeneratorField) return confirm_field(before, after, e, refusal);
  if (e.kind == HkxBehaviorEditKind::AddBlendChild || e.kind == HkxBehaviorEditKind::RemoveBlendChild) {
    return confirm_blend_children(before, after, e, refusal);
  }
  if (e.kind == HkxBehaviorEditKind::AddSelectorChild || e.kind == HkxBehaviorEditKind::RemoveSelectorChild) {
    return confirm_selector_children(before, after, e, refusal);
  }
  if (e.kind == HkxBehaviorEditKind::AddState || e.kind == HkxBehaviorEditKind::RemoveState) {
    return confirm_states(before, after, e, refusal);
  }
  if (binding_edit(e.kind)) return confirm_bindings(before, after, e, refusal);
  const bool same_shape = after.states.size() == before.states.size() && after.nodes.size() == before.nodes.size() &&
                          after.state_machines.size() == before.state_machines.size() &&
                          after.transitions.size() == before.transitions.size() && after.events.size() == before.events.size() &&
                          after.clips.size() == before.clips.size() && after.bindings.size() == before.bindings.size() &&
                          after.blend_children.size() == before.blend_children.size();
  if (!same_shape) return refuse(refusal, mismatch);
  const std::size_t variables = before.variables.size();
  switch (e.kind) {
    case HkxBehaviorEditKind::AddVariable: {
      if (after.variables.size() != variables + 1) return refuse(refusal, mismatch);
      const HkxGraphVariable& added = after.variables.back();
      if (added.name != e.name || added.type != static_cast<std::int8_t>(e.flags) || added.word != 0 || added.role != 0 ||
          added.role_flags != 0 || !same_variables(before, after, std::nullopt)) {
        return refuse(refusal, mismatch);
      }
      return true;
    }
    case HkxBehaviorEditKind::RenameVariable:
      if (after.variables.size() != variables || e.index >= variables || after.variables[e.index].name != e.name ||
          !same_variables(before, after, e.index)) {
        return refuse(refusal, mismatch);
      }
      return true;
    case HkxBehaviorEditKind::SetClipAnimation:
      if (after.variables.size() != variables || e.index >= after.clips.size() || after.clips[e.index].animation != e.name) {
        return refuse(refusal, mismatch);
      }
      return true;
    case HkxBehaviorEditKind::SetStartState:
      if (after.variables.size() != variables || e.state_machine >= after.state_machines.size() ||
          after.state_machines[e.state_machine].start_state_id != e.value) {
        return refuse(refusal, mismatch);
      }
      return true;
    default:
      return refuse(refusal, mismatch);
  }
}

// Constraint: each edit is read back and compared with what it was asked to do, so a layout slip is refused here and not saved.
bool confirm(const HkxGraphModel& before, const HkxGraphModel& after, const HkxBehaviorEdit& e, std::string& refusal) {
  if (graph_edit(e.kind)) return confirm_graph(before, after, e, refusal);
  const char* mismatch = "the written file does not read back as the edit asked";
  if (after.states.size() != before.states.size() || after.nodes.size() != before.nodes.size() ||
      after.variables.size() != before.variables.size() || after.state_machines.size() != before.state_machines.size()) {
    return refuse(refusal, mismatch);
  }
  const bool transition_edit = e.kind == HkxBehaviorEditKind::SetTransition || e.kind == HkxBehaviorEditKind::AddTransition ||
                               e.kind == HkxBehaviorEditKind::RemoveTransition;
  if (transition_edit) {
    const auto old_run = run_of(before, e.state_machine, e.state);
    const auto new_run = run_of(after, e.state_machine, e.state);
    if (!old_run || !new_run || after.events.size() != before.events.size()) return refuse(refusal, mismatch);
    const bool added = e.kind == HkxBehaviorEditKind::AddTransition;
    const bool removed = e.kind == HkxBehaviorEditKind::RemoveTransition;
    if (removed ? new_run->second + 1 != old_run->second : new_run->second != old_run->second + (added ? 1U : 0U)) {
      return refuse(refusal, mismatch);
    }
    if (removed ? after.transitions.size() + 1 != before.transitions.size()
                : after.transitions.size() != before.transitions.size() + (added ? 1U : 0U)) {
      return refuse(refusal, mismatch);
    }
    if (!removed && (e.index >= new_run->second || !same_row(after.transitions[new_run->first + e.index], e.transition))) {
      return refuse(refusal, mismatch);
    }
    return true;
  }
  if (after.transitions.size() != before.transitions.size()) return refuse(refusal, mismatch);
  if (e.kind == HkxBehaviorEditKind::AddEvent) {
    if (after.events.size() != before.events.size() + 1 || after.events.back().name != e.name ||
        after.events.back().flags != e.flags) {
      return refuse(refusal, mismatch);
    }
    return true;
  }
  if (after.events.size() != before.events.size() || e.index >= after.events.size()) return refuse(refusal, mismatch);
  if (e.kind == HkxBehaviorEditKind::RenameEvent && after.events[e.index].name != e.name) return refuse(refusal, mismatch);
  if (e.kind == HkxBehaviorEditKind::SetEventFlags && after.events[e.index].flags != e.flags) return refuse(refusal, mismatch);
  return true;
}

class Editor {
 public:
  explicit Editor(std::span<const std::byte> source) : bytes_(source.begin(), source.end()) {}

  const std::vector<std::byte>& bytes() const { return bytes_; }

  bool apply(const HkxBehaviorEdit& e, std::string& refusal) {
    switch (e.kind) {
      case HkxBehaviorEditKind::RemoveEvent:
        return refuse(refusal, "removing an event is not supported: modifier classes this model does not decode also store event ids");
      case HkxBehaviorEditKind::RemoveVariable:
        return refuse(refusal, "removing a variable is not supported: bindings, expressions and modifiers index the variable table");
      case HkxBehaviorEditKind::AddState:
        return add_state(e, refusal);
      case HkxBehaviorEditKind::AddBlendChild:
        return add_blend_child(e, refusal);
      case HkxBehaviorEditKind::RemoveSubtree:
        return remove_subtree(e, refusal);
      case HkxBehaviorEditKind::AddTransition:
        if (!prepare_array(e, refusal)) return false;
        break;
      case HkxBehaviorEditKind::SetTransition:
      case HkxBehaviorEditKind::RemoveTransition:
      case HkxBehaviorEditKind::AddEvent:
      case HkxBehaviorEditKind::RenameEvent:
      case HkxBehaviorEditKind::SetEventFlags:
      case HkxBehaviorEditKind::AddVariable:
      case HkxBehaviorEditKind::RenameVariable:
      case HkxBehaviorEditKind::SetClipAnimation:
      case HkxBehaviorEditKind::SetStartState:
      case HkxBehaviorEditKind::RemoveState:
      case HkxBehaviorEditKind::SetBinding:
      case HkxBehaviorEditKind::AddBinding:
      case HkxBehaviorEditKind::RemoveBinding:
      case HkxBehaviorEditKind::SetGeneratorField:
      case HkxBehaviorEditKind::RemoveBlendChild:
      case HkxBehaviorEditKind::AddSelectorChild:
      case HkxBehaviorEditKind::RemoveSelectorChild:
        break;
    }
    Work w;
    if (!load(bytes_, w, refusal)) return false;
    bool done = false;
    switch (e.kind) {
      case HkxBehaviorEditKind::SetTransition:
        done = set_transition(w, e, refusal);
        break;
      case HkxBehaviorEditKind::AddTransition:
        done = add_transition(w, e, refusal);
        break;
      case HkxBehaviorEditKind::RemoveTransition:
        done = remove_transition(w, e, refusal);
        break;
      case HkxBehaviorEditKind::AddEvent:
        done = add_event(w, e, refusal);
        break;
      case HkxBehaviorEditKind::RenameEvent:
        done = rename_event(w, e, refusal);
        break;
      case HkxBehaviorEditKind::SetEventFlags:
        done = set_event_flags(w, e, refusal);
        break;
      case HkxBehaviorEditKind::AddVariable:
        done = add_variable(w, e, refusal);
        break;
      case HkxBehaviorEditKind::RenameVariable:
        done = rename_variable(w, e, refusal);
        break;
      case HkxBehaviorEditKind::SetClipAnimation:
        done = set_clip_animation(w, e, refusal);
        break;
      case HkxBehaviorEditKind::SetStartState:
        done = set_start_state(w, e, refusal);
        break;
      case HkxBehaviorEditKind::RemoveState:
        done = remove_state(w, e, refusal);
        break;
      case HkxBehaviorEditKind::SetBinding:
        done = set_binding(w, e, refusal);
        break;
      case HkxBehaviorEditKind::AddBinding:
        done = add_binding(w, e, refusal);
        break;
      case HkxBehaviorEditKind::RemoveBinding:
        done = remove_binding(w, e, refusal);
        break;
      case HkxBehaviorEditKind::SetGeneratorField:
        done = set_generator_field(w, e, refusal);
        break;
      case HkxBehaviorEditKind::RemoveBlendChild:
        done = remove_blend_child(w, e, refusal);
        break;
      case HkxBehaviorEditKind::AddSelectorChild:
        done = add_selector_child(w, e, refusal);
        break;
      case HkxBehaviorEditKind::RemoveSelectorChild:
        done = remove_selector_child(w, e, refusal);
        break;
      case HkxBehaviorEditKind::AddState:
      case HkxBehaviorEditKind::AddBlendChild:
      case HkxBehaviorEditKind::RemoveSubtree:
      case HkxBehaviorEditKind::RemoveEvent:
      case HkxBehaviorEditKind::RemoveVariable:
        break;
    }
    if (!done) return false;
    if (!finish(w, refusal)) return false;
    Work after;
    if (!load(bytes_, after, refusal)) return false;
    return confirm(w.model, after.model, e, refusal);
  }

 private:
  std::vector<std::byte> bytes_;

  bool finish(Work& w, std::string& refusal) {
    if (w.failed) return refuse(refusal, "an edit position fell outside the data section");
    sort_fixups(w.data());
    std::vector<std::byte> next;
    if (const auto status = rewrite_packfile(bytes_, w.pf, next); status != HkxStatus::Ok) {
      return refuse(refusal, "the packfile could not be written again (status " + std::to_string(static_cast<int>(status)) + ")");
    }
    bytes_ = std::move(next);
    return true;
  }

  // Constraint: a run with no array gets one copied from an existing instance of the class in the same file, because the base-class header bytes of that object are not guessed.
  bool prepare_array(const HkxBehaviorEdit& e, std::string& refusal) {
    Work w;
    if (!load(bytes_, w, refusal)) return false;
    Run run;
    if (!resolve_run(w, e, run, refusal)) return false;
    if (run.array) return true;
    const HkxObjectView* sample = nullptr;
    for (const HkxObjectView& object : w.pf.objects) {
      if (object.class_name == k_array_class) {
        sample = &object;
        break;
      }
    }
    if (sample == nullptr) return refuse(refusal, "the file has no transition array to copy, so a run without one cannot be given rows");
    const HkxClassLayout* layout = find_layout(k_array_class, sample->signature);
    if (layout == nullptr || sample->data.size() < layout->size) return refuse(refusal, "the transition array sample does not match the layout table");
    std::vector<std::byte> instance(sample->data.begin(), sample->data.begin() + static_cast<std::ptrdiff_t>(layout->size));
    const std::size_t head = w.f.transition_array;
    if (head + k_array_header_size > instance.size()) return refuse(refusal, "the transition array layout is shorter than its array header");
    std::uint32_t capacity = 0;
    std::memcpy(&capacity, instance.data() + head + k_array_capacity_at, sizeof capacity);
    capacity &= k_capacity_flags;
    std::fill_n(instance.begin() + static_cast<std::ptrdiff_t>(head), k_array_header_size, std::byte{0});
    std::memcpy(instance.data() + head + k_array_capacity_at, &capacity, sizeof capacity);
    HkxAppend appended;
    if (const auto status = append_object(bytes_, k_array_class, sample->signature, instance, appended); status != HkxStatus::Ok) {
      return refuse(refusal, "the transition array could not be appended (status " + std::to_string(static_cast<int>(status)) + ")");
    }
    Work v;
    if (!load(appended.bytes, v, refusal)) return false;
    if (appended.object_index >= v.pf.objects.size() || v.pf.objects[appended.object_index].class_name != k_array_class) {
      return refuse(refusal, "the appended transition array was not found on reading it back");
    }
    set_global(v.data(), v.pf.data_section, run.field, v.pf.objects[appended.object_index].offset);
    sort_fixups(v.data());
    std::vector<std::byte> next;
    if (const auto status = rewrite_packfile(appended.bytes, v.pf, next); status != HkxStatus::Ok) {
      return refuse(refusal, "the packfile could not be written again (status " + std::to_string(static_cast<int>(status)) + ")");
    }
    bytes_ = std::move(next);
    return true;
  }

  // Constraint: the new state info is copied from an existing instance in the same file, like prepare_array, so its base-class header bytes are not guessed; only the array capacity flags of that instance are kept.
  bool add_state(const HkxBehaviorEdit& e, std::string& refusal) {
    Work w;
    if (!load(bytes_, w, refusal)) return false;
    if (e.state_machine >= w.model.state_machines.size()) return refuse(refusal, "the state machine index is outside the graph");
    const HkxGraphStateMachine machine = w.model.state_machines[e.state_machine];
    if (!valid_name(e.name)) return refuse(refusal, "a state name must be non-empty, free of NUL and at most 65535 bytes");
    if (e.value < 0) return refuse(refusal, "a state id must not be negative");
    for (std::size_t i = machine.first_state; i < machine.first_state + machine.state_count; ++i) {
      if (w.model.states[i].state_id == e.value) return refuse(refusal, "the state machine already has a state with that id");
    }
    if (!generator_node_ok(w.model, e.node, e.state_machine, refusal)) return false;
    const HkxObjectView* sample = nullptr;
    for (const HkxObjectView& object : w.pf.objects) {
      if (object.class_name == k_state_class) {
        sample = &object;
        break;
      }
    }
    if (sample == nullptr) return refuse(refusal, "the file has no state info to copy, so a state cannot be added");
    const HkxClassLayout* layout = find_layout(k_state_class, sample->signature);
    if (layout == nullptr || sample->data.size() < layout->size || layout->size < k_object_header) {
      return refuse(refusal, "the state info sample does not match the layout table");
    }
    if (w.f.state_listeners + k_array_header_size > layout->size || w.f.bindable_cached + k_array_header_size > layout->size) {
      return refuse(refusal, "the state info layout is shorter than its array headers");
    }
    std::vector<std::byte> instance(sample->data.begin(), sample->data.begin() + static_cast<std::ptrdiff_t>(layout->size));
    const auto capacity_flags = [&](std::size_t head) {
      std::uint32_t capacity = 0;
      std::memcpy(&capacity, instance.data() + head + k_array_capacity_at, sizeof capacity);
      return capacity & k_capacity_flags;
    };
    const std::uint32_t listener_flags = capacity_flags(w.f.state_listeners);
    const std::uint32_t cached_flags = capacity_flags(w.f.bindable_cached);
    std::fill(instance.begin() + static_cast<std::ptrdiff_t>(k_object_header), instance.end(), std::byte{0});
    std::memcpy(instance.data() + w.f.state_listeners + k_array_capacity_at, &listener_flags, sizeof listener_flags);
    std::memcpy(instance.data() + w.f.bindable_cached + k_array_capacity_at, &cached_flags, sizeof cached_flags);
    HkxAppend appended;
    if (const auto status = append_object(bytes_, k_state_class, sample->signature, instance, appended); status != HkxStatus::Ok) {
      return refuse(refusal, "the state info could not be appended (status " + std::to_string(static_cast<int>(status)) + ")");
    }
    Work v;
    if (!load(appended.bytes, v, refusal)) return false;
    if (appended.object_index >= v.pf.objects.size() || v.pf.objects[appended.object_index].class_name != k_state_class) {
      return refuse(refusal, "the appended state info was not found on reading it back");
    }
    const std::size_t object = v.pf.objects[appended.object_index].offset;
    v.put<std::int32_t>(object + v.f.state_id, e.value);
    v.put<float>(object + v.f.state_probability, 1.0f);
    v.put<std::uint8_t>(object + v.f.state_enable, 1);
    set_local(v.data(), object + v.f.state_name, v.add_string(e.name));
    set_global(v.data(), v.pf.data_section, object + v.f.state_generator,
               v.pf.objects[v.model.nodes[static_cast<std::size_t>(e.node)].object].offset);
    std::size_t head = 0;
    Elements states;
    if (!locate_states(v, v.model.state_machines[e.state_machine], head, states, refusal)) return false;
    const std::size_t first = relocate(v, head, states, k_pointer_size, states.count);
    set_global(v.data(), v.pf.data_section, first + states.count * k_pointer_size, object);
    if (v.failed) return refuse(refusal, "an edit position fell outside the data section");
    sort_fixups(v.data());
    std::vector<std::byte> next;
    if (const auto status = rewrite_packfile(appended.bytes, v.pf, next); status != HkxStatus::Ok) {
      return refuse(refusal, "the packfile could not be written again (status " + std::to_string(static_cast<int>(status)) + ")");
    }
    Work after;
    if (!load(next, after, refusal)) return false;
    if (!confirm_states(w.model, after.model, e, refusal)) return false;
    bytes_ = std::move(next);
    return true;
  }

  // Constraint: the new blend child is copied from an existing instance in the same file, like add_state, so its base-class header bytes are not guessed; only the array capacity flags of that instance are kept.
  bool add_blend_child(const HkxBehaviorEdit& e, std::string& refusal) {
    Work w;
    if (!load(bytes_, w, refusal)) return false;
    BlenderRef b;
    if (!resolve_blender(w, e.index, b, refusal)) return false;
    const HkxGraphBlender blender = w.model.blenders[e.index];
    if (!std::isfinite(e.real)) return refuse(refusal, "a blend child weight takes a finite real number in real");
    if (!blender.parametric && (e.real < 0.0f || e.real > 1.0f)) {
      return refuse(refusal, "a non-parametric blender takes a child weight from 0 to 1, which its validator checks");
    }
    if (!generator_target_ok(w.model, e.node, blender.node, "blender", refusal)) return false;
    const HkxObjectView* sample = nullptr;
    for (const HkxObjectView& object : w.pf.objects) {
      if (object.class_name == k_child_class) {
        sample = &object;
        break;
      }
    }
    if (sample == nullptr) return refuse(refusal, "the file has no blend child to copy, so one cannot be added");
    const HkxClassLayout* layout = find_layout(k_child_class, sample->signature);
    if (layout == nullptr || sample->data.size() < layout->size || layout->size < k_object_header) {
      return refuse(refusal, "the blend child sample does not match the layout table");
    }
    if (w.f.bindable_cached + k_array_header_size > layout->size) return refuse(refusal, "the blend child layout is shorter than its array header");
    std::vector<std::byte> instance(sample->data.begin(), sample->data.begin() + static_cast<std::ptrdiff_t>(layout->size));
    std::uint32_t cached_flags = 0;
    std::memcpy(&cached_flags, instance.data() + w.f.bindable_cached + k_array_capacity_at, sizeof cached_flags);
    cached_flags &= k_capacity_flags;
    std::fill(instance.begin() + static_cast<std::ptrdiff_t>(k_object_header), instance.end(), std::byte{0});
    std::memcpy(instance.data() + w.f.bindable_cached + k_array_capacity_at, &cached_flags, sizeof cached_flags);
    HkxAppend appended;
    if (const auto status = append_object(bytes_, k_child_class, sample->signature, instance, appended); status != HkxStatus::Ok) {
      return refuse(refusal, "the blend child could not be appended (status " + std::to_string(static_cast<int>(status)) + ")");
    }
    Work v;
    if (!load(appended.bytes, v, refusal)) return false;
    if (appended.object_index >= v.pf.objects.size() || v.pf.objects[appended.object_index].class_name != k_child_class) {
      return refuse(refusal, "the appended blend child was not found on reading it back");
    }
    const std::size_t child = v.pf.objects[appended.object_index].offset;
    v.put<float>(child + v.f.child_weight, e.real);
    v.put<float>(child + v.f.child_world_weight, 0.0f);
    set_global(v.data(), v.pf.data_section, child + v.f.child_generator,
               v.pf.objects[v.model.nodes[static_cast<std::size_t>(e.node)].object].offset);
    BlenderRef placed;
    if (!resolve_blender(v, e.index, placed, refusal)) return false;
    const std::size_t first = relocate(v, placed.head, placed.slots, k_pointer_size, placed.slots.count);
    set_global(v.data(), v.pf.data_section, first + placed.slots.count * k_pointer_size, child);
    if (v.failed) return refuse(refusal, "an edit position fell outside the data section");
    sort_fixups(v.data());
    std::vector<std::byte> next;
    if (const auto status = rewrite_packfile(appended.bytes, v.pf, next); status != HkxStatus::Ok) {
      return refuse(refusal, "the packfile could not be written again (status " + std::to_string(static_cast<int>(status)) + ")");
    }
    Work after;
    if (!load(next, after, refusal)) return false;
    if (!confirm_blend_children(w.model, after.model, e, refusal)) return false;
    bytes_ = std::move(next);
    return true;
  }

  // Constraint: only a generator nothing points at is removed, with everything only it reached; the nodes taken must all lie below it or be transition effects.
  bool remove_subtree(const HkxBehaviorEdit& e, std::string& refusal) {
    Work w;
    if (!load(bytes_, w, refusal)) return false;
    if (e.node < 0 || static_cast<std::size_t>(e.node) >= w.model.nodes.size()) return refuse(refusal, "the node is outside the nodes table");
    if (e.node == w.model.root_node) return refuse(refusal, "the root generator is never removed");
    const HkxGraphNode& node = w.model.nodes[static_cast<std::size_t>(e.node)];
    if (node.class_name != "hkbStateMachine" && !node.class_name.ends_with("Generator")) return refuse(refusal, "the node is not a generator");
    const std::size_t request[] = {node.object};
    HkxRemoval removal;
    if (remove_objects(w.pf, request, true, removal) != HkxStatus::Ok) return refuse(refusal, removal.refusal);
    std::vector<bool> dead(w.model.nodes.size(), false);
    for (std::size_t i = 0; i < w.model.nodes.size(); ++i) {
      dead[i] = std::binary_search(removal.removed.begin(), removal.removed.end(), w.model.nodes[i].object);
    }
    for (std::size_t i = 0; i < dead.size(); ++i) {
      if (!dead[i] || i == static_cast<std::size_t>(e.node)) continue;
      const bool effect = std::any_of(w.model.effects.begin(), w.model.effects.end(), [&](const HkxGraphEffect& row) { return row.node == i; });
      if (!effect && !reaches(w.model, static_cast<std::size_t>(e.node), i)) {
        return refuse(refusal, "the removal would also take node " + std::to_string(i) + " (" + printable_text(w.model.nodes[i].class_name) + " '" +
                                   printable_text(w.model.nodes[i].name) + "'), which is not below the requested node");
      }
    }
    if (!finish(w, refusal)) return false;
    Work after;
    if (!load(bytes_, after, refusal)) return false;
    return confirm_subtree(w.model, after.model, dead, refusal);
  }
};

std::map<std::string, std::size_t> error_counts(const HkxGraphValidation& validation) {
  std::map<std::string, std::size_t> counts;
  for (const HkxGraphIssue& issue : validation.issues) {
    if (issue.severity == HkxGraphIssueSeverity::Error) counts[issue.code] += 1;
  }
  return counts;
}

}  // namespace

HkxBehaviorEditReport apply_behavior_edits(std::span<const std::byte> source, std::span<const HkxBehaviorEdit> edits,
                                           std::vector<std::byte>& out) {
  HkxBehaviorEditReport report;
  if (edits.empty()) {
    report.refusal = "there are no edits to apply";
    return report;
  }
  Work first;
  if (!load(source, first, report.refusal)) return report;
  report.before = validate_behavior_model(first.model);

  Editor editor(source);
  for (std::size_t i = 0; i < edits.size(); ++i) {
    if (!editor.apply(edits[i], report.refusal)) {
      report.refused_edit = i;
      return report;
    }
    report.applied = i + 1;
  }

  Work last;
  if (!load(editor.bytes(), last, report.refusal)) {
    report.refused_edit = edits.size() - 1;
    return report;
  }
  report.after = validate_behavior_model(last.model);
  const auto baseline = error_counts(report.before);
  const auto current = error_counts(report.after);
  for (const auto& entry : current) {
    const auto old = baseline.find(entry.first);
    if (old != baseline.end() && entry.second <= old->second) continue;
    for (const HkxGraphIssue& issue : report.after.issues) {
      if (issue.severity == HkxGraphIssueSeverity::Error && issue.code == entry.first) {
        report.refusal = "the edits would add the validation error " + issue.code + ": " + issue.message;
        break;
      }
    }
    report.refused_edit = edits.size() - 1;
    report.applied = 0;
    return report;
  }
  out = editor.bytes();
  report.ok = true;
  return report;
}

}  // namespace fo4::hkx
