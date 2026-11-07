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

Result<fo4ide::papyrus::QuestFragmentAction> parseFragmentAction(const nlohmann::json& item) {
    using fo4ide::papyrus::QuestFragmentAction;
    using fo4ide::papyrus::QuestFragmentActionKind;
    const auto fail = [](std::string message) -> Result<QuestFragmentAction> {
        return std::unexpected(Error{"QuestFragmentFailed", std::move(message)});
    };
    if (!item.is_object() || !item.contains("kind") || !item.contains("value") ||
        !item["kind"].is_string() || !item["value"].is_number_integer()) return fail("invalid action");
    const auto value = item["value"].get<std::int64_t>();
    if (value < 0 || value > 65535) return fail("action value out of range");
    const auto kind = item["kind"].get<std::string>();
    QuestFragmentAction action;
    action.value = static_cast<std::uint16_t>(value);
    std::vector<std::string_view> allowed{"kind", "value"};
    if (kind == "display") {
        action.kind = QuestFragmentActionKind::Display;
        allowed.insert(allowed.end(), {"visible", "force"});
    } else if (kind == "complete") {
        action.kind = QuestFragmentActionKind::Complete;
        allowed.push_back("completed");
    } else if (kind == "stage") {
        action.kind = QuestFragmentActionKind::SetStage;
    } else {
        return fail("unknown action kind");
    }
    for (const auto& entry : item.items()) {
        if (std::find(allowed.begin(), allowed.end(), entry.key()) == allowed.end()) {
            return fail("action field " + entry.key() + " is not allowed for " + kind);
        }
    }
    const auto readFlag = [&](const char* name, std::optional<bool>& target) -> std::optional<std::string> {
        if (!item.contains(name)) return std::nullopt;
        if (!item[name].is_boolean()) return std::string(name) + " must be a boolean";
        target = item[name].get<bool>();
        return std::nullopt;
    };
    std::optional<std::string> problem;
    if (action.kind == QuestFragmentActionKind::Display) {
        problem = readFlag("visible", action.visible);
        if (!problem) problem = readFlag("force", action.force);
    } else if (action.kind == QuestFragmentActionKind::Complete) {
        problem = readFlag("completed", action.completed);
    }
    if (problem) return fail(*problem);
    return action;
}

Result<std::vector<fo4ide::papyrus::QuestFragmentAction>> parseFragmentActions(const nlohmann::json& actionJson) {
    if (!actionJson.is_array() || actionJson.empty()) {
        return std::unexpected(Error{"QuestFragmentFailed", "actions must be a nonempty array"});
    }
    std::vector<fo4ide::papyrus::QuestFragmentAction> actions;
    for (const auto& item : actionJson) {
        auto action = parseFragmentAction(item);
        if (!action) return std::unexpected(action.error());
        actions.push_back(*action);
    }
    return actions;
}

bool wellFormedFragmentArguments(const nlohmann::json& args, std::size_t count) {
    return args.is_array() && args.size() >= count && args[0].is_string() && args[1].is_string() &&
           args[2].is_number_integer() && args[3].is_number_integer() && args[4].is_string() &&
           args[5].is_string() && args[6].is_string();
}

}

#ifdef FO4IDE_TEST_HOOKS
Result<nlohmann::json> RadiclePluginBackend::testSetQuestFragment(const nlohmann::json& args) {
    return setQuestFragment(args);
}
#endif

