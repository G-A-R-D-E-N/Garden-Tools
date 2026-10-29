#include <fo4/graph/node_palette.hpp>

#include <algorithm>
#include <cctype>
#include <functional>

#include <fo4/papyrus/symbols.hpp>

namespace fo4::graph {
namespace {

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool containsIgnoreCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    const auto hl = lower(haystack);
    const auto nl = lower(needle);
    return hl.find(nl) != std::string::npos;
}

std::string scriptKey(std::string_view scriptName) { return lower(scriptName); }

}

NodePalette::NodePalette(papyrus::ScriptIndex& index, WikiDocProvider* docs)
    : index_(index), docs_(docs ? docs : &NullWikiDocProvider::instance()) {}

const NodeDefinition* NodePalette::find(std::string_view definitionId) {
    if (definitionId.empty()) return nullptr;

    const auto builtin = builtinDefinitions::find(definitionId);
    if (builtin) return builtin;

    const auto owner = ownerScriptOf(definitionId);
    if (!owner) return nullptr;

    for (const auto& definition : forScript(*owner)) {
        if (equalsIgnoreCase(definition.id, definitionId)) return &definition;
    }
    return nullptr;
}

const std::vector<NodeDefinition>& NodePalette::forScript(std::string_view scriptName) {
    const auto key = scriptKey(scriptName);
    const auto cached = byScript_.find(key);
    if (cached != byScript_.end()) return cached->second;
    auto built = buildForScript(scriptName);
    const auto inserted = byScript_.emplace(key, std::move(built));
    return inserted.first->second;
}

PaletteSearchResult NodePalette::search(std::optional<std::string_view> query, int limit,
                                        std::optional<std::string_view> scriptFilter) {
    std::vector<PaletteEntry> candidates = searchIndex();

    if (scriptFilter && !scriptFilter->empty()) {
        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(),
                           [&](const PaletteEntry& entry) {
                               return !equalsIgnoreCase(entry.category, *scriptFilter);
                           }),
            candidates.end());
    }

    if (query) {
        const auto needle = [&] {
            auto text = std::string(*query);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
                text.erase(text.begin());
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
                text.pop_back();
            }
            return text;
        }();

        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(),
                           [&](const PaletteEntry& entry) {
                               return !containsIgnoreCase(entry.title, needle) &&
                                      !containsIgnoreCase(entry.category, needle);
                           }),
            candidates.end());

        std::stable_sort(candidates.begin(), candidates.end(),
                         [&](const PaletteEntry& left, const PaletteEntry& right) {
                             const auto rank = [&](const PaletteEntry& entry) {
                                 if (equalsIgnoreCase(entry.title, needle)) return 0;
                                 if (startsWithIgnoreCase(entry.title, needle)) return 1;
                                 return 2;
                             };
                             const auto lr = rank(left);
                             const auto rr = rank(right);
                             if (lr != rr) return lr < rr;
                             if (left.title.size() != right.title.size()) {
                                 return left.title.size() < right.title.size();
                             }
                             return compareIgnoreCase(left.title, right.title) < 0;
                         });
    }

    PaletteSearchResult result;
    result.total = static_cast<int>(candidates.size());
    const auto take = std::max(0, limit);
    if (static_cast<std::size_t>(take) < candidates.size()) {
        result.entries.assign(candidates.begin(), candidates.begin() + take);
    } else {
        result.entries = std::move(candidates);
    }
    return result;
}

