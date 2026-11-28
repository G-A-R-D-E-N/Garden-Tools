#include "RadiclePluginSupport.hpp"

#include <fo4/plugin/sound_writer.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/text/encoding.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace fo4ide::plugin {

namespace {

std::string soundFailure(fo4::SoundDescriptorMutationStatus status, const std::string& what) {
    switch (status) {
        case fo4::SoundDescriptorMutationStatus::updated:
        case fo4::SoundDescriptorMutationStatus::unchanged:
            return {};
        case fo4::SoundDescriptorMutationStatus::record_missing:
            return "Record is not present in the plugin";
        case fo4::SoundDescriptorMutationStatus::record_mismatch:
            return what + " belongs only to SNDR records";
        case fo4::SoundDescriptorMutationStatus::discriminator_unproven:
            return what + " needs the AutoWeapon descriptor kind, which this record does not carry";
        case fo4::SoundDescriptorMutationStatus::already_present:
            return what + " is already present and a second copy is refused";
        case fo4::SoundDescriptorMutationStatus::subrecord_missing:
            return what + " is absent from the record";
        case fo4::SoundDescriptorMutationStatus::index_out_of_range:
            return what + " index is out of range";
        case fo4::SoundDescriptorMutationStatus::payload_unproven:
            return what + " payload does not match the proven arm";
    }
    return what + " reported an unknown status";
}

std::optional<fo4::FormKey> formKeyArgument(const std::string& text) {
    if (text.empty()) return std::nullopt;
    try {
        return fo4::FormKey::parse(text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::addSoundFile(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto file = support::stringArgument(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (file.empty()) return std::unexpected(Error{"InvalidSoundFile", "Sound file name is empty"});
    const auto key = formKeyArgument(formKeyText);
    if (!key) return std::unexpected(Error{"InvalidFormKey", "Record FormKey is invalid"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly", "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    std::size_t added = 0;
    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto status = fo4::add_sound_descriptor_file(mutated, *key, file, &added);
        if (const auto failure = soundFailure(status, "Sound file"); !failure.empty()) {
            return std::unexpected(Error{"SoundWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SoundWriteFailed", error.what()});
    }
    return nlohmann::json("Added sound file '" + file + "' at index " + std::to_string(added) + " to " +
                          formKeyText + " in " + pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::removeSoundFile(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto fileIndex = support::integerArgument<std::uint32_t>(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!fileIndex) return std::unexpected(Error{"SoundFileIndexMissing", "Sound file index is required"});
    const auto key = formKeyArgument(formKeyText);
    if (!key) return std::unexpected(Error{"InvalidFormKey", "Record FormKey is invalid"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly", "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto status = fo4::remove_sound_descriptor_file(mutated, *key, *fileIndex);
        if (const auto failure = soundFailure(status, "Sound file"); !failure.empty()) {
            return std::unexpected(Error{"SoundWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SoundWriteFailed", error.what()});
    }
    return nlohmann::json("Removed sound file index " + std::to_string(*fileIndex) + " from " + formKeyText +
                          " in " + pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::setSoundBase(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto targetText = support::stringArgument(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    const auto key = formKeyArgument(formKeyText);
    if (!key) return std::unexpected(Error{"InvalidFormKey", "Record FormKey is invalid"});
    std::optional<fo4::FormKey> target;
    if (!targetText.empty()) {
        target = formKeyArgument(targetText);
        if (!target) return std::unexpected(Error{"InvalidFormKey", "Base link target FormKey is invalid"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly", "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto removed = fo4::remove_sound_descriptor_base(mutated, *key);
        if (removed != fo4::SoundDescriptorMutationStatus::updated &&
            removed != fo4::SoundDescriptorMutationStatus::subrecord_missing) {
            return std::unexpected(Error{"SoundWriteFailed", soundFailure(removed, "Sound base link")});
        }
        if (target) {
            const auto added = fo4::add_sound_descriptor_base(mutated, *key, *target);
            if (const auto failure = soundFailure(added, "Sound base link"); !failure.empty()) {
                return std::unexpected(Error{"SoundWriteFailed", failure});
            }
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SoundWriteFailed", error.what()});
    }
    if (target) {
        return nlohmann::json("Set the sound base link of " + formKeyText + " to " + targetText + " in " +
                              pluginName + ". SavePlugin to persist.");
    }
    return nlohmann::json("Removed the sound base link of " + formKeyText + " in " + pluginName +
                          ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::createSound(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto signature = support::stringArgument(arguments, 1);
    const auto editorId = support::stringArgument(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (signature != "SOUN" && signature != "FSTS") {
        return std::unexpected(Error{"SoundRecordUnsupported", "Only SOUN and FSTS records are created here"});
    }
    if (editorId.empty()) return std::unexpected(Error{"InvalidEditorId", "Editor ID is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly", "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    fo4::FormKey created{};
    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto result = fo4::create_record(mutated, fo4::Signature::from_string(signature), editorId);
        if (result.status != fo4::RecordCreateStatus::created) {
            switch (result.status) {
                case fo4::RecordCreateStatus::not_authorable:
                    return std::unexpected(Error{"SoundCreateFailed", "Record type is not authorable"});
                case fo4::RecordCreateStatus::no_default:
                    return std::unexpected(Error{"SoundCreateFailed", "A required subrecord has no proved default"});
                case fo4::RecordCreateStatus::invalid_editor_id:
                    return std::unexpected(Error{"SoundCreateFailed", "Editor ID cannot be stored"});
                case fo4::RecordCreateStatus::preflight_failed:
                    return std::unexpected(Error{"SoundCreateFailed", "The default record failed preflight"});
                case fo4::RecordCreateStatus::created:
                    break;
            }
        }
        created = result.key;
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SoundCreateFailed", error.what()});
    }
    return nlohmann::json("Created " + signature + " '" + editorId + "' as " + created.to_string() + " in " +
                          pluginName + ". SavePlugin to persist.");
}

}  // namespace fo4ide::plugin
