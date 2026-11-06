#include "fo4/papyrus/quest_rules.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

constexpr const char* kIdentificationKey = "quest.startup.stage_identification";
constexpr const char* kFragmentTimingKey = "quest.runtime.fragment_dispatch_timing";
constexpr const char* kAliasKeyPrefix = "quest.alias.fill.";
constexpr const char* kAliasActorKey = "quest.alias.fill.actors";

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

const char* stateName(StageState state) {
    switch (state) {
        case StageState::Identified:
            return "identified";
        case StageState::Unidentified:
            return "unidentified";
        case StageState::Ambiguous:
            return "ambiguous";
        case StageState::BlockedByFragments:
            return "blockedbyfragments";
    }
    return "unidentified";
}

const char* boundKindName(BoundKind kind) {
    switch (kind) {
        case BoundKind::Int:
            return "int";
        case BoundKind::Float:
            return "float";
        case BoundKind::Bool:
            return "bool";
        case BoundKind::String:
            return "string";
        case BoundKind::Object:
            return "object";
        case BoundKind::AliasBound:
            return "aliasbound";
        case BoundKind::Array:
            return "array";
        case BoundKind::Struct:
            return "struct";
        case BoundKind::Variable:
            return "variable";
        case BoundKind::Unknown:
            return "unknown";
        case BoundKind::Unsupported:
            return "unsupported";
    }
    return "unsupported";
}

const char* boundKindLabel(BoundKind kind) {
    switch (kind) {
        case BoundKind::Int:
            return "Int";
        case BoundKind::Float:
            return "Float";
        case BoundKind::Bool:
            return "Bool";
        case BoundKind::String:
            return "String";
        case BoundKind::Object:
            return "Object";
        case BoundKind::AliasBound:
            return "AliasBound";
        case BoundKind::Array:
            return "Array";
        case BoundKind::Struct:
            return "Struct";
        case BoundKind::Variable:
            return "Variable";
        case BoundKind::Unknown:
            return "Unknown";
        case BoundKind::Unsupported:
            return "Unsupported";
    }
    return "Unsupported";
}

const char* aliasKindName(AliasFillKind kind) {
    switch (kind) {
        case AliasFillKind::ForcedReference:
            return "forcedreference";
        case AliasFillKind::CreateReference:
            return "createreference";
        case AliasFillKind::Location:
            return "location";
        case AliasFillKind::FindMatching:
            return "findmatching";
        case AliasFillKind::UniqueActor:
            return "uniqueactor";
    }
    return "forcedreference";
}

const char* aliasStateName(AliasState state) {
    return state == AliasState::Filled ? "filled" : "open";
}

BoundProperty unsupported(const OwnerId& owner, const VmadValueShape& shape, BoundKind kind,
                          std::string reason) {
    BoundProperty property;
    property.owner = owner;
    property.name = shape.name;
    property.kind = BoundKind::Unsupported;
    property.value = noneValue();
    property.reason = std::string(boundKindLabel(kind)) + ": " + std::move(reason);
    return property;
}

std::string requireText(const std::optional<std::string>& value, const char* what) {
    if (!value.has_value() || value->empty()) {
        throw std::invalid_argument(std::string("a proven fill still needs ") + what);
    }
    return *value;
}

BoundProperty bindShape(const OwnerId& owner, const VmadValueShape& shape, const std::string& declaredType);

bool isBlank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char ch) {
        return ch == ' ' || ch == '	' || ch == '
' || ch == '' || ch == '' || ch == '';
    });
}

bool fitsFloat(double value) {
    if (!std::isfinite(value)) return true;
    if (std::fabs(value) > static_cast<double>(std::numeric_limits<float>::max())) return false;
    return static_cast<double>(static_cast<float>(value)) == value;
}

bool isBindableScalar(const std::string& type) {
    return type == "int" || type == "float" || type == "bool" || type == "string" || type == "object";
}

