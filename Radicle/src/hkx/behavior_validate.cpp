#include "fo4/hkx/behavior_validate.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using Severity = HkxGraphIssueSeverity;

constexpr std::int8_t k_last_variable_type = static_cast<std::int8_t>(HkxVariableType::Quaternion);
// Invariant: PlaybackMode USER_CONTROLLED is 2 in HavokClassTypes.json.
constexpr std::int8_t k_user_controlled_mode = 2;
// Invariant: TransitionFlags FLAG_TO_NESTED_STATE_ID_IS_VALID is 8192 in HavokClassTypes.json.
constexpr std::uint16_t k_to_nested_state_flag = 0x2000;
// Constraint: a nested entry follows at most this many wrapper generators from a state's generator to its state machine.
constexpr std::size_t k_nested_depth = 6;
// Invariant: StartStateMode START_STATE_MODE_CHOOSER is 3 in HavokClassTypes.json.
constexpr std::int8_t k_start_mode_chooser = 3;
// Constraint: the engine check of one machine's state count is 0x8000, UNVERIFIED against the recorded exe (see the code note).
constexpr std::size_t k_state_limit = 0x8000;

bool run_fits(std::size_t first, std::size_t count, std::size_t total) {
  return first <= total && count <= total - first;
}

class Checker {
 public:
  explicit Checker(const HkxGraphModel& model) : model_(model) {}

  HkxGraphValidation run() {
    root();
    tables();
    machines();
    reachability();
    blenders();
    selectors();
    clips();
    bindings();
    edges();
    node_reachability();
    object_slots();
    const std::set<std::size_t> faulted = pointers();
    for (std::size_t i = 0; i < model_.diagnostics.size(); ++i) {
      if (faulted.contains(i)) continue;
      add(Severity::Warning, "decode.note", "diagnostics", i, model_.diagnostics[i]);
    }
    return std::move(out_);
  }

 private:
  const HkxGraphModel& model_;
  HkxGraphValidation out_;

  void add(Severity severity, const char* code, const char* table, std::size_t index, std::string message) {
    out_.issues.push_back({severity, code, table, index, std::move(message)});
    (severity == Severity::Error ? out_.errors : out_.warnings) += 1;
  }

  bool event_ok(std::int32_t id) const {
    return id == -1 || (id >= 0 && static_cast<std::size_t>(id) < model_.events.size());
  }

  bool variable_ok(std::int32_t index) const {
    return index == -1 || (index >= 0 && static_cast<std::size_t>(index) < model_.variables.size());
  }

  bool node_ok(std::int32_t index) const {
    return index >= 0 && static_cast<std::size_t>(index) < model_.nodes.size();
  }

  static std::string text(std::int64_t value) { return std::to_string(value); }

  template <typename NameAt>
  void names(const char* table, std::size_t count, NameAt name_at, const char* missing, const char* duplicate,
             Severity severity) {
    std::unordered_map<std::string, std::size_t> first;
    for (std::size_t i = 0; i < count; ++i) {
      const std::string& name = name_at(i);
      if (name.empty()) {
        if (missing != nullptr) add(Severity::Error, missing, table, i, std::string(table) + " entry " + text(i) + " has no name");
        continue;
      }
      const auto found = first.emplace(name, i);
      if (!found.second) add(severity, duplicate, table, i, "'" + name + "' repeats entry " + text(found.first->second));
    }
  }

  void root() {
    if (!node_ok(model_.root_node)) add(Severity::Error, "root.missing", "", 0, "the graph has no root generator that reaches a node");
  }

