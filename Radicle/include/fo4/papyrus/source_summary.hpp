#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

struct SummaryParameter {
    std::string name;
    std::string type;
    bool isArray{false};
};

struct SummaryFunction {
    std::string name;
    std::string state;
    bool isNative{false};
    bool isEvent{false};
};

struct SummaryEvent {
    std::string name;
    std::string state;
    bool isCustom{false};
    std::vector<SummaryParameter> parameters;
};

struct SummaryProperty {
    std::string name;
    bool isAuto{false};
};

struct SourceSummary {
    bool ok{true};
    int errorLine{0};
    int errorColumn{0};
    std::string errorMessage;
    std::string name;
    std::string extends;
    std::string autoState;
    std::vector<std::string> imports;
    std::vector<SummaryProperty> properties;
    std::vector<std::string> states;
    std::vector<SummaryFunction> functions;
    std::vector<SummaryEvent> events;
};

SourceSummary summarizeSource(std::string_view text);

}  // namespace fo4::papyrus
