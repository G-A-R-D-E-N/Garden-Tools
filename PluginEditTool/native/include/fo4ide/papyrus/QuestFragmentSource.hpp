#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::papyrus {

enum class QuestFragmentActionKind { Display, Complete, SetStage };

struct QuestFragmentAction {
    QuestFragmentActionKind kind{};
    std::uint16_t value{};
    std::optional<bool> visible{};
    std::optional<bool> force{};
    std::optional<bool> completed{};
};

std::expected<std::string, std::string> updateQuestFragmentSource(
    std::string_view source, std::string_view scriptName, std::uint16_t stage, std::uint32_t index,
    const std::vector<QuestFragmentAction>& actions);

bool isManagedQuestFragmentSource(std::string_view source);

std::expected<std::string, std::string> removeQuestFragmentSource(
    std::string_view source, std::string_view scriptName, std::uint16_t stage, std::uint32_t index);

}