  void tables() {
    names("events", model_.events.size(), [&](std::size_t i) -> const std::string& { return model_.events[i].name; },
          "event.name_missing", "event.duplicate_name", Severity::Error);
    names("variables", model_.variables.size(),
          [&](std::size_t i) -> const std::string& { return model_.variables[i].name; }, "variable.name_missing",
          "variable.duplicate_name", Severity::Error);
    names("nodes", model_.nodes.size(), [&](std::size_t i) -> const std::string& { return model_.nodes[i].name; },
          nullptr, "node.duplicate_name", Severity::Warning);
    if (model_.event_name_count != model_.event_info_count) {
      add(Severity::Error, "event.table_length", "events", 0,
          text(static_cast<std::int64_t>(model_.event_name_count)) + " event names but " +
              text(static_cast<std::int64_t>(model_.event_info_count)) + " event infos");
    }
    if (model_.variable_name_count != model_.variable_info_count) {
      add(Severity::Error, "variable.table_length", "variables", 0,
          text(static_cast<std::int64_t>(model_.variable_name_count)) + " variable names but " +
              text(static_cast<std::int64_t>(model_.variable_info_count)) + " variable infos");
    }
    if (model_.variable_value_count != HkxGraphModel::k_no_value_set && model_.variable_value_count < model_.variables.size()) {
      add(Severity::Warning, "variable.value_missing", "variables", model_.variable_value_count,
          "the initial value set holds " + text(static_cast<std::int64_t>(model_.variable_value_count)) + " values for " +
              text(static_cast<std::int64_t>(model_.variables.size())) + " variables");
    }
    if (model_.variable_value_count != HkxGraphModel::k_no_value_set && model_.variable_value_count > model_.variables.size()) {
      add(Severity::Warning, "variable.value_surplus", "variables", model_.variables.size(),
          "the initial value set holds " + text(static_cast<std::int64_t>(model_.variable_value_count)) + " values for " +
              text(static_cast<std::int64_t>(model_.variables.size())) + " variables");
    }
    if (model_.variable_bounds_count > model_.variable_name_count) {
      add(Severity::Warning, "variable.bounds_surplus", "variables", model_.variable_name_count,
          text(static_cast<std::int64_t>(model_.variable_bounds_count)) + " variable bounds for " +
              text(static_cast<std::int64_t>(model_.variable_name_count)) + " variables");
    }
    for (std::size_t i = 0; i < model_.variables.size(); ++i) {
      const HkxGraphVariable& variable = model_.variables[i];
      if (variable.type < 0 || variable.type > k_last_variable_type) {
        add(Severity::Warning, "variable.type_unknown", "variables", i, "variable type " + text(variable.type) + " is not a known value");
      }
      const bool vector_type = variable.type == static_cast<std::int8_t>(HkxVariableType::Vector3) ||
                               variable.type == static_cast<std::int8_t>(HkxVariableType::Vector4) ||
                               variable.type == static_cast<std::int8_t>(HkxVariableType::Quaternion);
      if (vector_type && !variable.has_vector) {
        add(Severity::Error, "variable.vector_missing", "variables", i, "the quad value of a vector variable is not in the file");
      }
    }
  }

  void check_transition(std::size_t index, const std::map<std::int32_t, std::size_t>& state_ids) {
    const HkxGraphTransition& t = model_.transitions[index];
    if (t.state >= 0 && static_cast<std::size_t>(t.state) >= model_.states.size()) {
      add(Severity::Error, "transition.state_range", "transitions", index, "the owning state is outside the states table");
    }
    if (!state_ids.contains(t.to_state_id)) {
      add(Severity::Error, "transition.target_missing", "transitions", index,
          "to_state_id " + text(t.to_state_id) + " names no state of its machine");
    }
    if (!event_ok(t.event_id)) {
      add(Severity::Error, "transition.event_range", "transitions", index,
          "event id " + text(t.event_id) + " is outside the " + text(model_.events.size()) + " events");
    }
    const std::int32_t interval_events[] = {t.trigger_interval.enter_event_id, t.trigger_interval.exit_event_id,
                                            t.initiate_interval.enter_event_id, t.initiate_interval.exit_event_id};
    for (const std::int32_t id : interval_events) {
      if (!event_ok(id)) {
        add(Severity::Error, "transition.interval_event_range", "transitions", index,
            "interval event id " + text(id) + " is outside the " + text(model_.events.size()) + " events");
      }
    }
    if (t.event_id == -1 && t.condition_kind == HkxGraphConditionKind::None) {
      add(Severity::Warning, "transition.eventless_no_condition", "transitions", index,
          "the transition has neither an event nor a condition, so nothing can fire it");
    }
    if (t.effect_node != -1) {
      if (!node_ok(t.effect_node)) {
        add(Severity::Error, "transition.effect_range", "transitions", index, "the effect node is outside the nodes table");
      } else if (!model_.nodes[static_cast<std::size_t>(t.effect_node)].class_name.ends_with("Effect")) {
        add(Severity::Warning, "transition.effect_class", "transitions", index, "the effect node is not a transition effect class");
      }
    }
  }

