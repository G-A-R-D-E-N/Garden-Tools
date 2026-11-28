#pragma once

#include "fo4/papyrus/owner.hpp"
#include "fo4/papyrus/quest_rules.hpp"
#include "fo4/papyrus/runtime.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

struct QuestStartupProperty {
    std::string script;
    VmadValueShape shape;
};

struct QuestStartupInput {
    std::string quest;
    bool startGameEnabled{true};
    std::vector<std::string> scripts;
    std::int32_t fragments{0};
    std::int32_t aliases{0};
    std::vector<std::string> unsupported;
    std::vector<QuestStartupProperty> properties;
};

enum class QuestStartupState : std::uint8_t {
    Ready = 0,
    Blocked = 1,
    Skipped = 2,
};

struct QuestStartupOutcome {
    std::string quest;
    QuestStartupState state{QuestStartupState::Blocked};
    std::string reason;
};

// Invariant: a plan is all-or-nothing; one blocked quest keeps the whole batch from attaching anything.
struct QuestStartupPlan {
    std::vector<QuestStartupOutcome> outcomes;
    std::int32_t ready{0};
    std::int32_t blocked{0};
    std::int32_t skipped{0};

    bool canApply() const { return blocked == 0; }
    std::string toReport() const;
    std::string digest() const;
};

struct QuestStartupResult {
    bool applied{false};
    std::int32_t started{0};
    std::int32_t attachedScripts{0};
    std::vector<std::string> reasons;
};

struct AliasScriptInput {
    std::string quest;
    std::int32_t aliasId{0};
    std::optional<std::string> script;
    std::vector<VmadValueShape> properties;
    std::vector<std::int32_t> objectives;
};

enum class AliasAttachState : std::uint8_t {
    Attached = 0,
    Skipped = 1,
    Refused = 2,
};

struct AliasAttachOutcome {
    OwnerId owner;
    AliasAttachState state{AliasAttachState::Skipped};
    std::string script;
    std::string reason;
    std::int32_t properties{0};
    std::int32_t natives{0};
    std::int32_t objectives{0};
};

struct AliasAttachReport {
    std::vector<AliasAttachOutcome> aliases;
    std::int32_t attached{0};
    std::int32_t skipped{0};
    std::int32_t refused{0};
    std::int32_t properties{0};
    std::int32_t natives{0};
    std::int32_t objectives{0};

    std::string toReport() const;
    std::string digest() const;
};

struct ActorAliasInput {
    AliasScriptInput alias;
    bool bound{false};
    std::optional<std::string> target;
};

enum class ActorBindState : std::uint8_t {
    Unbound = 0,
    Bound = 1,
    Attached = 2,
    Refused = 3,
};

struct ActorBindOutcome {
    OwnerId owner;
    ActorBindState state{ActorBindState::Unbound};
    std::string target;
    std::string reason;
};

struct ActorBindReport {
    std::vector<ActorBindOutcome> aliases;
    std::int32_t bound{0};
    std::int32_t unbound{0};
    std::int32_t attached{0};
    std::int32_t refused{0};
    std::vector<std::string> unresolved;

    std::string toReport() const;
    std::string digest() const;
};

struct QuestLifecycleCounters {
    std::int32_t started{0};
    std::int32_t blocked{0};
    std::int32_t skipped{0};
    std::int32_t attachedScripts{0};
    std::int32_t approximations{0};
};

// Invariant: the lifecycle borrows the runtime; a null gate allows every key and the runtime must outlive it.
class QuestLifecycle {
public:
    QuestLifecycle(Runtime& runtime, std::function<bool(std::string_view)> gate);

    QuestStartupPlan preflight(const std::vector<QuestStartupInput>& inputs) const;
    QuestStartupResult start(const std::vector<QuestStartupInput>& inputs);
    bool stop(const std::string& quest);
    bool isStarted(const std::string& quest) const { return started_.contains(quest); }
    std::vector<InstanceId> instancesFor(const std::string& quest) const;

    AliasAttachReport attachAliases(const std::vector<AliasScriptInput>& aliases);
    ActorBindReport bindActors(const std::vector<ActorAliasInput>& aliases);
    bool clearAlias(const OwnerId& alias);
    std::int32_t clearQuestAliases(const std::string& quest);
    std::optional<InstanceId> aliasInstance(const OwnerId& alias) const;
    std::optional<std::string> aliasActor(const OwnerId& alias) const;
    std::int32_t objectiveOf(const OwnerId& alias, std::int32_t index) const;

    const QuestLifecycleCounters& counters() const { return counters_; }

private:
    struct AliasAttempt {
        AliasAttachState state{AliasAttachState::Skipped};
        std::string reason;
        std::int32_t properties{0};
        std::int32_t natives{0};
        std::int32_t objectives{0};
    };

    bool allows(std::string_view key) const { return !gate_ || gate_(key); }
    AliasAttempt attachAliasScript(const AliasScriptInput& alias, const std::string& propertyPrefix,
                                   const std::string& initPrefix);
    void releaseAlias(const OwnerId& alias);

    Runtime& runtime_;
    std::function<bool(std::string_view)> gate_;
    std::set<std::string> started_;
    std::map<std::string, std::string> failed_;
    std::map<std::string, std::vector<InstanceId>> questInstances_;
    std::map<OwnerId, InstanceId> aliasInstances_;
    std::map<OwnerId, std::string> aliasActors_;
    std::map<OwnerId, std::map<std::int32_t, std::int32_t>> objectives_;
    QuestLifecycleCounters counters_;
};

}  // namespace fo4::papyrus
