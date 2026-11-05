#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4ide/archive/Ba2Reader.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/Pex.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/VanillaScriptSources.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "RadiclePluginQuestInternal.hpp"
#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kMiscArchive = "Fallout4 - Misc.ba2";
constexpr std::size_t kSourceLimit = 4u * 1024u * 1024u;
constexpr std::size_t kPexLimit = 64u * 1024u * 1024u;
constexpr std::size_t kMaxNameLength = 64;
constexpr std::string_view kDecompileFailed = "[decompile failed:";

std::unexpected<Error> failure(std::string type, std::string message) {
    return std::unexpected(Error{std::move(type), std::move(message)});
}

bool identifierName(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLength) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (std::isalpha(first) == 0 && first != '_') return false;
    return std::all_of(name.begin(), name.end(), [](char ch) {
        const auto c = static_cast<unsigned char>(ch);
        return std::isalnum(c) != 0 || c == '_';
    });
}

bool sameName(std::string_view a, std::string_view b) {
    return support::asciiLower(a) == support::asciiLower(b);
}

bool insideDirectory(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    const auto canonical = fs::weakly_canonical(path, ec);
    if (ec || canonical.empty()) return false;
    const auto base = fs::weakly_canonical(root, ec);
    if (ec || base.empty()) return false;
    const auto end = std::mismatch(base.begin(), base.end(), canonical.begin(), canonical.end()).first;
    return end == base.end();
}

std::string readBoundedFile(const fs::path& path, std::size_t limit) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > limit) throw std::runtime_error("file exceeds its read limit: " + path.string());
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (size && !in.read(contents.data(), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return contents;
}

std::vector<std::uint8_t> toBytes(const std::string& raw) {
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

std::string decompilePex(const std::vector<std::uint8_t>& bytes, std::string_view name) {
    const auto pex = fo4ide::papyrus::PexFile::fromBytes(bytes);
    if (pex.objects.size() != 1) throw std::runtime_error("PEX must contain exactly one object");
    if (!sameName(pex.objects.front().name, name)) {
        throw std::runtime_error("PEX object " + pex.objects.front().name + " is not " + std::string(name));
    }
    auto source = fo4ide::papyrus::Decompiler::toSource(pex);
    const auto at = source.find(kDecompileFailed);
    if (at != std::string::npos) {
        const auto end = source.find('\n', at);
        throw std::runtime_error("decompilation failed: " + source.substr(at, end == std::string::npos
                                                                             ? std::string::npos
                                                                             : end - at));
    }
    return source;
}

struct ScriptSource {
    std::string text;
    std::string origin;
};

std::optional<ScriptSource> loadQuestScriptSource(const fs::path& dataDirectory,
                                                  const std::optional<fs::path>& imports,
                                                  std::string_view name) {
    std::error_code ec;
    if (imports) {
        const auto psc = *imports / (std::string(name) + ".psc");
        if (fs::is_regular_file(psc, ec)) return ScriptSource{readBoundedFile(psc, kSourceLimit), "imports_psc"};
        const auto pex = *imports / (std::string(name) + ".pex");
        if (fs::is_regular_file(pex, ec)) {
            const auto raw = readBoundedFile(pex, kPexLimit);
            return ScriptSource{decompilePex(toBytes(raw), name), "imports_pex"};
        }
    }
    if (dataDirectory.empty()) return std::nullopt;
    const auto archive = dataDirectory / std::string(kMiscArchive);
    if (!fs::is_regular_file(archive, ec)) return std::nullopt;
    const auto reader = fo4ide::archive::Ba2Reader::open(archive);
    const auto wanted = "scripts/" + support::asciiLower(name) + ".pex";
    for (std::size_t i = 0; i < reader.entryCount(); ++i) {
        auto path = support::asciiLower(reader.entryPath(i));
        std::replace(path.begin(), path.end(), '\\', '/');
        if (path != wanted) continue;
        const auto raw = reader.extract(i);
        std::vector<std::uint8_t> bytes(raw.size());
        std::transform(raw.begin(), raw.end(), bytes.begin(),
                       [](std::byte b) { return static_cast<std::uint8_t>(b); });
        return ScriptSource{decompilePex(bytes, name), "vanilla_pex"};
    }
    return std::nullopt;
}

struct TextEdit {
    std::string find;
    std::string replace;
};

std::expected<std::vector<TextEdit>, Error> parseEdits(const nlohmann::json& json) {
    if (!json.is_array() || json.empty()) return failure("QuestScriptEditFailed", "edits must be a nonempty array");
    std::vector<TextEdit> edits;
    for (const auto& item : json) {
        if (!item.is_object() || item.size() != 2 || !item.contains("find") || !item.contains("replace") ||
            !item["find"].is_string() || !item["replace"].is_string()) {
            return failure("QuestScriptEditFailed", "each edit must be an object with exactly find and replace strings");
        }
        auto find = item["find"].get<std::string>();
        if (find.empty()) return failure("QuestScriptEditFailed", "an edit find text must not be empty");
        edits.push_back({std::move(find), item["replace"].get<std::string>()});
    }
    return edits;
}

std::expected<std::string, Error> applyEdits(std::string text, const std::vector<TextEdit>& edits) {
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const auto& edit = edits[i];
        const auto label = "edit " + std::to_string(i) + ": ";
        std::size_t matches = 0;
        std::size_t position = std::string::npos;
        for (auto at = text.find(edit.find); at != std::string::npos; at = text.find(edit.find, at + 1)) {
            if (++matches == 1) position = at;
        }
        if (matches == 0) return failure("QuestScriptEditNotFound", label + "find text does not match");
        if (matches > 1) {
            return failure("QuestScriptEditAmbiguous",
                           label + "find text matches " + std::to_string(matches) + " times; it must match exactly once");
        }
        text.replace(position, edit.find.size(), edit.replace);
    }
    return text;
}

std::string diagnosticsText(const std::vector<fo4ide::papyrus::Diagnostic>& diagnostics, std::string_view head) {
    std::string message(head);
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == fo4ide::papyrus::Severity::Error) message += "\n" + diagnostic.toString();
    }
    return message;
}

}

