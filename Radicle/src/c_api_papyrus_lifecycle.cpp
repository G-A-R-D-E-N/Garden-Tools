#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_papyrus_internal.hpp"
#include "c_api_papyrus_runtime_internal.hpp"
#include "fo4/c_api_papyrus_lifecycle.h"
#include "fo4/papyrus/quest_lifecycle.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using fo4::papyrus::ActorAliasInput;
using fo4::papyrus::AliasScriptInput;
using fo4::papyrus::OwnerId;
using fo4::papyrus::OwnerKind;
using fo4::papyrus::QuestLifecycle;
using fo4::papyrus::QuestStartupInput;
using fo4::papyrus::QuestStartupProperty;
using fo4::papyrus::Value;
using fo4::papyrus::VmadValueShape;

struct radicle_papyrus_lifecycle_handle {
  QuestLifecycle lifecycle;

  explicit radicle_papyrus_lifecycle_handle(radicle_papyrus_runtime* runtime)
      : lifecycle(runtime->runtime, [runtime](std::string_view key) {
          if (runtime->callbacks.gate == nullptr) return true;
          const std::string text(key);
          return runtime->callbacks.gate(runtime->callbacks.user, text.c_str()) != 0;
        }) {}
};

namespace {

using radicle_c_papyrus_detail::to_c_status;

char* copyText(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

std::string textOf(const char* text) { return text == nullptr ? std::string() : std::string(text); }

std::optional<OwnerId> ownerFrom(radicle_papyrus_owner_kind kind, const char* base, const char* discriminator) {
  if (base == nullptr) return std::nullopt;
  const std::string baseKey(base);
  const std::string disc = textOf(discriminator);
  switch (kind) {
    case RADICLE_PAPYRUS_OWNER_FORM:
      return OwnerId::forForm(baseKey);
    case RADICLE_PAPYRUS_OWNER_PLACED_REFERENCE:
      if (disc.empty()) return std::nullopt;
      return OwnerId::forPlacedReference(baseKey, disc);
    case RADICLE_PAPYRUS_OWNER_DYNAMIC_REFERENCE:
      if (disc.empty()) return std::nullopt;
      return OwnerId::forDynamicReference(baseKey, disc);
    case RADICLE_PAPYRUS_OWNER_QUEST_ALIAS:
      return OwnerId::forQuestAlias(baseKey, static_cast<std::int32_t>(std::strtol(disc.c_str(), nullptr, 10)));
    case RADICLE_PAPYRUS_OWNER_RUNTIME_INSTANCE:
      return OwnerId::forRuntimeInstance(baseKey, std::strtoll(disc.c_str(), nullptr, 10));
  }
  return std::nullopt;
}

VmadValueShape shapeFrom(const radicle_papyrus_lifecycle_property& property) {
  VmadValueShape shape;
  shape.name = textOf(property.name);
  shape.type = textOf(property.type);
  shape.hasAlias = property.has_alias != 0;
  if (property.text != nullptr) shape.text = std::string(property.text);
  if (property.has_real != 0) shape.real = property.real;
  if (property.has_integer != 0) shape.integer = property.integer;
  if (property.has_boolean != 0) shape.boolean = property.boolean != 0;
  if (property.object != nullptr) shape.object = std::string(property.object);
  return shape;
}

QuestStartupInput startupFrom(const radicle_papyrus_quest_startup_input& input) {
  QuestStartupInput out;
  out.quest = textOf(input.quest);
  out.startGameEnabled = input.start_game_enabled != 0;
  for (size_t i = 0; i < input.script_count; ++i) out.scripts.push_back(textOf(input.scripts[i]));
  out.fragments = input.fragments;
  out.aliases = input.aliases;
  for (size_t i = 0; i < input.unsupported_count; ++i) out.unsupported.push_back(textOf(input.unsupported[i]));
  for (size_t i = 0; i < input.property_count; ++i) {
    out.properties.push_back(QuestStartupProperty{textOf(input.properties[i].script), shapeFrom(input.properties[i])});
  }
  return out;
}

AliasScriptInput aliasFrom(const radicle_papyrus_alias_script_input& input) {
  AliasScriptInput out;
  out.quest = textOf(input.quest);
  out.aliasId = input.alias_id;
  if (input.script != nullptr) out.script = std::string(input.script);
  for (size_t i = 0; i < input.property_count; ++i) out.properties.push_back(shapeFrom(input.properties[i]));
  for (size_t i = 0; i < input.objective_count; ++i) out.objectives.push_back(input.objectives[i]);
  return out;
}

radicle_status fillFanOut(const fo4::papyrus::FanOutReport& report, radicle_papyrus_fanout_report* out) {
  out->outcomes = new radicle_papyrus_fanout_outcome[report.outcomes.size()]{};
  out->count = report.outcomes.size();
  for (size_t i = 0; i < report.outcomes.size(); ++i) {
    const fo4::papyrus::FanOutOutcome& outcome = report.outcomes[i];
    out->outcomes[i].owner_key = copyText(outcome.owner.key());
    out->outcomes[i].instance = outcome.instance;
    out->outcomes[i].script = copyText(outcome.script);
    out->outcomes[i].result = static_cast<radicle_papyrus_fanout_result>(outcome.result);
    out->outcomes[i].status = to_c_status(outcome.status);
    out->outcomes[i].reason = copyText(outcome.reason);
  }
  out->delivered = report.delivered;
  out->refused = report.refused;
  out->skipped_parked = report.skippedParked;
  out->no_handler = report.noHandler;
  out->unknown_owner = report.unknownOwner;
  out->handler = copyText(report.handler);
  out->report = copyText(report.toReport());
  out->digest = copyText(report.digest());
  return RADICLE_STATUS_OK;
}

bool collectArgs(const radicle_papyrus_value* const* args, size_t count, std::vector<Value>& out) {
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (args[i] == nullptr) return false;
    out.push_back(args[i]->value);
  }
  return true;
}

}  // namespace

