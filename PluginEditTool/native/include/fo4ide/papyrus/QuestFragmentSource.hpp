#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::papyrus {

enum class QuestFragmentActionKind { Display, Complete, SetStage };

struct QuestFragmentAction {
    QuestFragmentActionKind kind{};
    std::uint16_t value{};
};

std::expected<std::string, std::string> updateQuestFragmentSource(
    std::string_view source, std::string_view scriptName, std::uint16_t stage, std::uint32_t index,
    const std::vector<QuestFragmentAction>& actions);

}
