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
namespace lifecycle_detail {

std::string joinReasons(const std::vector<std::string>& reasons, const char* separator) {
    std::string text;
    for (std::size_t i = 0; i < reasons.size(); ++i) {
        if (i != 0) text += separator;
        text += reasons[i];
    }
    return text;
}

bool bindScriptProperties(const OwnerId& owner, const std::vector<std::string>& scripts,
                          const std::vector<QuestStartupProperty>& properties,
                          std::vector<ScriptProperties>& out, std::string& error) {
    out.clear();
    std::set<std::string> folded;
    for (const std::string& script : scripts) {
        if (script.empty() || !folded.insert(lowerAscii(script)).second) {
            error = "duplicate or empty quest script attachments are unsupported";
            return false;
        }
    }
    std::vector<std::vector<VmadValueShape>> shapes(scripts.size());
    for (const QuestStartupProperty& property : properties) {
        const auto found = std::find_if(scripts.begin(), scripts.end(), [&](const std::string& script) {
            return sameName(script, property.script);
        });
        if (found == scripts.end()) {
            error = "quest properties name an unattached script " + property.script;
            return false;
        }
        shapes[static_cast<std::size_t>(found - scripts.begin())].push_back(property.shape);
    }
    std::vector<ScriptProperties> bound(scripts.size());
    for (std::size_t i = 0; i < scripts.size(); ++i) {
        std::set<std::string> names;
        for (const VmadValueShape& shape : shapes[i]) {
            if (shape.name.empty() || !names.insert(lowerAscii(shape.name)).second) {
                error = "duplicate or empty property names on " + scripts[i] + " are unsupported";
                return false;
            }
            if (lowerAscii(shape.type) == "string" && !shape.text.has_value()) {
                error = "a string property carried no value";
                return false;
            }
        }
        const VmadBindReport report = bindVmadValues(owner, shapes[i]);
        if (!report.unsupportedShapes.empty()) {
            error = joinReasons(report.unsupportedShapes, "; ");
            return false;
        }
        for (const BoundProperty& property : report.properties) {
            bound[i][lowerAscii(property.name)] = property.value;
        }
    }
    out = std::move(bound);
    return true;
}

}  // namespace lifecycle_detail

namespace {

using lifecycle_detail::bindScriptProperties;
using lifecycle_detail::joinReasons;
using lifecycle_detail::ScriptProperties;

const char* startupStateName(QuestStartupState state) {
    switch (state) {
        case QuestStartupState::Ready:
            return "ready";
        case QuestStartupState::Blocked:
            return "blocked";
        case QuestStartupState::Skipped:
            return "skipped";
    }
    return "blocked";
}

bool hasObjectProperty(const QuestStartupInput& input) {
    return std::any_of(input.properties.begin(), input.properties.end(),
                       [](const QuestStartupProperty& property) {
                           return lowerAscii(property.shape.type) == "object";
                       });
}

}  // namespace

std::string QuestStartupPlan::toReport() const {
    std::string text = "quests=" + std::to_string(outcomes.size()) + " ready=" + std::to_string(ready) +
                       " blocked=" + std::to_string(blocked) + " skipped=" + std::to_string(skipped) + "\n";
    for (const QuestStartupOutcome& outcome : outcomes) {
        text += "  " + outcome.quest + " " + startupStateName(outcome.state) + " " + outcome.reason + "\n";
    }
    return text;
}

std::string QuestStartupPlan::digest() const { return sha256Hex(toReport()); }

QuestLifecycle::QuestLifecycle(Runtime& runtime, std::function<bool(std::string_view)> gate)
    : runtime_(runtime), gate_(std::move(gate)) {}

QuestStartupPlan QuestLifecycle::preflight(const std::vector<QuestStartupInput>& inputs) const {
    std::vector<const QuestStartupInput*> ordered;
    for (const QuestStartupInput& input : inputs) ordered.push_back(&input);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const QuestStartupInput* a, const QuestStartupInput* b) { return a->quest < b->quest; });
    std::map<std::string, int> seen;
    for (const QuestStartupInput* input : ordered) seen[input->quest] += 1;

    QuestStartupPlan plan;
    for (const QuestStartupInput* input : ordered) {
        QuestStartupOutcome outcome;
        outcome.quest = input->quest;
        const auto refuse = [&](std::string reason) {
            outcome.state = QuestStartupState::Blocked;
            outcome.reason = std::move(reason);
        };
        if (input->quest.empty()) {
            refuse("a quest needs a key");
        } else if (seen[input->quest] > 1) {
            refuse("duplicate quest inputs are unsupported");
        } else if (failed_.contains(input->quest)) {
            refuse(failed_.at(input->quest));
        } else if (!input->unsupported.empty()) {
            refuse(joinReasons(input->unsupported, "; "));
        } else if (started_.contains(input->quest)) {
            outcome.state = QuestStartupState::Skipped;
            outcome.reason = "already started";
        } else if (!input->startGameEnabled) {
            outcome.state = QuestStartupState::Skipped;
            outcome.reason = "not start-game enabled";
        } else if (!input->properties.empty() && !allows("vmad.value_binding")) {
            refuse("quest property binding is not allowed by the evidence registry");
        } else if (hasObjectProperty(*input) && !allows("owner.engine_identity")) {
            refuse("engine identities in quest properties are unproven");
        } else if (!input->scripts.empty() && !allows("quest.runtime.startup_ordering")) {
            refuse("the startup ordering of quest-owned scripts is unproven");
        } else if (input->fragments > 0 && !allows("quest.runtime.fragment_dispatch_timing")) {
            refuse("the dispatch timing of stage fragments is unproven");
        } else if (input->aliases > 0 && !allows("quest.runtime.alias_fill_order")) {
            refuse("the fill order of quest aliases is unproven");
        } else {
            outcome.state = QuestStartupState::Ready;
            outcome.reason = "ready to start";
        }
        switch (outcome.state) {
            case QuestStartupState::Ready:
                plan.ready += 1;
                break;
            case QuestStartupState::Blocked:
                plan.blocked += 1;
                break;
            case QuestStartupState::Skipped:
                plan.skipped += 1;
                break;
        }
        plan.outcomes.push_back(std::move(outcome));
    }
    return plan;
}

