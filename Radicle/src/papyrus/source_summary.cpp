#include <fo4/papyrus/source_summary.hpp>

#include <algorithm>
#include <cstddef>
#include <tuple>
#include <utility>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/parser.hpp>

namespace fo4::papyrus {

namespace {

template <typename Item>
void appendUnique(std::vector<Item>& out, const Item& value) {
    if (std::find(out.begin(), out.end(), value) == out.end()) out.push_back(value);
}

std::string typeText(std::string_view text, const TypeRef& type) {
    const auto start = static_cast<std::size_t>(type.span.start);
    const auto length = static_cast<std::size_t>(type.span.length);
    if (type.span.start < 0 || start + length > text.size()) return type.name;
    std::string written(text.substr(start, length));
    const auto bracket = written.find('[');
    if (bracket != std::string::npos) written.resize(bracket);
    while (!written.empty() && (written.back() == ' ' || written.back() == '\t')) written.pop_back();
    return written.empty() ? type.name : written;
}

std::vector<SummaryParameter> parametersOf(std::string_view text, const std::vector<Parameter>& parameters) {
    std::vector<SummaryParameter> out;
    out.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        out.push_back({parameter.name, typeText(text, parameter.type), parameter.type.isArray});
    }
    return out;
}

std::string eventName(const EventDecl& event) {
    return event.remoteObjectType ? *event.remoteObjectType + "." + event.name : event.name;
}

struct Callable {
    int start{0};
    const FunctionDecl* function{nullptr};
    const EventDecl* event{nullptr};
};

std::vector<Callable> inSourceOrder(const std::vector<FunctionDecl>& functions,
                                    const std::vector<EventDecl>& events) {
    std::vector<Callable> out;
    out.reserve(functions.size() + events.size());
    for (const auto& function : functions) out.push_back({function.span.start, &function, nullptr});
    for (const auto& event : events) out.push_back({event.span.start, nullptr, &event});
    std::stable_sort(out.begin(), out.end(),
                     [](const Callable& left, const Callable& right) { return left.start < right.start; });
    return out;
}

void addScope(SourceSummary& summary, std::string_view text, const std::string& state,
              const std::vector<FunctionDecl>& functions, const std::vector<EventDecl>& events,
              const std::vector<CustomEventDecl>& custom) {
    for (const auto& item : inSourceOrder(functions, events)) {
        if (item.function != nullptr) {
            summary.functions.push_back({item.function->name, state, item.function->isNative, false});
        } else {
            summary.functions.push_back({eventName(*item.event), state, item.event->isNative, true});
            summary.events.push_back(
                {eventName(*item.event), state, false, parametersOf(text, item.event->parameters)});
        }
    }
    for (const auto& event : custom) {
        summary.functions.push_back({event.name, state, false, true});
        summary.events.push_back({event.name, state, true, {}});
    }
}

}  // namespace

SourceSummary summarizeSource(std::string_view text) {
    const Script script = Parser::parse(text);
    SourceSummary summary;
    const Diagnostic* first = nullptr;
    for (const auto& diagnostic : script.diagnostics) {
        if (diagnostic.severity != Severity::Error) continue;
        if (!first || std::tie(diagnostic.span.line, diagnostic.span.column) < std::tie(first->span.line, first->span.column)) first = &diagnostic;
    }
    if (first) {
        summary.ok = false;
        summary.errorLine = first->span.line;
        summary.errorColumn = first->span.column;
        summary.errorMessage = first->message;
        return summary;
    }

    summary.name = script.name;
    summary.extends = script.extends ? *script.extends : std::string();
    for (const auto& import : script.imports) summary.imports.push_back(import.name);

    for (const auto& property : script.properties) {
        const bool isAuto = property.kind != PropertyKind::Full;
        const auto known = std::find_if(summary.properties.begin(), summary.properties.end(),
                                        [&](const SummaryProperty& item) { return item.name == property.name; });
        if (known == summary.properties.end()) summary.properties.push_back({property.name, isAuto});
        else known->isAuto = known->isAuto || isAuto;
        std::vector<const FunctionDecl*> accessors;
        if (property.getter) accessors.push_back(&*property.getter);
        if (property.setter) accessors.push_back(&*property.setter);
        std::stable_sort(accessors.begin(), accessors.end(), [](const FunctionDecl* left, const FunctionDecl* right) {
            return left->span.start < right->span.start;
        });
        for (const FunctionDecl* accessor : accessors) {
            summary.functions.push_back({property.name + "." + accessor->name, std::string(), accessor->isNative, false});
        }
    }
    addScope(summary, text, std::string(), script.functions, script.events, script.customEvents);
    for (const auto& state : script.states) {
        appendUnique(summary.states, state.name);
        if (state.isAuto && summary.autoState.empty()) summary.autoState = state.name;
        addScope(summary, text, state.name, state.functions, state.events, {});
    }
    return summary;
}

}  // namespace fo4::papyrus
