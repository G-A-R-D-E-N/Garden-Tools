#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/QuestFragmentSource.hpp>

#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/vmad_writer.hpp>
#include <fo4/schema/vmad.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>

#include "RadiclePluginQuestInternal.hpp"
#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {
namespace questinternal {

std::optional<std::filesystem::path> allowedVanillaFragmentImports(
    const std::filesystem::path& candidate, const std::filesystem::path& dataDirectory) {
    if (candidate.empty() || dataDirectory.empty()) return std::nullopt;
    std::error_code ec;
    const auto data = std::filesystem::weakly_canonical(dataDirectory, ec);
    if (ec || data.empty() || !std::filesystem::is_directory(data, ec) || ec) return std::nullopt;
    for (const auto* leaf : {"Base", "User"}) {
        const auto root = std::filesystem::weakly_canonical(data / "Scripts" / "Source" / leaf, ec);
        if (ec || root.empty() || !std::filesystem::is_directory(root, ec) || ec) {
            ec.clear();
            continue;
        }
        const auto expected = (data / "Scripts" / "Source" / leaf).lexically_normal();
        if (root != expected) continue;
        if (std::filesystem::equivalent(candidate, root, ec) && !ec) return root;
        ec.clear();
    }
    return std::nullopt;
}

}
namespace {

std::string readFile(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > limit) throw std::runtime_error("fragment file exceeds its read limit");
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (size && !in.read(contents.data(), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("cannot read " + path.string());
    }
    if (in.peek() != std::char_traits<char>::eof()) throw std::runtime_error("fragment file changed while reading");
    return contents;
}

bool regularOrMissing(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    return (!ec && (status.type() == std::filesystem::file_type::not_found ||
                    status.type() == std::filesystem::file_type::regular)) ||
           ec == std::errc::no_such_file_or_directory;
}

std::string functionName(std::uint16_t stage, std::uint32_t index) {
    std::ostringstream out;
    out << "Fragment_Stage_" << std::setfill('0') << std::setw(4) << stage
        << "_Item_" << std::setw(2) << index;
    return out.str();
}

bool sourceHasFunction(const fo4ide::papyrus::Script& script, std::string_view name) {
    const auto wanted = support::asciiLower(name);
    return std::count_if(script.functions.begin(), script.functions.end(), [&](const auto& fn) {
        return support::asciiLower(fn.name) == wanted;
    }) == 1;
}

}

#ifdef FO4IDE_TEST_HOOKS
Result<nlohmann::json> RadiclePluginBackend::testSetQuestFragment(const nlohmann::json& args) {
    return setQuestFragment(args);
}
#endif

