#pragma once

#include "fo4/papyrus/event_fanout.hpp"
#include "fo4/papyrus/host.hpp"
#include "fo4/papyrus/interpreter.hpp"
#include "fo4/papyrus/owner.hpp"
#include "fo4/papyrus/registration.hpp"
#include "fo4/papyrus/save_state.hpp"
#include "fo4/pex/pex.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {

enum class Lifecycle : std::uint8_t {
    NotInitialized = 0,
    Initializing = 1,
    Ready = 2,
    Failed = 3,
};

enum class OnInitArrivalPolicy : std::uint8_t {
    Refuse = 0,
    QueueAsApproximation = 1,
};

enum class NativeTargetKind : std::uint8_t {
    Static = 0,
    ScriptInstance = 1,
    EngineObject = 2,
};

enum class OverrideOutcome : std::uint8_t {
    NotRegistered = 0,
    Declined = 1,
    Handled = 2,
    Refused = 3,
};

struct OverrideResult {
    OverrideOutcome outcome{OverrideOutcome::NotRegistered};
    Value value;
    std::string reason;

    static OverrideResult notRegistered();
    static OverrideResult declined(std::string reason = {});
    static OverrideResult handled(Value value);
    static OverrideResult refused(std::string reason);
};

// Invariant: an override is consulted only at a bytecode entry point of an attached instance.
using OverrideHandler =
    std::function<OverrideResult(const Value& self, const std::string& function, const std::vector<Value>& args)>;

struct NativeRegistration {
    NativeTargetKind kind{NativeTargetKind::Static};
    std::string requiredScript;
    std::function<bool(const Value& target, const std::vector<Value>& args, Value& out, Status& status)> body;
    // Invariant: a latent registration returns a token above zero from begin; completeLatent resumes it.
    bool latent{false};
    std::function<std::uint64_t(const std::vector<Value>& args, Status& status)> begin;
    std::function<bool(const NativeCall& call, NativeResult& result)> invoke;
};

inline constexpr std::uint64_t kEngineHandleBit = std::uint64_t{1} << 63;

struct EngineObjectHooks {
    std::function<bool(const ObjectRef& object, const std::string& name, Value& out, Status& status)> get;
    std::function<bool(const ObjectRef& object, const std::string& name, const Value& value, Status& status)> set;
};

using InstanceId = std::uint64_t;

enum class AttachRefusal : std::uint8_t {
    None = 0,
    UnknownScript = 1,
    EmptyPropertyName = 2,
    DuplicatePropertyName = 3,
};

struct AttachResult {
    std::optional<InstanceId> instance;
    AttachRefusal refusal{AttachRefusal::None};
    std::string reason;
};

enum class CallState : std::uint8_t {
    Pending = 0,
    Waiting = 1,
    Done = 2,
    Failed = 3,
    Dropped = 4,
};

struct CallRecord {
    CallState state{CallState::Pending};
    Status status{Status::Ok};
    Value value;
    std::size_t instructions{0};
};

struct SchedulerStepResult {
    std::int32_t spent{0};
    std::int32_t completed{0};
    std::int32_t parked{0};
    std::int32_t paused{0};
    std::int32_t refused{0};
    std::int32_t dropped{0};
    std::int32_t waiting{0};
};

// Invariant: one Runtime owns the script set, instances, scheduler and registries; an instance runs OnInit once and fails closed.
class Runtime final : private Host {
public:
    explicit Runtime(std::vector<const pex::PexObject*> scripts);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    ~Runtime() override = default;

    void setGate(std::function<bool(std::string_view)> gate);
    void setOnInitArrivalPolicy(OnInitArrivalPolicy policy);
    void setEngineObjectHooks(EngineObjectHooks hooks);
    void setMaxParksPerEvent(std::int32_t parks) { maxParksPerEvent_ = parks; }
    void addScript(const pex::PexObject* object);