  void check_run(std::size_t first, std::size_t count, const std::map<std::int32_t, std::size_t>& state_ids) {
    std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t, int, unsigned, std::string>> seen;
    for (std::size_t i = first; i < first + count; ++i) {
      check_transition(i, state_ids);
      const HkxGraphTransition& t = model_.transitions[i];
      const auto key = std::make_tuple(t.event_id, t.to_state_id, t.from_nested_state_id, t.to_nested_state_id,
                                       static_cast<int>(t.priority), static_cast<unsigned>(t.flags), t.condition);
      if (!seen.insert(key).second) {
        add(Severity::Warning, "transition.duplicate", "transitions", i, "an earlier transition of the same run has the same event, target, priority, flags and condition");
      }
    }
  }

  void machines() {
    for (std::size_t mi = 0; mi < model_.state_machines.size(); ++mi) {
      const HkxGraphStateMachine& m = model_.state_machines[mi];
      const std::pair<const char*, std::int32_t> event_ids[] = {
          {"change", m.change_event_id},           {"return to previous", m.return_to_previous_event_id},
          {"random transition", m.random_transition_event_id}, {"next higher", m.next_higher_event_id},
          {"next lower", m.next_lower_event_id},
      };
      for (const auto& entry : event_ids) {
        if (!event_ok(entry.second)) {
          add(Severity::Error, "state_machine.event_range", "state_machines", mi,
              std::string(entry.first) + " event id " + text(entry.second) + " is outside the " + text(model_.events.size()) + " events");
        }
      }
      if (!variable_ok(m.sync_variable_index)) {
        add(Severity::Error, "state_machine.sync_variable_range", "state_machines", mi,
            "sync variable " + text(m.sync_variable_index) + " is outside the variables table");
      }
      if (!run_fits(m.first_state, m.state_count, model_.states.size())) {
        add(Severity::Error, "state_machine.state_range", "state_machines", mi, "the state run is outside the states table");
        continue;
      }
      if (m.state_count == 0) add(Severity::Error, "state_machine.no_states", "state_machines", mi, "the state machine has no states");
      if (m.state_count > k_state_limit) {
        add(Severity::Warning, "state_machine.too_many_states", "state_machines", mi,
            text(static_cast<std::int64_t>(m.state_count)) + " states are more than the " + text(static_cast<std::int64_t>(k_state_limit)) + " one machine may hold");
      }
      if (m.start_state_mode == k_start_mode_chooser && !m.has_start_state_selector) {
        add(Severity::Warning, "state_machine.chooser_missing", "state_machines", mi, "the start state mode names a chooser and the machine has none");
      }

      std::map<std::int32_t, std::size_t> state_ids;
      std::unordered_map<std::string, std::size_t> state_names;
      for (std::size_t si = m.first_state; si < m.first_state + m.state_count; ++si) {
        const HkxGraphState& s = model_.states[si];
        const auto id = state_ids.emplace(s.state_id, si);
        if (!id.second) {
          add(Severity::Error, "state.duplicate_id", "states", si, "state id " + text(s.state_id) + " repeats state " + text(id.first->second));
        }
        if (!s.name.empty()) {
          const auto name = state_names.emplace(s.name, si);
          if (!name.second) add(Severity::Warning, "state.duplicate_name", "states", si, "'" + s.name + "' repeats state " + text(name.first->second));
        }
        if (s.generator_node < 0) {
          add(Severity::Warning, "state.generator_missing", "states", si, "the state has no generator");
        } else if (!node_ok(s.generator_node)) {
          add(Severity::Error, "state.generator_range", "states", si, "the generator node is outside the nodes table");
        }
        if (!run_fits(s.first_transition, s.transition_count, model_.transitions.size())) {
          add(Severity::Error, "state.transition_range", "states", si, "the transition run is outside the transitions table");
        }
      }
      if (m.start_state_mode == 0 && m.state_count > 0 && !state_ids.contains(m.start_state_id)) {
        add(Severity::Error, "state_machine.start_state_missing", "state_machines", mi,
            "start state id " + text(m.start_state_id) + " names no state of the machine");
      }

      const bool wildcards_fit = run_fits(m.first_wildcard, m.wildcard_count, model_.transitions.size());
      if (!wildcards_fit) add(Severity::Error, "state_machine.wildcard_range", "state_machines", mi, "the wildcard run is outside the transitions table");
      for (std::size_t si = m.first_state; si < m.first_state + m.state_count; ++si) {
        const HkxGraphState& s = model_.states[si];
        if (run_fits(s.first_transition, s.transition_count, model_.transitions.size())) {
          check_run(s.first_transition, s.transition_count, state_ids);
        }
      }
      if (wildcards_fit) check_run(m.first_wildcard, m.wildcard_count, state_ids);
    }
  }