std::string NodePalette::remoteSignature(std::string_view scriptName, std::string_view eventName,
                                         std::optional<std::string_view> declaredSignature) {
    const auto sender = std::string(scriptName) + " " + std::string(kRemoteSenderName);

    if (!declaredSignature) {
        return "Event " + std::string(scriptName) + "." + std::string(eventName) +
               "(" + sender + ", Var[] akArgs)";
    }

    const auto open = declaredSignature->find('(');
    const auto close = declaredSignature->rfind(')');
    const auto declared = (open != std::string_view::npos && close != std::string_view::npos &&
                           close > open)
                              ? [&] {
                                    auto text = declaredSignature->substr(open + 1, close - open - 1);
                                    while (!text.empty() &&
                                           std::isspace(static_cast<unsigned char>(text.front()))) {
                                        text.remove_prefix(1);
                                    }
                                    while (!text.empty() &&
                                           std::isspace(static_cast<unsigned char>(text.back()))) {
                                        text.remove_suffix(1);
                                    }
                                    return std::string(text);
                                }()
                              : std::string();

    const auto parameters = declared.empty() ? sender : sender + ", " + declared;
    return "Event " + std::string(scriptName) + "." + std::string(eventName) + "(" + parameters +
           ")";
}

std::string NodePalette::localNameHint(std::string_view memberName) {
    for (const auto& prefix : {"Get", "Is", "Has", "Can"}) {
        if (memberName.size() <= std::string_view(prefix).size()) continue;
        if (!startsWithIgnoreCase(memberName, prefix)) continue;
        if (!std::isupper(static_cast<unsigned char>(memberName[std::string_view(prefix).size()]))) {
            continue;
        }
        memberName.remove_prefix(std::string_view(prefix).size());
        break;
    }

    if (memberName.empty()) return "value";
    std::string out(memberName);
    out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    return out;
}

std::string NodePalette::callId(std::string_view script, std::string_view member, bool isGlobal) {
    return std::string(isGlobal ? "global:" : "call:") + std::string(script) + "." +
           std::string(member);
}

std::string NodePalette::eventId(std::string_view script, std::string_view member) {
    return "event:" + std::string(script) + "." + std::string(member);
}

std::string NodePalette::remoteEventId(std::string_view script, std::string_view member) {
    return "remote:" + std::string(script) + "." + std::string(member);
}

std::string NodePalette::propertyGetId(std::string_view script, std::string_view member) {
    return "prop.get:" + std::string(script) + "." + std::string(member);
}

std::string NodePalette::propertySetId(std::string_view script, std::string_view member) {
    return "prop.set:" + std::string(script) + "." + std::string(member);
}

std::optional<std::string> NodePalette::ownerScriptOf(std::string_view definitionId) {
    const auto colon = definitionId.find(':');
    if (colon == std::string_view::npos) return std::nullopt;

    const auto body = definitionId.substr(colon + 1);
    const auto dot = body.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return std::nullopt;
    return std::string(body.substr(0, dot));
}

std::optional<std::string> NodePalette::memberNameOf(std::string_view definitionId) {
    const auto colon = definitionId.find(':');
    if (colon == std::string_view::npos) return std::nullopt;

    const auto body = definitionId.substr(colon + 1);
    const auto dot = body.rfind('.');
    if (dot == std::string_view::npos || dot == body.size() - 1) return std::nullopt;
    return std::string(body.substr(dot + 1));
}

std::optional<std::string> NodePalette::defaultText(const papyrus::Node* expression) {
    if (!expression) return std::nullopt;

    if (const auto* literal = dynamic_cast<const papyrus::LiteralExpression*>(expression)) {
        switch (literal->kind) {
            case papyrus::LiteralKind::String: {
                std::string text = literal->text;
                std::string escaped;
                escaped.reserve(text.size() + 2);
                for (const char c : text) {
                    if (c == '\\' || c == '"') escaped.push_back('\\');
                    escaped.push_back(c);
                }
                return "\"" + escaped + "\"";
            }
            case papyrus::LiteralKind::None:
                return std::string("None");
            default:
                return literal->text;
        }
    }

    if (const auto* unary = dynamic_cast<const papyrus::UnaryExpression*>(expression)) {
        if (const auto* inner = dynamic_cast<const papyrus::LiteralExpression*>(unary->operand.get())) {
            return "-" + inner->text;
        }
    }

    if (const auto* identifier = dynamic_cast<const papyrus::IdentifierExpression*>(expression)) {
        return identifier->name;
    }

    return std::nullopt;
}

