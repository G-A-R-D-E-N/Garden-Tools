#pragma once

#include "fo4/papyrus/owner.hpp"
#include "fo4/papyrus/value.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

// Invariant: the startup flag is supplied by the caller; Radicle does not guess which QUST stage flag marks it.
struct QuestStageInput {
    std::int32_t index{0};
    bool startupStage{false};
    std::int32_t fragments{0};
};

struct QuestStages {
    std::string quest;
    std::vector<QuestStageInput> stages;
};

enum class StageState : std::uint8_t {
    Identified = 0,
    Unidentified = 1,
    Ambiguous = 2,
    BlockedByFragments = 3,
};

struct StageOutcome {
    std::string quest;
    StageState state{StageState::Unidentified};
    std::optional<std::int32_t> stage;
    std::string reason;
};

struct StageReport {
    std::vector<StageOutcome> quests;
    std::int32_t identified{0};
    std::int32_t refused{0};
    std::int32_t unresolved{0};

    std::string toReport() const;
    std::string digest() const;
};

// Invariant: exactly one marked stage starts the quest; none or several stay unresolved and are never guessed.
StageReport identifyStartupStages(const std::vector<QuestStages>& quests,
                                  const std::function<bool(std::string_view)>& gate);

struct VmadValueShape {
    std::string name;
    std::string type;
    bool hasAlias{false};
    std::optional<std::int32_t> elementCount;
    std::optional<std::string> text;
    std::optional<double> real;
    std::optional<std::int64_t> integer;
    std::optional<bool> boolean;
    std::optional<std::string> object;
    std::string elementType;
    std::vector<VmadValueShape> elements;
    std::vector<VmadValueShape> members;
};

enum class BoundKind : std::uint8_t {
    Int = 0,
    Float = 1,
    Bool = 2,
    String = 3,
    Object = 4,
    AliasBound = 5,
    Array = 6,
    Struct = 7,
    Variable = 8,
    Unknown = 9,
    Unsupported = 10,
};

struct BoundProperty {
    OwnerId owner;
    std::string name;
    BoundKind kind{BoundKind::Unsupported};
    Value value;
    std::optional<std::string> reason;

    bool isBound() const;
};

struct VmadBindReport {
    std::vector<BoundProperty> properties;
    std::int32_t bound{0};
    std::int32_t unsupported{0};
    std::int32_t silentlyDefaulted{0};
    std::vector<std::string> unsupportedShapes;

    bool clean() const { return silentlyDefaulted == 0; }
    std::string toReport() const;
    std::string digest() const;
};

// Invariant: an unreproducible shape is reported unsupported with a reason and never defaulted.
VmadBindReport bindVmadValues(const OwnerId& owner, const std::vector<VmadValueShape>& shapes);

enum class AliasFillKind : std::uint8_t {
    ForcedReference = 0,
    CreateReference = 1,
    Location = 2,
    FindMatching = 3,
    UniqueActor = 4,
};

struct QuestAliasInput {
    std::string quest;
    std::int32_t aliasId{0};
    AliasFillKind kind{AliasFillKind::ForcedReference};
    std::optional<std::string> target;
    std::optional<std::string> location;
};

enum class AliasState : std::uint8_t {
    Filled = 0,
    Open = 1,
};

struct AliasOutcome {
    OwnerId owner;
    AliasFillKind kind{AliasFillKind::ForcedReference};
    AliasState state{AliasState::Open};
    std::optional<std::string> value;
    std::string reason;
};

struct AliasFillReport {
    std::vector<AliasOutcome> aliases;
    std::int32_t filled{0};
    std::int32_t open{0};
    std::vector<std::string> openKinds;

    std::string toReport() const;
    std::string digest() const;
};

// Invariant: an unproven fill leaves the alias empty and counted; actor kinds are never filled here.
AliasFillReport fillQuestAliases(const std::vector<QuestAliasInput>& aliases,
                                 const std::function<bool(std::string_view)>& gate,
                                 OwnerRegistry& references);

std::string aliasFillKey(AliasFillKind kind);

}  // namespace fo4::papyrus