QuestStartupResult QuestLifecycle::start(const std::vector<QuestStartupInput>& inputs) {
    const QuestStartupPlan plan = preflight(inputs);
    counters_.skipped += plan.skipped;
    QuestStartupResult result;

    std::map<std::string, const QuestStartupInput*> byQuest;
    for (const QuestStartupInput& input : inputs) byQuest.emplace(input.quest, &input);

    if (!plan.canApply()) {
        counters_.blocked += plan.blocked;
        for (const QuestStartupOutcome& outcome : plan.outcomes) {
            if (outcome.state != QuestStartupState::Blocked) continue;
            if (!failed_.contains(outcome.quest)) counters_.approximations += 1;
            result.reasons.push_back(outcome.quest + ": " + outcome.reason);
        }
        return result;
    }

    std::vector<const QuestStartupInput*> ready;
    for (const QuestStartupOutcome& outcome : plan.outcomes) {
        if (outcome.state == QuestStartupState::Ready) ready.push_back(byQuest.at(outcome.quest));
    }

    std::set<std::string> missing;
    std::vector<std::string> missingReasons;
    for (const QuestStartupInput* input : ready) {
        bool any = false;
        for (const std::string& script : input->scripts) {
            if (runtime_.hasScript(script)) continue;
            any = true;
            if (missing.insert(lowerAscii(script)).second) missingReasons.push_back("missing script " + script);
        }
        if (any) counters_.blocked += 1;
    }
    if (!missingReasons.empty()) {
        result.reasons = std::move(missingReasons);
        return result;
    }

    for (const QuestStartupInput* input : ready) {
        for (const std::string& script : input->scripts) {
            if (runtime_.hasInvalidParentChain(script)) {
                result.reasons.push_back(input->quest + ": missing or cyclic parent chain in a quest script");
                break;
            }
        }
    }
    if (!result.reasons.empty()) return result;

    std::map<std::string, std::vector<ScriptProperties>> bound;
    for (const QuestStartupInput* input : ready) {
        std::vector<ScriptProperties> properties;
        std::string error;
        if (!bindScriptProperties(OwnerId::forForm(input->quest), input->scripts, input->properties,
                                  properties, error)) {
            result.reasons.push_back(input->quest + ": " + error);
            continue;
        }
        bound.emplace(input->quest, std::move(properties));
    }
    if (!result.reasons.empty()) return result;

    for (const QuestStartupInput* input : ready) {
        if (input->scripts.empty() || allows("vm.lifecycle.oninit_barrier")) continue;
        result.reasons.push_back(input->quest +
                                 ": vm.lifecycle.oninit_barrier: the OnInit barrier is unproven");
    }
    if (!result.reasons.empty()) return result;

    for (const QuestStartupInput* input : ready) {
        const OwnerId owner = OwnerId::forForm(input->quest);
        std::vector<ScriptProperties>& properties = bound.at(input->quest);
        std::vector<InstanceId> attached;
        for (std::size_t i = 0; i < input->scripts.size(); ++i) {
            const std::optional<InstanceId> instance =
                runtime_.attach(input->scripts[i], owner, std::move(properties[i]));
            if (!instance.has_value()) {
                counters_.blocked += 1;
                failed_[input->quest] = "missing script " + input->scripts[i];
                for (const InstanceId id : attached) runtime_.detach(id);
                questInstances_.erase(input->quest);
                result.reasons.push_back(input->quest + " " + input->scripts[i] + ": missing script");
                return result;
            }
            attached.push_back(*instance);
            questInstances_[input->quest].push_back(*instance);
            counters_.attachedScripts += 1;
            result.attachedScripts += 1;
        }
        for (std::size_t i = 0; i < attached.size(); ++i) {
            const Status status = runtime_.initialize(attached[i]);
            if (status == Status::Ok) continue;
            const std::string message =
                runtime_.failure(attached[i]).empty() ? statusName(status) : runtime_.failure(attached[i]);
            failed_[input->quest] = message;
            for (const InstanceId id : attached) runtime_.detach(id);
            questInstances_.erase(input->quest);
            counters_.blocked += 1;
            result.reasons.push_back(input->quest + " " + input->scripts[i] + ": " + message);
            return result;
        }
        started_.insert(input->quest);
        counters_.started += 1;
        result.started += 1;
    }
    result.applied = true;
    return result;
}

bool QuestLifecycle::stop(const std::string& quest) {
    const bool wasStarted = started_.erase(quest) != 0;
    failed_.erase(quest);
    const auto found = questInstances_.find(quest);
    if (found != questInstances_.end()) {
        for (const InstanceId instance : found->second) runtime_.detach(instance);
        questInstances_.erase(found);
    }
    clearQuestAliases(quest);
    return wasStarted;
}

std::vector<InstanceId> QuestLifecycle::instancesFor(const std::string& quest) const {
    const auto found = questInstances_.find(quest);
    return found == questInstances_.end() ? std::vector<InstanceId>{} : found->second;
}

}  // namespace fo4::papyrus
