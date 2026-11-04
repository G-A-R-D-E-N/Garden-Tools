#include <fo4/papyrus/analysis_service.hpp>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/symbols.hpp>
#include <fo4/papyrus/token.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool isIdentifierChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool isIdentifier(std::string_view name) {
    if (name.empty()) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (std::isalpha(first) == 0 && first != '_') return false;
    for (const char ch : name) {
        if (!isIdentifierChar(ch)) return false;
    }
    return true;
}

std::optional<std::string> pathOf(const std::string* filePath) {
    if (!filePath || filePath->find_first_not_of(" \t\r\n") == std::string::npos) {
        return std::nullopt;
    }
    return *filePath;
}

std::string fullPath(const std::string& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute.string();
}

bool sameFile(const std::optional<std::string>& a, const std::optional<std::string>& b) {
    if (!a || !b) return !a && !b;
    return iequals(fullPath(*a), fullPath(*b));
}

bool underBaseRoot(const std::string& file, const std::vector<std::filesystem::path>& baseImports) {
    const std::filesystem::path target(fullPath(file));
    for (const auto& root : baseImports) {
        const auto relative = target.lexically_relative(fullPath(root.string()));
        if (!relative.empty() && relative.begin()->string() != "..") return true;
    }
    return false;
}

std::optional<std::string> readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void lineAndColumn(std::string_view text, int offset, int& line, int& column) {
    line = 1;
    std::size_t lineStart = 0;
    const auto limit = std::min(static_cast<std::size_t>(offset), text.size());
    for (std::size_t i = 0; i < limit; ++i) {
        const char ch = text[i];
        const bool crlf = ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n';
        if (ch == '\n' || (ch == '\r' && !crlf)) {
            ++line;
            lineStart = i + 1;
        }
    }
    column = offset - static_cast<int>(lineStart) + 1;
}

std::vector<int> wordOccurrences(std::string_view text, std::string_view word, int from, int to) {
    std::vector<int> starts;
    if (word.empty()) return starts;
    const auto limit = std::min(static_cast<std::size_t>(to), text.size());
    for (auto i = static_cast<std::size_t>(from); i + word.size() <= limit; ++i) {
        if (!iequals(text.substr(i, word.size()), word)) continue;
        if (i > 0 && isIdentifierChar(text[i - 1])) continue;
        const auto after = i + word.size();
        if (after < text.size() && isIdentifierChar(text[after])) continue;
        starts.push_back(static_cast<int>(i));
    }
    return starts;
}

std::string bareName(const std::string& name) {
    const auto split = name.find_last_of(":#");
    return split == std::string::npos ? name : name.substr(split + 1);
}

bool isSearchable(const Symbol& symbol) {
    return symbol.kind == SymbolKind::Script || symbol.kind == SymbolKind::Local ||
           symbol.kind == SymbolKind::Parameter;
}

std::optional<Symbol> targetAt(ScriptIndex& index, const Script& script, int offset) {
    if (auto symbol = Symbols::findDefinition(index, script, offset)) return symbol;
    for (const auto& import : script.imports) {
        if (!import.nameSpan.contains(offset)) continue;
        const Script* imported = index.resolve(import.name);
        if (!imported) return std::nullopt;
        Symbol symbol;
        symbol.name = imported->name;
        symbol.kind = SymbolKind::Script;
        symbol.signature = imported->signature();
        symbol.file = imported->filePath;
        symbol.span = imported->span;
        symbol.nameSpan = imported->nameSpan;
        return symbol;
    }
    return std::nullopt;
}

std::optional<Span> bodyOf(const Script& script, int offset) {
    std::optional<Span> body;
    for (const auto& entry : pathTo(script, offset)) {
        if (const auto* callable = dynamic_cast<const CallableDecl*>(entry.node)) {
            body = callable->span;
        }
    }
    return body;
}

struct Hit {
    std::optional<std::string> file;
    int start{0};
    int length{0};
    int line{1};
    int column{1};
    bool declaration{false};
};