  struct MachineReach {
    bool checked{false};
    std::map<std::int32_t, std::size_t> ids;
    std::set<std::int32_t> reached;
    bool wildcards_fired{false};
  };

  // Constraint: a machine the file does not start by its start state id alone is open, and an open machine reaches every enabled state, so nothing is reported for it.
  static bool entry_open(const HkxGraphStateMachine& m) {
    return m.start_state_mode != 0 || m.random_transition_event_id >= 0 || m.next_higher_event_id >= 0 ||
           m.next_lower_event_id >= 0 || m.sync_variable_index >= 0 || m.has_start_state_selector;
  }

  std::int32_t entered_machine(std::int32_t node, const std::vector<std::int32_t>& wrapped) const {
    for (std::size_t depth = 0; depth <= k_nested_depth && node_ok(node); ++depth) {
      const HkxGraphNode& n = model_.nodes[static_cast<std::size_t>(node)];
      if (n.kind == HkxGraphNodeKind::StateMachine) {
        return n.kind_index < model_.state_machines.size() ? static_cast<std::int32_t>(n.kind_index) : -1;
      }
      node = wrapped[static_cast<std::size_t>(node)];
    }
    return -1;
  }

  void reachability() {
    std::vector<MachineReach> info(model_.state_machines.size());
    std::vector<bool> usable(info.size(), false);
    for (std::size_t mi = 0; mi < info.size(); ++mi) {
      const HkxGraphStateMachine& m = model_.state_machines[mi];
      if (!run_fits(m.first_state, m.state_count, model_.states.size()) ||
          !run_fits(m.first_wildcard, m.wildcard_count, model_.transitions.size()) || m.state_count == 0) {
        continue;
      }
      usable[mi] = true;
      std::size_t rows = m.wildcard_count;
      for (std::size_t si = m.first_state; si < m.first_state + m.state_count; ++si) {
        info[mi].ids.emplace(model_.states[si].state_id, si);
        rows += model_.states[si].transition_count;
      }
      info[mi].checked = rows > 0 && !entry_open(m) && info[mi].ids.contains(m.start_state_id);
    }
    std::vector<std::int32_t> wrapped(model_.nodes.size(), -1);
    for (const HkxGraphEdge& e : model_.edges) {
      if (e.role == HkxGraphEdgeRole::Wrapped && e.parent < wrapped.size() && wrapped[e.parent] < 0) wrapped[e.parent] = e.child;
    }

    std::vector<std::pair<std::size_t, std::int32_t>> queue;
    const auto reach = [&](std::size_t mi, std::int32_t id) {
      MachineReach& r = info[mi];
      const auto found = r.ids.find(id);
      if (found == r.ids.end() || !model_.states[found->second].enable) return;
      if (r.reached.insert(id).second) queue.emplace_back(mi, id);
    };
    const auto take = [&](std::size_t mi, const HkxGraphTransition& t) {
      const auto target = info[mi].ids.find(t.to_state_id);
      if (target == info[mi].ids.end() || !model_.states[target->second].enable) return;
      reach(mi, t.to_state_id);
      if ((t.flags & k_to_nested_state_flag) == 0) return;
      const std::int32_t nested = entered_machine(model_.states[target->second].generator_node, wrapped);
      if (nested >= 0 && usable[static_cast<std::size_t>(nested)]) reach(static_cast<std::size_t>(nested), t.to_nested_state_id);
    };
    for (std::size_t mi = 0; mi < info.size(); ++mi) {
      if (!usable[mi]) continue;
      if (info[mi].checked) {
        reach(mi, model_.state_machines[mi].start_state_id);
        continue;
      }
      const HkxGraphStateMachine& m = model_.state_machines[mi];
      for (std::size_t si = m.first_state; si < m.first_state + m.state_count; ++si) reach(mi, model_.states[si].state_id);
    }
    while (!queue.empty()) {
      const auto [mi, id] = queue.back();
      queue.pop_back();
      const HkxGraphStateMachine& m = model_.state_machines[mi];
      if (!info[mi].wildcards_fired) {
        info[mi].wildcards_fired = true;
        for (std::size_t i = m.first_wildcard; i < m.first_wildcard + m.wildcard_count; ++i) take(mi, model_.transitions[i]);
      }
      const HkxGraphState& s = model_.states[info[mi].ids.at(id)];
      if (!run_fits(s.first_transition, s.transition_count, model_.transitions.size())) continue;
      for (std::size_t i = s.first_transition; i < s.first_transition + s.transition_count; ++i) take(mi, model_.transitions[i]);
    }

    for (std::size_t mi = 0; mi < info.size(); ++mi) {
      if (!info[mi].checked) continue;
      const HkxGraphStateMachine& m = model_.state_machines[mi];
      for (std::size_t si = m.first_state; si < m.first_state + m.state_count; ++si) {
        const HkxGraphState& s = model_.states[si];
        if (info[mi].reached.contains(s.state_id) || !s.enable) continue;
        add(Severity::Warning, "state.unreachable", "states", si,
            "no transition from the start state, a wildcard or a nested entry reaches state id " + text(s.state_id));
        if (!run_fits(s.first_transition, s.transition_count, model_.transitions.size())) continue;
        for (std::size_t i = s.first_transition; i < s.first_transition + s.transition_count; ++i) {
          add(Severity::Warning, "transition.unreachable", "transitions", i, "its state is never reached");
        }
      }
    }
  }

