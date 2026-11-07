#include "fo4/papyrus/runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

constexpr int kMaxInheritanceHops = 64;

std::string fold(const std::string& text) { return lowerAscii(text); }

std::string nativeKey(const std::string& script, const std::string& function) {
    return fold(script) + "." + fold(function);
}

const pex::PexFunction* findInStates(const pex::PexObject& object, const std::string& state,
                                     const std::string& name) {
    if (!state.empty()) {
        for (const pex::PexState& candidate : object.states) {
            if (!sameName(candidate.name, state)) continue;
            for (const pex::PexFunction& function : candidate.functions) {
                if (sameName(function.name, name)) return &function;
            }
        }
    }
    for (const pex::PexState& candidate : object.states) {
        if (!candidate.name.empty()) continue;
        for (const pex::PexFunction& function : candidate.functions) {
            if (sameName(function.name, name)) return &function;
        }
    }
    return nullptr;
}

}  // namespace

OverrideResult OverrideResult::notRegistered() { return OverrideResult{}; }

OverrideResult OverrideResult::declined(std::string reason) {
    OverrideResult result;
    result.outcome = OverrideOutcome::Declined;
    result.reason = std::move(reason);
    return result;
}

OverrideResult OverrideResult::handled(Value value) {
    OverrideResult result;
    result.outcome = OverrideOutcome::Handled;
    result.value = std::move(value);
    return result;
}

OverrideResult OverrideResult::refused(std::string reason) {
    OverrideResult result;
    result.outcome = OverrideOutcome::Refused;
    result.reason = std::move(reason);
    return result;
}

Runtime::Runtime(std::vector<const pex::PexObject*> scripts) : scripts_(std::move(scripts)) {}

void Runtime::setGate(std::function<bool(std::string_view)> gate) { gate_ = std::move(gate); }

void Runtime::setOnInitArrivalPolicy(OnInitArrivalPolicy policy) { onInitPolicy_ = policy; }

void Runtime::addScript(const pex::PexObject* object) {
    if (object != nullptr) scripts_.push_back(object);
}

std::optional<InstanceId> Runtime::attach(const std::string& scriptName, const OwnerId& owner,
                                          std::map<std::string, Value> properties) {
    const pex::PexObject* object = findObject(scriptName);
    if (object == nullptr) return std::nullopt;
    Instance instance;
    instance.id = nextInstance_++;
    instance.owner = owner;
    instance.script = object->name;
    instance.state = object->autoStateName;
    for (auto& [name, value] : properties) instance.properties[fold(name)] = std::move(value);
    instances_.emplace(instance.id, std::move(instance));
    return nextInstance_ - 1;
}

AttachResult Runtime::attachWithProperties(const std::string& scriptName, const OwnerId& owner,
                                           const std::vector<std::pair<std::string, Value>>& properties) {
    AttachResult result;
    if (findObject(scriptName) == nullptr) {
        result.refusal = AttachRefusal::UnknownScript;
        result.reason = "no loaded pex defines the script " + scriptName;
        return result;
    }
    std::map<std::string, Value> folded;
    for (const auto& [name, value] : properties) {
        const bool blank = std::all_of(name.begin(), name.end(), [](char ch) {
            return ch == ' ' || (ch >= '\t' && ch <= '\r');
        });
        if (blank) {
            result.refusal = AttachRefusal::EmptyPropertyName;
            result.reason = "an empty property name on " + scriptName + " is unsupported";
            return result;
        }
        if (!folded.emplace(fold(name), value).second) {
            result.refusal = AttachRefusal::DuplicatePropertyName;
            result.reason = "the property " + name + " is given twice on " + scriptName;
            return result;
        }
    }
    result.instance = attach(scriptName, owner, std::move(folded));
    return result;
}

