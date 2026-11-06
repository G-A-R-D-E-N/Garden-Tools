#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_papyrus_internal.hpp"
#include "fo4/c_api_papyrus_quest.h"
#include "fo4/papyrus/owner.hpp"
#include "fo4/papyrus/quest_rules.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

char* copyText(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

std::optional<fo4::papyrus::OwnerId> ownerFrom(radicle_papyrus_owner_kind kind, const char* base,
                                               const char* discriminator) {
  if (base == nullptr) return std::nullopt;
  const std::string baseKey(base);
  const std::string disc = discriminator == nullptr ? std::string() : std::string(discriminator);
  switch (kind) {
    case RADICLE_PAPYRUS_OWNER_FORM:
      return fo4::papyrus::OwnerId::forForm(baseKey);
    case RADICLE_PAPYRUS_OWNER_PLACED_REFERENCE:
      return fo4::papyrus::OwnerId::forPlacedReference(baseKey, disc);
    case RADICLE_PAPYRUS_OWNER_DYNAMIC_REFERENCE:
      return fo4::papyrus::OwnerId::forDynamicReference(baseKey, disc);
    case RADICLE_PAPYRUS_OWNER_QUEST_ALIAS:
      return fo4::papyrus::OwnerId::forQuestAlias(baseKey, static_cast<std::int32_t>(std::strtol(disc.c_str(), nullptr, 10)));
    case RADICLE_PAPYRUS_OWNER_RUNTIME_INSTANCE:
      return fo4::papyrus::OwnerId::forRuntimeInstance(baseKey, std::strtoll(disc.c_str(), nullptr, 10));
  }
  return std::nullopt;
}

constexpr int kMaxShapeDepth = 16;

bool shapeFrom(const radicle_papyrus_vmad_shape& in, int depth, fo4::papyrus::VmadValueShape& shape) {
  if (depth > kMaxShapeDepth) return false;
  if ((in.elements == nullptr && in.elements_len != 0) || (in.members == nullptr && in.members_len != 0)) {
    return false;
  }
  shape.name = in.name == nullptr ? std::string() : std::string(in.name);
  shape.type = in.type == nullptr ? std::string() : std::string(in.type);
  shape.hasAlias = in.has_alias != 0;
  if (in.has_element_count != 0) shape.elementCount = in.element_count;
  if (in.text != nullptr) shape.text = std::string(in.text);
  if (in.has_real != 0) shape.real = in.real;
  if (in.has_integer != 0) shape.integer = in.integer;
  if (in.has_boolean != 0) shape.boolean = in.boolean != 0;
  if (in.object != nullptr) shape.object = std::string(in.object);
  if (in.element_type != nullptr) shape.elementType = std::string(in.element_type);
  for (size_t i = 0; i < in.elements_len; ++i) {
    fo4::papyrus::VmadValueShape child;
    if (!shapeFrom(in.elements[i], depth + 1, child)) return false;
    shape.elements.push_back(std::move(child));
  }
  for (size_t i = 0; i < in.members_len; ++i) {
    fo4::papyrus::VmadValueShape child;
    if (!shapeFrom(in.members[i], depth + 1, child)) return false;
    shape.members.push_back(std::move(child));
  }
  return true;
}

}  // namespace

