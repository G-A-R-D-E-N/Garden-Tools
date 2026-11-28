#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/builtin_node_definitions.hpp>
#include <fo4/graph/wiki_docs.hpp>
#include <fo4/papyrus/script_index.hpp>

namespace fo4::graph {

struct PaletteEntry {
    std::string id;
    std::string title;
    std::string category;
    GraphNodeKind kind{GraphNodeKind::Unknown};
    std::string signature;
    bool isPure{false};

    std::string toString() const { return id; }
};

struct PaletteSearchResult {
    std::vector<PaletteEntry> entries;
    int total{0};

    bool truncated() const { return static_cast<int>(entries.size()) < total; }
};

class NodePalette {
public:
    NodePalette(papyrus::ScriptIndex& index, WikiDocProvider* docs = nullptr);

    const std::vector<NodeDefinition>& builtins() const {
        return builtinDefinitions::all();
    }

    std::vector<std::string> scriptNames() const { return index_.scriptNames(); }

    WikiDocStats wikiStats() const {
        return docs_ ? docs_->stats() : WikiDocStats::empty();
    }

    const NodeDefinition* find(std::string_view definitionId);

    const std::vector<NodeDefinition>& forScript(std::string_view scriptName);

    PaletteSearchResult search(std::optional<std::string_view> query, int limit = 50,
                               std::optional<std::string_view> scriptFilter = std::nullopt);

    static constexpr std::string_view kRemoteSenderName = "akSender";

    static std::string remoteSignature(std::string_view scriptName, std::string_view eventName,
                                       std::optional<std::string_view> declaredSignature);
    static std::string localNameHint(std::string_view memberName);
    static std::string callId(std::string_view script, std::string_view member, bool isGlobal);
    static std::string eventId(std::string_view script, std::string_view member);
    static std::string remoteEventId(std::string_view script, std::string_view member);
    static std::string propertyGetId(std::string_view script, std::string_view member);
    static std::string propertySetId(std::string_view script, std::string_view member);
    static std::optional<std::string> ownerScriptOf(std::string_view definitionId);
    static std::optional<std::string> memberNameOf(std::string_view definitionId);

    static std::optional<std::string> defaultText(const papyrus::Node* expression);

private:
    std::vector<PaletteEntry> buildSearchIndex();
    const std::vector<PaletteEntry>& searchIndex();

    std::vector<NodeDefinition> buildForScript(std::string_view scriptName);
    NodeDefinition functionDefinition(std::string_view scriptName,
                                      const papyrus::FunctionDecl& function);
    NodeDefinition eventDefinition(std::string_view scriptName,
                                   const papyrus::EventDecl& declared);
    NodeDefinition remoteEventDefinition(std::string_view scriptName, std::string_view eventName,
                                         std::vector<PinDefinition> tail,
                                         std::optional<std::string> summary);
    NodeDefinition propertyGetDefinition(std::string_view scriptName,
                                         const papyrus::PropertyDecl& property);
    NodeDefinition propertySetDefinition(std::string_view scriptName,
                                         const papyrus::PropertyDecl& property);
    PinDefinition parameterPin(const papyrus::Parameter& parameter,
                               const WikiFunctionDoc* docs);
    PinDefinition customEventArgsPin() const;

    static bool isGlobalSignature(std::string_view signature);
    static PinTypeExpr typeOf(const papyrus::TypeRef* reference);

    papyrus::ScriptIndex& index_;
    WikiDocProvider* docs_;
    std::map<std::string, std::vector<NodeDefinition>, std::less<>> byScript_;
    std::optional<std::vector<PaletteEntry>> searchIndex_;
};

}