bool importedHere(ScriptIndex& index, const Script& script, int offset, const Symbol& target) {
    if (target.kind != SymbolKind::Script) return false;
    for (const auto& import : script.imports) {
        if (!import.nameSpan.contains(offset)) continue;
        const Script* imported = index.resolve(import.name);
        return imported && sameFile(imported->filePath, target.file);
    }
    return false;
}

bool namesTarget(ScriptIndex& index, const Script& script, const std::optional<std::string>& file,
                 int offset, const Symbol& target, bool& declaration) {
    declaration = false;
    const auto found = Symbols::findDefinition(index, script, offset);
    if (!found) return importedHere(index, script, offset, target);
    if (found->kind != target.kind || !sameFile(found->file, target.file)) return false;
    if (target.kind != SymbolKind::Script && found->nameSpan.start != target.nameSpan.start) {
        return false;
    }
    declaration = sameFile(file, target.file) && found->nameSpan.start <= offset &&
                  offset < found->nameSpan.end();
    return true;
}

void collectHits(ScriptIndex& index, const Script& script, const std::optional<std::string>& file,
                 std::string_view text, int from, int to, const Symbol& target,
                 std::vector<Hit>& hits) {
    const auto word = bareName(target.name);
    for (const int start : wordOccurrences(text, word, from, to)) {
        bool declaration = false;
        if (!namesTarget(index, script, file, start, target, declaration)) continue;
        Hit hit;
        hit.file = file;
        hit.start = start;
        hit.length = static_cast<int>(word.size());
        lineAndColumn(text, start, hit.line, hit.column);
        hit.declaration = declaration;
        hits.push_back(std::move(hit));
    }
}

void collectOtherScripts(ScriptIndex& index, const std::optional<std::string>& bufferFile,
                         const Symbol& target, const std::vector<std::filesystem::path>& baseImports,
                         std::vector<Hit>& hits) {
    const auto word = bareName(target.name);
    for (const auto& name : index.scriptNames()) {
        const auto path = index.findFile(name);
        if (!path) continue;
        const std::optional<std::string> file = fullPath(path->string());
        if (sameFile(file, bufferFile) || underBaseRoot(*file, baseImports)) continue;
        const auto content = readText(*file);
        if (!content || wordOccurrences(*content, word, 0, static_cast<int>(content->size())).empty()) {
            continue;
        }
        const Script* candidate = nullptr;
        try {
            candidate = index.parseCached(*path);
        } catch (const std::exception&) {
            continue;
        }
        if (!candidate) continue;
        collectHits(index, *candidate, file, *content, 0, static_cast<int>(content->size()), target,
                    hits);
    }
}

std::vector<Hit> findHits(ScriptIndex& index, const Script& script,
                          const std::optional<std::string>& file, const std::string& text,
                          const Symbol& target, const std::vector<std::filesystem::path>& baseImports) {
    std::vector<Hit> hits;
    if (target.kind == SymbolKind::Script) {
        collectHits(index, script, file, text, 0, static_cast<int>(text.size()), target, hits);
        collectOtherScripts(index, file, target, baseImports, hits);
        return hits;
    }
    if (const auto body = bodyOf(script, target.nameSpan.start)) {
        collectHits(index, script, file, text, body->start, body->end(), target, hits);
    }
    return hits;
}

bool collides(ScriptIndex& index, const Script& script, const std::string& text, const Span& body,
              const Symbol& target, const std::string& newName) {
    for (const int start : wordOccurrences(text, newName, body.start, body.end())) {
        const auto found = Symbols::findDefinition(index, script, start);
        if (found && (found->kind == SymbolKind::Local || found->kind == SymbolKind::Parameter) &&
            found->nameSpan.start != target.nameSpan.start) {
            return true;
        }
    }
    return false;
}

nlohmann::json fileJson(const std::optional<std::string>& file) {
    return file ? nlohmann::json(*file) : nlohmann::json(nullptr);
}

