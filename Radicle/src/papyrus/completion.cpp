#include <fo4/papyrus/analysis_service.hpp>

#include <fo4/papyrus/binding.hpp>
#include <fo4/papyrus/lexer.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/symbols.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::papyrus {

namespace {

constexpr std::string_view k_placeholder = "zzcompletion";
constexpr std::string_view k_arrayMembers[] = {
    "Length", "Find", "RFind", "FindStruct", "RFindStruct", "Add", "Insert",
    "Remove", "RemoveLast", "Clear", "GetMatchingStructs"};
constexpr std::string_view k_primitiveTypes[] = {"Bool", "Float", "Int", "String"};

std::string lowerCopy(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool isIdentifierChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::optional<std::string> pathOf(const std::string* filePath) {
    return filePath ? std::optional<std::string>(*filePath) : std::nullopt;
}

nlohmann::json nullableText(const std::optional<std::string>& text) {
    return text ? nlohmann::json(*text) : nlohmann::json(nullptr);
}

int clampOffset(int offset, const std::string& text) {
    return std::clamp(offset, 0, static_cast<int>(text.size()));
}

int wordStart(const std::string& text, int offset, bool allowColon) {
    int start = offset;
    while (start > 0) {
        const char ch = text[static_cast<std::size_t>(start - 1)];
        if (!isIdentifierChar(ch) && !(allowColon && ch == ':')) break;
        --start;
    }
    return start;
}

bool isTypeNameContext(TokenKind kind) {
    return kind == TokenKind::As || kind == TokenKind::Extends || kind == TokenKind::Import ||
           kind == TokenKind::New;
}

std::string withPlaceholder(const std::string& text, int offset) {
    const auto at = static_cast<std::size_t>(offset);
    return text.substr(0, at) + std::string(k_placeholder) + text.substr(at);
}

TokenKind tokenBefore(const std::vector<Token>& tokens, int position) {
    TokenKind kind = TokenKind::EndOfFile;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::EndOfFile) break;
        if (token.span.start + token.span.length > position) break;
        kind = token.kind;
    }
    return kind;
}

std::string_view contextName(TokenKind kind) {
    switch (kind) {
        case TokenKind::Dot: return "member";
        case TokenKind::As: return "as";
        case TokenKind::Extends: return "extends";
        case TokenKind::Import: return "import";
        case TokenKind::New: return "new";
        default: return "general";
    }
}

std::string inactiveSignature() {
    return nlohmann::json{{"active", false}}.dump();
}

std::string completionReply(std::string_view context, int start, int offset,
                            const nlohmann::json& items) {
    nlohmann::json payload;
    payload["context"] = std::string(context);
    payload["replaceStart"] = start;
    payload["replaceLength"] = offset - start;
    payload["items"] = items;
    return payload.dump();
}

class Completions {
public:
    explicit Completions(std::string prefix) : prefix_(lowerCopy(prefix)) {}

    void add(const std::string& label, std::string_view kind, std::string detail = "",
             std::optional<std::string> documentation = std::nullopt) {
        if (items_.size() >= static_cast<std::size_t>(AnalysisService::k_maxCompletions)) return;
        const auto key = lowerCopy(label);
        if (!key.starts_with(prefix_) || !seen_.insert(key).second) return;
        items_.push_back(
            Item{label, std::string(kind), std::move(detail), std::move(documentation)});
    }

    nlohmann::json toJson() const {
        nlohmann::json values = nlohmann::json::array();
        for (const auto& item : items_) {
            values.push_back({
                {"label", item.label},
                {"kind", item.kind},
                {"detail", item.detail},
                {"insertText", item.label},
                {"documentation", nullableText(item.documentation)},
            });
        }
        return values;
    }

private:
    struct Item {
        std::string label;
        std::string kind;
        std::string detail;
        std::optional<std::string> documentation;
    };

    std::string prefix_;
    std::set<std::string> seen_;
    std::vector<Item> items_;
};

enum class MemberScope { Globals, Foreign, Own };