bool NodePalette::isGlobalSignature(std::string_view signature) {
    return containsIgnoreCase(signature, " global");
}

PinTypeExpr NodePalette::typeOf(const papyrus::TypeRef* reference) {
    return reference ? PinTypeExpr::concrete(reference->name, reference->isArray)
                     : PinTypeExpr::concrete("None");
}

std::vector<PaletteEntry> NodePalette::buildSearchIndex() {
    std::vector<PaletteEntry> entries;
    entries.reserve(builtinDefinitions::all().size() + 64);

    for (const auto& definition : builtinDefinitions::all()) {
        entries.push_back(PaletteEntry{definition.id, definition.title, definition.category,
                                       definition.kind, definition.title, definition.isPure});
    }

    for (const auto& scriptName : index_.scriptNames()) {
        const auto* script = index_.resolve(scriptName);
        if (!script) continue;

        for (const auto& symbol : papyrus::Symbols::documentSymbols(*script)) {
            switch (symbol.kind) {
                case papyrus::SymbolKind::Function:
                    entries.push_back(PaletteEntry{
                        callId(scriptName, symbol.name, isGlobalSignature(symbol.signature)),
                        symbol.name, scriptName, GraphNodeKind::Call, symbol.signature, false});
                    break;

                case papyrus::SymbolKind::Event:
                    entries.push_back(PaletteEntry{eventId(scriptName, symbol.name), symbol.name,
                                                   scriptName, GraphNodeKind::EventEntry,
                                                   symbol.signature, false});
                    entries.push_back(PaletteEntry{
                        remoteEventId(scriptName, symbol.name), scriptName + "." + symbol.name,
                        scriptName, GraphNodeKind::EventEntry,
                        remoteSignature(scriptName, symbol.name, symbol.signature), false});
                    break;

                case papyrus::SymbolKind::CustomEvent:
                    entries.push_back(PaletteEntry{
                        remoteEventId(scriptName, symbol.name), scriptName + "." + symbol.name,
                        scriptName, GraphNodeKind::EventEntry,
                        remoteSignature(scriptName, symbol.name, std::nullopt), false});
                    break;

                case papyrus::SymbolKind::Property:
                    entries.push_back(PaletteEntry{propertyGetId(scriptName, symbol.name),
                                                   symbol.name, scriptName,
                                                   GraphNodeKind::PropertyGet, symbol.signature, true});
                    entries.push_back(PaletteEntry{propertySetId(scriptName, symbol.name),
                                                   symbol.name, scriptName,
                                                   GraphNodeKind::PropertySet, symbol.signature, false});
                    break;

                default:
                    break;
            }
        }
    }

    return entries;
}

const std::vector<PaletteEntry>& NodePalette::searchIndex() {
    if (!searchIndex_) searchIndex_ = buildSearchIndex();
    return *searchIndex_;
}

std::vector<NodeDefinition> NodePalette::buildForScript(std::string_view scriptName) {
    const auto* script = index_.resolve(scriptName);
    if (!script) return {};

    std::vector<NodeDefinition> definitions;
    definitions.reserve(script->functions.size() + script->events.size() * 2 +
                        script->customEvents.size() + script->properties.size() * 2);

    for (const auto& function : script->functions) {
        definitions.push_back(functionDefinition(scriptName, function));
    }

    for (const auto& declared : script->events) {
        auto entry = eventDefinition(scriptName, declared);
        std::vector<PinDefinition> tail;
        for (const auto* pin : entry.dataOutputs()) tail.push_back(*pin);
        const auto* docs = docs_->function(scriptName, declared.name);
        definitions.push_back(std::move(entry));
        definitions.push_back(remoteEventDefinition(
            scriptName, declared.name, std::move(tail),
            docs ? docs->summary : std::nullopt));
    }

    for (const auto& declared : script->customEvents) {
        std::vector<PinDefinition> tail{customEventArgsPin()};
        definitions.push_back(remoteEventDefinition(scriptName, declared.name, std::move(tail),
                                                    std::nullopt));
    }

    for (const auto& property : script->properties) {
        definitions.push_back(propertyGetDefinition(scriptName, property));
        if (property.kind != papyrus::PropertyKind::AutoReadOnly) {
            definitions.push_back(propertySetDefinition(scriptName, property));
        }
    }

    return definitions;
}

