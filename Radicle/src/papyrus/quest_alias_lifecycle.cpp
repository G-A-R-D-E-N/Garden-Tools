#include "fo4/papyrus/quest_lifecycle.hpp"

#include "quest_lifecycle_detail.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

const char* attachStateName(AliasAttachState state) {
    switch (state) {
        case AliasAttachState::Attached:
            return "attached";
        case AliasAttachState::Skipped:
            return "skipped";
        case AliasAttachState::Refused:
            return "refused";
    }
    return "refused";
}

const char* bindStateName(ActorBindState state) {
    switch (state) {
        case ActorBindState::Unbound:
            return "unbound";
        case ActorBindState::Bound:
            return "bound";
        case ActorBindState::Attached:
            return "attached";
        case ActorBindState::Refused:
            return "refused";
    }
    return "unbound";
}

OwnerId aliasOwner(const AliasScriptInput& alias) { return OwnerId::forQuestAlias(alias.quest, alias.aliasId); }

bool hasObjectShape(const std::vector<VmadValueShape>& shapes) {
    return std::any_of(shapes.begin(), shapes.end(),
                       [](const VmadValueShape& shape) { return lowerAscii(shape.type) == "object"; });
}

}  // namespace

std::string AliasAttachReport::toReport() const {
    std::string text = "aliases=" + std::to_string(aliases.size()) + " attached=" + std::to_string(attached) +
                       " skipped=" + std::to_string(skipped) + " refused=" + std::to_string(refused) +
                       " properties=" + std::to_string(properties) + " natives=" + std::to_string(natives) +
                       " objectives=" + std::to_string(objectives) + "\n";
    for (const AliasAttachOutcome& outcome : aliases) {
        text += "  " + outcome.owner.key() + " " + attachStateName(outcome.state) + " script=" +
                outcome.script + " " + outcome.reason + "\n";
    }
    return text;
}

std::string AliasAttachReport::digest() const { return sha256Hex(toReport()); }

std::string ActorBindReport::toReport() const {
    std::string text = "aliases=" + std::to_string(aliases.size()) + " bound=" + std::to_string(bound) +
                       " unbound=" + std::to_string(unbound) + " attached=" + std::to_string(attached) +
                       " refused=" + std::to_string(refused) + "\n";
    for (const ActorBindOutcome& outcome : aliases) {
        text += "  " + outcome.owner.key() + " " + bindStateName(outcome.state) + " target=" +
                outcome.target + " " + outcome.reason + "\n";
    }
    for (const std::string& key : unresolved) text += "  unresolved " + key + "\n";
    return text;
}

std::string ActorBindReport::digest() const { return sha256Hex(toReport()); }

QuestLifecycle::AliasAttempt QuestLifecycle::attachAliasScript(const AliasScriptInput& alias,
                                                              const std::string& propertyPrefix,
                                                              const std::string& initPrefix) {
    AliasAttempt attempt;
    const OwnerId owner = aliasOwner(alias);
    if (!alias.script.has_value() || alias.script->empty()) {
        attempt.reason = "the alias has no script, so nothing was attached";
        return attempt;
    }
    attempt.state = AliasAttachState::Refused;
    const auto refuse = [&](const std::string& reason) {
        attempt.reason = propertyPrefix + reason;
        return attempt;
    };
    if (!allows("quest.alias.runtime")) return refuse("unverified quest.alias.runtime");
    if (!alias.properties.empty() && !allows("vmad.value_binding")) {
        return refuse("unverified vmad.value_binding");
    }
    if (hasObjectShape(alias.properties) && !allows("owner.engine_identity")) {
        return refuse("unverified owner.engine_identity");
    }
    std::vector<QuestStartupProperty> properties;
    for (const VmadValueShape& shape : alias.properties) properties.push_back({*alias.script, shape});
    std::vector<lifecycle_detail::ScriptProperties> bound;
    std::string error;
    if (!lifecycle_detail::bindScriptProperties(owner, {*alias.script}, properties, bound, error)) {
        return refuse(error);
    }
    if (aliasInstances_.contains(owner)) return refuse("the alias already has an attached script");
    if (!runtime_.hasScript(*alias.script)) return refuse("missing script " + *alias.script);

    const std::optional<InstanceId> instance = runtime_.attach(*alias.script, owner, std::move(bound.front()));
    if (!instance.has_value()) return refuse("missing script " + *alias.script);
    const Status status = runtime_.initialize(*instance);
    if (status != Status::Ok) {
        const std::string message =
            runtime_.failure(*instance).empty() ? statusName(status) : runtime_.failure(*instance);
        runtime_.detach(*instance);
        attempt.reason = initPrefix + owner.key() + ": " + message;
        return attempt;
    }

    aliasInstances_[owner] = *instance;
    attempt.state = AliasAttachState::Attached;
    attempt.properties = static_cast<std::int32_t>(alias.properties.size());
    std::set<std::int32_t> indices(alias.objectives.begin(), alias.objectives.end());
    if (!indices.empty()) {
        NativeRegistration registration;
        registration.kind = NativeTargetKind::Static;
        registration.body = [](const Value&, const std::vector<Value>&, Value& out, Status&) {
            out = boolValue(true);
            return true;
        };
        runtime_.registerNative("Alias", "SetObjective" + owner.discriminator, std::move(registration));
        attempt.natives = 1;
        for (const std::int32_t index : indices) objectives_[owner][index] = 1;
        attempt.objectives = static_cast<std::int32_t>(indices.size());
    }
    return attempt;
}