  void blenders() {
    for (std::size_t bi = 0; bi < model_.blenders.size(); ++bi) {
      const HkxGraphBlender& b = model_.blenders[bi];
      if (!run_fits(b.first_child, b.child_count, model_.blend_children.size())) {
        add(Severity::Error, "blender.child_range", "blenders", bi, "the child run is outside the blend children table");
        continue;
      }
      if (b.child_count == 0) add(Severity::Warning, "blender.no_children", "blenders", bi, "the blender has no children");
      if (b.index_of_sync_master_child < -1 || b.index_of_sync_master_child >= static_cast<std::int32_t>(b.child_count)) {
        add(Severity::Error, "blender.sync_master_range", "blenders", bi, "the sync master child is not one of the blender's children");
      }
      if (!variable_ok(b.blend_parameter_variable)) {
        add(Severity::Error, "blender.variable_range", "blenders", bi, "the blend parameter variable is outside the variables table");
      }
      for (std::size_t ci = b.first_child; ci < b.first_child + b.child_count; ++ci) {
        const HkxGraphBlendChild& c = model_.blend_children[ci];
        if (c.generator_node < 0) {
          add(Severity::Warning, "blend_child.generator_missing", "blend_children", ci, "the blend child has no generator");
        } else if (!node_ok(c.generator_node)) {
          add(Severity::Error, "blend_child.generator_range", "blend_children", ci, "the generator node is outside the nodes table");
        }
        if (!variable_ok(c.weight_variable)) {
          add(Severity::Error, "blend_child.variable_range", "blend_children", ci, "the weight variable is outside the variables table");
        }
        if (!std::isfinite(c.weight)) add(Severity::Error, "blend_child.weight_invalid", "blend_children", ci, "the weight is not finite");
        if (!(c.world_from_model_weight >= 0.0f && c.world_from_model_weight <= 1.0f)) {
          add(Severity::Warning, "blender.motion_weight_range", "blend_children", ci, "the motion weight is outside 0 to 1");
        }
        if (!b.parametric && c.weight_variable < 0 && std::isfinite(c.weight) && !(c.weight >= 0.0f && c.weight <= 1.0f)) {
          add(Severity::Warning, "blender.child_weight_range", "blend_children", ci, "a child weight of a non-parametric blender is outside 0 to 1");
        }
      }
    }
  }

