#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4ide/archive/Ba2Reader.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/Pex.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/VanillaScriptSources.hpp>

namespace {

namespace fs = std::filesystem;
using fo4ide::archive::Ba2Reader;
using fo4ide::papyrus::Diagnostic;
using fo4ide::papyrus::Decompiler;
using fo4ide::papyrus::PapyrusCompileOptions;
using fo4ide::papyrus::PexFile;
using fo4ide::papyrus::PexObject;
using fo4ide::papyrus::Parser;
using fo4ide::papyrus::Script;
using fo4ide::papyrus::ScriptIndex;
using Bytes = std::vector<std::uint8_t>;

// The vanilla import roots for shipped scripts; a missing root must be skipped, not fatal.
constexpr std::string_view kImportFolders[] = {"Base", "User", "CreationClub", "DLC01", "DLC02",
                                               "DLC03", "DLC04", "DLC05", "DLC06"};

std::string lowerText(std::string text) {
    for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

bool sameText(const std::string& a, const std::string& b) { return lowerText(a) == lowerText(b); }

// FO4_DATA_DIR must name a Data folder; a missing Misc archive or Base folder skips the test.
std::optional<fs::path> vanillaDataDir() {
    fs::path data = "D:/SteamLibrary/steamapps/common/Fallout 4/Data";
    if (const char* env = std::getenv("FO4_DATA_DIR"); env != nullptr && *env != '\0') data = env;
    std::error_code error;
    if (!fs::exists(data / "Fallout4 - Misc.ba2", error)) return std::nullopt;
    if (!fs::exists(data / "Scripts" / "Source" / "Base", error)) return std::nullopt;
    return data;
}

struct Sample {
    std::size_t index{0};
    std::string name;  // lowercase file stem
};

std::vector<Sample> questSamples(const Ba2Reader& reader) {
    std::vector<Sample> out;
    for (std::size_t i = 0; i < reader.entryCount(); ++i) {
        auto path = lowerText(reader.entryPath(i));
        std::replace(path.begin(), path.end(), '\\', '/');
        if (!path.starts_with("scripts/") || !path.ends_with(".pex")) continue;
        const auto leaf = path.substr(path.find_last_of('/') + 1);
        const auto name = leaf.substr(0, leaf.size() - 4);
        const bool fragment = name.starts_with("qf_");
        const bool mainQuest = name.size() > 2 && name.starts_with("mq") &&
                               std::isdigit(static_cast<unsigned char>(name[2])) != 0;
        if (fragment || mainQuest) out.push_back(Sample{i, name});
    }
    return out;
}

Bytes extractBytes(const Ba2Reader& reader, std::size_t index) {
    const auto raw = reader.extract(index);
    Bytes bytes(raw.size());
    std::transform(raw.begin(), raw.end(), bytes.begin(),
                   [](std::byte b) { return static_cast<std::uint8_t>(b); });
    return bytes;
}

std::optional<PexFile> loadPex(const Ba2Reader& reader, std::size_t index, std::string& error) {
    try {
        return PexFile::fromBytes(extractBytes(reader, index));
    } catch (const std::exception& ex) {
        error = ex.what();
        return std::nullopt;
    }
}

std::size_t countOf(const std::string& text, std::string_view needle) {
    std::size_t count = 0;
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
    return count;
}

std::string firstFallback(const std::string& text) {
    const auto at = text.find("[decompile failed:");
    if (at == std::string::npos) return {};
    const auto end = text.find('\n', at);
    return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

std::string firstError(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == fo4ide::papyrus::Severity::Error) return diagnostic.toString();
    }
    return {};
}

struct Compiled {
    std::optional<PexFile> pex;
    std::string stage;   // "parse" or "compile" when pex is empty
    std::string error;
};

// Any error diagnostic must fail the compile, even when a PEX came out.
Compiled compileSource(ScriptIndex& index, const std::string& source, const std::string& name,
                       bool debugOnly = false) {
    Compiled out;
    Script script = Parser::parse(source, name + ".psc");
    if (script.hasErrors()) {
        out.stage = "parse";
        out.error = firstError(script.diagnostics);
        return out;
    }
    PapyrusCompileOptions options;
    options.emitDebugInfo = false;
    options.emitDebugOnlyCode = debugOnly;
    options.emitBetaOnlyCode = false;
    auto result = fo4ide::papyrus::compileScript(index, &script, name + ".psc", options);
    const auto errorText = firstError(result.diagnostics);
    if (!result.pex || !errorText.empty()) {
        out.stage = "compile";
        out.error = (result.sourcesComplete ? "" : "imports incomplete; ") + errorText;
        if (errorText.empty()) out.error += "no PEX produced";
        return out;
    }
    out.pex = std::move(*result.pex);
    return out;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string out = "[";
    for (const auto& name : names) out += (out.size() > 1 ? ", " : "") + name;
    return out + "]";
}

// Object-level comparison against the vanilla PEX; names must compare case-insensitively.
std::string comparePex(const PexFile& original, const PexFile& recompiled) {
    if (original.objects.size() != 1 || recompiled.objects.size() != 1) return "object count";
    const auto& a = original.objects[0];
    const auto& b = recompiled.objects[0];
    if (!sameText(a.name, b.name)) return "script name " + a.name + " vs " + b.name;
    if (!sameText(a.parentClassName, b.parentClassName)) {
        return "parent " + a.parentClassName + " vs " + b.parentClassName;
    }
    if (a.properties.size() != b.properties.size()) {
        return "property count " + std::to_string(a.properties.size()) + " vs " +
               std::to_string(b.properties.size());
    }
    for (std::size_t i = 0; i < a.properties.size(); ++i) {
        if (!sameText(a.properties[i].name, b.properties[i].name) ||
            !sameText(a.properties[i].type, b.properties[i].type)) {
            return "property " + a.properties[i].name + ":" + a.properties[i].type + " vs " +
                   b.properties[i].name + ":" + b.properties[i].type;
        }
    }
    const auto functionNames = [](const PexObject& object) {
        std::vector<std::string> names;
        for (const auto& state : object.states) {
            for (const auto& fn : state.functions) {
                names.push_back(lowerText(state.name + "." + fn.name));
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    const auto fa = functionNames(a);
    const auto fb = functionNames(b);
    if (fa != fb) {
        std::vector<std::string> onlyOriginal, onlyRecompiled;
        std::set_difference(fa.begin(), fa.end(), fb.begin(), fb.end(), std::back_inserter(onlyOriginal));
        std::set_difference(fb.begin(), fb.end(), fa.begin(), fa.end(), std::back_inserter(onlyRecompiled));
        return "function set differs; only original: " + joinNames(onlyOriginal) +
               "; only recompiled: " + joinNames(onlyRecompiled);
    }
    return {};
}

std::string shapeOf(const PexFile& pex, bool& alias, bool& properties, bool& states) {
    const auto& object = pex.objects.at(0);
    properties = !object.properties.empty();
    for (const auto& property : object.properties) {
        if (lowerText(property.type).ends_with("alias")) alias = true;
    }
    for (const auto& state : object.states) {
        if (!state.name.empty()) states = true;
    }
    return object.name;
}

struct Outcome {
    std::string name;
    std::string stage{"ok"};  // read | decompile | parse | compile | reparse | compare | ok
    std::string reason;
    bool fragment{false};
    bool alias{false};
    bool properties{false};
    bool states{false};
    bool mainQuest{false};
    std::size_t fallbacks{0};
};

Outcome roundTrip(const Ba2Reader& reader, const Sample& sample, ScriptIndex& index) {
    Outcome o;
    o.mainQuest = !sample.name.starts_with("qf_");
    std::string error;
    auto original = loadPex(reader, sample.index, error);
    if (!original || original->objects.size() != 1) {
        o.stage = "read";
        o.reason = original ? "objects=" + std::to_string(original->objects.size()) : error;
        return o;
    }
    o.fragment = sample.name.starts_with("qf_");
    o.name = shapeOf(*original, o.alias, o.properties, o.states);
    std::string source;
    try {
        source = Decompiler::toSource(*original);
    } catch (const std::exception& ex) {
        o.stage = "decompile";
        o.reason = ex.what();
        return o;
    }
    o.fallbacks = countOf(source, "[decompile failed:");
    if (o.fallbacks > 0) {
        o.stage = "decompile";
        o.reason = firstFallback(source);
        return o;
    }
    auto compiled = compileSource(index, source, o.name);
    if (!compiled.pex) {
        o.stage = compiled.stage;
        o.reason = compiled.error;
        return o;
    }
    PexFile reread;
    try {
        reread = PexFile::fromBytes(compiled.pex->toBytes());
    } catch (const std::exception& ex) {
        o.stage = "reparse";
        o.reason = ex.what();
        return o;
    }
    const auto diff = comparePex(*original, reread);
    if (!diff.empty()) {
        o.stage = "compare";
        o.reason = diff;
    }
    return o;
}

std::string insertTraceAfterFirstHeader(const std::string& source, const std::string& marker) {
    std::vector<std::string> lines;
    std::istringstream in(source);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string head = lines[i];
        head.erase(0, head.find_first_not_of(" \t"));
        const bool header = head.starts_with("Function ") || head.starts_with("Event ") ||
                            head.find(" Function ") != std::string::npos;
        if (!header || head.find(" Native") != std::string::npos) continue;
        if (i + 1 < lines.size() && lines[i + 1].find_first_not_of(" \t") != std::string::npos &&
            lines[i + 1].find_first_not_of(" \t") != std::string::npos &&
            lines[i + 1].substr(lines[i + 1].find_first_not_of(" \t")).starts_with("{")) {
            continue;
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i + 1),
                     "\tDebug.Trace(\"" + marker + "\")");
        std::string out;
        for (const auto& line : lines) out += line + "\n";
        return out;
    }
    return {};
}

// The bumped SetStage(N+1) text must appear in the redecompiled source.
std::optional<std::pair<std::string, std::string>> bumpSetStage(const std::string& source) {
    const auto lower = lowerText(source);
    const auto at = lower.find("setstage(");
    if (at == std::string::npos) return std::nullopt;
    const auto start = at + std::string("setstage(").size();
    auto end = start;
    while (end < source.size() && std::isdigit(static_cast<unsigned char>(source[end])) != 0) ++end;
    if (end == start || end - start > 6 || end >= source.size() || source[end] != ')') return std::nullopt;
    const auto value = std::stoi(source.substr(start, end - start)) + 1;
    const auto replacement = std::to_string(value);
    auto edited = source.substr(0, start) + replacement + source.substr(end);
    return std::make_pair(std::move(edited), "SetStage(" + replacement + ")");
}

std::string saveAndRedecompile(const PexFile& pex, const fs::path& file, Bytes& savedBytes) {
    savedBytes = pex.toBytes();
    fs::create_directories(file.parent_path());
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(savedBytes.data()),
                  static_cast<std::streamsize>(savedBytes.size()));
    }
    Bytes readBack(fs::file_size(file));
    std::ifstream in(file, std::ios::binary);
    in.read(reinterpret_cast<char*>(readBack.data()), static_cast<std::streamsize>(readBack.size()));
    if (readBack != savedBytes) throw std::runtime_error("saved PEX does not read back identically");
    return Decompiler::toSource(PexFile::fromBytes(readBack));
}

}  // namespace