AliasAttachReport QuestLifecycle::attachAliases(const std::vector<AliasScriptInput>& aliases) {
    std::vector<const AliasScriptInput*> ordered;
    for (const AliasScriptInput& alias : aliases) ordered.push_back(&alias);
    std::stable_sort(ordered.begin(), ordered.end(), [](const AliasScriptInput* a, const AliasScriptInput* b) {
        return aliasOwner(*a).key() < aliasOwner(*b).key();
    });
    AliasAttachReport report;
    for (const AliasScriptInput* alias : ordered) {
        const AliasAttempt attempt = attachAliasScript(*alias, std::string(), "initialization refused: ");
        AliasAttachOutcome outcome;
        outcome.owner = aliasOwner(*alias);
        outcome.state = attempt.state;
        outcome.script = alias->script.value_or(std::string());
        outcome.reason = attempt.reason;
        outcome.properties = attempt.properties;
        outcome.natives = attempt.natives;
        outcome.objectives = attempt.objectives;
        if (attempt.state == AliasAttachState::Skipped) report.skipped += 1;
        if (attempt.state == AliasAttachState::Refused) report.refused += 1;
        report.properties += attempt.properties;
        report.natives += attempt.natives;
        report.objectives += attempt.objectives;
        report.aliases.push_back(std::move(outcome));
    }
    report.attached = static_cast<std::int32_t>(report.aliases.size()) - report.skipped - report.refused;
    return report;
}

ActorBindReport QuestLifecycle::bindActors(const std::vector<ActorAliasInput>& aliases) {
    std::vector<const ActorAliasInput*> ordered;
    for (const ActorAliasInput& alias : aliases) ordered.push_back(&alias);
    std::stable_sort(ordered.begin(), ordered.end(), [](const ActorAliasInput* a, const ActorAliasInput* b) {
        return aliasOwner(a->alias).key() < aliasOwner(b->alias).key();
    });
    ActorBindReport report;
    for (const ActorAliasInput* input : ordered) {
        ActorBindOutcome outcome;
        outcome.owner = aliasOwner(input->alias);
        if (!input->bound || !input->target.has_value() || input->target->empty()) {
            outcome.reason = "the alias is not bound to an actor";
            report.unbound += 1;
            report.unresolved.push_back(outcome.owner.key());
        } else if (!allows("quest.alias.actor_binding")) {
            outcome.reason = "the actor binding is not proven, so the alias stays unbound";
            report.unbound += 1;
            report.unresolved.push_back(outcome.owner.key());
        } else {
            outcome.target = *input->target;
            aliasActors_[outcome.owner] = outcome.target;
            report.bound += 1;
            outcome.state = ActorBindState::Bound;
            if (!input->alias.script.has_value() || input->alias.script->empty()) {
                outcome.reason = "the alias is bound to its actor but declares no script";
            } else {
                const AliasAttempt attempt = attachAliasScript(
                    input->alias, "the alias is bound to its actor but script properties refused: ",
                    "the alias is bound to its actor but script initialization refused: ");
                if (attempt.state == AliasAttachState::Attached) {
                    outcome.state = ActorBindState::Attached;
                    outcome.reason = "the alias runs its script against its actor";
                    report.attached += 1;
                } else {
                    outcome.state = ActorBindState::Refused;
                    outcome.reason = attempt.reason;
                    report.refused += 1;
                }
            }
        }
        report.aliases.push_back(std::move(outcome));
    }
    return report;
}

void QuestLifecycle::releaseAlias(const OwnerId& alias) {
    const auto instance = aliasInstances_.find(alias);
    if (instance != aliasInstances_.end()) {
        runtime_.detach(instance->second);
        aliasInstances_.erase(instance);
    }
    objectives_.erase(alias);
    aliasActors_.erase(alias);
}

bool QuestLifecycle::clearAlias(const OwnerId& alias) {
    const bool known = aliasInstances_.contains(alias) || aliasActors_.contains(alias) || objectives_.contains(alias);
    releaseAlias(alias);
    return known;
}

std::int32_t QuestLifecycle::clearQuestAliases(const std::string& quest) {
    std::set<OwnerId> owners;
    const auto collect = [&](const OwnerId& owner) {
        if (owner.kind == OwnerKind::QuestAlias && owner.baseFormKey == quest) owners.insert(owner);
    };
    for (const auto& entry : aliasInstances_) collect(entry.first);
    for (const auto& entry : aliasActors_) collect(entry.first);
    for (const auto& entry : objectives_) collect(entry.first);
    for (const OwnerId& owner : owners) releaseAlias(owner);
    return static_cast<std::int32_t>(owners.size());
}

std::optional<InstanceId> QuestLifecycle::aliasInstance(const OwnerId& alias) const {
    const auto found = aliasInstances_.find(alias);
    if (found == aliasInstances_.end()) return std::nullopt;
    return found->second;
}

std::optional<std::string> QuestLifecycle::aliasActor(const OwnerId& alias) const {
    const auto found = aliasActors_.find(alias);
    if (found == aliasActors_.end()) return std::nullopt;
    return found->second;
}

std::int32_t QuestLifecycle::objectiveOf(const OwnerId& alias, std::int32_t index) const {
    const auto owner = objectives_.find(alias);
    if (owner == objectives_.end()) return -1;
    const auto found = owner->second.find(index);
    return found == owner->second.end() ? -1 : found->second;
}

}  // namespace fo4::papyrus
