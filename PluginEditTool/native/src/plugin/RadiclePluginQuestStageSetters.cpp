#include "RadiclePluginQuestInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fo4/forms/quest.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>

namespace fo4ide::plugin::questinternal {

namespace {

std::optional<std::size_t> conditionRunLength(const std::vector<fo4::Subrecord>& members,
                                              std::vector<fo4::CtdaCondition>& conditions) {
    std::size_t consumed = 0;
    if (!fo4::forms::lossless_quest_log_entry_conditions(members, conditions, consumed)) {
        return std::nullopt;
    }
    return consumed;
}

}

std::optional<Error> nonFiniteConditionError(const fo4::CtdaCondition& condition) {
    if (condition.use_global() || std::isfinite(condition.comparison_float())) return std::nullopt;
    return Error{
        "QuestAuthoringCoverageIncomplete",
        "existing QUST condition has a non-finite comparison value"
    };
}

std::optional<Error> parseQuestStageCondition(const nlohmann::json& row,
                                              const fo4::Plugin& plugin,
                                              const fo4::ConflictGraph& graph,
                                              PendingQuestCondition& out,
                                              std::vector<fo4::FormKey>& conditionKeys) {
    if (!row.is_object() || !row.contains("_native")) {
        if (row.is_object() && row.contains("_edited")) {
            return Error{"InvalidQuestStagePayload", "_edited is only valid on a preserved _native condition"};
        }
        return parseQuestCondition(row, graph, out, conditionKeys);
    }
    const auto invalid = [](std::string message) {
        return Error{"InvalidQuestStagePayload", std::move(message)};
    };
    const auto& metadata = row.at("_native");
    if (!metadata.is_object() || !metadata.contains("ctda") || !metadata.contains("cis1") ||
        !metadata.contains("cis2") || !metadata.at("ctda").is_array() ||
        metadata.at("ctda").size() != 32) {
        return invalid("invalid native condition data");
    }
    std::vector<std::byte> bytes;
    bytes.reserve(32);
    for (const auto& byte : metadata.at("ctda")) {
        if (!byte.is_number_integer()) return invalid("invalid native condition byte");
        const auto value = byte.get<std::int64_t>();
        if (value < 0 || value > 255) return invalid("invalid native condition byte");
        bytes.push_back(std::byte{static_cast<std::uint8_t>(value)});
    }
    auto condition = fo4::parse_ctda_condition(bytes);
    if (fo4::serialize_ctda_condition(condition) != bytes) {
        return invalid("native condition payload cannot round-trip");
    }
    const auto restoreString = [&](const char* key,
                                   std::optional<std::string>& target) -> std::optional<Error> {
        const auto& value = metadata.at(key);
        if (value.is_null()) return std::nullopt;
        if (!value.is_string()) return invalid(std::string("preserved ") + key + " must be a string");
        const auto stored = conditionCisFromUtf8(value.get<std::string>());
        if (!stored) {
            return invalid(std::string("preserved ") + key + " is not representable in Windows-1252");
        }
        target = *stored;
        return std::nullopt;
    };
    if (const auto error = restoreString("cis1", condition.parameter1_string)) return error;
    if (const auto error = restoreString("cis2", condition.parameter2_string)) return error;
    if (const auto error = adapterConditionUnrepresentable(plugin, graph, condition)) return error;

    nlohmann::json expected = describeCondition(condition, plugin);
    expected.erase("_native");
    nlohmann::json edited = row;
    edited.erase("_native");
    edited.erase("_edited");
    if (edited == expected) {
        out = {};
        out.condition = condition;
        out.preserved = true;
        return std::nullopt;
    }
    return parseQuestCondition(edited, graph, out, conditionKeys);
}

std::optional<Error> refuseOwnerMasterShift(const fo4::Plugin& plugin,
                                            const std::vector<fo4::FormKey>& conditionKeys,
                                            bool rawConditionsPresent) {
    if (!rawConditionsPresent) return std::nullopt;
    for (const auto& key : conditionKeys) {
        const auto& owner = key.owner();
        if (owner.empty() || owner == plugin.key) continue;
        const auto& masters = plugin.header.masters;
        if (std::any_of(masters.begin(), masters.end(),
                        [&owner](const auto& master) { return master.key == owner; })) {
            continue;
        }
        return Error{
            "InvalidQuestStagePayload",
            "SetQuestStages would add master " + std::string(owner.filename()) + " to " +
                std::string(plugin.key.filename()) +
                ", which renumbers own-plugin FormIDs in carried or preserved conditions; "
                "add that master to the plugin first, or send explicit conditions without _native "
                "for every log entry"
        };
    }
    return std::nullopt;
}

std::vector<fo4::CtdaCondition> previousConditionsNotPreserved(
    const std::vector<fo4::CtdaCondition>& previous,
    const std::vector<PendingQuestCondition>& pending) {
    const auto withoutPadding = [](fo4::CtdaCondition condition) {
        condition.padding = {};
        return condition;
    };
    std::vector<fo4::CtdaCondition> remaining = previous;
    for (const auto& row : pending) {
        if (!row.preserved) continue;
        const auto payload = fo4::serialize_ctda_condition(withoutPadding(row.condition));
        const auto match = std::find_if(
            remaining.begin(), remaining.end(), [&](const fo4::CtdaCondition& candidate) {
                return candidate.parameter1_string == row.condition.parameter1_string &&
                       candidate.parameter2_string == row.condition.parameter2_string &&
                       fo4::serialize_ctda_condition(withoutPadding(candidate)) == payload;
            });
        if (match != remaining.end()) remaining.erase(match);
    }
    return remaining;
}

std::optional<Error> checkOmittedStageConditions(const fo4::forms::QuestStages& previous,
                                                 std::uint16_t index,
                                                 std::size_t entryCount,
                                                 const std::vector<bool>& omitted) {
    if (std::find(omitted.begin(), omitted.end(), true) == omitted.end()) return std::nullopt;
    std::size_t matches = 0;
    std::size_t previousCount = 0;
    bool previousHasConditions = false;
    for (const auto& stage : previous.stages) {
        if (stage.index != index) continue;
        ++matches;
        previousCount = stage.log_entries.size();
        for (const auto& entry : stage.log_entries) {
            std::vector<fo4::CtdaCondition> conditions;
            const auto consumed = conditionRunLength(entry.members, conditions);
            if (!consumed.has_value() || !conditions.empty()) previousHasConditions = true;
        }
    }
    if (!previousHasConditions) return std::nullopt;
    if (matches == 1 && previousCount == entryCount) return std::nullopt;
    return Error{
        "InvalidQuestStagePayload",
        "stage " + std::to_string(index) +
            " changed its log entry count while it has existing conditions; "
            "send an explicit conditions array on every log entry"
    };
}

std::vector<fo4::Subrecord> carriedStageConditionRun(const fo4::forms::QuestStages& previous,
                                                     std::uint16_t index,
                                                     std::size_t ordinal,
                                                     std::size_t entryCount) {
    std::vector<fo4::Subrecord> run;
    const fo4::forms::QuestStage* match = nullptr;
    std::size_t matches = 0;
    for (const auto& stage : previous.stages) {
        if (stage.index != index) continue;
        ++matches;
        match = &stage;
    }
    if (matches != 1 || match->log_entries.size() != entryCount || ordinal >= entryCount) {
        return run;
    }
    const auto& members = match->log_entries[ordinal].members;
    std::vector<fo4::CtdaCondition> conditions;
    const auto consumed = conditionRunLength(members, conditions);
    if (!consumed.has_value()) return run;
    run.assign(members.begin(), std::next(members.begin(), static_cast<std::ptrdiff_t>(*consumed)));
    return run;
}

}