std::optional<Value> Runtime::instanceProperty(InstanceId instance, const std::string& name) const {
    const Instance* found = findInstance(instance);
    if (found == nullptr) return std::nullopt;
    const auto value = found->properties.find(fold(name));
    if (value == found->properties.end()) return std::nullopt;
    return value->second;
}

bool Runtime::detach(InstanceId instance) {
    Instance* found = findInstance(instance);
    if (found == nullptr) return false;
    found->detached = true;
    registrations_.unregisterAll(found->owner);
    return true;
}

std::optional<InstanceId> Runtime::instanceForOwner(const OwnerId& owner) const {
    for (const auto& [id, instance] : instances_) {
        if (instance.owner == owner) return id;
    }
    return std::nullopt;
}

Lifecycle Runtime::lifecycle(InstanceId instance) const {
    const Instance* found = findInstance(instance);
    return found == nullptr ? Lifecycle::Failed : found->lifecycle;
}

const std::string& Runtime::failure(InstanceId instance) const {
    static const std::string kMissing = "no such instance";
    const Instance* found = findInstance(instance);
    return found == nullptr ? kMissing : found->failure;
}

const std::string& Runtime::instanceScript(InstanceId instance) const {
    static const std::string kMissing;
    const Instance* found = findInstance(instance);
    return found == nullptr ? kMissing : found->script;
}

std::optional<Value> Runtime::instanceVariable(InstanceId instance, const std::string& name) const {
    const Instance* found = findInstance(instance);
    if (found == nullptr) return std::nullopt;
    const auto value = found->variables.find(fold(name));
    if (value == found->variables.end()) return std::nullopt;
    return value->second;
}

bool Runtime::setInstanceVariable(InstanceId instance, const std::string& name, const Value& value) {
    Instance* found = findInstance(instance);
    if (found == nullptr) return false;
    found->variables[fold(name)] = value;
    return true;
}

const std::string& Runtime::instanceState(InstanceId instance) const {
    static const std::string kMissing;
    const Instance* found = findInstance(instance);
    return found == nullptr ? kMissing : found->state;
}

std::optional<OwnerId> Runtime::instanceOwner(InstanceId instance) const {
    const Instance* found = findInstance(instance);
    if (found == nullptr) return std::nullopt;
    return found->owner;
}

Runtime::Instance* Runtime::findInstance(InstanceId instance) {
    const auto found = instances_.find(instance);
    return found == instances_.end() ? nullptr : &found->second;
}

const Runtime::Instance* Runtime::findInstance(InstanceId instance) const {
    const auto found = instances_.find(instance);
    return found == instances_.end() ? nullptr : &found->second;
}

Runtime::Instance* Runtime::instanceFor(const ObjectRef& object) {
    if ((object.handle & kEngineHandleBit) != 0) return nullptr;
    const auto found = instances_.find(object.handle);
    if (found == instances_.end() || !sameName(found->second.script, object.type)) return nullptr;
    return &found->second;
}

Status Runtime::initialize(InstanceId instance) {
    Instance* found = findInstance(instance);
    if (found == nullptr) return Status::HostError;
    if (!gate("vm.lifecycle.oninit_barrier")) return Status::GateRefused;
    if (found->lifecycle == Lifecycle::Ready) return Status::Ok;
    if (found->lifecycle == Lifecycle::Failed) return Status::HostError;
    if (found->lifecycle == Lifecycle::Initializing) return Status::HostError;
    if (found->detached) return Status::HostError;

    found->lifecycle = Lifecycle::Initializing;
    const OverrideResult overridden = tryOverride(*found, "OnInit", {});
    if (overridden.outcome == OverrideOutcome::Handled) {
        found->lifecycle = Lifecycle::Ready;
        drainQueued(*found);
        return Status::Ok;
    }
    if (overridden.outcome == OverrideOutcome::Refused) {
        found->lifecycle = Lifecycle::Failed;
        found->failure = overridden.reason;
        refusedEvents_ += static_cast<std::int32_t>(found->queued.size());
        found->queued.clear();
        return Status::HostError;
    }

    const pex::PexObject* object = findObject(found->script);
    const pex::PexFunction* onInit =
        object == nullptr ? nullptr : findInStates(*object, found->state, "OnInit");
    if (onInit == nullptr) {
        found->lifecycle = Lifecycle::Ready;
        drainQueued(*found);
        return Status::Ok;
    }
    Interpreter interpreter(*this);
    const Result result =
        interpreter.runFunction(objectValue(found->id, found->script, found->state), "OnInit");
    if (result.status != Status::Ok) {
        found->lifecycle = Lifecycle::Failed;
        found->failure = statusName(result.status);
        refusedEvents_ += static_cast<std::int32_t>(found->queued.size());
        found->queued.clear();
        return result.status;
    }
    found->lifecycle = Lifecycle::Ready;
    drainQueued(*found);
    return Status::Ok;
}