nlohmann::json declarationJson(const Symbol& symbol) {
    return {
        {"name", symbol.name},
        {"kind", std::string(symbolKindName(symbol.kind))},
        {"signature", symbol.signature},
        {"file", fileJson(symbol.file)},
        {"line", symbol.nameSpan.line},
        {"column", symbol.nameSpan.column},
        {"start", symbol.nameSpan.start},
        {"length", symbol.nameSpan.length},
    };
}

std::string unresolved(const std::string& reason) {
    return nlohmann::json{{"resolved", false}, {"reason", reason}}.dump();
}

std::string refused(const std::string& reason) {
    return nlohmann::json{{"ok", false}, {"reason", reason}}.dump();
}

}  // namespace

std::string AnalysisService::referencesJson(const std::string& text, const std::string* filePath,
                                            int offset, const std::string* imports,
                                            const std::vector<std::filesystem::path>& baseImports) {
    const auto file = pathOf(filePath);
    const auto script = Parser::parse(text, file);
    auto index = buildIndex(file ? *file : std::string(), imports, baseImports);

    const auto target = targetAt(index, script, offset);
    if (!target) return unresolved("unresolved");
    if (!isSearchable(*target)) return unresolved("unsupported");

    nlohmann::json references = nlohmann::json::array();
    for (const auto& hit : findHits(index, script, file, text, *target, baseImports)) {
        references.push_back({
            {"file", fileJson(hit.file)},
            {"line", hit.line},
            {"column", hit.column},
            {"start", hit.start},
            {"length", hit.length},
            {"isDeclaration", hit.declaration},
        });
    }

    nlohmann::json payload;
    payload["declaration"] = declarationJson(*target);
    payload["references"] = std::move(references);
    return payload.dump();
}

std::string AnalysisService::renameJson(const std::string& text, const std::string* filePath,
                                        int offset, const std::string& newName,
                                        const std::string* imports,
                                        const std::vector<std::filesystem::path>& baseImports) {
    const auto file = pathOf(filePath);
    const auto script = Parser::parse(text, file);
    auto index = buildIndex(file ? *file : std::string(), imports, baseImports);

    const auto target = targetAt(index, script, offset);
    if (!target) return refused("No declaration is at that position.");
    if (!isSearchable(*target)) {
        return refused("Only locals, parameters and script names can be renamed; '" + target->name +
                       "' is a " + std::string(symbolKindName(target->kind)) + ".");
    }
    if (!isIdentifier(newName)) return refused("'" + newName + "' is not a Papyrus identifier.");
    TokenKind keyword{};
    if (Keywords::tryGet(newName, keyword)) return refused("'" + newName + "' is a Papyrus keyword.");

    if (target->kind == SymbolKind::Script) {
        if (target->file && underBaseRoot(*target->file, baseImports)) {
            return refused("'" + target->name + "' is a base script, so it is not renamed.");
        }
        const auto existing = index.findFile(newName);
        if (existing && !sameFile(fullPath(existing->string()), target->file)) {
            return refused("A script named '" + newName + "' already exists.");
        }
    } else if (const auto body = bodyOf(script, target->nameSpan.start)) {
        if (collides(index, script, text, *body, *target, newName)) {
            return refused("'" + newName + "' is already a local or parameter in this function.");
        }
    }

    nlohmann::json edits = nlohmann::json::array();
    for (const auto& hit : findHits(index, script, file, text, *target, baseImports)) {
        edits.push_back({
            {"file", fileJson(hit.file)},
            {"start", hit.start},
            {"length", hit.length},
            {"newText", newName},
        });
    }

    nlohmann::json fileRename;
    if (target->kind == SymbolKind::Script && target->file) {
        const std::filesystem::path from(*target->file);
        fileRename = nlohmann::json{
            {"from", from.string()},
            {"to", (from.parent_path() / (newName + ".psc")).string()},
        };
    }

    nlohmann::json payload;
    payload["ok"] = true;
    payload["edits"] = std::move(edits);
    payload["fileRename"] = fileRename;
    return payload.dump();
}

}  // namespace fo4::papyrus
