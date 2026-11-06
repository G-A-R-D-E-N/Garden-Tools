#include "fo4/papyrus/runtime.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

constexpr const char* kEventKeyPrefix = "event.";

void tally(FanOutReport& report, FanOutOutcome outcome) {
    switch (outcome.result) {
        case FanOutResult::Delivered:
            report.delivered += 1;
            break;
        case FanOutResult::Refused:
            report.refused += 1;
            break;
        case FanOutResult::SkippedParked:
            report.skippedParked += 1;
            break;
        case FanOutResult::NoHandler:
            report.noHandler += 1;
            break;
        case FanOutResult::UnknownOwner:
            report.unknownOwner += 1;
            break;
    }
    report.outcomes.push_back(std::move(outcome));
}

}  // namespace

std::string eventHandlerName(const std::string& eventName) {
    const std::size_t dot = eventName.find('.');
    if (dot == std::string::npos) return eventName;
    return "::remote_" + eventName.substr(0, dot) + "_" + eventName.substr(dot + 1);
}

const char* fanOutResultName(FanOutResult result) {
    switch (result) {
        case FanOutResult::Delivered:
            return "delivered";
        case FanOutResult::Refused:
            return "refused";
        case FanOutResult::SkippedParked:
            return "skippedparked";
        case FanOutResult::NoHandler:
            return "nohandler";
        case FanOutResult::UnknownOwner:
            return "unknownowner";
    }
    return "refused";
}

std::string FanOutReport::toReport() const {
    std::string text = "event=" + eventName + " delivered=" + std::to_string(delivered) +
                       " refused=" + std::to_string(refused) +
                       " skipped-parked=" + std::to_string(skippedParked) +
                       " no-handler=" + std::to_string(noHandler) +
                       " unknown-owner=" + std::to_string(unknownOwner) + "\n";
    for (const FanOutOutcome& outcome : outcomes) {
        text += "  " + outcome.owner.key() + " instance=" + std::to_string(outcome.instance) + " script=" +
                outcome.script + " " + fanOutResultName(outcome.result) + " " + outcome.reason + "\n";
    }
    return text;
}

std::string FanOutReport::digest() const { return sha256Hex(toReport()); }

std::vector<InstanceId> Runtime::instancesForOwner(const OwnerId& owner) const {
    std::vector<InstanceId> found;
    for (const auto& [id, instance] : instances_) {
        if (instance.owner == owner) found.push_back(id);
    }
    return found;
}

bool Runtime::hasScript(const std::string& name) const { return findObject(name) != nullptr; }

bool Runtime::hasInvalidParentChain(const std::string& name) const {
    std::set<std::string> seen;
    const pex::PexObject* current = findObject(name);
    while (current != nullptr) {
        if (!seen.insert(lowerAscii(current->name)).second) return true;
        if (current->parentClassName.empty()) return false;
        current = findObject(current->parentClassName);
    }
    return true;
}

bool Runtime::instanceDeclares(InstanceId instance, const std::string& function) const {
    const Instance* found = findInstance(instance);
    if (found == nullptr) return false;
    const pex::PexObject* object = findObject(found->script);
    if (object == nullptr) return false;
    for (const pex::PexState& state : object->states) {
        if (!state.name.empty() && !sameName(state.name, found->state)) continue;
        for (const pex::PexFunction& candidate : state.functions) {
            if (sameName(candidate.name, function)) return true;
        }
    }
    return false;
}

bool Runtime::instanceDetached(InstanceId instance) const {
    const Instance* found = findInstance(instance);
    return found == nullptr || found->detached;
}

void Runtime::parkOwner(const OwnerId& owner) { parked_.insert(owner); }

void Runtime::unparkOwner(const OwnerId& owner) { parked_.erase(owner); }

bool Runtime::ownerParked(const OwnerId& owner) const { return parked_.contains(owner); }

void Runtime::fanOutOwner(FanOutReport& report, const OwnerId& owner, const std::vector<Value>& args) {
    const bool allowed = gate(kEventKeyPrefix + report.eventName);
    const std::vector<InstanceId> instances = instancesForOwner(owner);
    if (instances.empty()) {
        FanOutOutcome outcome;
        outcome.owner = owner;
        outcome.result = allowed ? FanOutResult::UnknownOwner : FanOutResult::Refused;
        outcome.reason = allowed ? "no instance is registered for this owner"
                                 : "the event is not allowed by the evidence gate";
        tally(report, std::move(outcome));
        return;
    }
    for (const InstanceId id : instances) {
        FanOutOutcome outcome;
        outcome.owner = owner;
        outcome.instance = id;
        outcome.script = instanceScript(id);
        if (!allowed) {
            outcome.result = FanOutResult::Refused;
            outcome.reason = "the event is not allowed by the evidence gate";
        } else if (ownerParked(owner) || instanceDetached(id)) {
            outcome.result = FanOutResult::SkippedParked;
            outcome.reason = "the owner is parked or detached, so its scripts are not attached";
        } else if (!instanceDeclares(id, report.handler)) {
            outcome.result = FanOutResult::NoHandler;
            outcome.reason = "the script " + outcome.script + " does not declare " + report.handler;
        } else {
            outcome.status = sendEvent(id, report.handler, args);
            if (outcome.status == Status::Ok) {
                outcome.result = FanOutResult::Delivered;
            } else {
                outcome.result = FanOutResult::Refused;
                outcome.reason = std::string("the instance refused the event: ") + statusName(outcome.status);
            }
        }
        tally(report, std::move(outcome));
    }
}

FanOutReport Runtime::fanOutEvent(const OwnerId& owner, const std::string& eventName,
                                  const std::vector<Value>& args) {
    FanOutReport report;
    report.eventName = eventName;
    report.handler = eventHandlerName(eventName);
    fanOutOwner(report, owner, args);
    return report;
}

FanOutReport Runtime::broadcastEvent(const std::string& eventName, const std::vector<Value>& args) {
    FanOutReport report;
    report.eventName = eventName;
    report.handler = eventHandlerName(eventName);
    std::map<std::string, OwnerId> owners;
    for (const auto& [id, instance] : instances_) owners.emplace(instance.owner.key(), instance.owner);
    for (const auto& [key, owner] : owners) fanOutOwner(report, owner, args);
    return report;
}

}  // namespace fo4::papyrus