Status Runtime::sendEvent(InstanceId instance, const std::string& function, std::vector<Value> args) {
    Instance* found = findInstance(instance);
    if (found == nullptr || found->detached) {
        refusedEvents_ += 1;
        return Status::HostError;
    }
    if (found->lifecycle == Lifecycle::Failed) {
        refusedEvents_ += 1;
        return Status::HostError;
    }
    if (found->lifecycle == Lifecycle::NotInitialized) {
        refusedEvents_ += 1;
        return Status::HostError;
    }
    if (found->lifecycle == Lifecycle::Initializing) {
        if (gate("vm.lifecycle.event_during_oninit") &&
            onInitPolicy_ == OnInitArrivalPolicy::QueueAsApproximation) {
            found->queued.emplace_back(function, std::move(args));
            queuedDuringOnInit_ += 1;
            return Status::Ok;
        }
        refusedEvents_ += 1;
        return Status::GateRefused;
    }

    const pex::PexObject* object = findObject(found->script);
    const pex::PexFunction* declared =
        object == nullptr ? nullptr : findInStates(*object, found->state, function);
    const OverrideResult overridden = tryOverride(*found, function, args);
    if (overridden.outcome == OverrideOutcome::Handled) {
        completedEvents_ += 1;
        return Status::Ok;
    }
    if (overridden.outcome == OverrideOutcome::Refused) {
        refusedEvents_ += 1;
        return Status::HostError;
    }
    if (declared == nullptr) {
        refusedEvents_ += 1;
        return Status::FunctionNotFound;
    }
    Execution execution;
    execution.instance = instance;
    execution.function = function;
    execution.arguments = std::move(args);
    ready_.push_back(std::move(execution));
    return Status::Ok;
}

void Runtime::drainQueued(Instance& instance) {
    std::vector<std::pair<std::string, std::vector<Value>>> queued = std::move(instance.queued);
    instance.queued.clear();
    for (auto& [function, arguments] : queued) {
        Execution execution;
        execution.instance = instance.id;
        execution.function = std::move(function);
        execution.arguments = std::move(arguments);
        ready_.push_back(std::move(execution));
    }
}

void Runtime::registerNative(std::string script, std::string function, NativeRegistration registration) {
    natives_[nativeKey(script, function)] = std::move(registration);
}

bool Runtime::tryRegisterNative(std::string script, std::string function,
                                NativeRegistration registration) {
    return natives_.emplace(nativeKey(script, function), std::move(registration)).second;
}

void Runtime::registerOverride(std::string script, std::string function, std::int32_t parameterCount,
                               OverrideHandler handler) {
    OverrideEntry entry;
    entry.parameterCount = parameterCount;
    entry.handler = std::move(handler);
    overrides_[nativeKey(script, function)] = std::move(entry);
}