NodeDefinition NodePalette::functionDefinition(std::string_view scriptName,
                                               const papyrus::FunctionDecl& function) {
    const auto* docs = docs_->function(scriptName, function.name);

    std::vector<PinDefinition> pins;
    pins.push_back(PinDefinition{std::string(pinIds::kExec), "", PinDirection::In, PinKind::Exec,
                                 std::nullopt, false, std::nullopt, std::nullopt});

    if (!function.isGlobal) {
        pins.push_back(PinDefinition{std::string(pinIds::kSelf), "Target", PinDirection::In,
                                     PinKind::Data, PinTypeExpr::concrete(std::string(scriptName)),
                                     false, std::nullopt, std::nullopt});
    }

    for (const auto& parameter : function.parameters) {
        pins.push_back(parameterPin(parameter, docs));
    }

    pins.push_back(PinDefinition{std::string(pinIds::kThen), "", PinDirection::Out, PinKind::Exec,
                                 std::nullopt, false, std::nullopt, std::nullopt});

    if (function.returnType) {
        pins.push_back(PinDefinition{
            std::string(pinIds::kReturn), "Return", PinDirection::Out, PinKind::Data,
            typeOf(&*function.returnType), false, std::nullopt,
            docs ? docs->returnValue : std::nullopt});
    }

    NodeDefinition definition;
    definition.id = callId(scriptName, function.name, function.isGlobal);
    definition.kind = GraphNodeKind::Call;
    definition.title = function.name;
    definition.category = std::string(scriptName);
    definition.summary = docs ? docs->summary : std::nullopt;
    definition.ownerScript = std::string(scriptName);
    definition.memberName = function.name;
    definition.isGlobal = function.isGlobal;
    definition.isPure = false;
    definition.localNameHint = localNameHint(function.name);
    definition.pins = std::move(pins);
    return definition;
}

NodeDefinition NodePalette::eventDefinition(std::string_view scriptName,
                                            const papyrus::EventDecl& declared) {
    const auto* docs = docs_->function(scriptName, declared.name);

    std::vector<PinDefinition> pins;
    pins.push_back(PinDefinition{std::string(pinIds::kExec), "", PinDirection::Out, PinKind::Exec,
                                 std::nullopt, false, std::nullopt, std::nullopt});

    for (const auto& parameter : declared.parameters) {
        std::optional<std::string> description;
        if (docs) {
            const auto found = docs->parameters.find(lower(parameter.name));
            if (found != docs->parameters.end()) description = found->second.description;
        }
        pins.push_back(PinDefinition{pinIds::parameter(parameter.name), parameter.name,
                                     PinDirection::Out, PinKind::Data,
                                     typeOf(&parameter.type), false, std::nullopt,
                                     std::move(description)});
    }

    NodeDefinition definition;
    definition.id = eventId(scriptName, declared.name);
    definition.kind = GraphNodeKind::EventEntry;
    definition.title = declared.name;
    definition.category = std::string(scriptName);
    definition.summary = docs ? docs->summary : std::nullopt;
    definition.ownerScript = std::string(scriptName);
    definition.memberName = declared.name;
    definition.pins = std::move(pins);
    return definition;
}