TEST_CASE("QuestScriptReal.VanillaBaseImportsLoadForCompilation") {
    const auto data = vanillaDataDir();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");

    const auto base = fo4ide::papyrus::loadVanillaImportRoot(*data, "Base");
    REQUIRE(base.has_value());
    CHECK(base->sources.size() > 7000);

    ScriptIndex index;
    const auto added = fo4ide::papyrus::addVanillaImports(index, *data, kImportFolders);
    CHECK(added == std::size(kImportFolders));
    CHECK(index.resolve("Quest") != nullptr);
    CHECK(index.resolve("ReferenceAlias") != nullptr);
    CHECK(index.resolve("Debug") != nullptr);
}

TEST_CASE("QuestScriptReal.RecompilesVanillaQuestScripts") {
    const auto data = vanillaDataDir();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");

    const Ba2Reader reader = Ba2Reader::open(*data / "Fallout4 - Misc.ba2");
    ScriptIndex index;
    REQUIRE(fo4ide::papyrus::addVanillaImports(index, *data, kImportFolders) == std::size(kImportFolders));

    const auto samples = questSamples(reader);
    std::vector<Outcome> outcomes;
    outcomes.reserve(samples.size());
    for (const auto& sample : samples) outcomes.push_back(roundTrip(reader, sample, index));

    std::size_t fragments = 0, mainQuests = 0, decompiled = 0, recompiled = 0, passed = 0;
    std::size_t withProperties = 0, withAlias = 0, withStates = 0;
    std::vector<std::string> failures;
    std::ostringstream tsv;
    tsv << "name\tkind\tstage\treason\n";
    for (const auto& o : outcomes) {
        (o.fragment ? fragments : mainQuests) += 1;
        if (o.stage != "read" && o.stage != "decompile") ++decompiled;
        if (o.stage != "read" && o.stage != "decompile" && o.stage != "parse" && o.stage != "compile") ++recompiled;
        if (o.stage == "ok") ++passed;
        if (o.properties) ++withProperties;
        if (o.alias) ++withAlias;
        if (o.states) ++withStates;
        if (o.stage != "ok") {
            failures.push_back(o.name + " [" + o.stage + "] " + o.reason);
        }
        tsv << o.name << '\t' << (o.fragment ? "fragment" : "main") << '\t' << o.stage << '\t' << o.reason << '\n';
    }
    const auto report = fs::temp_directory_path() / "quest-script-real" / "sweep.tsv";
    fs::create_directories(report.parent_path());
    std::ofstream(report) << tsv.str();

    std::cout << "QuestScriptReal sweep: attempted=" << outcomes.size() << " fragments=" << fragments
              << " main=" << mainQuests << " decompiled=" << decompiled << " recompiled=" << recompiled
              << " roundTripOk=" << passed << " withProperties=" << withProperties
              << " withAliasProperty=" << withAlias << " withNamedStates=" << withStates
              << " report=" << report.string() << '\n';
    for (const auto& failure : failures) std::cout << "  FAIL " << failure << '\n';

    REQUIRE(fragments >= 50);
    REQUIRE(mainQuests >= 1);
    REQUIRE(withProperties > 0);
    REQUIRE(withAlias > 0);
    REQUIRE(withStates > 0);
    CHECK(failures.empty());
}