OverrideResult Runtime::tryOverride(const Instance& instance, const std::string& function,
                                    const std::vector<Value>& args) {
    const auto found = overrides_.find(nativeKey(instance.script, function));
    if (found == overrides_.end()) return OverrideResult::notRegistered();
    const pex::PexObject* object = findObject(instance.script);
    const pex::PexFunction* declared =
        object == nullptr ? nullptr : findInStates(*object, instance.state, function);
    if (declared == nullptr) {
        overridesRefused_ += 1;
        return OverrideResult::refused("the script does not define " + function);
    }
    if (declared->params.size() != args.size() ||
        (found->second.parameterCount != -1 &&
         static_cast<std::size_t>(found->second.parameterCount) != args.size())) {
        overridesRefused_ += 1;
        return OverrideResult::refused("the override argument count does not match " + function);
    }
    OverrideResult result = found->second.handler(objectValue(instance.id, instance.script, instance.state),
                                                  function, args);
    switch (result.outcome) {
        case OverrideOutcome::Handled:
            overridesHandled_ += 1;
            break;
        case OverrideOutcome::Declined:
            overridesDeclined_ += 1;
            break;
        case OverrideOutcome::Refused:
            overridesRefused_ += 1;
            break;
        case OverrideOutcome::NotRegistered:
            break;
    }
    return result;
}

const NativeRegistration* Runtime::findNative(const std::string& script, const std::string& function,
                                              std::string& ownerScript) const {
    std::string current = script;
    for (int hop = 0; hop < kMaxInheritanceHops; ++hop) {
        const auto found = natives_.find(nativeKey(current, function));
        if (found != natives_.end()) {
            ownerScript = current;
            return &found->second;
        }
        const pex::PexObject* object = findObject(current);
        if (object == nullptr || object->parentClassName.empty()) return nullptr;
        current = object->parentClassName;
    }
    return nullptr;
}

const pex::PexObject* Runtime::findObject(const std::string& name) const {
    for (const pex::PexObject* object : scripts_) {
        if (object != nullptr && sameName(object->name, name)) return object;
    }
    return nullptr;
}

bool Runtime::getVariable(const ObjectRef& object, const std::string& name, Value& out,
                          Status& status) {
    Instance* instance = instanceFor(object);
    if (instance == nullptr) {
        status = Status::UnknownIdentifier;
        return false;
    }
    const auto found = instance->variables.find(fold(name));
    if (found == instance->variables.end()) {
        status = Status::UnknownIdentifier;
        return false;
    }
    out = found->second;
    return true;
}

bool Runtime::setVariable(const ObjectRef& object, const std::string& name, const Value& value,
                          Status& status) {
    Instance* instance = instanceFor(object);
    if (instance == nullptr) {
        status = Status::UnknownIdentifier;
        return false;
    }
    instance->variables[fold(name)] = value;
    return true;
}

bool Runtime::getProperty(const ObjectRef& object, const std::string& name, Value& out,
                          Status& status) {
    if ((object.handle & kEngineHandleBit) != 0) {
        if (!engineHooks_.get) {
            status = Status::UnknownIdentifier;
            return false;
        }
        return engineHooks_.get(object, name, out, status);
    }
    Instance* instance = instanceFor(object);
    if (instance == nullptr) {
        status = Status::UnknownIdentifier;
        return false;
    }
    const auto found = instance->properties.find(fold(name));
    if (found == instance->properties.end()) {
        status = Status::UnknownIdentifier;
        return false;
    }
    out = found->second;
    return true;
}

bool Runtime::setProperty(const ObjectRef& object, const std::string& name, const Value& value,
                          Status& status) {
    if ((object.handle & kEngineHandleBit) != 0 && engineHooks_.set) {
        return engineHooks_.set(object, name, value, status);
    }
    nativeTargetRefusals_ += 1;
    status = Status::HostError;
    return false;
}

bool Runtime::callNative(const std::string& script, const std::string& function,
                         const std::vector<Value>& args, Value& out, Status& status) {
    const Value none = noneValue();
    NativeResult native;
    if (!callNativeEx(NativeCall{none, script, function, args}, native)) {
        status = native.status;
        return false;
    }
    if (native.latentToken != 0) {
        status = Status::HostError;
        return false;
    }
    out = std::move(native.value);
    return true;
}