Result<RadiclePluginBackend::QuestFragmentWrite> RadiclePluginBackend::prepareQuestFragmentLocked(
    const LoadedPlugin& loaded, const fo4::FormKey& key, std::uint16_t stage, std::uint32_t index,
    std::string_view scriptName, std::string_view outputDirectory, std::string_view importsDirectory) const {
    using namespace fo4ide::papyrus;
    const auto fail = [](std::string message) -> Result<QuestFragmentWrite> {
        return std::unexpected(Error{"QuestFragmentFailed", std::move(message)});
    };
    const std::string name(scriptName);
    if (name.find(':') != std::string::npos) return fail("namespaced fragment scripts are not supported");
    const auto directory = allowedLocalizationDirectory(std::filesystem::path(outputDirectory));
    if (!directory) return fail("output directory is outside trusted writable roots");
    const std::filesystem::path importsPath(importsDirectory);
    auto imports = allowedLocalizationDirectory(importsPath);
    if (!imports) imports = questinternal::allowedVanillaFragmentImports(importsPath, dataDirectory_);
    if (!imports) return fail("import directory is outside trusted writable roots");

    QuestFragmentWrite write;
    write.key = key;
    write.scriptName = name;
    write.sourcePath = *directory / (name + ".psc");
    write.pexPath = *directory / (name + ".pex");
    write.imports = *imports;
    write.function = functionName(stage, index);
    if (write.sourcePath.parent_path() != *directory || write.pexPath.parent_path() != *directory ||
        !regularOrMissing(write.sourcePath) || !regularOrMissing(write.pexPath)) return fail("unsafe output path");
    const auto* pending = loaded.stagedQuestFragments ? &*loaded.stagedQuestFragments : nullptr;
    if (pending && (pending->questKey != key || pending->sourcePath != write.sourcePath ||
                    pending->pexPath != write.pexPath)) {
        return fail("another quest fragment script is already staged for this plugin");
    }
    const bool diskSource = std::filesystem::exists(write.sourcePath);
    const bool diskPex = std::filesystem::exists(write.pexPath);
    if (diskSource != diskPex) return fail("source and PEX must both exist or both be absent");
    write.hasSource = pending || diskSource;
    if (pending) {
        write.source = pending->source;
        write.originalSource = pending->originalSource;
        write.originalPex = pending->originalPex;
    } else if (diskSource) {
        write.source = readFile(write.sourcePath, 4u * 1024u * 1024u);
        write.originalSource = write.source;
        write.originalPex = readFile(write.pexPath, 64u * 1024u * 1024u);
    }

    auto mutated = loaded.plugin;
    const auto* record = fo4::find_record(mutated, key);
    if (!record || record->header.signature != fo4::sig_qust) return fail("quest not found");
    const fo4::Subrecord* vmad = nullptr;
    for (const auto& sub : record->subrecords) {
        if (sub.signature != fo4::Signature::from_string("VMAD")) continue;
        if (vmad) return fail("duplicate quest VMAD");
        vmad = &sub;
    }
    if (vmad) {
        const auto scripts = fo4::read_vmad_scripts(vmad->data);
        if (scripts.trailing_bytes) {
            const auto extras = fo4::read_quest_vmad_extras(
                std::span<const std::byte>(vmad->data).last(scripts.trailing_bytes), scripts.object_format);
            if (extras.unsupported || !extras.fully_consumed) return fail("unsupported quest extras");
            write.hasHeader = !extras.script_name.empty();
            if (write.hasHeader && support::asciiLower(extras.script_name) != support::asciiLower(name)) {
                return fail("quest fragment script differs from the requested script");
            }
            if (write.hasHeader && !write.hasSource) return fail("existing fragment source is unavailable");
            const auto parsed = Parser::parse(write.source);
            for (const auto& binding : extras.fragments) {
                if (support::asciiLower(binding.script) != support::asciiLower(name) ||
                    !sourceHasFunction(parsed, binding.function)) return fail("existing fragment source cannot be preserved");
                if (binding.stage == stage && binding.stage_index == static_cast<std::int32_t>(index)) {
                    if (write.hasBinding || support::asciiLower(binding.function) != support::asciiLower(write.function)) {
                        return fail("existing fragment binding is ambiguous");
                    }
                    write.hasBinding = true;
                }
            }
        }
    }
    if (!write.hasHeader && write.hasSource) return fail("existing fragment files are not bound to this quest");
    write.mutated = std::move(mutated);
    return write;
}

