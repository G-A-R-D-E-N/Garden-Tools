#include <fo4ide/papyrus/Symbols.hpp>

#include <cctype>
#include <string_view>

namespace fo4ide::papyrus {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool isWhitespace(std::string_view text) {
    for (const char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

SymbolKind kindOf(const Declaration& decl) {
    if (dynamic_cast<const Script*>(&decl)) return SymbolKind::Script;
    if (dynamic_cast<const FunctionDecl*>(&decl)) return SymbolKind::Function;
    if (dynamic_cast<const EventDecl*>(&decl)) return SymbolKind::Event;
    if (dynamic_cast<const PropertyDecl*>(&decl)) return SymbolKind::Property;
    if (dynamic_cast<const StructDecl*>(&decl)) return SymbolKind::Struct;
    if (dynamic_cast<const GroupDecl*>(&decl)) return SymbolKind::Group;
    if (dynamic_cast<const StateDecl*>(&decl)) return SymbolKind::State;
    if (dynamic_cast<const CustomEventDecl*>(&decl)) return SymbolKind::CustomEvent;
    if (dynamic_cast<const Parameter*>(&decl)) return SymbolKind::Parameter;
    return SymbolKind::Variable;
}

Symbol make(const Declaration& decl, SymbolKind kind,
            const std::optional<std::string>& container,
            const std::optional<std::string>& file) {
    Symbol symbol;
    symbol.name = decl.name;
    symbol.kind = kind;
    symbol.signature = decl.signature();
    symbol.documentation = decl.documentation;
    symbol.container = container;
    symbol.file = file;
    symbol.span = decl.span;
    symbol.nameSpan = decl.nameSpan;
    return symbol;
}

Symbol localSymbol(const DefineStatement& define, const CallableDecl* callable,
                   const std::optional<std::string>& file) {
    Symbol symbol;
    symbol.name = define.name;
    symbol.kind = SymbolKind::Local;
    symbol.signature = define.type.toString() + " " + define.name;
    if (callable) symbol.container = callable->name;
    symbol.file = file;
    symbol.span = define.span;
    symbol.nameSpan = define.nameSpan;
    return symbol;
}

// A child of a node in the C# enumeration order: either a node or a type ref.
struct ChildRef {
    const Node* node{nullptr};
    const TypeRef* typeRef{nullptr};
};

void childrenOf(const Node& node, std::vector<ChildRef>& out) {
    switch (node.nodeKind()) {
        case NodeKind::Script: {
            const auto& script = static_cast<const Script&>(node);
            for (const auto& i : script.imports) out.push_back({&i, nullptr});
            for (const auto& s : script.structs) out.push_back({&s, nullptr});
            for (const auto& c : script.customEvents) out.push_back({&c, nullptr});
            for (const auto& v : script.variables) out.push_back({&v, nullptr});
            for (const auto& g : script.groups) out.push_back({&g, nullptr});
            // Grouped properties live under their group, exactly like the C#
            // `Properties.Where(p => p.GroupName == null)`.
            for (const auto& p : script.properties) {
                if (!p.groupName) out.push_back({&p, nullptr});
            }
            for (const auto& f : script.functions) out.push_back({&f, nullptr});
            for (const auto& e : script.events) out.push_back({&e, nullptr});
            for (const auto& s : script.states) out.push_back({&s, nullptr});
            break;
        }

        case NodeKind::VariableDecl: {
            const auto& decl = static_cast<const VariableDecl&>(node);
            out.push_back({nullptr, &decl.type});
            if (decl.initializer) out.push_back({decl.initializer.get(), nullptr});
            break;
        }

        case NodeKind::StructDecl: {
            const auto& decl = static_cast<const StructDecl&>(node);
            for (const auto& member : decl.members) out.push_back({&member, nullptr});
            break;
        }

        case NodeKind::Parameter: {
            const auto& parameter = static_cast<const Parameter&>(node);
            out.push_back({nullptr, &parameter.type});
            if (parameter.defaultValue) out.push_back({parameter.defaultValue.get(), nullptr});
            break;
        }

        case NodeKind::FunctionDecl: {
            const auto& decl = static_cast<const FunctionDecl&>(node);
            if (decl.returnType) out.push_back({nullptr, &*decl.returnType});
            for (const auto& p : decl.parameters) out.push_back({&p, nullptr});
            for (const auto& statement : decl.body) out.push_back({statement.get(), nullptr});
            break;
        }

        case NodeKind::EventDecl: {
            const auto& decl = static_cast<const EventDecl&>(node);
            for (const auto& p : decl.parameters) out.push_back({&p, nullptr});
            for (const auto& statement : decl.body) out.push_back({statement.get(), nullptr});
            break;
        }

        case NodeKind::PropertyDecl: {
            const auto& decl = static_cast<const PropertyDecl&>(node);
            out.push_back({nullptr, &decl.type});
            if (decl.initializer) out.push_back({decl.initializer.get(), nullptr});
            if (decl.getter) out.push_back({&*decl.getter, nullptr});
            if (decl.setter) out.push_back({&*decl.setter, nullptr});
            break;
        }

        case NodeKind::GroupDecl: {
            const auto& decl = static_cast<const GroupDecl&>(node);
            for (const auto* property : decl.properties) out.push_back({property, nullptr});
            break;
        }

        case NodeKind::StateDecl: {
            const auto& decl = static_cast<const StateDecl&>(node);
            for (const auto& f : decl.functions) out.push_back({&f, nullptr});
            for (const auto& e : decl.events) out.push_back({&e, nullptr});
            break;
        }

        case NodeKind::DefineStatement: {
            const auto& define = static_cast<const DefineStatement&>(node);
            out.push_back({nullptr, &define.type});
            if (define.initializer) out.push_back({define.initializer.get(), nullptr});
            break;
        }

        case NodeKind::AssignStatement: {
            const auto& assign = static_cast<const AssignStatement&>(node);
            out.push_back({assign.target.get(), nullptr});
            out.push_back({assign.value.get(), nullptr});
            break;
        }

        case NodeKind::ExpressionStatement: {
            const auto& statement = static_cast<const ExpressionStatement&>(node);
            out.push_back({statement.expression.get(), nullptr});
            break;
        }

        case NodeKind::ReturnStatement: {
            const auto& statement = static_cast<const ReturnStatement&>(node);
            if (statement.value) out.push_back({statement.value.get(), nullptr});
            break;
        }

        case NodeKind::IfStatement: {
            // IfBranch reports the same NodeKind; it carries a condition and a
            // body instead of branches.
            if (const auto* branch = dynamic_cast<const IfBranch*>(&node)) {
                out.push_back({branch->condition.get(), nullptr});
                for (const auto& statement : branch->body) out.push_back({statement.get(), nullptr});
                break;
            }
            const auto& statement = static_cast<const IfStatement&>(node);
            for (const auto& branch : statement.branches) out.push_back({&branch, nullptr});
            for (const auto& statementChild : statement.elseBody) {
                out.push_back({statementChild.get(), nullptr});
            }
            break;
        }

        case NodeKind::WhileStatement: {
            const auto& statement = static_cast<const WhileStatement&>(node);
            out.push_back({statement.condition.get(), nullptr});
            for (const auto& child : statement.body) out.push_back({child.get(), nullptr});
            break;
        }

        case NodeKind::MemberExpression: {
            const auto& member = static_cast<const MemberExpression&>(node);
            out.push_back({member.target.get(), nullptr});
            break;
        }

        case NodeKind::IndexExpression: {
            const auto& index = static_cast<const IndexExpression&>(node);
            out.push_back({index.target.get(), nullptr});
            out.push_back({index.index.get(), nullptr});
            break;
        }

        case NodeKind::Argument: {
            const auto& argument = static_cast<const Argument&>(node);
            out.push_back({argument.value.get(), nullptr});
            break;
        }

        case NodeKind::CallExpression: {
            const auto& call = static_cast<const CallExpression&>(node);
            out.push_back({call.callee.get(), nullptr});
            for (const auto& argument : call.arguments) out.push_back({&argument, nullptr});
            break;
        }

        case NodeKind::UnaryExpression: {
            const auto& unary = static_cast<const UnaryExpression&>(node);
            out.push_back({unary.operand.get(), nullptr});
            break;
        }

        case NodeKind::BinaryExpression: {
            const auto& binary = static_cast<const BinaryExpression&>(node);
            out.push_back({binary.left.get(), nullptr});
            out.push_back({binary.right.get(), nullptr});
            break;
        }

        case NodeKind::CastExpression: {
            const auto& cast = static_cast<const CastExpression&>(node);
            out.push_back({cast.operand.get(), nullptr});
            out.push_back({nullptr, &cast.type});
            break;
        }

        case NodeKind::TypeCheckExpression: {
            const auto& check = static_cast<const TypeCheckExpression&>(node);
            out.push_back({check.operand.get(), nullptr});
            out.push_back({nullptr, &check.type});
            break;
        }

        case NodeKind::NewArrayExpression: {
            const auto& newArray = static_cast<const NewArrayExpression&>(node);
            out.push_back({nullptr, &newArray.elementType});
            out.push_back({newArray.size.get(), nullptr});
            break;
        }

        case NodeKind::NewStructExpression: {
            const auto& newStruct = static_cast<const NewStructExpression&>(node);
            out.push_back({nullptr, &newStruct.type});
            break;
        }

        default:
            break;
    }
}

bool walk(const Node* node, int offset, std::vector<AstPathEntry>& path) {
    if (!node->span.contains(offset)) return false;
    path.push_back({node, nullptr});

    std::vector<ChildRef> children;
    childrenOf(*node, children);
    for (const auto& child : children) {
        if (child.node) {
            if (walk(child.node, offset, path)) return true;
        } else if (child.typeRef && child.typeRef->span.contains(offset)) {
            path.push_back({nullptr, child.typeRef});
            return true;
        }
    }
    return true;
}

const CallableDecl* enclosingCallable(const std::vector<AstPathEntry>& path) {
    for (auto i = path.size(); i-- > 0;) {
        const auto& entry = path[i];
        if (!entry.node) continue;
        if (const auto* callable = dynamic_cast<const CallableDecl*>(entry.node)) {
            return callable;
        }
    }
    return nullptr;
}

void collectDefines(const std::vector<StatementPtr>& body, std::vector<const DefineStatement*>& out) {
    for (const auto& statement : body) {
        switch (statement->nodeKind()) {
            case NodeKind::DefineStatement:
                out.push_back(static_cast<const DefineStatement*>(statement.get()));
                break;
            case NodeKind::IfStatement: {
                const auto* iff = static_cast<const IfStatement*>(statement.get());
                for (const auto& branch : iff->branches) collectDefines(branch.body, out);
                collectDefines(iff->elseBody, out);
                break;
            }
            case NodeKind::WhileStatement:
                collectDefines(static_cast<const WhileStatement*>(statement.get())->body, out);
                break;
            default:
                break;
        }
    }
}

std::optional<Symbol> resolveTypeName(ScriptIndex& index, const Script& script,
                                      const std::string& name);

std::optional<Symbol> resolveIdentifier(ScriptIndex& index, const Script& script,
                                        const std::vector<AstPathEntry>& path,
                                        const std::string& name, int offset);

}  // namespace

std::string_view symbolKindName(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Script: return "Script";
        case SymbolKind::Function: return "Function";
        case SymbolKind::Event: return "Event";
        case SymbolKind::Property: return "Property";
        case SymbolKind::Variable: return "Variable";
        case SymbolKind::Struct: return "Struct";
        case SymbolKind::StructMember: return "StructMember";
        case SymbolKind::Group: return "Group";
        case SymbolKind::State: return "State";
        case SymbolKind::CustomEvent: return "CustomEvent";
        case SymbolKind::Parameter: return "Parameter";
        case SymbolKind::Local: return "Local";
        case SymbolKind::Import: return "Import";
    }
    return "";
}

std::vector<AstPathEntry> pathTo(const Script& script, int offset) {
    std::vector<AstPathEntry> path;
    walk(&script, offset, path);
    return path;
}

std::vector<Symbol> Symbols::documentSymbols(const Script& script) {
    std::vector<Symbol> symbols;
    symbols.push_back(make(script, SymbolKind::Script, std::nullopt, script.filePath));

    for (const auto& import : script.imports) {
        Symbol symbol;
        symbol.name = import.name;
        symbol.kind = SymbolKind::Import;
        symbol.signature = "Import " + import.name;
        symbol.file = script.filePath;
        symbol.span = import.span;
        symbol.nameSpan = import.nameSpan;
        symbols.push_back(std::move(symbol));
    }

    for (const auto& s : script.structs) {
        symbols.push_back(make(s, SymbolKind::Struct, script.name, script.filePath));
        for (const auto& member : s.members) {
            symbols.push_back(make(member, SymbolKind::StructMember, s.name, script.filePath));
        }
    }

    for (const auto& c : script.customEvents) {
        symbols.push_back(make(c, SymbolKind::CustomEvent, script.name, script.filePath));
    }
    for (const auto& g : script.groups) {
        symbols.push_back(make(g, SymbolKind::Group, script.name, script.filePath));
    }
    for (const auto& p : script.properties) {
        symbols.push_back(
            make(p, SymbolKind::Property, p.groupName.value_or(script.name), script.filePath));
    }
    for (const auto& v : script.variables) {
        symbols.push_back(make(v, SymbolKind::Variable, script.name, script.filePath));
    }
    for (const auto& f : script.functions) {
        symbols.push_back(make(f, SymbolKind::Function, script.name, script.filePath));
    }
    for (const auto& e : script.events) {
        symbols.push_back(make(e, SymbolKind::Event, script.name, script.filePath));
    }

    for (const auto& state : script.states) {
        symbols.push_back(make(state, SymbolKind::State, script.name, script.filePath));
        for (const auto& f : state.functions) {
            symbols.push_back(make(f, SymbolKind::Function, state.name, script.filePath));
        }
        for (const auto& e : state.events) {
            symbols.push_back(make(e, SymbolKind::Event, state.name, script.filePath));
        }
    }

    return symbols;
}

namespace {

std::optional<Symbol> resolveTypeName(ScriptIndex& index, const Script& script,
                                      const std::string& name) {
    if (name.empty()) return std::nullopt;

    const auto key = lower(name);
    if (key == "int" || key == "float" || key == "bool" || key == "string" || key == "var" ||
        key == "scripteventname" || key == "customeventname" || key == "structvarname") {
        return std::nullopt;
    }

    // The lexer accepts '#' as a ':'-equivalent in qualified names (decompiled
    // Script#Struct types), so the definition lookup honours both.
    const auto colon = name.find_last_of(":#");
    if (colon != std::string::npos && colon > 0) {
        const auto ownerName = name.substr(0, colon);
        const auto structName = name.substr(colon + 1);
        const Script* owner = index.resolve(ownerName);
        if (owner) {
            const Declaration* member = ScriptIndex::findMemberOn(owner, structName);
            if (const auto* s = dynamic_cast<const StructDecl*>(member)) {
                return make(*s, SymbolKind::Struct, owner->name, owner->filePath);
            }
        }
    } else {
        for (const auto& local : script.structs) {
            if (iequals(local.name, name)) {
                return make(local, SymbolKind::Struct, script.name, script.filePath);
            }
        }
    }

    const Script* target = index.resolve(name);
    if (!target) return std::nullopt;
    return make(*target, SymbolKind::Script, std::nullopt, target->filePath);
}

std::optional<Symbol> resolveIdentifier(ScriptIndex& index, const Script& script,
                                        const std::vector<AstPathEntry>& path,
                                        const std::string& name, int offset) {
    const CallableDecl* callable = enclosingCallable(path);
    if (callable) {
        for (const auto& parameter : callable->parameters) {
            if (iequals(parameter.name, name)) {
                return make(parameter, SymbolKind::Parameter, callable->name, script.filePath);
            }
        }

        std::vector<const DefineStatement*> defines;
        collectDefines(callable->body, defines);
        const DefineStatement* best = nullptr;
        for (const auto* define : defines) {
            if (define->span.start >= offset) continue;
            if (!iequals(define->name, name)) continue;
            if (!best || define->span.start > best->span.start) best = define;
        }
        if (best) return localSymbol(*best, callable, script.filePath);
    }

    const Script* owner = nullptr;
    const Declaration* member = index.findMember(&script, name, owner);
    if (member && owner) return make(*member, kindOf(*member), owner->name, owner->filePath);

    for (const auto& import : script.imports) {
        const Script* imported = index.resolve(import.name);
        if (!imported) continue;
        const Declaration* hit = ScriptIndex::findMemberOn(imported, name);
        if (hit) return make(*hit, kindOf(*hit), imported->name, imported->filePath);
    }

    return resolveTypeName(index, script, name);
}

}  // namespace

std::optional<Symbol> Symbols::findDefinition(ScriptIndex& index, const Script& script,
                                              int offset) {
    const auto path = pathTo(script, offset);
    if (path.empty()) return std::nullopt;

    for (auto i = path.size(); i-- > 0;) {
        const auto& entry = path[i];
        if (!entry.node) continue;
        if (const auto* decl = dynamic_cast<const Declaration*>(entry.node)) {
            if (decl->nameSpan.length > 0 && decl->nameSpan.contains(offset)) {
                return make(*decl, kindOf(*decl), script.name, script.filePath);
            }
        }
        if (const auto* define = dynamic_cast<const DefineStatement*>(entry.node)) {
            if (define->nameSpan.contains(offset)) {
                return localSymbol(*define, enclosingCallable(path), script.filePath);
            }
        }
    }

    const auto& innermost = path.back();
    if (innermost.typeRef) return resolveTypeName(index, script, innermost.typeRef->name);

    if (script.extends && script.extendsSpan.contains(offset)) {
        return resolveTypeName(index, script, *script.extends);
    }

    if (innermost.node) {
        if (const auto* identifier = dynamic_cast<const IdentifierExpression*>(innermost.node)) {
            return resolveIdentifier(index, script, path, identifier->name, offset);
        }
    }

    for (auto i = path.size(); i-- > 0;) {
        const auto& entry = path[i];
        if (!entry.node) continue;
        const auto* member = dynamic_cast<const MemberExpression*>(entry.node);
        if (!member) continue;
        if (!member->nameSpan.contains(offset)) continue;

        if (const auto* receiver = dynamic_cast<const IdentifierExpression*>(member->target.get())) {
            const Script* receiverScript = index.resolve(receiver->name);
            if (receiverScript) {
                const Declaration* onReceiver = ScriptIndex::findMemberOn(receiverScript, member->name);
                if (onReceiver) {
                    return make(*onReceiver, kindOf(*onReceiver), receiverScript->name,
                                receiverScript->filePath);
                }
            }
        }

        const Script* owner = nullptr;
        const Declaration* own = index.findMember(&script, member->name, owner);
        if (own && owner) return make(*own, kindOf(*own), owner->name, owner->filePath);
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::string> Symbols::hover(ScriptIndex& index, const Script& script, int offset) {
    const auto symbol = findDefinition(index, script, offset);
    if (!symbol) return std::nullopt;
    if (!symbol->documentation || isWhitespace(*symbol->documentation)) return symbol->signature;
    return symbol->signature + "\n\n" + trim(*symbol->documentation);
}

}  // namespace fo4ide::papyrus
