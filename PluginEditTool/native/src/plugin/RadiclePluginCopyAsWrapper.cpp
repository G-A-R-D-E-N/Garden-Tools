#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "RadiclePluginSupport.hpp"

#include <fo4/forms/leveled_list.hpp>

namespace fo4ide::plugin {

Result<nlohmann::json> RadiclePluginBackend::copyAsWrapper(const nlohmann::json& arguments) {
    const auto sourceName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto targetName = support::stringArgument(arguments, 2);
    const auto editorIdArgument = support::argumentAt(arguments, 3);

    if (sourceName.empty()) {
        return std::unexpected(Error{"SourcePluginMissing", "Source plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (targetName.empty()) {
        return std::unexpected(Error{"TargetPluginMissing", "Target plugin name is empty"});
    }
    if (!editorIdArgument.is_null() && !editorIdArgument.is_string()) {
        return std::unexpected(Error{"InvalidEditorId", "Wrapper editor ID must be a string or null"});
    }
    std::optional<std::string_view> editorId;
    if (editorIdArgument.is_string()) editorId = editorIdArgument.get_ref<const std::string&>();

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {targetName, sourceName});
    const auto* source = findPlugin(sourceName);
    if (source == nullptr) {
        return std::unexpected(Error{
            "SourcePluginNotLoaded",
            "Source plugin is not loaded: " + sourceName
        });
    }
    auto* target = findPlugin(targetName);
    if (target == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded",
            "Target plugin is not loaded yet: " + targetName
        });
    }
    if (!isEditable(*target)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to write wrapper into protected or game-Data plugin: " + target->name
        });
    }
    if (!fileUnchangedSinceLoad(*target)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Target plugin was modified on disk after it was loaded; reload it before writing: " +
                target->path.string()
        });
    }

    const auto* sourceRecord = fo4::find_record(source->plugin, key);
    if (sourceRecord == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + sourceName + ": " + formKeyText
        });
    }
    const auto listSignature =
        fo4::forms::leveled_list_for_entry_target(sourceRecord->header.signature);
    if (!listSignature) {
        return std::unexpected(Error{
            "WrapperUnsupportedType",
            "Copy as wrapper does not support " + sourceRecord->header.signature.to_string() +
                ": " + formKeyText
        });
    }

    try {
        auto mutated = target->plugin;
        ensureOwnerMaster(mutated, key.owner());
        const auto wrapper = fo4::forms::create_leveled_list(
            mutated, *listSignature, editorId, 0, 0, 0);
        fo4::forms::LeveledListEntry entry;
        entry.item = key;
        if (!fo4::forms::insert_leveled_list_entry(mutated, wrapper, 0, entry)) {
            return std::unexpected(Error{
                "LeveledListCreateFailed",
                "Wrapper entry could not be added to " + wrapper.to_string()
            });
        }
        stagePluginMutationLocked(*target, std::move(mutated));
        return nlohmann::json(
            "Copied " + formKeyText + " as wrapper " + wrapper.to_string() + " in " +
            targetName + ".");
    } catch (const fo4::forms::LeveledListError& error) {
        return std::unexpected(Error{"LeveledListCreateFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CopyWrapperFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