extern "C" {

radicle_status radicle_papyrus_lifecycle_create(radicle_papyrus_runtime* runtime,
                                                radicle_papyrus_lifecycle_handle** out_lifecycle) {
  radicle_c_detail::clear_error();
  if (out_lifecycle != nullptr) *out_lifecycle = nullptr;
  if (runtime == nullptr || out_lifecycle == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out_lifecycle are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while creating the quest lifecycle", [&] {
    *out_lifecycle = new radicle_papyrus_lifecycle_handle(runtime);
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_lifecycle_destroy(radicle_papyrus_lifecycle_handle* lifecycle) {
  radicle_c_detail::clear_error();
  delete lifecycle;
}

radicle_status radicle_papyrus_lifecycle_preflight(radicle_papyrus_lifecycle_handle* lifecycle,
                                                   const radicle_papyrus_quest_startup_input* inputs, size_t count,
                                                   radicle_papyrus_quest_startup_plan* out_plan) {
  radicle_c_detail::clear_error();
  if (out_plan != nullptr) *out_plan = {};
  if (lifecycle == nullptr || out_plan == nullptr || (inputs == nullptr && count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "lifecycle, inputs and out_plan are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while planning quest startup", [&] {
    std::vector<QuestStartupInput> list;
    for (size_t i = 0; i < count; ++i) list.push_back(startupFrom(inputs[i]));
    const auto plan = lifecycle->lifecycle.preflight(list);
    out_plan->ready = plan.ready;
    out_plan->blocked = plan.blocked;
    out_plan->skipped = plan.skipped;
    out_plan->report = copyText(plan.toReport());
    out_plan->digest = copyText(plan.digest());
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_quest_startup_plan_destroy(radicle_papyrus_quest_startup_plan* plan) {
  radicle_c_detail::clear_error();
  if (plan == nullptr) return;
  std::free(plan->report);
  std::free(plan->digest);
  *plan = radicle_papyrus_quest_startup_plan{};
}

radicle_status radicle_papyrus_lifecycle_start(radicle_papyrus_lifecycle_handle* lifecycle,
                                               const radicle_papyrus_quest_startup_input* inputs, size_t count,
                                               radicle_papyrus_quest_startup_result* out_result) {
  radicle_c_detail::clear_error();
  if (out_result != nullptr) *out_result = {};
  if (lifecycle == nullptr || out_result == nullptr || (inputs == nullptr && count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "lifecycle, inputs and out_result are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while starting quests", [&] {
    std::vector<QuestStartupInput> list;
    for (size_t i = 0; i < count; ++i) list.push_back(startupFrom(inputs[i]));
    const auto result = lifecycle->lifecycle.start(list);
    out_result->applied = result.applied ? 1 : 0;
    out_result->started = result.started;
    out_result->attached_scripts = result.attachedScripts;
    return radicle_c_detail::copy_strings(result.reasons, &out_result->reasons, &out_result->reason_count);
  });
}

void radicle_papyrus_quest_startup_result_destroy(radicle_papyrus_quest_startup_result* result) {
  radicle_c_detail::clear_error();
  if (result == nullptr) return;
  radicle_free_string_array(result->reasons, result->reason_count);
  *result = radicle_papyrus_quest_startup_result{};
}

radicle_status radicle_papyrus_lifecycle_stop(radicle_papyrus_lifecycle_handle* lifecycle, const char* quest,
                                              int32_t* out_was_started) {
  radicle_c_detail::clear_error();
  if (out_was_started != nullptr) *out_was_started = 0;
  if (lifecycle == nullptr || quest == nullptr || out_was_started == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "lifecycle, quest and out_was_started are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while stopping a quest", [&] {
    *out_was_started = lifecycle->lifecycle.stop(quest) ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

int32_t radicle_papyrus_lifecycle_is_started(const radicle_papyrus_lifecycle_handle* lifecycle, const char* quest) {
  radicle_c_detail::clear_error();
  if (lifecycle == nullptr || quest == nullptr) return 0;
  return lifecycle->lifecycle.isStarted(quest) ? 1 : 0;
}

size_t radicle_papyrus_lifecycle_instance_count(const radicle_papyrus_lifecycle_handle* lifecycle,
                                                const char* quest) {
  radicle_c_detail::clear_error();
  if (lifecycle == nullptr || quest == nullptr) return 0;
  return lifecycle->lifecycle.instancesFor(quest).size();
}

int32_t radicle_papyrus_lifecycle_instance_at(const radicle_papyrus_lifecycle_handle* lifecycle, const char* quest,
                                              size_t index, uint64_t* out_instance) {
  radicle_c_detail::clear_error();
  if (out_instance != nullptr) *out_instance = 0;
  if (lifecycle == nullptr || quest == nullptr || out_instance == nullptr) return 0;
  const std::vector<fo4::papyrus::InstanceId> instances = lifecycle->lifecycle.instancesFor(quest);
  if (index >= instances.size()) return 0;
  *out_instance = instances[index];
  return 1;
}

radicle_status radicle_papyrus_lifecycle_get_counters(const radicle_papyrus_lifecycle_handle* lifecycle,
                                                      radicle_papyrus_lifecycle_counters* out_counters) {
  radicle_c_detail::clear_error();
  if (out_counters != nullptr) *out_counters = {};
  if (lifecycle == nullptr || out_counters == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "lifecycle and out_counters are required");
  }
  const fo4::papyrus::QuestLifecycleCounters& counters = lifecycle->lifecycle.counters();
  out_counters->started = counters.started;
  out_counters->blocked = counters.blocked;
  out_counters->skipped = counters.skipped;
  out_counters->attached_scripts = counters.attachedScripts;
  out_counters->approximations = counters.approximations;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_papyrus_lifecycle_attach_aliases(radicle_papyrus_lifecycle_handle* lifecycle,
                                                        const radicle_papyrus_alias_script_input* aliases,
                                                        size_t count,
                                                        radicle_papyrus_alias_attach_report* out_report) {
  radicle_c_detail::clear_error();
  if (out_report != nullptr) *out_report = {};
  if (lifecycle == nullptr || out_report == nullptr || (aliases == nullptr && count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "lifecycle, aliases and out_report are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while attaching alias scripts", [&] {
    std::vector<AliasScriptInput> list;
    for (size_t i = 0; i < count; ++i) list.push_back(aliasFrom(aliases[i]));
    const auto report = lifecycle->lifecycle.attachAliases(list);
    out_report->attached = report.attached;
    out_report->skipped = report.skipped;
    out_report->refused = report.refused;
    out_report->properties = report.properties;
    out_report->natives = report.natives;
    out_report->objectives = report.objectives;
    out_report->report = copyText(report.toReport());
    out_report->digest = copyText(report.digest());
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_alias_attach_report_destroy(radicle_papyrus_alias_attach_report* report) {
  radicle_c_detail::clear_error();
  if (report == nullptr) return;
  std::free(report->report);
  std::free(report->digest);
  *report = radicle_papyrus_alias_attach_report{};
}

radicle_status radicle_papyrus_lifecycle_bind_actors(radicle_papyrus_lifecycle_handle* lifecycle,
                                                     const radicle_papyrus_actor_alias_input* aliases, size_t count,
                                                     radicle_papyrus_actor_bind_report* out_report) {
  radicle_c_detail::clear_error();
  if (out_report != nullptr) *out_report = {};
  if (lifecycle == nullptr || out_report == nullptr || (aliases == nullptr && count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "lifecycle, aliases and out_report are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while binding alias actors", [&] {
    std::vector<ActorAliasInput> list;
    for (size_t i = 0; i < count; ++i) {
      ActorAliasInput input;
      input.alias = aliasFrom(aliases[i].alias);
      input.bound = aliases[i].bound != 0;
      if (aliases[i].target != nullptr) input.target = std::string(aliases[i].target);
      list.push_back(std::move(input));
    }
    const auto report = lifecycle->lifecycle.bindActors(list);
    out_report->bound = report.bound;
    out_report->unbound = report.unbound;
    out_report->attached = report.attached;
    out_report->refused = report.refused;
    out_report->report = copyText(report.toReport());
    out_report->digest = copyText(report.digest());
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_actor_bind_report_destroy(radicle_papyrus_actor_bind_report* report) {
  radicle_c_detail::clear_error();
  if (report == nullptr) return;
  std::free(report->report);
  std::free(report->digest);
  *report = radicle_papyrus_actor_bind_report{};
}

radicle_status radicle_papyrus_lifecycle_clear_alias(radicle_papyrus_lifecycle_handle* lifecycle, const char* quest,
                                                     int32_t alias_id, int32_t* out_cleared) {
  radicle_c_detail::clear_error();
  if (out_cleared != nullptr) *out_cleared = 0;
  if (lifecycle == nullptr || quest == nullptr || out_cleared == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "lifecycle, quest and out_cleared are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while clearing an alias", [&] {
    *out_cleared = lifecycle->lifecycle.clearAlias(OwnerId::forQuestAlias(quest, alias_id)) ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_lifecycle_clear_quest_aliases(radicle_papyrus_lifecycle_handle* lifecycle,
                                                             const char* quest, int32_t* out_cleared) {
  radicle_c_detail::clear_error();
  if (out_cleared != nullptr) *out_cleared = 0;
  if (lifecycle == nullptr || quest == nullptr || out_cleared == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "lifecycle, quest and out_cleared are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while clearing quest aliases", [&] {
    *out_cleared = lifecycle->lifecycle.clearQuestAliases(quest);
    return RADICLE_STATUS_OK;
  });
}

int32_t radicle_papyrus_lifecycle_alias_instance(const radicle_papyrus_lifecycle_handle* lifecycle,
                                                 const char* quest, int32_t alias_id, uint64_t* out_instance) {
  radicle_c_detail::clear_error();
  if (out_instance != nullptr) *out_instance = 0;
  if (lifecycle == nullptr || quest == nullptr || out_instance == nullptr) return 0;
  const auto instance = lifecycle->lifecycle.aliasInstance(OwnerId::forQuestAlias(quest, alias_id));
  if (!instance.has_value()) return 0;
  *out_instance = *instance;
  return 1;
}

int32_t radicle_papyrus_lifecycle_objective(const radicle_papyrus_lifecycle_handle* lifecycle, const char* quest,
                                            int32_t alias_id, int32_t index) {
  radicle_c_detail::clear_error();
  if (lifecycle == nullptr || quest == nullptr) return -1;
  return lifecycle->lifecycle.objectiveOf(OwnerId::forQuestAlias(quest, alias_id), index);
}

radicle_status radicle_papyrus_runtime_fan_out_event(radicle_papyrus_runtime* runtime,
                                                     radicle_papyrus_owner_kind owner_kind,
                                                     const char* base_form_key, const char* discriminator,
                                                     const char* event_name, const radicle_papyrus_value* const* args,
                                                     size_t arg_count, radicle_papyrus_fanout_report* out_report) {
  radicle_c_detail::clear_error();
  if (out_report != nullptr) *out_report = {};
  if (runtime == nullptr || event_name == nullptr || out_report == nullptr || (args == nullptr && arg_count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime, event_name, args and out_report are required");
  }
  const std::optional<OwnerId> owner = ownerFrom(owner_kind, base_form_key, discriminator);
  if (!owner.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the owner is not valid");
  return radicle_c_env_detail::guarded("unknown exception while fanning out an event", [&] {
    std::vector<Value> values;
    if (!collectArgs(args, arg_count, values)) {
      return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "argument handles must not be null");
    }
    return fillFanOut(runtime->runtime.fanOutEvent(*owner, event_name, values), out_report);
  });
}

radicle_status radicle_papyrus_runtime_broadcast_event(radicle_papyrus_runtime* runtime, const char* event_name,
                                                       const radicle_papyrus_value* const* args, size_t arg_count,
                                                       radicle_papyrus_fanout_report* out_report) {
  radicle_c_detail::clear_error();
  if (out_report != nullptr) *out_report = {};
  if (runtime == nullptr || event_name == nullptr || out_report == nullptr || (args == nullptr && arg_count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime, event_name, args and out_report are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while broadcasting an event", [&] {
    std::vector<Value> values;
    if (!collectArgs(args, arg_count, values)) {
      return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "argument handles must not be null");
    }
    return fillFanOut(runtime->runtime.broadcastEvent(event_name, values), out_report);
  });
}

void radicle_papyrus_fanout_report_destroy(radicle_papyrus_fanout_report* report) {
  radicle_c_detail::clear_error();
  if (report == nullptr) return;
  for (size_t i = 0; i < report->count; ++i) {
    std::free(report->outcomes[i].owner_key);
    std::free(report->outcomes[i].script);
    std::free(report->outcomes[i].reason);
  }
  delete[] report->outcomes;
  std::free(report->handler);
  std::free(report->report);
  std::free(report->digest);
  *report = radicle_papyrus_fanout_report{};
}

radicle_status radicle_papyrus_runtime_park_owner(radicle_papyrus_runtime* runtime,
                                                  radicle_papyrus_owner_kind owner_kind, const char* base_form_key,
                                                  const char* discriminator, int32_t parked) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime is required");
  const std::optional<OwnerId> owner = ownerFrom(owner_kind, base_form_key, discriminator);
  if (!owner.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the owner is not valid");
  return radicle_c_env_detail::guarded("unknown exception while parking an owner", [&] {
    if (parked != 0) {
      runtime->runtime.parkOwner(*owner);
    } else {
      runtime->runtime.unparkOwner(*owner);
    }
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
