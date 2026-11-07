#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "RadiclePluginSupport.hpp"

#include <fo4/forms/leveled_list.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/form_links.hpp>

namespace fo4ide::plugin {

namespace {

bool sameRecordContent(const fo4::Record& left, const fo4::Record& right) {
    const auto withoutEditorId = [](const fo4::Record& record) {
        std::vector<const fo4::Subrecord*> kept;
        for (const auto& subrecord : record.subrecords) {
            if (subrecord.signature != fo4::sig_edid) kept.push_back(&subrecord);
        }
        return kept;
    };
    const auto a = withoutEditorId(left);
    const auto b = withoutEditorId(right);
    return left.header.signature == right.header.signature && a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](const fo4::Subrecord* x, const fo4::Subrecord* y) {
                          return x->signature == y->signature && x->data == y->data;
                      });
}

bool sameOverrideContent(const fo4::Plugin& targetPlugin, const fo4::Record& targetRecord,
                         const fo4::Plugin& sourcePlugin, const fo4::Record& sourceRecord) {
    auto remapped = targetRecord;
    try {
        fo4::remap_record_form_links(remapped, fo4::plugin_form_id_context(targetPlugin),
                                     fo4::plugin_form_id_context(sourcePlugin),
                                     fo4::plugin_type_lookup(targetPlugin));
    } catch (const std::exception&) {
        return false;
    }
    return sameRecordContent(remapped, sourceRecord);
}

}  // namespace

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
    std::string wrapperEditorId;
    if (editorIdArgument.is_string()) wrapperEditorId = editorIdArgument.get<std::string>();

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
    if (const auto guard = writeGuard(*target)) return std::unexpected(*guard);

    const auto* sourceRecord = fo4::find_record(source->plugin, key);
    if (sourceRecord == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + sourceName + ": " + formKeyText
        });
    }
    const auto listType = sourceRecord->header.signature.to_string();
    if (listType != "LVLI" && listType != "LVLN" && listType != "LVSP") {
        return std::unexpected(Error{
            "WrapperUnsupportedType",
            "Copy as wrapper supports only LVLI, LVLN, or LVSP, not " + listType + ": " + formKeyText
        });
    }
    const auto sourceEditorId = support::editorId(*sourceRecord);
    if (!wrapperEditorId.empty() &&
        support::asciiLower(wrapperEditorId) == support::asciiLower(sourceEditorId)) {
        return std::unexpected(Error{
            "WrapperEditorIdSameAsSource",
            "Wrapped copy needs an EditorID different from " + sourceEditorId + ": " + formKeyText
        });
    }
    const auto later = laterOverridesLocked(*target, key);
    if (!later.empty()) {
        return nlohmann::json(
            "LOSES: " + target->name + " loads before " + support::joinNames(later) +
            ", which also override " + formKeyText + ". The copied override will not win "
            "until " + target->name + " loads after them.");
    }
    const auto* targetRecord = fo4::find_record(target->plugin, key);
    if (source != target && targetRecord != nullptr &&
        !sameOverrideContent(target->plugin, *targetRecord, source->plugin, *sourceRecord)) {
        return std::unexpected(Error{
            "WrapperTargetOverrideDiffers",
            targetName + " already overrides " + formKeyText +
                " with different content; wrap from " + targetName +
                " instead so its entries are kept"
        });
    }

    if (support::topLevelRecord(source->plugin, key) == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + source->name + ": " + key.to_string()
        });
    }

    try {
        auto mutated = target->plugin;
        const auto copiedKey = copyRecordAsNewLocked(
            mutated, *source, key, std::optional<std::string_view>{wrapperEditorId});
        if (!copiedKey) return std::unexpected(copiedKey.error());

        const auto status = fo4::copy_record_as_override(mutated, source->plugin, key, false);
        switch (status) {
            case fo4::RecordCopyStatus::source_missing:
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + sourceName + ": " + formKeyText
                });
            case fo4::RecordCopyStatus::source_containment_unsupported:
                return std::unexpected(Error{
                    "RecordContainmentUnsupported",
                    "Cannot insert this record without reproducing its nested source GRUP hierarchy: " +
                        formKeyText
                });
            case fo4::RecordCopyStatus::inserted:
            case fo4::RecordCopyStatus::already_exists:
                break;
            case fo4::RecordCopyStatus::replaced:
                return std::unexpected(Error{
                    "CopyWrapperFailed",
                    "Unexpected replaced copy status for wrapper " + formKeyText
                });
        }
        if (!fo4::forms::reset_leveled_list(mutated, key)) {
            return std::unexpected(Error{
                "LeveledListCreateFailed",
                "Wrapper override is not present in " + targetName + ": " + key.to_string()
            });
        }
        if (!sourceEditorId.empty() && !fo4::set_record_editor_id(mutated, key, sourceEditorId)) {
            return std::unexpected(Error{
                "LeveledListCreateFailed",
                "Wrapper override EditorID could not be set on " + key.to_string()
            });
        }

        fo4::forms::LeveledListEntry entry;
        entry.item = *copiedKey;
        if (!fo4::forms::insert_leveled_list_entry(mutated, key, 0, entry)) {
            return std::unexpected(Error{
                "LeveledListCreateFailed",
                "Wrapper entry could not be added to " + key.to_string()
            });
        }
        stagePluginMutationLocked(*target, std::move(mutated));
        return nlohmann::json(
            "Copied " + key.to_string() + " as wrapper in " + targetName +
            "; entries moved to NEW record " + copiedKey->to_string() +
            (wrapperEditorId.empty() ? std::string{} : " ('" + wrapperEditorId + "')") + ".");
    } catch (const fo4::forms::LeveledListError& error) {
        return std::unexpected(Error{"LeveledListCreateFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CopyWrapperFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
