#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/graph_document.hpp>

namespace fo4::graph {

enum class PinDirection { In, Out };

enum class PinKind { Exec, Data };

enum class PinTypeForm {
    Concrete,
    Generic,
    ArrayOfGeneric,
    ElementOfGeneric,
    SelfType,
    Any,
};

struct PinTypeExpr {
    PinTypeForm form{PinTypeForm::Concrete};
    std::string typeName;
    bool isArray{false};
    std::string variable{"T"};

    static PinTypeExpr concrete(std::string typeName, bool isArray = false) {
        return PinTypeExpr{PinTypeForm::Concrete, std::move(typeName), isArray, "T"};
    }
    static PinTypeExpr generic(std::string variable = "T") {
        return PinTypeExpr{PinTypeForm::Generic, "", false, std::move(variable)};
    }
    static PinTypeExpr arrayOfGeneric(std::string variable = "T") {
        return PinTypeExpr{PinTypeForm::ArrayOfGeneric, "", false, std::move(variable)};
    }
    static PinTypeExpr elementOfGeneric(std::string variable = "T") {
        return PinTypeExpr{PinTypeForm::ElementOfGeneric, "", false, std::move(variable)};
    }
    static PinTypeExpr selfType() { return PinTypeExpr{PinTypeForm::SelfType, "", false, "T"}; }
    static PinTypeExpr any() { return PinTypeExpr{PinTypeForm::Any, "", false, "T"}; }

    std::string toString() const {
        switch (form) {
            case PinTypeForm::Concrete:
                return typeName + (isArray ? "[]" : "");
            case PinTypeForm::Generic:
                return variable;
            case PinTypeForm::ArrayOfGeneric:
                return variable + "[]";
            case PinTypeForm::ElementOfGeneric:
                return "elementof " + variable;
            case PinTypeForm::SelfType:
                return "Self";
            default:
                return "var";
        }
    }
};

struct PinDefinition {
    std::string id;
    std::string label;
    PinDirection direction{PinDirection::In};
    PinKind kind{PinKind::Data};
    std::optional<PinTypeExpr> type;
    bool isOptional{false};
    std::optional<std::string> declaredDefault;
    std::optional<std::string> description;

    std::string toString() const {
        std::string text = direction == PinDirection::In ? "In" : "Out";
        text += kind == PinKind::Exec ? " Exec " : " Data ";
        text += id;
        if (type) text += " : " + type->toString();
        return text;
    }
};

struct NodeDefinition {
    std::string id;
    GraphNodeKind kind{GraphNodeKind::Unknown};
    std::string title;
    std::string category;
    std::optional<std::string> summary;
    bool isPure{false};
    std::optional<std::string> ownerScript;
    std::optional<std::string> memberName;
    bool isGlobal{false};
    bool isRemoteEvent{false};
    std::optional<std::string> localNameHint;
    std::vector<PinDefinition> pins;

    std::vector<const PinDefinition*> inputs() const {
        std::vector<const PinDefinition*> out;
        for (const auto& p : pins) {
            if (p.direction == PinDirection::In) out.push_back(&p);
        }
        return out;
    }
    std::vector<const PinDefinition*> outputs() const {
        std::vector<const PinDefinition*> out;
        for (const auto& p : pins) {
            if (p.direction == PinDirection::Out) out.push_back(&p);
        }
        return out;
    }
    std::vector<const PinDefinition*> execInputs() const {
        std::vector<const PinDefinition*> out;
        for (const auto& p : pins) {
            if (p.kind == PinKind::Exec && p.direction == PinDirection::In) out.push_back(&p);
        }
        return out;
    }
    std::vector<const PinDefinition*> execOutputs() const {
        std::vector<const PinDefinition*> out;
        for (const auto& p : pins) {
            if (p.kind == PinKind::Exec && p.direction == PinDirection::Out) out.push_back(&p);
        }
        return out;
    }
    std::vector<const PinDefinition*> dataInputs() const {
        std::vector<const PinDefinition*> out;
        for (const auto& p : pins) {
            if (p.kind == PinKind::Data && p.direction == PinDirection::In) out.push_back(&p);
        }
        return out;
    }
    std::vector<const PinDefinition*> dataOutputs() const {
        std::vector<const PinDefinition*> out;
        for (const auto& p : pins) {
            if (p.kind == PinKind::Data && p.direction == PinDirection::Out) out.push_back(&p);
        }
        return out;
    }