NodeDefinition NodePalette::remoteEventDefinition(std::string_view scriptName,
                                                  std::string_view eventName,
                                                  std::vector<PinDefinition> tail,
                                                  std::optional<std::string> summary) {
    std::vector<PinDefinition> pins;
    pins.push_back(PinDefinition{std::string(pinIds::kExec), "", PinDirection::Out, PinKind::Exec,
                                 std::nullopt, false, std::nullopt, std::nullopt});
    pins.push_back(PinDefinition{
        pinIds::parameter(std::string(kRemoteSenderName)), std::string(kRemoteSenderName),
        PinDirection::Out, PinKind::Data, PinTypeExpr::concrete(std::string(scriptName)), false,
        std::nullopt, "The object that raised the event."});
    for (auto& pin : tail) pins.push_back(std::move(pin));

    NodeDefinition definition;
    definition.id = remoteEventId(scriptName, eventName);
    definition.kind = GraphNodeKind::EventEntry;
    definition.title = std::string(scriptName) + "." + std::string(eventName);
    definition.category = std::string(scriptName);
    definition.summary = std::move(summary);
    definition.ownerScript = std::string(scriptName);
    definition.memberName = std::string(eventName);
    definition.isRemoteEvent = true;
    definition.pins = std::move(pins);
    return definition;
}

NodeDefinition NodePalette::propertyGetDefinition(std::string_view scriptName,
                                                  const papyrus::PropertyDecl& property) {
    NodeDefinition definition;
    definition.id = propertyGetId(scriptName, property.name);
    definition.kind = GraphNodeKind::PropertyGet;
    definition.title = "Get " + property.name;
    definition.category = std::string(scriptName);
    definition.ownerScript = std::string(scriptName);
    definition.memberName = property.name;
    definition.isPure = true;
    definition.localNameHint = localNameHint(property.name);
    definition.pins = {
        PinDefinition{std::string(pinIds::kSelf), "Target", PinDirection::In, PinKind::Data,
                      PinTypeExpr::concrete(std::string(scriptName)), false, std::nullopt,
                      std::nullopt},
        PinDefinition{std::string(pinIds::kValue), property.name, PinDirection::Out, PinKind::Data,
                      typeOf(&property.type), false, std::nullopt, std::nullopt},
    };
    return definition;
}

NodeDefinition NodePalette::propertySetDefinition(std::string_view scriptName,
                                                  const papyrus::PropertyDecl& property) {
    NodeDefinition definition;
    definition.id = propertySetId(scriptName, property.name);
    definition.kind = GraphNodeKind::PropertySet;
    definition.title = "Set " + property.name;
    definition.category = std::string(scriptName);
    definition.ownerScript = std::string(scriptName);
    definition.memberName = property.name;
    definition.pins = {
        PinDefinition{std::string(pinIds::kExec), "", PinDirection::In, PinKind::Exec, std::nullopt,
                      false, std::nullopt, std::nullopt},
        PinDefinition{std::string(pinIds::kSelf), "Target", PinDirection::In, PinKind::Data,
                      PinTypeExpr::concrete(std::string(scriptName)), false, std::nullopt,
                      std::nullopt},
        PinDefinition{std::string(pinIds::kValue), property.name, PinDirection::In, PinKind::Data,
                      typeOf(&property.type), false, std::nullopt, std::nullopt},
        PinDefinition{std::string(pinIds::kThen), "", PinDirection::Out, PinKind::Exec, std::nullopt,
                      false, std::nullopt, std::nullopt},
    };
    return definition;
}

PinDefinition NodePalette::parameterPin(const papyrus::Parameter& parameter,
                                        const WikiFunctionDoc* docs) {
    std::optional<std::string> description;
    if (docs) {
        const auto found = docs->parameters.find(lower(parameter.name));
        if (found != docs->parameters.end()) description = found->second.description;
    }

    return PinDefinition{
        pinIds::argument(parameter.name), parameter.name, PinDirection::In, PinKind::Data,
        typeOf(&parameter.type), parameter.defaultValue != nullptr,
        defaultText(parameter.defaultValue.get()), std::move(description)};
}

PinDefinition NodePalette::customEventArgsPin() const {
    return PinDefinition{
        pinIds::parameter("akArgs"), "akArgs", PinDirection::Out, PinKind::Data,
        PinTypeExpr::concrete("Var", true), false, std::nullopt,
        "The values the sender passed to SendCustomEvent."};
}

}
