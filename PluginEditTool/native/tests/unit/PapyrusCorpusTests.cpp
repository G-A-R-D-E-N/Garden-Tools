#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/CodeGenerator.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/Symbols.hpp>

// Mirrors the C# corpus gates (PapyrusCorpusTests.cs / VanillaSweep.cs): the
// real Fallout 4 base scripts are validated against the native front end, so
// a shipped script the Creation Kit compiles must parse, yield symbols and
// pass the duplicate declaration check. Gated on the same environment
// variables (FO4RE_PSC_CORPUS / FO4RE_PSC_ROOTS) so the sweep skips when a
// corpus is not configured, exactly like the C#.

namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
    if (value.size() < suffix.size()) return false;
    const auto tail = value.substr(value.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(tail[i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

std::vector<std::filesystem::path> rootsFromEnv(const char* variable) {
    std::vector<std::filesystem::path> roots;
    const char* raw = std::getenv(variable);
    if (!raw || !*raw) return roots;
    std::istringstream stream(raw);
    std::string part;
    while (std::getline(stream, part, ':')) {
        const auto cleaned = trim(part);
        if (!cleaned.empty() && std::filesystem::is_directory(cleaned)) {
            roots.emplace_back(cleaned);
        }
    }
    return roots;
}

std::vector<std::filesystem::path> enumeratePsc(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> stack{root};
    std::error_code ec;
    while (!stack.empty()) {
        const auto dir = stack.back();
        stack.pop_back();
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code statusEc;
            const auto& entry = *it;
            if (entry.is_symlink(statusEc)) continue;
            if (entry.is_directory(statusEc)) {
                stack.push_back(entry.path());
            } else if (entry.is_regular_file(statusEc) &&
                       endsWithIgnoreCase(entry.path().extension().string(), ".psc")) {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

TEST_CASE("The whole corpus parses without crashing") {
    const auto roots = rootsFromEnv("FO4RE_PSC_CORPUS");
    if (roots.empty()) {
        WARN("FO4RE_PSC_CORPUS is not set; corpus sweep skipped.");
        return;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& root : roots) {
        auto found = enumeratePsc(root);
        files.insert(files.end(), found.begin(), found.end());
    }

    int clean = 0;
    std::vector<std::string> withErrors;
    std::map<std::string, int> byCode;
    for (const auto& file : files) {
        std::optional<fo4ide::papyrus::Script> script;
        try {
            script = fo4ide::papyrus::Parser::parseFile(file);
        } catch (const std::exception& ex) {
            FAIL("Parsing threw on " << file.string() << ": " << ex.what());
        }
        if (!script || script->hasErrors()) {
            withErrors.push_back(file.filename().string());
            if (script) {
                for (const auto& diagnostic : script->diagnostics) {
                    if (diagnostic.severity == fo4ide::papyrus::Severity::Error) {
                        byCode[std::string(fo4ide::papyrus::codeString(diagnostic.code))]++;
                    }
                }
            }
            continue;
        }
        ++clean;
    }

    INFO(files.size() << " files, " << clean << " clean, " << withErrors.size()
                      << " with errors");
    for (const auto& [code, count] : byCode) {
        INFO("  " << code << ": " << count);
    }
    for (const auto& file : withErrors) {
        INFO("  " << file);
    }

    REQUIRE(!files.empty());
    const auto rate = static_cast<double>(clean) / static_cast<double>(files.size());
    REQUIRE(rate > 0.995);
}

TEST_CASE("The whole corpus yields symbols without crashing") {
    const auto roots = rootsFromEnv("FO4RE_PSC_CORPUS");
    if (roots.empty()) {
        WARN("FO4RE_PSC_CORPUS is not set; corpus sweep skipped.");
        return;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& root : roots) {
        auto found = enumeratePsc(root);
        files.insert(files.end(), found.begin(), found.end());
    }

    fo4ide::papyrus::ScriptIndex index;
    for (const auto& root : roots) index.addRoot(root);

    std::int64_t symbols = 0;
    for (const auto& file : files) {
        std::optional<fo4ide::papyrus::Script> script;
        try {
            script = fo4ide::papyrus::Parser::parseFile(file);
        } catch (const std::exception& ex) {
            FAIL("Parsing threw on " << file.string() << ": " << ex.what());
        }
        if (!script) continue;

        symbols += static_cast<std::int64_t>(
            fo4ide::papyrus::Symbols::documentSymbols(*script).size());

        const auto length = std::max(1, script->span.end());
        for (auto offset = 0; offset < length;
             offset += std::max(1, static_cast<int>(length) / 25)) {
            (void)fo4ide::papyrus::Symbols::hover(index, *script, offset);
        }
    }

    INFO(files.size() << " files, " << symbols << " symbols");
    REQUIRE(symbols > 0);
}

TEST_CASE("The duplicate check refuses nothing that ships") {
    const auto roots = rootsFromEnv("FO4RE_PSC_ROOTS");
    if (roots.empty()) {
        WARN("FO4RE_PSC_ROOTS is not set; sweep skipped.");
        return;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& root : roots) {
        auto found = enumeratePsc(root);
        files.insert(files.end(), found.begin(), found.end());
    }

    int parsed = 0;
    int unparseable = 0;
    std::vector<std::string> offenders;
    for (const auto& file : files) {
        std::optional<fo4ide::papyrus::Script> script;
        try {
            script = fo4ide::papyrus::Parser::parseFile(file);
        } catch (...) {
            ++unparseable;
            continue;
        }
        if (!script || script->hasErrors()) {
            ++unparseable;
            continue;
        }
        ++parsed;

        for (const auto& problem : fo4ide::papyrus::declarationCheck(*script)) {
            offenders.push_back(file.filename().string() + ": " + problem.message);
        }
    }

    INFO("SWEEP roots=" << roots.size() << " files=" << files.size()
                        << " parsed=" << parsed << " unparseable=" << unparseable
                        << " flagged=" << offenders.size());
    for (const auto& line : offenders) {
        INFO("  FLAGGED " << line);
    }

    REQUIRE(parsed > 0);
    REQUIRE(offenders.empty());
}