BoundProperty bindArray(const OwnerId& owner, const VmadValueShape& shape) {
    if (!shape.elementCount.has_value() ||
        *shape.elementCount != static_cast<std::int32_t>(shape.elements.size())) {
        return unsupported(owner, shape, BoundKind::Array,
                           "the array's " + std::to_string(shape.elementCount.value_or(0)) +
                               " elements are not decoded yet");
    }
    if (!isBindableScalar(lower(shape.elementType))) {
        return unsupported(owner, shape, BoundKind::Array,
                           "the array's " + shape.elementType + " elements are not a bindable scalar type");
    }
    auto array = std::make_shared<Array>();
    array->elementType = declaredValueType(shape.elementType);
    array->elementTypeName = shape.elementType;
    for (std::size_t i = 0; i < shape.elements.size(); ++i) {
        const BoundProperty element = bindShape(owner, shape.elements[i], shape.elementType);
        if (!element.isBound()) {
            return unsupported(owner, shape, BoundKind::Array,
                               "element " + std::to_string(i) + " " + element.reason.value_or(std::string()));
        }
        array->elements.push_back(element.value);
    }
    BoundProperty property;
    property.owner = owner;
    property.name = shape.name;
    property.kind = BoundKind::Array;
    property.value = arrayValue(std::move(array));
    return property;
}

BoundProperty bindStruct(const OwnerId& owner, const VmadValueShape& shape) {
    if (shape.members.empty() || !shape.elementCount.has_value() ||
        *shape.elementCount != static_cast<std::int32_t>(shape.members.size())) {
        return unsupported(owner, shape, BoundKind::Struct,
                           "the struct's " + std::to_string(shape.elementCount.value_or(0)) +
                               " members are not decoded yet");
    }
    auto structure = std::make_shared<Struct>();
    for (const VmadValueShape& member : shape.members) {
        const BoundProperty bound = bindShape(owner, member, member.type);
        if (!bound.isBound()) {
            return unsupported(owner, shape, BoundKind::Struct,
                               "member " + member.name + " " + bound.reason.value_or(std::string()));
        }
        const std::string key = lower(member.name);
        if (structure->members.contains(key)) {
            return unsupported(owner, shape, BoundKind::Struct, "the struct repeats member " + member.name);
        }
        structure->members.emplace(key, bound.value);
    }
    BoundProperty property;
    property.owner = owner;
    property.name = shape.name;
    property.kind = BoundKind::Struct;
    property.value = structValue(std::move(structure));
    return property;
}

BoundProperty bindShape(const OwnerId& owner, const VmadValueShape& shape, const std::string& declaredType) {
    BoundProperty property;
    property.owner = owner;
    property.name = shape.name;
    const std::string type = lower(declaredType);
    if (type == "int") {
        if (shape.integer.has_value() && (*shape.integer < std::numeric_limits<std::int32_t>::min() ||
                                          *shape.integer > std::numeric_limits<std::int32_t>::max())) {
            property = unsupported(owner, shape, BoundKind::Int,
                                   "an integer property's value " + std::to_string(*shape.integer) +
                                       " does not fit a 32-bit Papyrus int");
        } else if (shape.integer.has_value()) {
            property.kind = BoundKind::Int;
            property.value = integerValue(static_cast<std::int32_t>(*shape.integer));
        } else {
            property = unsupported(owner, shape, BoundKind::Int, "an integer property carried no value");
        }
    } else if (type == "float") {
        if (shape.real.has_value() && !fitsFloat(*shape.real)) {
            property = unsupported(owner, shape, BoundKind::Float,
                                   "a float property's value does not fit a single-precision Papyrus float exactly");
        } else if (shape.real.has_value()) {
            property.kind = BoundKind::Float;
            property.value = floatValue(static_cast<float>(*shape.real));
        } else {
            property = unsupported(owner, shape, BoundKind::Float, "a float property carried no value");
        }
    } else if (type == "bool") {
        if (shape.boolean.has_value()) {
            property.kind = BoundKind::Bool;
            property.value = boolValue(*shape.boolean);
        } else {
            property = unsupported(owner, shape, BoundKind::Bool, "a boolean property carried no value");
        }
    } else if (type == "string") {
        property.kind = BoundKind::String;
        property.value = stringValue(shape.text.value_or(std::string()));
    } else if (type == "object" && shape.hasAlias) {
        property = unsupported(owner, shape, BoundKind::AliasBound,
                               "the object is bound to a quest alias, and the alias fill order is unproven");
    } else if (type == "object") {
        if (shape.object.has_value() && !isBlank(*shape.object)) {
            property.kind = BoundKind::Object;
            property.value = objectValue(0, *shape.object, std::string());
        } else {
            property = unsupported(owner, shape, BoundKind::Object,
                                   "an object property carried no reference");
        }
    } else if (type == "array") {
        property = bindArray(owner, shape);
    } else if (type == "struct") {
        property = bindStruct(owner, shape);
    } else if (type == "variable") {
        property = unsupported(owner, shape, BoundKind::Variable,
                               "a variable property's runtime type is unknown");
    } else {
        property = unsupported(owner, shape, BoundKind::Unknown,
                               "the value kind " + shape.type + " is not recognised");
    }
    return property;
}

}  // namespace