  bool time_fraction_bound(std::size_t clip) const {
    for (const HkxGraphBinding& b : model_.bindings) {
      if (b.owner_kind == HkxGraphOwnerKind::Clip && b.owner_index == clip &&
          b.member_path.ends_with("userControlledTimeFraction")) {
        return true;
      }
    }
    return false;
  }

  void selectors() {
    for (std::size_t si = 0; si < model_.selectors.size(); ++si) {
      const HkxGraphSelector& s = model_.selectors[si];
      if (s.generator_count == 0) add(Severity::Warning, "selector.no_children", "selectors", si, "the selector has no generators");
      if (s.null_generator_count > 0) {
        add(Severity::Warning, "selector.null_child", "selectors", si, "the selector has a generator slot that points at nothing");
      }
      if (s.generator_count > 0 &&
          (s.selected_generator_index < 0 || static_cast<std::size_t>(s.selected_generator_index) >= s.generator_count)) {
        add(Severity::Warning, "selector.index_range", "selectors", si,
            "selected generator " + text(s.selected_generator_index) + " is outside the " +
                text(static_cast<std::int64_t>(s.generator_count)) + " generators");
      }
    }
  }

  void clips() {
    for (std::size_t ci = 0; ci < model_.clips.size(); ++ci) {
      const HkxGraphClip& clip = model_.clips[ci];
      if (clip.animation.empty()) add(Severity::Warning, "clip.animation_missing", "clips", ci, "the clip names no animation");
      if (clip.animation_binding_index != -1) {
        add(Severity::Warning, "clip.binding_index_set", "clips", ci,
            "animation binding index " + text(clip.animation_binding_index) + " is not -1, so the engine skips resolving the animation by name");
      }
      if (clip.mode == k_user_controlled_mode && !time_fraction_bound(ci)) {
        add(Severity::Warning, "clip.user_time_unbound", "clips", ci, "a user controlled clip has no binding on userControlledTimeFraction");
      }
      for (const HkxGraphClipTrigger& trigger : clip.triggers) {
        if (!event_ok(trigger.event_id)) {
          add(Severity::Error, "clip.trigger_event_range", "clips", ci,
              "trigger event id " + text(trigger.event_id) + " is outside the " + text(model_.events.size()) + " events");
        }
        if (!std::isfinite(trigger.local_time)) add(Severity::Error, "clip.trigger_time_invalid", "clips", ci, "a trigger time is not finite");
      }
    }
  }

  std::size_t owner_count(HkxGraphOwnerKind kind) const {
    switch (kind) {
      case HkxGraphOwnerKind::StateMachine:
        return model_.state_machines.size();
      case HkxGraphOwnerKind::State:
        return model_.states.size();
      case HkxGraphOwnerKind::Blender:
        return model_.blenders.size();
      case HkxGraphOwnerKind::BlendChild:
        return model_.blend_children.size();
      case HkxGraphOwnerKind::Clip:
        return model_.clips.size();
    }
    return 0;
  }

