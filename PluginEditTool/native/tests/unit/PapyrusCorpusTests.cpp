#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/CodeGenerator.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/Symbols.hpp>

#include "../helpers/CorpusRoots.hpp"

using fo4ide::papyrus::test::enumerateFiles;
using fo4ide::papyrus::test::rootsFromEnv;

TEST_CASE("The whole corpus parses without crashing") {
    const auto roots = rootsFromEnv("FO4RE_PSC_CORPUS");
    if (roots.empty()) SKIP("FO4RE_PSC_CORPUS is not set; corpus sweep skipped.");

    std::vector<std::filesystem::path> files;
    for (const auto& root : roots) {
        auto found = enumerateFiles(root, ".psc");
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
    if (roots.empty()) SKIP("FO4RE_PSC_CORPUS is not set; corpus sweep skipped.");

    std::vector<std::filesystem::path> files;
    for (const auto& root : roots) {
        auto found = enumerateFiles(root, ".psc");
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
    if (roots.empty()) SKIP("FO4RE_PSC_ROOTS is not set; sweep skipped.");

    std::vector<std::filesystem::path> files;
    for (const auto& root : roots) {
        auto found = enumerateFiles(root, ".psc");
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
