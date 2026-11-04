#include <fo4/papyrus/analysis_service.hpp>

#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/file_walk.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/symbols.hpp>
#include <fo4/papyrus/type_checker.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <typeinfo>

namespace fo4::papyrus {

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

std::string trimEnd(std::string text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

std::string stripQuotes(std::string_view text) {
    auto cleaned = trim(text);
    if (cleaned.size() >= 2 && cleaned.front() == '"' && cleaned.back() == '"') {
        cleaned = trim(cleaned.substr(1, cleaned.size() - 2));
    }
    return cleaned;
}

std::vector<std::string> splitSemicolons(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(';', start);
        const auto piece = end == std::string_view::npos ? text.substr(start)
                                                         : text.substr(start, end - start);
        if (!piece.empty()) parts.emplace_back(piece);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

std::string fullPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return error ? path.string() : absolute.string();
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not be read");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("could not open for writing");
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

std::string padRight(std::string text, int width) {
    if (static_cast<int>(text.size()) < width) text.append(width - text.size(), ' ');
    return text;
}

void resolveSources(const std::string& source, std::vector<std::filesystem::path>& files,
                    std::optional<std::string>& error) {
    std::error_code errorCode;
    if (std::filesystem::is_regular_file(source, errorCode)) {
        if (!iequals(std::filesystem::path(source).extension().string(), ".psc")) {
            error = "'" + std::filesystem::path(source).filename().string() +
                    "' is not a .psc. This reads Papyrus source; use decompile_papyrus for a "
                    "compiled .pex.";
            return;
        }
        files.push_back(source);
        return;
    }
    if (std::filesystem::is_directory(source, errorCode)) {
        files = enumeratePapyrusFiles(source);
        return;
    }
    error = "Not found: '" + source + "'.";
}

PapyrusCompileResult compileFile(ScriptIndex& index, const std::filesystem::path& path,
                                 const PapyrusCompileOptions& options) {
    for (const auto& root : options.importRoots) index.addRoot(root);

    const Script* script = index.parseCached(path);
    if (!script) {
        PapyrusCompileResult result;
        result.script = nullptr;
        result.diagnostics.push_back(Diagnostic{DiagnosticCode::CannotEmit, Severity::Error,
                                                "Could not read the source.", Span{},
                                                path.string()});
        result.sourcesComplete = false;
        return result;
    }

    return compileScript(index, script, fullPath(path), options);
}

nlohmann::json diagnosticJson(const Diagnostic& d) {
    return {
        {"code", std::string(codeString(d.code))},
        {"severity", d.severity == Severity::Error ? "error" : "warning"},
        {"message", d.message},
        {"line", d.span.line},
        {"column", d.span.column},
        {"start", d.span.start},
        {"length", d.span.length},
    };
}

struct Hint {
    std::string code;
    std::string message;
    Span span;
};

nlohmann::json hintJson(const Hint& hint) {
    return {
        {"code", hint.code},
        {"severity", "hint"},
        {"message", hint.message},
        {"unnecessary", true},
        {"line", hint.span.line},
        {"column", hint.span.column},
        {"start", hint.span.start},
        {"length", hint.span.length},
    };
}

std::vector<Hint> unusedHints(ScriptIndex& index, const Script& script,
                              const Resolution& resolution) {
    std::vector<Hint> hints;
    for (const auto& import : script.imports) {
        const Script* imported = index.resolve(import.name);
        if (!imported) continue;
        const auto chain = index.baseChain(imported);
        const bool used = std::any_of(resolution.bindings.begin(), resolution.bindings.end(),
                                      [&chain](const auto& entry) {
                                          return std::find(chain.begin(), chain.end(),
                                                           entry.second.owner) != chain.end();
                                      });
        if (!used) {
            hints.push_back({"PAPH001", "Import '" + import.name + "' is never used.", import.nameSpan});
        }
    }

    for (const auto& [node, binding] : resolution.bindings) {
        if (binding.kind != BindingKind::Local || node->nodeKind() != NodeKind::DefineStatement) {
            continue;
        }
        const auto& define = *static_cast<const DefineStatement*>(node);
        const bool used = std::any_of(
            resolution.bindings.begin(), resolution.bindings.end(), [&define](const auto& entry) {
                return entry.first->nodeKind() == NodeKind::IdentifierExpression &&
                       entry.first->span.start > define.span.end() &&
                       entry.second.kind == BindingKind::Local &&
                       iequals(entry.second.name, define.name);
            });
        if (!used) {
            hints.push_back({"PAPH002", "Local '" + define.name + "' is never used.", define.nameSpan});
        }
    }

    std::sort(hints.begin(), hints.end(),
              [](const Hint& a, const Hint& b) { return a.span.start < b.span.start; });
    return hints;
}

std::string checkReply(int revision, bool complete, const std::vector<Diagnostic>& diagnostics,
                       const std::vector<Hint>& hints) {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& d : diagnostics) list.push_back(diagnosticJson(d));

    nlohmann::json hintList = nlohmann::json::array();
    for (const auto& hint : hints) hintList.push_back(hintJson(hint));

    nlohmann::json payload;
    payload["revision"] = revision;
    payload["complete"] = complete;
    payload["errorCount"] = diagnostics.size();
    payload["diagnostics"] = std::move(list);
    payload["hints"] = std::move(hintList);
    return payload.dump();
}

}  // namespace

int AnalysisService::lineColumnToOffset(std::string_view text, int line, int column) {
    if (line < 1 || column < 1) return -1;

    std::size_t offset = 0;
    int currentLine = 1;
    while (currentLine < line) {
        const auto newline = text.find('\n', offset);
        if (newline == std::string_view::npos) return -1;
        offset = newline + 1;
        ++currentLine;
    }

    const auto newline = text.find('\n', offset);
    const auto lineEnd = newline == std::string_view::npos ? text.size() : newline;
    const auto target = offset + static_cast<std::size_t>(column - 1);
    return target > lineEnd ? -1 : static_cast<int>(target);
}

std::string AnalysisService::check(const std::string& source, bool all, bool semantic,
                                   const std::string* imports,
                                   const std::vector<std::filesystem::path>& baseImports) {
    (void)all;
    if (isWhitespace(source)) return "No source given.";

    std::vector<std::filesystem::path> files;
    std::optional<std::string> error;
    resolveSources(source, files, error);
    if (error) return *error;
    if (files.empty()) return "No .psc files found under '" + source + "'.";

    std::optional<ScriptIndex> index;
    std::optional<Resolver> resolver;
    std::optional<TypeChecker> checker;
    if (semantic) {
        index.emplace();
        const auto start = std::filesystem::is_directory(source)
                               ? std::filesystem::path(source)
                               : std::filesystem::path(fullPath(source)).parent_path();
        for (const auto& root : naturalRootsFor(start)) index->addRoot(root);
        if (imports && !isWhitespace(*imports)) {
            for (const auto& root : splitSemicolons(*imports)) {
                const auto cleaned = stripQuotes(root);
                if (!cleaned.empty()) index->addRoot(cleaned);
            }
        }
        for (const auto& root : baseImports) index->addRoot(root);
        resolver.emplace(*index);
        checker.emplace(*index);
    }

    std::ostringstream detail;
    int clean = 0;
    int syntaxErrors = 0;
    int semanticErrors = 0;
    int incompleteSources = 0;
    int reported = 0;

    for (const auto& file : files) {
        std::optional<Script> parsedLocal;
        const Script* script = nullptr;
        std::string failure;
        try {
            if (index) {
                script = index->parseCached(file);
            } else {
                parsedLocal = Parser::parseFile(file);
                script = parsedLocal ? &*parsedLocal : nullptr;
            }
            if (!script) failure = "could not be parsed";
        } catch (const std::exception& ex) {
            failure = ex.what();
        }

        if (!script) {
            syntaxErrors++;
            if (reported++ < k_maxReportedFiles) {
                detail << file.string() << ": could not be read: " << failure << "\n";
            }
            continue;
        }

        std::vector<Diagnostic> parseErrors;
        for (const auto& d : script->diagnostics) {
            if (d.severity == Severity::Error) parseErrors.push_back(d);
        }
        if (!parseErrors.empty()) {
            syntaxErrors++;
            if (reported++ < k_maxReportedFiles) {
                for (const auto& d : parseErrors) detail << d.toString() << "\n";
            }
            continue;
        }

        if (!resolver) {
            clean++;
            continue;
        }

        std::vector<Diagnostic> found;
        bool sourcesComplete = true;
        try {
            const auto resolution = resolver->resolve(script);
            sourcesComplete = resolution.baseChainComplete;
            for (const auto& d : resolution.diagnostics) {
                if (d.severity == Severity::Error) found.push_back(d);
            }
            for (const auto& d : checker->check(resolution)) {
                if (d.severity == Severity::Error) found.push_back(d);
            }
        } catch (const std::exception& ex) {
            semanticErrors++;
            if (reported++ < k_maxReportedFiles) {
                detail << file.string() << ": " << typeid(ex).name() << ": " << ex.what() << "\n";
            }
            continue;
        }

        if (!sourcesComplete) incompleteSources++;
        if (found.empty()) {
            clean++;
            continue;
        }

        semanticErrors++;
        if (reported++ >= k_maxReportedFiles) continue;
        for (const auto& d : found) detail << d.toString() << "\n";
    }

    std::ostringstream summary;
    summary << "RESULT: " << clean << " clean, " << syntaxErrors << " with syntax errors";
    if (semantic) summary << ", " << semanticErrors << " with name or type errors";
    summary << ", of " << files.size() << " file(s).\n";

    if (!semantic) {
        summary << "Syntax only. This does not resolve names or check types, so a clean result does "
                   "not guarantee the script compiles. Set semantic=true, or use compile_papyrus.\n";
    } else if (incompleteSources > 0) {
        summary << incompleteSources
                << " file(s) referred to scripts that are not on the roots, so name and type "
                   "reporting was switched off for those -- otherwise every inherited member reads "
                   "as undefined. They are neither confirmed clean nor confirmed broken. Add the "
                   "missing root with 'imports'.\n";
    }

    if (reported > k_maxReportedFiles) {
        detail << "(" << (reported - k_maxReportedFiles) << " more file(s) not listed.)\n";
    }

    const auto detailText = trimEnd(detail.str());
    if (detailText.empty()) return trimEnd(summary.str());
    return trimEnd(summary.str()) + "\n\n" + detailText;
}

std::string AnalysisService::outline(const std::string& source) {
    if (isWhitespace(source)) return "No source given.";
    if (!std::filesystem::exists(source)) return "Not found: '" + source + "'.";

    std::optional<Script> script;
    try {
        script = Parser::parseFile(source);
    } catch (const std::exception& ex) {
        return "Could not read '" + source + "': " + ex.what();
    }
    if (!script) return "Could not read '" + source + "'.";

    const auto symbols = Symbols::documentSymbols(*script);
    std::ostringstream sb;
    sb << "RESULT: " << symbols.size() << " symbol(s) in "
       << std::filesystem::path(source).filename().string() << ".\n";

    int errors = 0;
    for (const auto& d : script->diagnostics) {
        if (d.severity == Severity::Error) ++errors;
    }
    if (errors > 0) sb << "(" << errors << " syntax error(s); the outline is what could still be "
                          "read.)\n";
    sb << "\n";

    for (const auto& symbol : symbols) {
        const auto where = "(" + std::to_string(symbol.span.line) + "," +
                           std::to_string(symbol.span.column) + ")";
        const auto container = symbol.container ? "  [" + *symbol.container + "]" : "";
        sb << padRight(where, 12) << " " << padRight(std::string(symbolKindName(symbol.kind)), 12)
           << " " << symbol.signature << container << "\n";
    }
    return trimEnd(sb.str());
}

std::string AnalysisService::definition(const std::string& source, int line, int column,
                                        const std::string* imports,
                                        const std::vector<std::filesystem::path>& baseImports) {
    if (isWhitespace(source)) return "No source given.";
    if (!std::filesystem::exists(source)) return "Not found: '" + source + "'.";

    std::string text;
    try {
        text = readFile(source);
    } catch (const std::exception& ex) {
        return "Could not read '" + source + "': " + ex.what();
    }

    const auto offset = lineColumnToOffset(text, line, column);
    if (offset < 0) {
        return "Position (" + std::to_string(line) + "," + std::to_string(column) +
               ") is outside '" + std::filesystem::path(source).filename().string() + "'.";
    }

    auto index = buildIndex(source, imports, baseImports);
    const Script* script = index.parseCached(source);
    std::optional<Script> parsedLocal;
    if (!script) {
        parsedLocal = Parser::parse(text, source);
        script = &*parsedLocal;
    }

    const auto symbol = Symbols::findDefinition(index, *script, offset);
    if (!symbol) {
        return "RESULT: not resolved.\n\n"
               "No declaration could be found for that position. This front end resolves by name "
               "(locals, parameters, this script's members, its Extends chain, imports, and script "
               "names); it has no type checker, so a member reached through an expression whose "
               "type is not written down cannot be resolved. It returns nothing rather than guess.";
    }

    std::ostringstream sb;
    sb << "RESULT: " << symbolKindName(symbol->kind) << " " << symbol->name << "\n";
    sb << symbol->signature << "\n";
    if (symbol->container) sb << "in: " << *symbol->container << "\n";
    sb << "at: " << (symbol->file ? *symbol->file : std::string("<same file>")) << "("
       << symbol->nameSpan.line << "," << symbol->nameSpan.column << ")\n";
    if (symbol->documentation && !isWhitespace(*symbol->documentation)) {
        sb << "\n" << trim(*symbol->documentation) << "\n";
    }
    return trimEnd(sb.str());
}

std::string AnalysisService::analyzeJson(const std::string& text, const std::string* filePath) {
    Script script;
    try {
        script = Parser::parse(text,
                               filePath ? std::optional<std::string>(*filePath) : std::nullopt);
    } catch (const std::exception& ex) {
        return nlohmann::json{{"error", ex.what()}}.dump();
    }

    int errorCount = 0;
    for (const auto& d : script.diagnostics) {
        if (d.severity == Severity::Error) ++errorCount;
    }

    nlohmann::json payload;
    payload["script"] = script.name;
    payload["extends"] = script.extends ? nlohmann::json(*script.extends) : nlohmann::json(nullptr);
    payload["errorCount"] = errorCount;

    nlohmann::json diagnostics = nlohmann::json::array();
    for (const auto& d : script.diagnostics) diagnostics.push_back(diagnosticJson(d));
    payload["diagnostics"] = std::move(diagnostics);

    nlohmann::json symbols = nlohmann::json::array();
    for (const auto& s : Symbols::documentSymbols(script)) {
        symbols.push_back({
            {"name", s.name},
            {"kind", std::string(symbolKindName(s.kind))},
            {"signature", s.signature},
            {"documentation",
             s.documentation ? nlohmann::json(*s.documentation) : nlohmann::json(nullptr)},
            {"container", s.container ? nlohmann::json(*s.container) : nlohmann::json(nullptr)},
            {"line", s.span.line},
            {"column", s.span.column},
            {"start", s.span.start},
            {"nameStart", s.nameSpan.start},
            {"nameLength", s.nameSpan.length},
            {"nameLine", s.nameSpan.line},
        });
    }
    payload["symbols"] = std::move(symbols);
    return payload.dump();
}

std::string AnalysisService::symbolAtJson(const std::string& text, const std::string* filePath,
                                          int offset, const std::string* imports,
                                          const std::vector<std::filesystem::path>& baseImports) {
    Script script;
    try {
        script = Parser::parse(text,
                               filePath ? std::optional<std::string>(*filePath) : std::nullopt);
    } catch (const std::exception& ex) {
        return nlohmann::json{{"error", ex.what()}}.dump();
    }

    auto index = filePath && !isWhitespace(*filePath)
                     ? buildIndex(*filePath, imports, baseImports)
                     : ScriptIndex();

    const auto symbol = Symbols::findDefinition(index, script, offset);
    if (!symbol) return nlohmann::json{{"resolved", false}}.dump();

    bool sameFile = false;
    if (!symbol->file) {
        sameFile = true;
    } else if (filePath) {
        sameFile = iequals(fullPath(*symbol->file), fullPath(*filePath));
    }

    nlohmann::json payload;
    payload["resolved"] = true;
    payload["name"] = symbol->name;
    payload["kind"] = std::string(symbolKindName(symbol->kind));
    payload["signature"] = symbol->signature;
    payload["documentation"] =
        symbol->documentation ? nlohmann::json(*symbol->documentation) : nlohmann::json(nullptr);
    payload["container"] =
        symbol->container ? nlohmann::json(*symbol->container) : nlohmann::json(nullptr);
    payload["file"] = symbol->file ? nlohmann::json(*symbol->file) : nlohmann::json(nullptr);
    payload["sameFile"] = sameFile;
    payload["line"] = symbol->nameSpan.line;
    payload["column"] = symbol->nameSpan.column;
    payload["start"] = symbol->nameSpan.start;
    payload["length"] = symbol->nameSpan.length;
    return payload.dump();
}

std::string AnalysisService::checkTextJson(const std::string& text, const std::string* filePath,
                                           int revision, const std::string* imports,
                                           const std::vector<std::filesystem::path>& baseImports) {
    const auto script =
        Parser::parse(text, filePath ? std::optional<std::string>(*filePath) : std::nullopt);

    std::vector<Diagnostic> syntax;
    for (const auto& d : script.diagnostics) {
        if (d.severity == Severity::Error) syntax.push_back(d);
    }
    if (!syntax.empty()) return checkReply(revision, false, syntax, {});

    auto index = buildIndex(filePath ? *filePath : std::string(), imports, baseImports);
    const auto resolution = Resolver(index).resolve(&script);
    if (!resolution.baseChainComplete) return checkReply(revision, false, {}, {});

    std::vector<Diagnostic> found;
    for (const auto& d : resolution.diagnostics) {
        if (d.severity == Severity::Error) found.push_back(d);
    }
    for (const auto& d : TypeChecker(index).check(resolution)) {
        if (d.severity == Severity::Error) found.push_back(d);
    }
    return checkReply(revision, true, found, unusedHints(index, script, resolution));
}

ScriptIndex AnalysisService::buildIndex(const std::string& sourceFile, const std::string* imports,
                                        const std::vector<std::filesystem::path>& baseImports) {
    ScriptIndex index;
    if (!isWhitespace(sourceFile)) {
        const auto folder = std::filesystem::path(fullPath(sourceFile)).parent_path();
        if (!folder.empty()) index.addRoot(folder);
    }
    if (imports && !isWhitespace(*imports)) {
        for (const auto& root : splitSemicolons(*imports)) {
            const auto cleaned = stripQuotes(root);
            if (!cleaned.empty()) index.addRoot(cleaned);
        }
    }
    for (const auto& root : baseImports) index.addRoot(root);
    return index;
}

std::vector<std::filesystem::path> AnalysisService::naturalRootsFor(
    const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> roots;
    const auto full = std::filesystem::absolute(folder).lexically_normal();
    roots.push_back(full);

    std::vector<std::string> parts;
    for (const auto& part : full) parts.push_back(part.string());

    for (std::size_t i = parts.size(); i-- > 1;) {
        if (!iequals(parts[i], "Source")) continue;
        std::filesystem::path sourceRoot;
        for (std::size_t j = 0; j <= i; ++j) sourceRoot /= parts[j];
        const auto user = sourceRoot / "User";
        if (std::filesystem::is_directory(user)) roots.push_back(user);
        roots.push_back(sourceRoot);
        return roots;
    }
    return roots;
}

std::string AnalysisService::compile(const std::string& source, const std::string* output,
                                     const std::string* imports, bool release, bool debugInfo,
                                     const std::string* flagFile,
                                     const std::vector<std::filesystem::path>& baseImports) {
    if (isWhitespace(source)) return "No source given.";

    auto trimmed = stripQuotes(source);
    if (trimmed.empty()) return "No source given.";

    if (std::filesystem::is_regular_file(trimmed) &&
        iequals(std::filesystem::path(trimmed).extension().string(), ".pas")) {
        return "That is a Papyrus ASSEMBLY listing (.pas), not source. Compile the .psc instead; "
               "decompile again with assembly=false to get one.";
    }

    std::vector<std::filesystem::path> files;
    std::optional<std::string> error;
    resolveSources(trimmed, files, error);
    if (error) return *error;
    if (files.empty()) return "No .psc files found under '" + trimmed + "'.";

    const auto rootFolder = std::filesystem::is_directory(trimmed)
                                ? std::filesystem::path(trimmed)
                                : std::filesystem::path(fullPath(trimmed)).parent_path();
    const auto outputFolder = output && !isWhitespace(*output)
                                  ? std::filesystem::path(stripQuotes(*output))
                                  : rootFolder;

    ScriptIndex index;
    for (const auto& root : naturalRootsFor(rootFolder)) index.addRoot(root);
    if (imports && !isWhitespace(*imports)) {
        for (const auto& root : splitSemicolons(*imports)) {
            const auto cleaned = stripQuotes(root);
            if (!cleaned.empty()) index.addRoot(cleaned);
        }
    }
    int baseRoots = 0;
    for (const auto& root : baseImports) {
        index.addRoot(root);
        ++baseRoots;
    }

    PapyrusCompileOptions options;
    options.emitDebugInfo = debugInfo;
    options.emitDebugOnlyCode = !release;
    options.emitBetaOnlyCode = !release;
    if (flagFile && !isWhitespace(*flagFile)) {
        options.flagFile = std::filesystem::path(stripQuotes(*flagFile));
    }

    std::ostringstream sb;
    int succeeded = 0;
    int failed = 0;
    int reported = 0;
    int incompleteSources = 0;

    for (const auto& file : files) {
        PapyrusCompileResult result;
        try {
            result = compileFile(index, file, options);
        } catch (const std::exception& ex) {
            failed++;
            if (reported++ < k_maxReportedFiles) {
                sb << file.string() << ": " << typeid(ex).name() << ": " << ex.what() << "\n";
            }
            continue;
        }

        if (!result.success()) {
            failed++;
            if (!result.sourcesComplete) incompleteSources++;
            if (reported++ >= k_maxReportedFiles) continue;
            int shown = 0;
            for (const auto& d : result.diagnostics) {
                if (d.severity != Severity::Error) continue;
                if (shown++ >= 10) break;
                sb << d.toString() << "\n";
            }
            continue;
        }

        std::string converted;
        for (const char ch : result.script->name) converted += (ch == ':' ? '/' : ch);
        const auto target = outputFolder / (converted + ".pex");
        try {
            std::filesystem::create_directories(target.parent_path());
            writeBytes(target, result.pex->toBytes());
            for (const auto& d : result.diagnostics) {
                if (d.severity == Severity::Warning) sb << d.toString() << "\n";
            }
            succeeded++;
        } catch (const std::exception& ex) {
            failed++;
            if (reported++ < k_maxReportedFiles) {
                sb << target.string() << ": could not be written: " << ex.what() << "\n";
            }
        }
    }

    std::ostringstream summary;
    summary << "RESULT: " << succeeded << " succeeded, " << failed << " failed, of " << files.size()
            << " file(s) (output -> " << outputFolder.string() << ")\n";
    summary << "engine : built-in (no Creation Kit)\n";
    if (release) summary << "mode   : release; DebugOnly and BetaOnly calls are not compiled\n";
    if (reported > k_maxReportedFiles) {
        summary << "(" << (reported - k_maxReportedFiles) << " more file(s) not listed.)\n";
    }

    if (incompleteSources > 0) {
        summary << "\n" << incompleteSources
                << " of the failures could not see every script they refer to. The back end "
                   "refuses rather than guessing: an unresolved callee has unknown arity once "
                   "optional parameters exist. Add the missing source root with 'imports' "
                   "(semicolon-separated).\n";
        if (baseRoots == 0) {
            summary << "No base-script root was detected on this machine at all, so every vanilla "
                       "type (Form, ObjectReference, Actor, ...) is unknown. The built-in engine "
                       "needs no PapyrusCompiler.exe, but it does need the vanilla base script "
                       "SOURCES to resolve against. Point Settings > Papyrus base imports, or "
                       "PAPYRUS_BASE_IMPORTS, at a folder of them.\n";
        }
    }

    const auto detail = trimEnd(sb.str());
    if (detail.empty()) return trimEnd(summary.str());
    return trimEnd(summary.str()) + "\n\n" + detail;
}

}  // namespace fo4::papyrus