void addOwnMembers(Completions& list, const Script& level, MemberScope scope) {
    for (const auto& f : level.functions) {
        if (scope != MemberScope::Globals || f.isGlobal) {
            list.add(f.name, "Function", f.signature(), f.documentation);
        }
    }
    if (scope == MemberScope::Globals) return;
    if (scope == MemberScope::Own) {
        for (const auto& e : level.events) {
            list.add(e.name, "Event", e.signature(), e.documentation);
        }
    }
    for (const auto& p : level.properties) {
        list.add(p.name, "Property", p.signature(), p.documentation);
    }
    if (scope != MemberScope::Own) return;
    for (const auto& v : level.variables) {
        list.add(v.name, "Variable", v.signature(), v.documentation);
    }
    for (const auto& c : level.customEvents) {
        list.add(c.name, "CustomEvent", c.signature(), c.documentation);
    }
}

void addChain(Completions& list, const std::vector<const Script*>& chain, MemberScope scope) {
    for (const Script* level : chain) addOwnMembers(list, *level, scope);
}

const Script* scriptNamed(ScriptIndex& index, const Script& script, const std::string& name) {
    if (lowerCopy(name) == lowerCopy(script.name)) return &script;
    return index.resolve(name);
}

void addStructFields(Completions& list, const Script& owner, const std::string& structName) {
    for (const auto& s : owner.structs) {
        if (lowerCopy(s.name) != lowerCopy(structName)) continue;
        for (const auto& member : s.members) {
            list.add(member.name, "StructMember", member.signature(), member.documentation);
        }
        return;
    }
}

void addArrayMembers(Completions& list) {
    for (const auto name : k_arrayMembers) {
        list.add(std::string(name), name == "Length" ? "Property" : "Function");
    }
}

void addMembers(Completions& list, ScriptIndex& index, const Script& script, const Node& target,
                const Resolution& resolution) {
    const auto* binding = resolution.bindingFor(&target);
    if (binding && binding->kind == BindingKind::Script && binding->owner) {
        addChain(list, index.baseChain(binding->owner), MemberScope::Globals);
        return;
    }

    const auto* type = resolution.typeOf(&target);
    if (!type) return;
    switch (type->kind) {
        case TypeKind::Object: {
            if (const Script* owner = scriptNamed(index, script, type->name)) {
                const bool ownScript = binding && (binding->kind == BindingKind::SelfKeyword ||
                                                   binding->kind == BindingKind::ParentKeyword);
                addChain(list, index.baseChain(owner),
                         ownScript ? MemberScope::Own : MemberScope::Foreign);
            }
            break;
        }
        case TypeKind::Struct: {
            const auto colon = type->name.find_last_of(':');
            if (colon == std::string::npos) break;
            if (const Script* owner = scriptNamed(index, script, type->name.substr(0, colon))) {
                addStructFields(list, *owner, type->name.substr(colon + 1));
            }
            break;
        }
        case TypeKind::Array:
            addArrayMembers(list);
            break;
        default:
            break;
    }
}

const MemberExpression* memberAt(const Script& script, int position) {
    const auto path = pathTo(script, position);
    for (auto i = path.size(); i-- > 0;) {
        const auto* member = dynamic_cast<const MemberExpression*>(path[i].node);
        if (member && member->nameSpan.contains(position)) return member;
    }
    return nullptr;
}

const CallableDecl* enclosingCallable(const std::vector<AstPathEntry>& path) {
    for (auto i = path.size(); i-- > 0;) {
        const auto* callable = dynamic_cast<const CallableDecl*>(path[i].node);
        if (callable) return callable;
    }
    return nullptr;
}

void collectDefines(const std::vector<StatementPtr>& body, int offset, Completions& list) {
    for (const auto& statement : body) {
        switch (statement->nodeKind()) {
            case NodeKind::DefineStatement: {
                const auto& define = static_cast<const DefineStatement&>(*statement);
                if (define.span.start < offset) {
                    list.add(define.name, "Local", define.type.toString() + " " + define.name);
                }
                break;
            }
            case NodeKind::IfStatement: {
                const auto& statementIf = static_cast<const IfStatement&>(*statement);
                for (const auto& branch : statementIf.branches) {
                    collectDefines(branch.body, offset, list);
                }
                collectDefines(statementIf.elseBody, offset, list);
                break;
            }
            case NodeKind::WhileStatement:
                collectDefines(static_cast<const WhileStatement&>(*statement).body, offset, list);
                break;
            default:
                break;
        }
    }
}