bool Runtime::callLatent(const std::string& script, const std::string& function,
                         const std::vector<Value>& args, Status& status, std::uint64_t& token) {
    const Value none = noneValue();
    NativeResult native;
    if (!callNativeEx(NativeCall{none, script, function, args, true}, native)) {
        status = native.status;
        token = 0;
        return false;
    }
    status = native.status;
    token = native.latentToken;
    return token != 0;
}

bool Runtime::callNativeEx(const NativeCall& call, NativeResult& result) {
    std::string owner;
    const NativeRegistration* registration = findNative(call.script, call.function, owner);
    if (registration == nullptr) {
        result.status = Status::FunctionNotFound;
        return false;
    }
    if (call.mayPark && registration->latent && !registration->invoke && registration->begin) {
        Status beginStatus = Status::Ok;
        const std::uint64_t token = registration->begin(call.args, beginStatus);
        if (token != 0) {
            result = NativeResult{};
            result.latentToken = token;
            return true;
        }
    }
    const bool accepted = registration->invoke ? nativeTargetAccepted(*registration, call.self)
                                              : registration->kind == NativeTargetKind::Static;
    if (!accepted) {
        nativeTargetRefusals_ += 1;
        result.status = Status::HostError;
        return false;
    }
    if (registration->invoke) return registration->invoke(call, result);
    if (!registration->body) {
        result.status = Status::FunctionNotFound;
        return false;
    }
    const Value none = noneValue();
    Value out;
    Status status = Status::Ok;
    if (!registration->body(none, call.args, out, status)) {
        result.status = status;
        return false;
    }
    result = NativeResult{};
    result.value = std::move(out);
    return true;
}

bool Runtime::nativeTargetAccepted(const NativeRegistration& registration, const Value& self) const {
    if (registration.kind == NativeTargetKind::Static) return true;
    if (self.type != ValueType::Object || self.object == nullptr || self.object->handle == 0) return false;
    const std::uint64_t handle = self.object->handle;
    if (registration.kind == NativeTargetKind::EngineObject) {
        if ((handle & kEngineHandleBit) == 0) return false;
        return registration.requiredScript.empty() || scriptInherits(self.object->type, registration.requiredScript);
    }
    if ((handle & kEngineHandleBit) != 0) return false;
    const Instance* instance = findInstance(handle);
    if (instance == nullptr || instance->detached || !sameName(instance->script, self.object->type)) return false;
    return registration.requiredScript.empty() || scriptInherits(instance->script, registration.requiredScript);
}

bool Runtime::scriptInherits(const std::string& script, const std::string& ancestor) const {
    std::string current = script;
    for (int hop = 0; hop <= kMaxInheritanceHops && !current.empty(); ++hop) {
        if (sameName(current, ancestor)) return true;
        const pex::PexObject* object = findObject(current);
        if (object == nullptr) return false;
        current = object->parentClassName;
    }
    return false;
}

std::uint64_t Runtime::beginWait() {
    while (usedTokens_.count(nextToken_) != 0) nextToken_ += 1;
    const std::uint64_t token = nextToken_;
    nextToken_ += 1;
    issued_.insert(token);
    return token;
}

void Runtime::setEngineObjectHooks(EngineObjectHooks hooks) { engineHooks_ = std::move(hooks); }

bool Runtime::setState(ObjectRef& object, const std::string& state, Status& status) {
    object.state = state;
    Instance* instance = instanceFor(object);
    if (instance != nullptr) instance->state = state;
    status = Status::Ok;
    return true;
}

bool Runtime::gate(std::string_view key) { return !gate_ || gate_(key); }

std::size_t Runtime::readyCount() const { return ready_.size(); }

std::size_t Runtime::waitingCount() const { return waiting_.size(); }

}  // namespace fo4::papyrus
