#include "RadiclePluginSupport.hpp"
#include "RadiclePluginQuestInternal.hpp"

#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/vmad_writer.hpp>
#include <fo4/text/encoding.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fo4ide::plugin {

using namespace questinternal;

namespace {

std::optional<Error> nameArgument(const nlohmann::json& arguments, std::size_t index,
                                  std::string_view label, std::string& out) {
    const auto text = support::stringArgument(arguments, index);
    if (text.empty()) {
        return Error{"InvalidFragmentArgument", std::string(label) + " must be a non-empty string"};
    }
    if (text.find('\0') != std::string::npos) {
        return Error{"InvalidFragmentArgument", std::string(label) + " holds an embedded NUL byte"};
    }
    if (!fo4::text::utf8_to_windows1252(text)) {
        return Error{"InvalidFragmentArgument",
                     std::string(label) + " has characters that Windows-1252 cannot hold"};
    }
    out = text;
    return std::nullopt;
}

std::optional<Error> unsignedArgument(const nlohmann::json& arguments, std::size_t index,
                                      std::string_view label, std::uint32_t& out) {
    const auto value = support::integerValue<std::uint32_t>(support::argumentAt(arguments, index));
    if (!value) return Error{"InvalidFragmentArgument", std::string(label) + " must be an unsigned integer"};
    out = *value;
    return std::nullopt;
}

std::optional<Error> fragmentFailure(fo4::VmadStructureMutationStatus status, std::string_view subject) {
    switch (status) {
    case fo4::VmadStructureMutationStatus::updated:
    case fo4::VmadStructureMutationStatus::unchanged:
    case fo4::VmadStructureMutationStatus::already_present:
        return std::nullopt;
    case fo4::VmadStructureMutationStatus::record_missing:
        return Error{"RecordNotFound", "Record is not present: " + std::string(subject)};
    case fo4::VmadStructureMutationStatus::subrecord_missing:
        return Error{"FragmentMissing", "the record has no VMAD to hold a fragment: " + std::string(subject)};
    case fo4::VmadStructureMutationStatus::index_out_of_range:
        return Error{"FragmentIndexOutOfRange", "fragment index is out of range: " + std::string(subject)};
    case fo4::VmadStructureMutationStatus::not_authorable:
        return Error{"FragmentNotAuthorable", "this record type does not store script fragments: " + std::string(subject)};
    case fo4::VmadStructureMutationStatus::binding_missing:
        return Error{"FragmentMissing", "no fragment is bound at that index: " + std::string(subject)};
    }
    return Error{"SetFragmentFailed", "fragment binding could not be written: " + std::string(subject)};
}

std::optional<Error> addFailure(fo4::VmadStructureMutationStatus status, std::string_view subject) {
    if (status == fo4::VmadStructureMutationStatus::already_present) {
        return Error{"FragmentAlreadyBound", "a fragment is already bound at that position: " + std::string(subject)};
    }
    if (status == fo4::VmadStructureMutationStatus::binding_missing ||
        status == fo4::VmadStructureMutationStatus::subrecord_missing) {
        return Error{"FragmentSectionMissing", "the record has no fragment section to add into: " + std::string(subject)};
    }
    return fragmentFailure(status, subject);
}

std::optional<Error> bindFlaggedFragment(fo4::Plugin& plugin, const fo4::FormKey& record,
                                        std::uint32_t index, const std::string& script,
                                        const std::string& function, std::string_view subject, bool addOnly) {
    if (addOnly) return addFailure(fo4::add_vmad_fragment(plugin, record, index, script, function), subject);
    auto status = fo4::set_vmad_fragment(plugin, record, index, script, function);
    if (status == fo4::VmadStructureMutationStatus::binding_missing ||
        status == fo4::VmadStructureMutationStatus::subrecord_missing) {
        const auto bootstrapped = fo4::bootstrap_vmad_fragment_section(plugin, record, script);
        if (bootstrapped != fo4::VmadStructureMutationStatus::updated &&
            bootstrapped != fo4::VmadStructureMutationStatus::already_present) {
            return fragmentFailure(bootstrapped, subject);
        }
        status = fo4::add_vmad_fragment(plugin, record, index, script, function);
    }
    return fragmentFailure(status, subject);
}

std::optional<Error> bindScenePhaseFragment(fo4::Plugin& plugin, const fo4::FormKey& record,
                                           std::uint32_t phase, std::uint8_t flag,
                                           const std::string& script, const std::string& function,
                                           std::string_view subject, bool addOnly) {
    if (addOnly) {
        return addFailure(fo4::add_scene_vmad_phase_fragment(plugin, record, phase, flag, script, function), subject);
    }
    auto status = fo4::set_scene_vmad_phase_fragment(plugin, record, phase, flag, script, function);
    if (status == fo4::VmadStructureMutationStatus::binding_missing ||
        status == fo4::VmadStructureMutationStatus::subrecord_missing) {
        const auto bootstrapped = fo4::bootstrap_vmad_fragment_section(plugin, record, script);
        if (bootstrapped != fo4::VmadStructureMutationStatus::updated &&
            bootstrapped != fo4::VmadStructureMutationStatus::already_present) {
            return fragmentFailure(bootstrapped, subject);
        }
        status = fo4::add_scene_vmad_phase_fragment(plugin, record, phase, flag, script, function);
    }
    return fragmentFailure(status, subject);
}

std::optional<Error> recordKeyArgument(const nlohmann::json& arguments, std::size_t index,
                                       fo4::FormKey& out) {
    const auto text = questStringArg(arguments, index);
    if (text.empty()) return Error{"RecordMissing", "Record is empty"};
    auto parsed = support::formKeyArgument(text);
    if (!parsed) return parsed.error();
    out = *parsed;
    return std::nullopt;
}

}
Result<nlohmann::json> RadiclePluginBackend::setInfoFragment(const nlohmann::json& arguments) {
    return writeInfoFragment(arguments, false);
}