Result<nlohmann::json> RadiclePluginBackend::stageQuestFragmentWriteLocked(
    LoadedPlugin& loaded, QuestFragmentWrite write, const std::string& source) {
    using namespace fo4ide::papyrus;
    const auto fail = [](std::string message) -> Result<nlohmann::json> {
        return std::unexpected(Error{"QuestFragmentFailed", std::move(message)});
    };
    auto parsed = Parser::parse(source);
    ScriptIndex scriptIndex;
    scriptIndex.setReadLimits(4u * 1024u * 1024u, 64u * 1024u * 1024u);
    scriptIndex.addRoot(write.imports, 10000);
    for (const auto* leaf : {"Base", "User"}) {
        const auto vanilla = questinternal::allowedVanillaFragmentImports(
            dataDirectory_ / "Scripts" / "Source" / leaf, dataDirectory_);
        if (vanilla) scriptIndex.addRoot(*vanilla, 10000);
    }
    PapyrusCompileOptions options;
    auto compiled = compileScript(scriptIndex, &parsed, write.sourcePath.string(), options);
    if (!compiled.success()) {
        std::string message = "Papyrus compilation failed";
        for (const auto& diagnostic : compiled.diagnostics) {
            if (diagnostic.severity == Severity::Error) message += "\n" + diagnostic.toString();
        }
        return fail(message);
    }
    nlohmann::json result{{"script", write.scriptName}, {"function", write.function},
                          {"source", write.sourcePath.string()}, {"pex", write.pexPath.string()}};
    StagedQuestFragments staged{write.key, write.sourcePath, write.pexPath, source, compiled.pex->toBytes(),
                                 write.originalSource, write.originalPex};
    if (!stageQuestFragmentMutationLocked(loaded, std::move(write.mutated), std::move(staged))) {
        return fail("fragment output is already staged by another plugin");
    }
    return result;
}