std::string StageReport::toReport() const {
    std::string text = "quests=" + std::to_string(quests.size()) + " identified=" +
                       std::to_string(identified) + " refused=" + std::to_string(refused) +
                       " unresolved=" + std::to_string(unresolved) + "\n";
    for (const StageOutcome& outcome : quests) {
        text += "  " + outcome.quest + " " + stateName(outcome.state) + " stage=" +
                (outcome.stage.has_value() ? std::to_string(*outcome.stage) : std::string()) + " " +
                outcome.reason + "\n";
    }
    return text;
}

std::string StageReport::digest() const { return sha256Hex(toReport()); }

StageReport identifyStartupStages(const std::vector<QuestStages>& quests,
                                  const std::function<bool(std::string_view)>& gate) {
    std::vector<QuestStages> ordered = quests;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const QuestStages& a, const QuestStages& b) { return a.quest < b.quest; });
    StageReport report;
    for (const QuestStages& quest : ordered) {
        StageOutcome outcome;
        outcome.quest = quest.quest;
        if (gate && !gate(kIdentificationKey)) {
            report.refused += 1;
            outcome.state = StageState::Unidentified;
            outcome.reason = "startup-stage identification is not proven, so nothing was chosen";
            report.quests.push_back(std::move(outcome));
            continue;
        }
        std::vector<QuestStageInput> marked;
        for (const QuestStageInput& stage : quest.stages) {
            if (stage.startupStage) marked.push_back(stage);
        }
        if (marked.empty()) {
            report.unresolved += 1;
            outcome.state = StageState::Unidentified;
            outcome.reason = "no stage is marked as the startup stage, and an unidentified stage is never guessed";
            report.quests.push_back(std::move(outcome));
            continue;
        }
        if (marked.size() > 1) {
            report.unresolved += 1;
            outcome.state = StageState::Ambiguous;
            outcome.reason = std::to_string(marked.size()) +
                             " stages are marked as the startup stage, and a tie is never broken by index";
            report.quests.push_back(std::move(outcome));
            continue;
        }
        const QuestStageInput& stage = marked.front();
        if (stage.fragments > 0 && gate && !gate(kFragmentTimingKey)) {
            report.refused += 1;
            outcome.state = StageState::BlockedByFragments;
            outcome.stage = stage.index;
            outcome.reason = "the startup stage is " + std::to_string(stage.index) + ", but its " +
                             std::to_string(stage.fragments) + " fragments have unproven dispatch timing";
            report.quests.push_back(std::move(outcome));
            continue;
        }
        report.identified += 1;
        outcome.state = StageState::Identified;
        outcome.stage = stage.index;
        outcome.reason = "the startup stage is " + std::to_string(stage.index);
        report.quests.push_back(std::move(outcome));
    }
    return report;
}

bool BoundProperty::isBound() const {
    return kind == BoundKind::Int || kind == BoundKind::Float || kind == BoundKind::Bool ||
           kind == BoundKind::String || kind == BoundKind::Object || kind == BoundKind::Array ||
           kind == BoundKind::Struct;
}

std::string VmadBindReport::toReport() const {
    std::string text = "properties=" + std::to_string(properties.size()) + " bound=" +
                       std::to_string(bound) + " unsupported=" + std::to_string(unsupported) +
                       " silently-defaulted=" + std::to_string(silentlyDefaulted) + "\n";
    for (const BoundProperty& property : properties) {
        text += "  " + property.owner.key() + " " + property.name + " " + boundKindName(property.kind) +
                " " + property.reason.value_or(std::string()) + "\n";
    }
    return text;
}

std::string VmadBindReport::digest() const { return sha256Hex(toReport()); }