#ifdef FO4IDE_TEST_HOOKS
std::optional<std::pair<std::string, std::vector<std::uint8_t>>> RadiclePluginBackend::testStagedQuestScript(
    std::string_view plugin) const {
    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(std::string(plugin));
    if (!loaded || !loaded->stagedQuestFragments) return std::nullopt;
    return std::make_pair(loaded->stagedQuestFragments->source, loaded->stagedQuestFragments->pex);
}
#endif

Result<nlohmann::json> RadiclePluginBackend::getQuestScript(const nlohmann::json& args) const {
    try {
        if (!args.is_array() || args.empty() || !args[0].is_string()) {
            return failure("QuestScriptFailed", "invalid script arguments");
        }
        const auto name = args[0].get<std::string>();
        if (!identifierName(name)) return failure("QuestScriptFailed", "script name must be a Papyrus identifier");
        if (dataDirectory_.empty() && (args.size() < 2 || args[1].is_null())) {
            return failure("QuestScriptFailed", "game Data directory is not configured");
        }
        std::optional<std::filesystem::path> imports;
        if (args.size() > 1 && args[1].is_string() && !args[1].get<std::string>().empty()) {
            const std::filesystem::path requested(args[1].get<std::string>());
            imports = allowedLocalizationDirectory(requested);
            if (!imports) imports = questinternal::allowedVanillaFragmentImports(requested, dataDirectory_);
            if (!imports) return failure("QuestScriptFailed", "import directory is outside trusted writable roots");
        }
        const auto source = loadQuestScriptSource(dataDirectory_, imports, name);
        if (!source) return failure("QuestScriptNotFound", "no source for " + name + " in imports or Fallout4 - Misc.ba2");
        const auto parsed = fo4ide::papyrus::Parser::parse(source->text, name + ".psc");
        return nlohmann::json{{"script", name}, {"origin", source->origin}, {"source", source->text},
                              {"extends", parsed.extends.value_or("")}, {"parses", !parsed.hasErrors()}};
    } catch (const std::exception& error) {
        return failure("QuestScriptFailed", error.what());
    }
}