Result<nlohmann::json> RadiclePluginBackend::setQuestFragment(const nlohmann::json& args) {
    using namespace fo4ide::papyrus;
    const auto fail = [](std::string message) -> Result<nlohmann::json> {
        return std::unexpected(Error{"QuestFragmentFailed", std::move(message)});
    };
    try {
        if (!args.is_array() || args.size() < 8 || !args[0].is_string() || !args[1].is_string() ||
            !args[2].is_number_integer() || !args[3].is_number_integer() || !args[4].is_string() ||
            !args[5].is_string() || !args[6].is_string()) return fail("invalid fragment arguments");
        const auto pluginName = args[0].get<std::string>();
        const auto key = fo4::FormKey::parse(args[1].get<std::string>());
        const auto stageValue = args[2].get<std::int64_t>();
        const auto indexValue = args[3].get<std::int64_t>();
        if (stageValue < 0 || stageValue > 65535 || indexValue < 0 ||
            indexValue > std::numeric_limits<std::int32_t>::max()) return fail("stage or index out of range");
        const auto stage = static_cast<std::uint16_t>(stageValue);
        const auto index = static_cast<std::uint32_t>(indexValue);
        const auto scriptName = args[4].get<std::string>();
        if (scriptName.find(':') != std::string::npos) return fail("namespaced fragment scripts are not supported");
        auto actionJson = args[7].is_string() ? nlohmann::json::parse(args[7].get<std::string>()) : args[7];
        if (!actionJson.is_array() || actionJson.empty()) return fail("actions must be a nonempty array");
        std::vector<QuestFragmentAction> actions;
        for (const auto& item : actionJson) {
            if (!item.is_object() || item.size() != 2 || !item.contains("kind") || !item.contains("value") ||
                !item["kind"].is_string() || !item["value"].is_number_integer()) return fail("invalid action");
            const auto value = item["value"].get<std::int64_t>();
            if (value < 0 || value > 65535) return fail("action value out of range");
            const auto kind = item["kind"].get<std::string>();
            if (kind == "display") actions.push_back({QuestFragmentActionKind::Display, static_cast<std::uint16_t>(value)});
            else if (kind == "complete") actions.push_back({QuestFragmentActionKind::Complete, static_cast<std::uint16_t>(value)});
            else if (kind == "stage") actions.push_back({QuestFragmentActionKind::SetStage, static_cast<std::uint16_t>(value)});
            else return fail("unknown action kind");
        }

        OpLock lock(*this, OpLock::Mode::Write, {pluginName});
        auto* loaded = findPlugin(pluginName);
        if (!loaded) return fail("plugin is not loaded");
        if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
        const auto directory = allowedLocalizationDirectory(args[5].get<std::string>());
        if (!directory) return fail("output directory is outside trusted writable roots");
        auto imports = allowedLocalizationDirectory(args[6].get<std::string>());
        if (!imports) imports = questinternal::allowedVanillaFragmentImports(args[6].get<std::string>(), dataDirectory_);
        if (!imports) return fail("import directory is outside trusted writable roots");
        const auto sourcePath = *directory / (scriptName + ".psc");
        const auto pexPath = *directory / (scriptName + ".pex");
        if (sourcePath.parent_path() != *directory || pexPath.parent_path() != *directory ||
            !regularOrMissing(sourcePath) || !regularOrMissing(pexPath)) return fail("unsafe output path");
        const auto* pending = loaded->stagedQuestFragments ? &*loaded->stagedQuestFragments : nullptr;
        if (pending && (pending->questKey != key || pending->sourcePath != sourcePath || pending->pexPath != pexPath)) {
            return fail("another quest fragment script is already staged for this plugin");
        }
        const bool diskSource = std::filesystem::exists(sourcePath);
        const bool diskPex = std::filesystem::exists(pexPath);
        if (diskSource != diskPex) return fail("source and PEX must both exist or both be absent");
        const bool hasSource = pending || diskSource;
        const auto source = pending ? pending->source : diskSource ? readFile(sourcePath, 4u * 1024u * 1024u) : std::string{};
        const auto originalSource = pending ? pending->originalSource
            : diskSource ? std::optional<std::string>{source} : std::nullopt;
        const auto originalPex = pending ? pending->originalPex
            : diskPex ? std::optional<std::string>{readFile(pexPath, 64u * 1024u * 1024u)} : std::nullopt;
        auto updatedSource = updateQuestFragmentSource(source, scriptName, stage, index, actions);
        if (!updatedSource) return fail(updatedSource.error());

        auto mutated = loaded->plugin;
        const auto* record = fo4::find_record(mutated, key);
        if (!record || record->header.signature != fo4::sig_qust) return fail("quest not found");
        const auto stages = fo4::forms::decode_quest_stages(mutated, key);
        if (std::none_of(stages.stages.begin(), stages.stages.end(), [&](const auto& value) {
                return value.index == stage;
            })) return fail("quest stage not found");
        const fo4::Subrecord* vmad = nullptr;
        for (const auto& sub : record->subrecords) {
            if (sub.signature != fo4::Signature::from_string("VMAD")) continue;
            if (vmad) return fail("duplicate quest VMAD");
            vmad = &sub;
        }
        bool hasHeader = false;
        bool hasBinding = false;
        if (vmad) {
            const auto scripts = fo4::read_vmad_scripts(vmad->data);
            if (scripts.trailing_bytes) {
                const auto extras = fo4::read_quest_vmad_extras(
                    std::span<const std::byte>(vmad->data).last(scripts.trailing_bytes), scripts.object_format);
                if (extras.unsupported || !extras.fully_consumed) return fail("unsupported quest extras");
                hasHeader = !extras.script_name.empty();
                if (hasHeader && support::asciiLower(extras.script_name) != support::asciiLower(scriptName)) {
                    return fail("quest fragment script differs from the requested script");
                }
                if (hasHeader && !hasSource) return fail("existing fragment source is unavailable");
                const auto parsed = Parser::parse(source);
                for (const auto& binding : extras.fragments) {
                    if (support::asciiLower(binding.script) != support::asciiLower(scriptName) ||
                        !sourceHasFunction(parsed, binding.function)) return fail("existing fragment source cannot be preserved");
                    if (binding.stage == stage && binding.stage_index == static_cast<std::int32_t>(index)) {
                        if (hasBinding || support::asciiLower(binding.function) != support::asciiLower(functionName(stage, index))) {
                            return fail("existing fragment binding is ambiguous");
                        }
                        hasBinding = true;
                    }
                }
            }
        }
        if (!hasHeader && hasSource) return fail("existing fragment files are not bound to this quest");
        const auto function = functionName(stage, index);
        const auto status = !hasHeader ? fo4::bootstrap_quest_vmad_fragment(mutated, key, stage, static_cast<std::int32_t>(index), scriptName, function)
            : hasBinding ? fo4::set_quest_vmad_fragment(mutated, key, stage, static_cast<std::int32_t>(index), scriptName, function)
                         : fo4::add_quest_vmad_fragment(mutated, key, stage, static_cast<std::int32_t>(index), scriptName, function);
        if (status != fo4::VmadStructureMutationStatus::updated && status != fo4::VmadStructureMutationStatus::unchanged) {
            return fail("quest fragment binding could not be updated");
        }

        auto parsed = Parser::parse(*updatedSource);
        ScriptIndex scriptIndex;
        scriptIndex.setReadLimits(4u * 1024u * 1024u, 64u * 1024u * 1024u);
        scriptIndex.addRoot(*imports, 10000);
        for (const auto* leaf : {"Base", "User"}) {
            const auto vanilla = questinternal::allowedVanillaFragmentImports(
                dataDirectory_ / "Scripts" / "Source" / leaf, dataDirectory_);
            if (vanilla) scriptIndex.addRoot(*vanilla, 10000);
        }
        PapyrusCompileOptions options;
        auto compiled = compileScript(scriptIndex, &parsed, sourcePath.string(), options);
        if (!compiled.success()) {
            std::string message = "Papyrus compilation failed";
            for (const auto& diagnostic : compiled.diagnostics) {
                if (diagnostic.severity == Severity::Error) message += "\n" + diagnostic.toString();
            }
            return fail(message);
        }
        StagedQuestFragments staged{key, sourcePath, pexPath, *updatedSource, compiled.pex->toBytes(),
                                     originalSource, originalPex};
        if (!stageQuestFragmentMutationLocked(*loaded, std::move(mutated), std::move(staged))) {
            return fail("fragment output is already staged by another plugin");
        }
        return nlohmann::json{{"script", scriptName}, {"function", function},
                              {"source", sourcePath.string()}, {"pex", pexPath.string()}};
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

}