void addScripts(Completions& list, ScriptIndex& index) {
    for (const auto& name : index.scriptNames()) list.add(name, "Script");
}

void addPrimitiveTypes(Completions& list) {
    for (const auto name : k_primitiveTypes) list.add(std::string(name), "Type");
}

void addNewTypes(Completions& list, const Script& script, ScriptIndex& index) {
    addPrimitiveTypes(list);
    for (const Script* level : index.baseChain(&script)) {
        for (const auto& s : level->structs) {
            list.add(level == &script ? s.name : level->name + ":" + s.name, "Struct",
                     s.signature());
        }
    }
    addScripts(list, index);
}

void addKeywords(Completions& list) {
    std::vector<std::string> words;
    for (const auto& entry : Keywords::all()) words.push_back(entry.first);
    std::sort(words.begin(), words.end());
    for (const auto& word : words) list.add(word, "Keyword");
}

void addGeneral(Completions& list, ScriptIndex& index, const Script& script, int offset) {
    if (const auto* callable = enclosingCallable(pathTo(script, offset))) {
        for (const auto& parameter : callable->parameters) {
            list.add(parameter.name, "Parameter", parameter.signature(),
                     parameter.documentation);
        }
        collectDefines(callable->body, offset, list);
    }
    addChain(list, index.baseChain(&script), MemberScope::Own);
    addKeywords(list);
    addScripts(list, index);
}

struct EnclosingCall {
    int calleeStart{0};
    int activeParameter{0};
};

struct TextScan {
    std::vector<std::pair<std::size_t, int>> open;
    bool insideText{false};
};

std::size_t stringEnd(const std::string& text, std::size_t open) {
    std::size_t i = open + 1;
    while (i < text.size() && text[i] != '"' && text[i] != '\r' && text[i] != '\n') {
        i += (text[i] == '\\' && i + 1 < text.size()) ? 2 : 1;
    }
    return i;
}

TextScan scanTo(const std::string& text, int offset) {
    TextScan scan;
    const auto end = static_cast<std::size_t>(offset);
    for (std::size_t i = 0; i < end; ++i) {
        const char ch = text[i];
        if (ch == '"') {
            const auto close = stringEnd(text, i);
            if (close >= end) {
                scan.insideText = true;
                break;
            }
            i = close;
        } else if (ch == ';' && i + 1 < text.size() && text[i + 1] == '/') {
            const auto close = text.find("/;", i + 2);
            if (close == std::string::npos || close + 2 > end) {
                scan.insideText = true;
                break;
            }
            i = close + 1;
        } else if (ch == ';') {
            const auto newline = text.find_first_of("\r\n", i);
            if (newline >= end) {
                scan.insideText = true;
                break;
            }
            i = newline;
        } else if (ch == '{') {
            const auto close = text.find('}', i + 1);
            if (close == std::string::npos || close >= end) {
                scan.insideText = true;
                break;
            }
            i = close;
        } else if (ch == '(') {
            scan.open.emplace_back(i, 0);
        } else if (ch == ')') {
            if (!scan.open.empty()) scan.open.pop_back();
        } else if (ch == ',') {
            if (!scan.open.empty()) ++scan.open.back().second;
        }
    }
    return scan;
}

std::optional<EnclosingCall> findEnclosingCall(const std::string& text, int offset) {
    const auto scan = scanTo(text, offset);
    if (scan.insideText || scan.open.empty()) return std::nullopt;

    const auto [paren, commas] = scan.open.back();
    std::size_t nameEnd = paren;
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(text[nameEnd - 1]))) --nameEnd;
    std::size_t nameStart = nameEnd;
    while (nameStart > 0 && isIdentifierChar(text[nameStart - 1])) --nameStart;
    if (nameStart == nameEnd) return std::nullopt;
    return EnclosingCall{static_cast<int>(nameStart), commas};
}