Result<nlohmann::json> RadiclePluginBackend::editQuestScript(const nlohmann::json& args) {
    using namespace fo4ide::papyrus;
    try {
        if (!args.is_array() || args.size() < 3 || !args[0].is_string() || !args[1].is_string() ||
            !args[2].is_string()) return failure("QuestScriptFailed", "invalid script arguments");
        const auto pluginName = args[0].get<std::string>();
        const auto name = args[1].get<std::string>();
        const auto outputDirectory = args[2].get<std::string>();
        if (!identifierName(name)) return failure("QuestScriptFailed", "script name must be a Papyrus identifier");
        std::optional<std::string> replacement;
        if (args.size() > 3 && !args[3].is_null()) {
            if (!args[3].is_string()) return failure("QuestScriptFailed", "source must be a string");
            replacement = args[3].get<std::string>();
        }
        std::optional<std::vector<TextEdit>> edits;
        if (args.size() > 4 && !args[4].is_null()) {
            if (!args[4].is_string()) return failure("QuestScriptFailed", "edits_json must be a JSON string");
            const auto parsedJson = nlohmann::json::parse(args[4].get<std::string>(), nullptr, false);
            if (parsedJson.is_discarded()) return failure("QuestScriptFailed", "edits_json is not valid JSON");
            auto parsedEdits = parseEdits(parsedJson);
            if (!parsedEdits) return std::unexpected(parsedEdits.error());
            edits = std::move(*parsedEdits);
        }
        if (!replacement && !edits) return failure("QuestScriptEditFailed", "no edits given: pass source and/or edits_json");
        std::optional<std::string> importsText;
        if (args.size() > 5 && args[5].is_string() && !args[5].get<std::string>().empty()) {
            importsText = args[5].get<std::string>();
        }

        if (dataDirectory_.empty()) return failure("QuestScriptFailed", "game Data directory is not configured");
        if (insideDirectory(outputDirectory, dataDirectory_)) {
            return failure("QuestScriptOutputInsideData", "output directory must be outside the game Data folder");
        }
        const auto directory = allowedLocalizationDirectory(std::filesystem::path(outputDirectory));
        if (!directory) return failure("QuestScriptFailed", "output directory is outside trusted writable roots");
        std::optional<std::filesystem::path> imports;
        if (importsText) {
            const std::filesystem::path requested(*importsText);
            imports = allowedLocalizationDirectory(requested);
            if (!imports) imports = questinternal::allowedVanillaFragmentImports(requested, dataDirectory_);
            if (!imports) return failure("QuestScriptFailed", "import directory is outside trusted writable roots");
        }

        OpLock lock(*this, OpLock::Mode::Write, {pluginName});
        auto* loaded = findPlugin(pluginName);
        if (!loaded) return failure("QuestScriptFailed", "plugin is not loaded");
        if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

        const auto sourcePath = *directory / (name + ".psc");
        const auto pexPath = *directory / (name + ".pex");
        if (sourcePath.parent_path() != *directory || pexPath.parent_path() != *directory) {
            return failure("QuestScriptFailed", "unsafe output path");
        }
        const auto* pending = loaded->stagedQuestFragments ? &*loaded->stagedQuestFragments : nullptr;
        if (pending && (pending->sourcePath != sourcePath || pending->pexPath != pexPath)) {
            return failure("QuestScriptFailed", "another quest fragment script is already staged for this plugin");
        }
        std::error_code ec;
        const bool diskSource = fs::is_regular_file(sourcePath, ec);
        const bool diskPex = fs::is_regular_file(pexPath, ec);
        if (diskSource != diskPex) return failure("QuestScriptFailed", "source and PEX must both exist or both be absent");

        std::optional<std::string> originalSource;
        std::optional<std::string> originalPex;
        std::string text;
        std::string origin;
        if (pending) {
            text = pending->source;
            origin = "staged";
            originalSource = pending->originalSource;
            originalPex = pending->originalPex;
        } else if (diskSource) {
            originalSource = readBoundedFile(sourcePath, kSourceLimit);
            originalPex = readBoundedFile(pexPath, kPexLimit);
            text = *originalSource;
            origin = "output_psc";
        } else {
            auto source = loadQuestScriptSource(dataDirectory_, imports, name);
            if (!source) return failure("QuestScriptNotFound", "no source for " + name + " in imports or Fallout4 - Misc.ba2");
            text = std::move(source->text);
            origin = std::move(source->origin);
        }

        if (replacement) text = *replacement;
        if (edits) {
            auto applied = applyEdits(std::move(text), *edits);
            if (!applied) return std::unexpected(applied.error());
            text = std::move(*applied);
        }

        auto parsed = Parser::parse(text, name + ".psc");
        if (parsed.hasErrors()) {
            return failure("QuestScriptCompileFailed", diagnosticsText(parsed.diagnostics, "Papyrus parse failed"));
        }
        if (!sameName(parsed.name, name)) {
            return failure("QuestScriptNameMismatch", "source declares ScriptName " + parsed.name + ", not " + name);
        }
        if (!parsed.extends || !sameName(*parsed.extends, "Quest")) {
            return failure("QuestScriptNotQuest", name + " must extend Quest to be edited by edit_quest_script");
        }

        ScriptIndex index;
        addVanillaImports(index, dataDirectory_, kVanillaImportFolders);
        if (imports) index.addRoot(*imports, 10000);
        PapyrusCompileOptions options;
        options.emitDebugInfo = false;
        options.emitDebugOnlyCode = false;
        options.emitBetaOnlyCode = false;
        auto compiled = compileScript(index, &parsed, sourcePath.string(), options);
        if (!compiled.success() || !compiled.sourcesComplete) {
            return failure("QuestScriptCompileFailed",
                           diagnosticsText(compiled.diagnostics, compiled.sourcesComplete
                                                                     ? "Papyrus compilation failed"
                                                                     : "Papyrus compilation failed: imports incomplete"));
        }
        auto pexBytes = compiled.pex->toBytes();

        StagedQuestFragments staged{fo4::FormKey{}, sourcePath, pexPath, text, std::move(pexBytes),
                                    originalSource, originalPex};
        auto mutated = loaded->plugin;
        if (!stageQuestFragmentMutationLocked(*loaded, std::move(mutated), std::move(staged))) {
            return failure("QuestScriptFailed", "script output is already staged by another plugin");
        }
        return nlohmann::json{{"script", name}, {"origin", origin}, {"source", sourcePath.string()},
                              {"pex", pexPath.string()}};
    } catch (const std::exception& error) {
        return failure("QuestScriptFailed", error.what());
    }
}

}