    std::optional<InstanceId> attach(const std::string& scriptName, const OwnerId& owner,
                                     std::map<std::string, Value> properties = {});
    // Invariant: all or nothing; a refusal leaves no instance behind and does not consume an instance id.
    AttachResult attachWithProperties(const std::string& scriptName, const OwnerId& owner,
                                      const std::vector<std::pair<std::string, Value>>& properties);
    std::optional<Value> instanceProperty(InstanceId instance, const std::string& name) const;
    bool detach(InstanceId instance);
    std::optional<InstanceId> instanceForOwner(const OwnerId& owner) const;
    std::vector<InstanceId> instancesForOwner(const OwnerId& owner) const;
    bool hasScript(const std::string& name) const;
    bool hasInvalidParentChain(const std::string& name) const;
    bool instanceDeclares(InstanceId instance, const std::string& function) const;
    bool instanceDetached(InstanceId instance) const;
    void parkOwner(const OwnerId& owner);
    void unparkOwner(const OwnerId& owner);
    bool ownerParked(const OwnerId& owner) const;
    FanOutReport fanOutEvent(const OwnerId& owner, const std::string& eventName,
                             const std::vector<Value>& args);
    FanOutReport broadcastEvent(const std::string& eventName, const std::vector<Value>& args);

    Lifecycle lifecycle(InstanceId instance) const;
    const std::string& failure(InstanceId instance) const;
    const std::string& instanceScript(InstanceId instance) const;
    std::optional<Value> instanceVariable(InstanceId instance, const std::string& name) const;
    bool setInstanceVariable(InstanceId instance, const std::string& name, const Value& value);
    const std::string& instanceState(InstanceId instance) const;
    std::optional<OwnerId> instanceOwner(InstanceId instance) const;

    Status initialize(InstanceId instance);
    Status sendEvent(InstanceId instance, const std::string& function, std::vector<Value> args = {});
    std::uint64_t submitCall(InstanceId instance, const std::string& function, std::vector<Value> args, Status& status,
                             bool track = true);
    bool takeResult(std::uint64_t ticket, bool consume, CallRecord& out);
    std::vector<InstanceId> instanceIds() const;
    SchedulerStepResult runStep(std::int32_t budget);
    bool completeLatent(std::uint64_t token, const Value& value);
    std::uint64_t beginWait();

    void registerNative(std::string script, std::string function, NativeRegistration registration);
    bool tryRegisterNative(std::string script, std::string function, NativeRegistration registration);
    void registerOverride(std::string script, std::string function, std::int32_t parameterCount,
                          OverrideHandler handler);

    std::vector<SavedThread> captureThreads() const;

    RegistrationRegistry& registrations() { return registrations_; }
    const RegistrationRegistry& registrations() const { return registrations_; }
    OwnerRegistry& owners() { return owners_; }

    std::size_t readyCount() const;
    std::size_t waitingCount() const;
    std::int32_t completedEvents() const { return completedEvents_; }
    std::int32_t parkedEvents() const { return parkedEvents_; }
    std::int32_t pausedEvents() const { return pausedEvents_; }
    std::int32_t refusedEvents() const { return refusedEvents_; }
    std::int32_t droppedEvents() const { return droppedEvents_; }
    std::int32_t waitingEvents() const { return waitingEvents_; }
    std::int32_t refusedCompletions() const { return refusedCompletions_; }
    std::int32_t queuedDuringOnInit() const { return queuedDuringOnInit_; }
    std::int32_t nativeTargetRefusals() const { return nativeTargetRefusals_; }
    std::int32_t overridesHandled() const { return overridesHandled_; }
    std::int32_t overridesDeclined() const { return overridesDeclined_; }
    std::int32_t overridesRefused() const { return overridesRefused_; }

private:
    struct QueuedCall {
        std::string function;
        std::vector<Value> arguments;
        std::uint64_t ticket{0};
    };

    struct Instance {
        InstanceId id{0};
        OwnerId owner;
        std::string script;
        std::string state;
        std::map<std::string, Value> properties;
        std::map<std::string, Value> variables;
        Lifecycle lifecycle{Lifecycle::NotInitialized};
        std::string failure;
        bool detached{false};
        std::vector<QueuedCall> queued;
    };

