#pragma once

#include <optional>
#include <string>
#include <vector>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/script_index.hpp>

namespace fo4::papyrus {

enum class SymbolKind {
    Script,
    Function,
    Event,
    Property,
    Variable,
    Struct,
    StructMember,
    Group,
    State,
    CustomEvent,
    Parameter,
    Local,
    Import,
};

std::string_view symbolKindName(SymbolKind kind);

struct Symbol {
    std::string name;
    SymbolKind kind{SymbolKind::Script};
    std::string signature;
    std::optional<std::string> documentation;
    std::optional<std::string> container;
    std::optional<std::string> file;
    Span span;
    Span nameSpan;

    std::string toString() const {
        return std::string(symbolKindName(kind)) + " " + name;
    }
};

struct AstPathEntry {
    const Node* node{nullptr};
    const TypeRef* typeRef{nullptr};
};

std::vector<AstPathEntry> pathTo(const Script& script, int offset);

class Symbols {
public:
    static std::vector<Symbol> documentSymbols(const Script& script);

    static std::optional<Symbol> findDefinition(ScriptIndex& index, const Script& script,
                                                int offset);

    static std::optional<std::string> hover(ScriptIndex& index, const Script& script,
                                            int offset);
};

}
