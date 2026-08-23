#pragma once

#include <optional>
#include <string>
#include <vector>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusSymbolKind, in the same order as the C# enum so the names
// serialize identically.
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

// Port of PapyrusSymbol.
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

// One entry on the C# generic node path. TypeRefs are value members of the
// AST (not nodes), so a path entry is either a node or a type reference.
struct AstPathEntry {
    const Node* node{nullptr};
    const TypeRef* typeRef{nullptr};
};

// Port of PapyrusNode.PathTo: the chain of ancestors of the innermost node
// whose span contains `offset`, root first. Includes TypeRef entries where
// the C# tree carries a PapyrusTypeRef node. Empty when nothing contains the
// offset.
std::vector<AstPathEntry> pathTo(const Script& script, int offset);

// Port of PapyrusSymbols: document outline plus name-based definition lookup
// (locals, parameters, this script's members, the Extends chain, imports and
// script names). Resolves by name only -- there is no type checker, so a
// member reached through an expression whose type is not written down cannot
// be resolved, and null is returned rather than a guess.
class Symbols {
public:
    static std::vector<Symbol> documentSymbols(const Script& script);

    static std::optional<Symbol> findDefinition(ScriptIndex& index, const Script& script,
                                                int offset);

    static std::optional<std::string> hover(ScriptIndex& index, const Script& script,
                                            int offset);
};

}  // namespace fo4ide::papyrus