VmadBindReport bindVmadValues(const OwnerId& owner, const std::vector<VmadValueShape>& shapes) {
    std::vector<VmadValueShape> ordered = shapes;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const VmadValueShape& a, const VmadValueShape& b) { return a.name < b.name; });
    VmadBindReport report;
    for (const VmadValueShape& shape : ordered) {
        BoundProperty property = bindShape(owner, shape, shape.type);
        if (property.isBound()) {
            report.bound += 1;
        } else {
            report.unsupported += 1;
            report.unsupportedShapes.push_back(owner.key() + " " + shape.name + ": " +
                                               property.reason.value_or(std::string()));
        }
        report.properties.push_back(std::move(property));
    }
    return report;
}

std::string aliasFillKey(AliasFillKind kind) {
    switch (kind) {
        case AliasFillKind::ForcedReference:
            return std::string(kAliasKeyPrefix) + "forced_reference";
        case AliasFillKind::CreateReference:
            return std::string(kAliasKeyPrefix) + "create_reference";
        case AliasFillKind::Location:
            return std::string(kAliasKeyPrefix) + "location";
        case AliasFillKind::FindMatching:
        case AliasFillKind::UniqueActor:
            break;
    }
    return kAliasActorKey;
}

std::string AliasFillReport::toReport() const {
    std::string text = "aliases=" + std::to_string(aliases.size()) + " filled=" + std::to_string(filled) +
                       " open=" + std::to_string(open) + "\n";
    for (const AliasOutcome& alias : aliases) {
        text += "  " + alias.owner.key() + " " + aliasKindName(alias.kind) + " " +
                aliasStateName(alias.state) + " value=" + alias.value.value_or(std::string()) + " " +
                alias.reason + "\n";
    }
    return text;
}

std::string AliasFillReport::digest() const { return sha256Hex(toReport()); }

AliasFillReport fillQuestAliases(const std::vector<QuestAliasInput>& aliases,
                                 const std::function<bool(std::string_view)>& gate,
                                 OwnerRegistry& references) {
    std::vector<QuestAliasInput> ordered = aliases;
    std::stable_sort(ordered.begin(), ordered.end(), [](const QuestAliasInput& a, const QuestAliasInput& b) {
        if (a.quest != b.quest) return a.quest < b.quest;
        return a.aliasId < b.aliasId;
    });
    AliasFillReport report;
    for (const QuestAliasInput& alias : ordered) {
        AliasOutcome outcome;
        outcome.owner = OwnerId::forQuestAlias(alias.quest, alias.aliasId);
        outcome.kind = alias.kind;
        if (alias.kind == AliasFillKind::FindMatching || alias.kind == AliasFillKind::UniqueActor) {
            report.open += 1;
            outcome.state = AliasState::Open;
            outcome.reason = std::string("the ") + aliasKindName(alias.kind) +
                             " fill needs the actor runtime, so the alias was left empty and counted";
            const std::string actorKind = aliasKindName(alias.kind);
            if (std::find(report.openKinds.begin(), report.openKinds.end(), actorKind) ==
                report.openKinds.end()) {
                report.openKinds.push_back(actorKind);
            }
            report.aliases.push_back(std::move(outcome));
            continue;
        }
        const std::string key = aliasFillKey(alias.kind);
        if (gate && !gate(key)) {
            report.open += 1;
            outcome.state = AliasState::Open;
            outcome.reason = "the " + std::string(aliasKindName(alias.kind)) +
                             " fill is not proven, so the alias was left empty and counted";
            const std::string kind = aliasKindName(alias.kind);
            if (std::find(report.openKinds.begin(), report.openKinds.end(), kind) == report.openKinds.end()) {
                report.openKinds.push_back(kind);
            }
            report.aliases.push_back(std::move(outcome));
            continue;
        }
        switch (alias.kind) {
            case AliasFillKind::ForcedReference:
                outcome.value = requireText(alias.target, "the forced reference");
                break;
            case AliasFillKind::CreateReference:
                outcome.value = references
                                    .allocateRuntimeInstance(requireText(alias.target, "the base form"))
                                    .key();
                break;
            case AliasFillKind::Location:
                outcome.value = requireText(alias.location, "the location");
                break;
            case AliasFillKind::FindMatching:
            case AliasFillKind::UniqueActor:
                break;
        }
        report.filled += 1;
        outcome.state = AliasState::Filled;
        outcome.reason = "the fill is proven and the alias was filled";
        report.aliases.push_back(std::move(outcome));
    }
    return report;
}

}  // namespace fo4::papyrus