    struct Execution {
        InstanceId instance{0};
        std::string function;
        std::vector<Value> arguments;
        std::unique_ptr<Interpreter> interpreter;
        std::int32_t parks{0};
        std::size_t lastInstructions{0};
        std::optional<std::uint64_t> resumeToken;
        std::optional<Value> resumeValue;
        std::uint64_t ticket{0};
    };

    struct Waiting {
        Execution execution;
        std::optional<Value> completion;
    };

    const pex::PexObject* findObject(const std::string& name) const override;
    bool getProperty(const ObjectRef& object, const std::string& name, Value& out,
                     Status& status) override;
    bool setProperty(const ObjectRef& object, const std::string& name, const Value& value,
                     Status& status) override;
    bool getVariable(const ObjectRef& object, const std::string& name, Value& out,
                     Status& status) override;
    bool setVariable(const ObjectRef& object, const std::string& name, const Value& value,
                     Status& status) override;
    bool callNative(const std::string& script, const std::string& function,
                    const std::vector<Value>& args, Value& out, Status& status) override;
    bool callLatent(const std::string& script, const std::string& function,
                    const std::vector<Value>& args, Status& status, std::uint64_t& token) override;
    bool callNativeEx(const NativeCall& call, NativeResult& result) override;
    bool setState(ObjectRef& object, const std::string& state, Status& status) override;
    bool gate(std::string_view key) override;

    Instance* findInstance(InstanceId instance);
    const Instance* findInstance(InstanceId instance) const;
    Instance* instanceFor(const ObjectRef& object);
    bool nativeTargetAccepted(const NativeRegistration& registration, const Value& self) const;
    bool scriptInherits(const std::string& script, const std::string& ancestor) const;
    OverrideResult tryOverride(const Instance& instance, const std::string& function,
                               const std::vector<Value>& args);
    const NativeRegistration* findNative(const std::string& script, const std::string& function,
                                         std::string& ownerScript) const;
    void drainQueued(Instance& instance);
    void refuseQueued(Instance& instance);
    std::uint64_t openCall();
    void settleCall(std::uint64_t ticket, CallState state, Status status, Value value = Value{},
                    std::size_t instructions = 0);
    void trimResults();
    void fanOutOwner(FanOutReport& report, const OwnerId& owner, const std::vector<Value>& args);

    std::vector<const pex::PexObject*> scripts_;
    std::map<InstanceId, Instance> instances_;
    std::set<OwnerId> parked_;
    InstanceId nextInstance_{1};
    std::function<bool(std::string_view)> gate_;
    OnInitArrivalPolicy onInitPolicy_{OnInitArrivalPolicy::Refuse};
    std::int32_t maxParksPerEvent_{8};
    std::map<std::string, NativeRegistration> natives_;
    struct OverrideEntry {
        std::int32_t parameterCount{-1};
        OverrideHandler handler;
    };
    std::map<std::string, OverrideEntry> overrides_;
    std::vector<Execution> ready_;
    std::map<std::uint64_t, Waiting> waiting_;
    std::map<std::uint64_t, bool> usedTokens_;
    std::uint64_t nextToken_{1};
    std::set<std::uint64_t> issued_;
    std::map<std::uint64_t, Value> early_;
    std::map<std::uint64_t, CallRecord> results_;
    std::uint64_t nextTicket_{1};
    EngineObjectHooks engineHooks_;
    RegistrationRegistry registrations_;
    OwnerRegistry owners_;
    std::int32_t completedEvents_{0};
    std::int32_t parkedEvents_{0};
    std::int32_t pausedEvents_{0};
    std::int32_t refusedEvents_{0};
    std::int32_t droppedEvents_{0};
    std::int32_t waitingEvents_{0};
    std::int32_t refusedCompletions_{0};
    std::int32_t queuedDuringOnInit_{0};
    std::int32_t nativeTargetRefusals_{0};
    std::int32_t overridesHandled_{0};
    std::int32_t overridesDeclined_{0};
    std::int32_t overridesRefused_{0};
};

}  // namespace fo4::papyrus