const Node* calleeAt(const Script& script, int position) {
    const auto path = pathTo(script, position);
    for (auto i = path.size(); i-- > 0;) {
        const Node* node = path[i].node;
        const auto* identifier = dynamic_cast<const IdentifierExpression*>(node);
        if (identifier && identifier->span.start == position) return identifier;
        const auto* member = dynamic_cast<const MemberExpression*>(node);
        if (member && member->nameSpan.start == position) return member;
    }
    return nullptr;
}

}  // namespace

std::string AnalysisService::completionJson(const std::string& text, const std::string* filePath,
                                            int offset, const std::string* imports,
                                            const std::vector<std::filesystem::path>& baseImports) {
    offset = clampOffset(offset, text);
    const auto lexed = Lexer::lex(text);
    const auto typeStart = wordStart(text, offset, true);
    const auto typeContext = tokenBefore(lexed.tokens, typeStart);
    const bool qualifiedType = isTypeNameContext(typeContext);
    const auto start = qualifiedType ? typeStart : wordStart(text, offset, false);
    const auto before = qualifiedType ? typeContext : tokenBefore(lexed.tokens, start);

    if (scanTo(text, offset).insideText) {
        return completionReply("none", start, offset, nlohmann::json::array());
    }

    auto index = buildIndex(filePath ? *filePath : std::string(), imports, baseImports);
    Completions list(text.substr(static_cast<std::size_t>(start),
                                 static_cast<std::size_t>(offset - start)));

    if (before == TokenKind::Dot) {
        const auto script = Parser::parse(withPlaceholder(text, offset), pathOf(filePath));
        const auto resolution = Resolver(index).resolve(&script);
        if (const auto* member = memberAt(script, offset)) {
            addMembers(list, index, script, *member->target, resolution);
        }
    } else {
        const auto script = Parser::parse(text, pathOf(filePath));
        switch (before) {
            case TokenKind::As:
                addPrimitiveTypes(list);
                addScripts(list, index);
                break;
            case TokenKind::Extends:
            case TokenKind::Import:
                addScripts(list, index);
                break;
            case TokenKind::New:
                addNewTypes(list, script, index);
                break;
            default:
                addGeneral(list, index, script, offset);
                break;
        }
    }

    return completionReply(contextName(before), start, offset, list.toJson());
}

std::string AnalysisService::signatureHelpJson(
    const std::string& text, const std::string* filePath, int offset, const std::string* imports,
    const std::vector<std::filesystem::path>& baseImports) {
    offset = clampOffset(offset, text);
    const auto call = findEnclosingCall(text, offset);
    if (!call) return inactiveSignature();

    const auto script = Parser::parse(withPlaceholder(text, offset), pathOf(filePath));
    auto index = buildIndex(filePath ? *filePath : std::string(), imports, baseImports);
    const auto resolution = Resolver(index).resolve(&script);
    const auto* callee = calleeAt(script, call->calleeStart);
    const auto* binding = callee ? resolution.bindingFor(callee) : nullptr;
    const auto* callable =
        binding ? dynamic_cast<const CallableDecl*>(binding->declaration) : nullptr;
    if (!callable) return inactiveSignature();

    nlohmann::json parameters = nlohmann::json::array();
    for (const auto& parameter : callable->parameters) {
        parameters.push_back({{"label", parameter.signature()},
                              {"documentation", nullableText(parameter.documentation)}});
    }
    nlohmann::json signature{
        {"label", callable->signature()},
        {"documentation", nullableText(callable->documentation)},
        {"parameters", std::move(parameters)},
    };
    nlohmann::json signatures = nlohmann::json::array();
    signatures.push_back(std::move(signature));

    nlohmann::json payload;
    payload["active"] = true;
    payload["signatures"] = std::move(signatures);
    payload["activeSignature"] = 0;
    payload["activeParameter"] = call->activeParameter;
    return payload.dump();
}

}  // namespace fo4::papyrus