Result<nlohmann::json> RadiclePluginBackend::addInfoFragment(const nlohmann::json& arguments) {
    return writeInfoFragment(arguments, true);
}

Result<nlohmann::json> RadiclePluginBackend::writeInfoFragment(const nlohmann::json& arguments, bool addOnly) {
    const auto pluginName = questStringArg(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    fo4::FormKey info;
    if (auto error = recordKeyArgument(arguments, 1, info)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "fragment index", index)) return std::unexpected(*error);
    if (index >= 2) {
        return std::unexpected(Error{"FragmentIndexOutOfRange",
                                     "INFO fragments are indexed 0 (OnBegin) and 1 (OnEnd)"});
    }
    std::string script;
    std::string function;
    if (auto error = nameArgument(arguments, 3, "script name", script)) return std::unexpected(*error);
    if (auto error = nameArgument(arguments, 4, "function name", function)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        if (auto error = bindFlaggedFragment(mutated, info, index, script, function, pluginName, addOnly)) {
            return std::unexpected(*error);
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(std::string(addOnly ? "Added" : "Bound") + " INFO fragment " +
                              std::to_string(index) + " of " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{addOnly ? "AddInfoFragmentFailed" : "SetInfoFragmentFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeInfoFragment(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    fo4::FormKey info;
    if (auto error = recordKeyArgument(arguments, 1, info)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "fragment index", index)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::remove_vmad_fragment(mutated, info, index);
        if (auto error = fragmentFailure(status, pluginName)) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Removed INFO fragment " + std::to_string(index) + " from " + pluginName +
                              ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RemoveInfoFragmentFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setScenePhaseFragment(const nlohmann::json& arguments) {
    return writeScenePhaseFragment(arguments, false);
}

Result<nlohmann::json> RadiclePluginBackend::addScenePhaseFragment(const nlohmann::json& arguments) {
    return writeScenePhaseFragment(arguments, true);
}

Result<nlohmann::json> RadiclePluginBackend::writeScenePhaseFragment(const nlohmann::json& arguments, bool addOnly) {
    const auto pluginName = questStringArg(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    fo4::FormKey scene;
    if (auto error = recordKeyArgument(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t phase = 0;
    if (auto error = unsignedArgument(arguments, 2, "phase index", phase)) return std::unexpected(*error);
    std::uint32_t flag = 0;
    if (auto error = unsignedArgument(arguments, 3, "phase flag", flag)) return std::unexpected(*error);
    if (flag == 0 || flag > 2) {
        return std::unexpected(Error{"InvalidFragmentArgument",
                                     "scene phase flag is 1 for start and 2 for completion"});
    }
    std::string script;
    std::string function;
    if (auto error = nameArgument(arguments, 4, "script name", script)) return std::unexpected(*error);
    if (auto error = nameArgument(arguments, 5, "function name", function)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        if (auto error = bindScenePhaseFragment(mutated, scene, phase, static_cast<std::uint8_t>(flag),
                                                script, function, pluginName, addOnly)) {
            return std::unexpected(*error);
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(std::string(addOnly ? "Added" : "Bound") + " scene phase fragment of " +
                              pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{addOnly ? "AddScenePhaseFragmentFailed" : "SetScenePhaseFragmentFailed",
                                     error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeScenePhaseFragment(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    fo4::FormKey scene;
    if (auto error = recordKeyArgument(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t phase = 0;
    if (auto error = unsignedArgument(arguments, 2, "phase index", phase)) return std::unexpected(*error);
    std::uint32_t flag = 0;
    if (auto error = unsignedArgument(arguments, 3, "phase flag", flag)) return std::unexpected(*error);
    if (flag == 0 || flag > 2) {
        return std::unexpected(Error{"InvalidFragmentArgument",
                                     "scene phase flag is 1 for start and 2 for completion"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::remove_scene_vmad_phase_fragment(
            mutated, scene, phase, static_cast<std::uint8_t>(flag));
        if (auto error = fragmentFailure(status, pluginName)) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Removed scene phase fragment from " + pluginName +
                              ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RemoveScenePhaseFragmentFailed", error.what()});
    }
}

}
