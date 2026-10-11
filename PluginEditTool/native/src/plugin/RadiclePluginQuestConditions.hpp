#pragma once

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

#include <fo4/graph/conflict_graph.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

inline constexpr std::uint16_t kMutagenGetEventData = 576;
inline constexpr std::uint32_t kRunOnReference = 2;

struct PendingQuestCondition {
    fo4::CtdaCondition condition;
    std::optional<fo4::FormKey> param1;
    std::optional<fo4::FormKey> param2;
    std::optional<fo4::FormKey> reference;
    std::optional<fo4::FormKey> compareGlobal;
};

bool isPlacedSignature(fo4::Signature signature);

std::optional<Error> requireLoadedFill(
    const fo4::ConflictGraph& graph,
    const fo4::FormKey& key,
    std::initializer_list<std::string_view> accepted,
    bool placedTargets,
    std::string_view slotName);

std::optional<Error> requireConditionTarget(
    const fo4::ConflictGraph& graph,
    const fo4::FormKey& key,
    std::string_view paramType,
    std::string_view slotName);

bool conditionSlotIsNumeric(std::string_view paramType, std::uint8_t flags);

bool conditionSlotIsText(std::string_view paramType);

std::optional<Error> rejectUnknownKeys(const nlohmann::json& object,
                                       bool (*known)(std::string_view),
                                       std::string_view label,
                                       std::string_view errorType = "InvalidQuestStagePayload");

std::optional<Error> parseQuestCondition(
    const nlohmann::json& element,
    const fo4::ConflictGraph& graph,
    PendingQuestCondition& out,
    std::vector<fo4::FormKey>& conditionKeys,
    std::string_view errorType = "InvalidQuestStagePayload",
    std::string_view surface = "quest log entries");

}  // namespace fo4ide::plugin