Result<nlohmann::json> RadiclePluginBackend::setQuestFragment(const nlohmann::json& args) {
    using namespace fo4ide::papyrus;
    const auto fail = [](std::string message) -> Result<nlohmann::json> {
        return std::unexpected(Error{"QuestFragmentFailed", std::move(message)});
    };
    try {
        if (!wellFormedFragmentArguments(args, 8)) return fail("invalid fragment arguments");
        const auto pluginName = args[0].get<std::string>();
        const auto key = fo4::FormKey::parse(args[1].get<std::string>());
        const auto stageValue = args[2].get<std::int64_t>();
        const auto indexValue = args[3].get<std::int64_t>();
        if (stageValue < 0 || stageValue > 65535 || indexValue < 0 ||
            indexValue > std::numeric_limits<std::int32_t>::max()) return fail("stage or index out of range");
        const auto stage = static_cast<std::uint16_t>(stageValue);
        const auto index = static_cast<std::uint32_t>(indexValue);
        const auto scriptName = args[4].get<std::string>();
        auto actionJson = args[7].is_string() ? nlohmann::json::parse(args[7].get<std::string>()) : args[7];
        auto actions = parseFragmentActions(actionJson);
        if (!actions) return std::unexpected(actions.error());

        OpLock lock(*this, OpLock::Mode::Write, {pluginName});
        auto* loaded = findPlugin(pluginName);
        if (!loaded) return fail("plugin is not loaded");
        if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
        auto write = prepareQuestFragmentLocked(*loaded, key, stage, index, scriptName,
                                                args[5].get<std::string>(), args[6].get<std::string>());
        if (!write) return std::unexpected(write.error());
        const auto stages = fo4::forms::decode_quest_stages(write->mutated, key);
        if (std::none_of(stages.stages.begin(), stages.stages.end(), [&](const auto& value) {
                return value.index == stage;
            })) return fail("quest stage not found");
        auto updatedSource = updateQuestFragmentSource(write->source, scriptName, stage, index, *actions);
        if (!updatedSource) return fail(updatedSource.error());
        const auto status = !write->hasHeader
            ? fo4::bootstrap_quest_vmad_fragment(write->mutated, key, stage, static_cast<std::int32_t>(index), scriptName, write->function)
            : write->hasBinding
                ? fo4::set_quest_vmad_fragment(write->mutated, key, stage, static_cast<std::int32_t>(index), scriptName, write->function)
                : fo4::add_quest_vmad_fragment(write->mutated, key, stage, static_cast<std::int32_t>(index), scriptName, write->function);
        if (status != fo4::VmadStructureMutationStatus::updated && status != fo4::VmadStructureMutationStatus::unchanged) {
            return fail("quest fragment binding could not be updated");
        }
        return stageQuestFragmentWriteLocked(*loaded, std::move(*write), *updatedSource);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeQuestFragment(const nlohmann::json& args) {
    using namespace fo4ide::papyrus;
    const auto fail = [](std::string message) -> Result<nlohmann::json> {
        return std::unexpected(Error{"QuestFragmentFailed", std::move(message)});
    };
    try {
        if (!wellFormedFragmentArguments(args, 7)) return fail("invalid fragment arguments");
        const auto pluginName = args[0].get<std::string>();
        const auto key = fo4::FormKey::parse(args[1].get<std::string>());
        const auto stageValue = args[2].get<std::int64_t>();
        const auto indexValue = args[3].get<std::int64_t>();
        if (stageValue < 0 || stageValue > 65535 || indexValue < 0 ||
            indexValue > std::numeric_limits<std::int32_t>::max()) return fail("stage or index out of range");
        const auto stage = static_cast<std::uint16_t>(stageValue);
        const auto index = static_cast<std::uint32_t>(indexValue);
        const auto scriptName = args[4].get<std::string>();

        OpLock lock(*this, OpLock::Mode::Write, {pluginName});
        auto* loaded = findPlugin(pluginName);
        if (!loaded) return fail("plugin is not loaded");
        if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
        auto write = prepareQuestFragmentLocked(*loaded, key, stage, index, scriptName,
                                                args[5].get<std::string>(), args[6].get<std::string>());
        if (!write) return std::unexpected(write.error());
        if (!write->hasBinding) return fail("quest fragment binding not found");
        if (!isManagedQuestFragmentSource(write->source)) {
            return std::unexpected(Error{"UnmanagedQuestFragmentSource",
                "quest fragment script " + scriptName + " is not GardenTools managed source; remove_quest_fragment only removes managed fragment functions"});
        }
        auto updatedSource = removeQuestFragmentSource(write->source, scriptName, stage, index);
        if (!updatedSource) return fail(updatedSource.error());
        if (fo4::remove_quest_vmad_fragment(write->mutated, key, stage, static_cast<std::int32_t>(index)) !=
            fo4::VmadStructureMutationStatus::updated) {
            return fail("quest fragment binding could not be removed");
        }
        return stageQuestFragmentWriteLocked(*loaded, std::move(*write), *updatedSource);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

Result<nlohmann::json> RadiclePluginBackend::getQuestFragments(const nlohmann::json& args) const {
    const auto fail = [](std::string message) -> Result<nlohmann::json> {
        return std::unexpected(Error{"QuestFragmentFailed", std::move(message)});
    };
    if (!args.is_array() || args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
        return fail("invalid fragment arguments");
    }
    try {
        const auto pluginName = args[0].get<std::string>();
        OpLock lock(*this, OpLock::Mode::Read);
        const auto* loaded = findPlugin(pluginName);
        if (!loaded) return fail("plugin is not loaded");
        const auto key = fo4::FormKey::parse(args[1].get<std::string>());
        const auto* record = fo4::find_record(loaded->plugin, key);
        if (!record || record->header.signature != fo4::sig_qust) return fail("quest not found");
        const fo4::Subrecord* vmad = nullptr;
        for (const auto& sub : record->subrecords) {
            if (sub.signature != fo4::Signature::from_string("VMAD")) continue;
            if (vmad) return fail("duplicate quest VMAD");
            vmad = &sub;
        }
        nlohmann::json out = nlohmann::json::array();
        if (vmad == nullptr) return out;
        const auto layout = fo4::inspect_quest_vmad(vmad->data);
        if (layout.unparseable || layout.unsupported) return fail("quest VMAD fragments cannot be inspected");
        const auto scripts = fo4::read_vmad_scripts(vmad->data);
        if (scripts.trailing_bytes == 0) return out;
        const auto extras = fo4::read_quest_vmad_extras(
            std::span<const std::byte>(vmad->data).last(scripts.trailing_bytes), scripts.object_format);
        if (extras.unsupported || !extras.fully_consumed) return fail("unsupported quest extras");
        for (const auto& binding : extras.fragments) {
            out.push_back({{"stage", binding.stage}, {"logEntryIndex", binding.stage_index},
                           {"script", binding.script}, {"function", binding.function},
                           {"fragmentScript", extras.script_name}});
        }
        return out;
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

}