extern "C" {

radicle_status radicle_papyrus_identify_startup_stages(const radicle_papyrus_quest_stages* quests,
                                                       size_t count,
                                                       radicle_papyrus_stage_report* out_report) {
  radicle_c_detail::clear_error();
  if (out_report != nullptr) *out_report = {};
  if (out_report == nullptr || (quests == nullptr && count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_report and quests are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while identifying startup stages", [&] {
    std::vector<fo4::papyrus::QuestStages> input;
    input.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      fo4::papyrus::QuestStages quest;
      quest.quest = quests[i].quest == nullptr ? std::string() : std::string(quests[i].quest);
      for (size_t j = 0; j < quests[i].stage_count; ++j) {
        const radicle_papyrus_stage_input& stage = quests[i].stages[j];
        quest.stages.push_back(fo4::papyrus::QuestStageInput{stage.index, stage.startup_stage != 0, stage.fragments});
      }
      input.push_back(std::move(quest));
    }
    const fo4::papyrus::StageReport report = fo4::papyrus::identifyStartupStages(input, {});
    out_report->outcomes = new radicle_papyrus_stage_outcome[report.quests.size()];
    out_report->count = report.quests.size();
    for (size_t i = 0; i < report.quests.size(); ++i) {
      out_report->outcomes[i].quest = copyText(report.quests[i].quest);
      out_report->outcomes[i].state = static_cast<int32_t>(report.quests[i].state);
      out_report->outcomes[i].has_stage = report.quests[i].stage.has_value() ? 1 : 0;
      out_report->outcomes[i].stage = report.quests[i].stage.value_or(0);
      out_report->outcomes[i].reason = copyText(report.quests[i].reason);
    }
    out_report->identified = report.identified;
    out_report->refused = report.refused;
    out_report->unresolved = report.unresolved;
    out_report->report = copyText(report.toReport());
    out_report->digest = copyText(report.digest());
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_stage_report_destroy(radicle_papyrus_stage_report* report) {
  radicle_c_detail::clear_error();
  if (report == nullptr) return;
  for (size_t i = 0; i < report->count; ++i) {
    std::free(const_cast<char*>(report->outcomes[i].quest));
    std::free(const_cast<char*>(report->outcomes[i].reason));
  }
  delete[] report->outcomes;
  std::free(report->report);
  std::free(report->digest);
  *report = radicle_papyrus_stage_report{};
}

radicle_status radicle_papyrus_bind_vmad_values(radicle_papyrus_owner_kind owner_kind,
                                                const char* base_form_key, const char* discriminator,
                                                const radicle_papyrus_vmad_shape* shapes, size_t count,
                                                radicle_papyrus_bind_report* out_report) {
  radicle_c_detail::clear_error();
  if (out_report != nullptr) *out_report = {};
  if (out_report == nullptr || (shapes == nullptr && count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_report and shapes are required");
  }
  const std::optional<fo4::papyrus::OwnerId> owner = ownerFrom(owner_kind, base_form_key, discriminator);
  if (!owner.has_value()) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the owner is not valid");
  }
  return radicle_c_env_detail::guarded("unknown exception while binding VMAD values", [&] {
    std::vector<fo4::papyrus::VmadValueShape> input;
    input.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      fo4::papyrus::VmadValueShape shape;
      if (!shapeFrom(shapes[i], 0, shape)) {
        return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                      "a shape is nested too deeply or has a null child array");
      }
      input.push_back(std::move(shape));
    }
    const fo4::papyrus::VmadBindReport report = fo4::papyrus::bindVmadValues(*owner, input);
    out_report->properties = new radicle_papyrus_bound_property[report.properties.size()]();
    out_report->count = report.properties.size();
    for (size_t i = 0; i < report.properties.size(); ++i) {
      const fo4::papyrus::BoundProperty& property = report.properties[i];
      out_report->properties[i].owner_key = copyText(property.owner.key());
      out_report->properties[i].name = copyText(property.name);
      out_report->properties[i].kind = static_cast<int32_t>(property.kind);
      out_report->properties[i].reason = copyText(property.reason.value_or(std::string()));
      out_report->properties[i].value = nullptr;
      if (property.isBound()) {
        radicle_papyrus_value* value = new radicle_papyrus_value{property.value};
        out_report->properties[i].value = value;
      }
    }
    out_report->bound = report.bound;
    out_report->unsupported = report.unsupported;
    out_report->silently_defaulted = report.silentlyDefaulted;
    out_report->unsupported_shapes = new char*[report.unsupportedShapes.size()]();
    out_report->unsupported_shape_count = report.unsupportedShapes.size();
    for (size_t i = 0; i < report.unsupportedShapes.size(); ++i) {
      out_report->unsupported_shapes[i] = copyText(report.unsupportedShapes[i]);
    }
    out_report->report = copyText(report.toReport());
    out_report->digest = copyText(report.digest());
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_bind_report_destroy(radicle_papyrus_bind_report* report) {
  radicle_c_detail::clear_error();
  if (report == nullptr) return;
  for (size_t i = 0; i < report->count; ++i) {
    std::free(const_cast<char*>(report->properties[i].owner_key));
    std::free(const_cast<char*>(report->properties[i].name));
    std::free(const_cast<char*>(report->properties[i].reason));
    delete report->properties[i].value;
  }
  delete[] report->properties;
  for (size_t i = 0; i < report->unsupported_shape_count; ++i) std::free(report->unsupported_shapes[i]);
  delete[] report->unsupported_shapes;
  std::free(report->report);
  std::free(report->digest);
  *report = radicle_papyrus_bind_report{};
}

radicle_status radicle_papyrus_fill_quest_aliases(const radicle_papyrus_alias_input* aliases,
                                                  size_t count,
                                                  radicle_papyrus_alias_report* out_report) {
  radicle_c_detail::clear_error();
  if (out_report != nullptr) *out_report = {};
  if (out_report == nullptr || (aliases == nullptr && count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_report and aliases are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while filling quest aliases", [&] {
    std::vector<fo4::papyrus::QuestAliasInput> input;
    input.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      fo4::papyrus::QuestAliasInput alias;
      alias.quest = aliases[i].quest == nullptr ? std::string() : std::string(aliases[i].quest);
      alias.aliasId = aliases[i].alias_id;
      alias.kind = static_cast<fo4::papyrus::AliasFillKind>(aliases[i].kind);
      if (aliases[i].target != nullptr) alias.target = std::string(aliases[i].target);
      if (aliases[i].location != nullptr) alias.location = std::string(aliases[i].location);
      input.push_back(std::move(alias));
    }
    fo4::papyrus::OwnerRegistry references;
    const fo4::papyrus::AliasFillReport report = fo4::papyrus::fillQuestAliases(input, {}, references);
    out_report->outcomes = new radicle_papyrus_alias_outcome[report.aliases.size()];
    out_report->count = report.aliases.size();
    for (size_t i = 0; i < report.aliases.size(); ++i) {
      const fo4::papyrus::AliasOutcome& alias = report.aliases[i];
      out_report->outcomes[i].owner_key = copyText(alias.owner.key());
      out_report->outcomes[i].kind = static_cast<int32_t>(alias.kind);
      out_report->outcomes[i].state = static_cast<int32_t>(alias.state);
      out_report->outcomes[i].value = copyText(alias.value.value_or(std::string()));
      out_report->outcomes[i].reason = copyText(alias.reason);
    }
    out_report->filled = report.filled;
    out_report->open = report.open;
    out_report->report = copyText(report.toReport());
    out_report->digest = copyText(report.digest());
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_alias_report_destroy(radicle_papyrus_alias_report* report) {
  radicle_c_detail::clear_error();
  if (report == nullptr) return;
  for (size_t i = 0; i < report->count; ++i) {
    std::free(const_cast<char*>(report->outcomes[i].owner_key));
    std::free(const_cast<char*>(report->outcomes[i].value));
    std::free(const_cast<char*>(report->outcomes[i].reason));
  }
  delete[] report->outcomes;
  std::free(report->report);
  std::free(report->digest);
  *report = radicle_papyrus_alias_report{};
}

}  // extern "C"