  void bindings() {
    for (std::size_t i = 0; i < model_.bindings.size(); ++i) {
      const HkxGraphBinding& b = model_.bindings[i];
      if (b.owner_index >= owner_count(b.owner_kind)) {
        add(Severity::Error, "binding.owner_range", "bindings", i, "the owner index is outside its table");
      }
      if (b.binding_type != 0) continue;
      if (b.variable_index == -1) {
        add(Severity::Warning, "binding.variable_unset", "bindings", i, "the binding names no variable");
      } else if (!variable_ok(b.variable_index)) {
        add(Severity::Error, "binding.variable_range", "bindings", i, "variable " + text(b.variable_index) + " is outside the variables table");
      }
    }
  }

  void edges() {
    for (std::size_t i = 0; i < model_.edges.size(); ++i) {
      const HkxGraphEdge& e = model_.edges[i];
      if (e.parent >= model_.nodes.size() || !node_ok(e.child)) {
        add(Severity::Error, "edge.range", "edges", i, "the edge's parent or child is outside the nodes table");
      }
    }
  }

  void object_slots() {
    for (const HkxGraphObjectSlot& slot : model_.object_event_slots) {
      if (static_cast<std::size_t>(slot.value) < model_.events.size()) continue;
      add(Severity::Error, "object.event_range", "objects", slot.object,
          slot.member + " names event " + text(slot.value) + ", outside the " + text(model_.events.size()) + " events");
    }
    for (const HkxGraphObjectSlot& slot : model_.object_variable_slots) {
      if (static_cast<std::size_t>(slot.value) < model_.variables.size()) continue;
      add(Severity::Error, "object.variable_range", "objects", slot.object,
          slot.member + " names variable " + text(slot.value) + ", outside the " + text(model_.variables.size()) + " variables");
    }
  }

  std::set<std::size_t> pointers() {
    std::set<std::size_t> mirrored;
    for (const HkxGraphPointerFault& fault : model_.pointer_faults) {
      add(Severity::Error, fault.wrong_class ? "reference.wrong_class" : "reference.dangling", "pointers", fault.offset,
          fault.message);
      if (fault.diagnostic != HkxGraphPointerFault::k_no_diagnostic) mirrored.insert(fault.diagnostic);
    }
    return mirrored;
  }

  void node_reachability() {
    if (!node_ok(model_.root_node)) return;
    std::vector<std::vector<std::size_t>> next(model_.nodes.size());
    const auto link = [&](std::size_t from, std::int32_t to) {
      if (from < next.size() && node_ok(to)) next[from].push_back(static_cast<std::size_t>(to));
    };
    for (const HkxGraphEdge& e : model_.edges) link(e.parent, e.child);
    for (const HkxGraphState& s : model_.states) {
      if (s.state_machine < model_.state_machines.size()) link(model_.state_machines[s.state_machine].node, s.generator_node);
    }
    for (const HkxGraphTransition& t : model_.transitions) {
      if (t.state_machine < model_.state_machines.size()) link(model_.state_machines[t.state_machine].node, t.effect_node);
    }
    for (const HkxGraphBlendChild& c : model_.blend_children) {
      if (c.blender < model_.blenders.size()) link(model_.blenders[c.blender].node, c.generator_node);
    }
    std::vector<bool> seen(model_.nodes.size(), false);
    std::vector<std::size_t> queue{static_cast<std::size_t>(model_.root_node)};
    seen[queue.front()] = true;
    while (!queue.empty()) {
      const std::size_t at = queue.back();
      queue.pop_back();
      for (const std::size_t to : next[at]) {
        if (seen[to]) continue;
        seen[to] = true;
        queue.push_back(to);
      }
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
      if (!seen[i]) add(Severity::Warning, "node.unreachable", "nodes", i, "no path from the root generator reaches this node");
    }
  }
};

}  // namespace

HkxGraphValidation validate_behavior_model(const HkxGraphModel& model) { return Checker(model).run(); }

}  // namespace fo4::hkx