    const PinDefinition* pin(std::string_view pinId) const {
        if (pinId.empty()) return nullptr;
        for (const auto& p : pins) {
            if (equalsIgnoreCase(p.id, pinId)) return &p;
        }
        return nullptr;
    }

    std::vector<PinDefinition> pinsFor(const GraphNode& node,
                                       const GraphDocument* document = nullptr) const {
        const auto written = [&](std::string_view key) -> std::optional<std::string> {
            const auto value = node.configString(std::string(key));
            if (!value || value->empty() ||
                std::all_of(value->begin(), value->end(), [](unsigned char c) {
                    return std::isspace(c);
                })) {
                return std::nullopt;
            }
            return value;
        };

        switch (kind) {
            case GraphNodeKind::VariableGet:
            case GraphNodeKind::VariableSet: {
                const auto name = node.configString("name");
                const GraphVariable* declared = nullptr;
                if (document) {
                    for (const auto& v : document->variables) {
                        if (equalsIgnoreCase(v.name, name.value_or(""))) {
                            declared = &v;
                            break;
                        }
                    }
                }
                const auto resolved = declared ? std::optional<std::string>(declared->type)
                                               : written("type");
                if (!resolved) return pins;
                const auto type = typeFromWritten(*resolved);
                std::vector<PinDefinition> out;
                out.reserve(pins.size());
                for (const auto& p : pins) {
                    out.push_back(p.kind == PinKind::Data
                                      ? PinDefinition{p.id, p.label, p.direction, p.kind, type,
                                                      p.isOptional, p.declaredDefault, p.description}
                                      : p);
                }
                return out;
            }

            case GraphNodeKind::Cast:
            case GraphNodeKind::NewArray: {
                const auto resolved = written("type");
                if (!resolved) return pins;
                const auto type = kind == GraphNodeKind::NewArray
                                      ? PinTypeExpr::concrete(baseOf(*resolved), true)
                                      : typeFromWritten(*resolved);
                std::vector<PinDefinition> out;
                out.reserve(pins.size());
                for (const auto& p : pins) {
                    out.push_back(p.id == pinIds::kReturn && p.kind == PinKind::Data
                                      ? PinDefinition{p.id, p.label, p.direction, p.kind, type,
                                                      p.isOptional, p.declaredDefault, p.description}
                                      : p);
                }
                return out;
            }

            case GraphNodeKind::StructNew:
            case GraphNodeKind::StructGet: {
                const auto resolved = written("type");
                if (!resolved) return pins;
                const auto type = typeFromWritten(*resolved);
                std::vector<PinDefinition> out;
                out.reserve(pins.size());
                for (const auto& p : pins) {
                    out.push_back(p.direction == PinDirection::Out && p.kind == PinKind::Data
                                      ? PinDefinition{p.id, p.label, p.direction, p.kind, type,
                                                      p.isOptional, p.declaredDefault, p.description}
                                      : p);
                }
                return out;
            }

            default:
                return pins;
        }
    }

    std::string toString() const { return id + " (" + std::string(graphNodeKindName(kind)) + ")"; }

private:
    static std::string baseOf(std::string_view written) {
        if (written.empty()) return "";
        const std::string_view suffix = "[]";
        if (written.size() > suffix.size() &&
            written.substr(written.size() - suffix.size()) == suffix) {
            return std::string(written.substr(0, written.size() - suffix.size()));
        }
        return std::string(written);
    }
    static PinTypeExpr typeFromWritten(std::string_view written) {
        const bool isArray = written.size() > 2 &&
                             written.substr(written.size() - 2) == "[]";
        return PinTypeExpr::concrete(baseOf(written), isArray);
    }
};

}