TEST_CASE("QuestScriptReal.EditedVanillaScriptSavesAndRedecompilesWithTheChange") {
    const auto data = vanillaDataDir();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");

    const Ba2Reader reader = Ba2Reader::open(*data / "Fallout4 - Misc.ba2");
    ScriptIndex index;
    REQUIRE(fo4ide::papyrus::addVanillaImports(index, *data, kImportFolders) == std::size(kImportFolders));
    const auto temp = fs::temp_directory_path() / "quest-script-real" / "edits";

    std::size_t traceEdits = 0;
    std::size_t setStageEdits = 0;
    for (const auto& sample : questSamples(reader)) {
        if (traceEdits >= 3 && setStageEdits >= 2) break;
        std::string error;
        const auto original = loadPex(reader, sample.index, error);
        if (!original || original->objects.size() != 1) continue;
        const auto name = original->objects[0].name;
        const auto source = Decompiler::toSource(*original);
        if (countOf(source, "[decompile failed:") > 0) continue;
        const auto baseline = compileSource(index, source, name, true);
        if (!baseline.pex) continue;
        Bytes baselineBytes = baseline.pex->toBytes();

        if (traceEdits < 3) {
            const std::string marker = "QuestScriptRealEdit" + std::to_string(traceEdits);
            const auto edited = insertTraceAfterFirstHeader(source, marker);
            if (!edited.empty()) {
                const auto compiled = compileSource(index, edited, name, true);
                REQUIRE(compiled.pex.has_value());
                Bytes saved;
                const auto redecompiled = saveAndRedecompile(*compiled.pex, temp / (name + ".pex"), saved);
                CHECK(redecompiled.find(marker) != std::string::npos);
                CHECK(saved != baselineBytes);
                std::cout << "edit trace " << name << " -> " << marker << " saved "
                          << saved.size() << " bytes\n";
                ++traceEdits;
            }
        }

        if (setStageEdits < 2) {
            if (const auto bumped = bumpSetStage(source)) {
                const auto compiled = compileSource(index, bumped->first, name, true);
                REQUIRE(compiled.pex.has_value());
                Bytes saved;
                const auto redecompiled = saveAndRedecompile(*compiled.pex, temp / (name + "_stage.pex"), saved);
                CHECK(redecompiled.find(bumped->second) != std::string::npos);
                CHECK(saved != baselineBytes);
                std::cout << "edit SetStage " << name << " -> " << bumped->second << " saved "
                          << saved.size() << " bytes\n";
                ++setStageEdits;
            }
        }
    }
    CHECK(traceEdits == 3);
    CHECK(setStageEdits == 2);
}
