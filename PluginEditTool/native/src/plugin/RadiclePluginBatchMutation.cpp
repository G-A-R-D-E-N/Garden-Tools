#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginSupport.hpp"

#include <exception>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool stringFieldIfPresent(const nlohmann::json& object, const char* key, std::string& value) {
    value.clear();
    const auto field = object.find(key);
    if (field == object.end()) return true;
    if (!field->is_string()) return false;
    value = field->get<std::string>();
    return true;
}

bool optionalBoolArgument(const nlohmann::json& args, std::size_t index, bool& value) {
    value = false;
    if (!args.is_array() || index >= args.size() || args[index].is_null()) return true;
    if (!args[index].is_boolean()) return false;
    value = args[index].get<bool>();
    return true;
}

struct BatchItem {
    std::string source;
    std::string formKeyText;
    fo4::FormKey key;
};

}

Result<nlohmann::json> RadiclePluginBackend::copyAsOverrideMany(
    const nlohmann::json& arguments) {
    const auto itemsText = stringArgument(arguments, 0);
    const auto targetName = stringArgument(arguments, 1);
    bool overwrite = false;

    if (itemsText.empty()) {
        return std::unexpected(Error{"BatchItemsMissing", "Batch override item list is empty"});
    }
    if (targetName.empty()) {
        return std::unexpected(Error{"TargetPluginMissing", "Target plugin name is empty"});
    }
    if (!support::tryBoolArgument(arguments, 2, overwrite)) {
        return std::unexpected(Error{"InvalidOverwriteFlag", "Overwrite flag must be boolean"});
    }
    bool allowLosing = false;
    if (!optionalBoolArgument(arguments, 3, allowLosing)) {
        return std::unexpected(Error{"InvalidAllowLosingFlag", "Allow-losing flag must be boolean"});
    }

    nlohmann::json rawItems;
    try {
        rawItems = nlohmann::json::parse(itemsText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidBatchItems", error.what()});
    }
    if (!rawItems.is_array()) {
        return std::unexpected(Error{"InvalidBatchItems", "Batch override items must be a JSON array"});
    }

    std::vector<BatchItem> items;
    items.reserve(rawItems.size());
    nlohmann::json parseFailures = nlohmann::json::array();
    std::set<fo4::FormKey> seenKeys;

    for (const auto& raw : rawItems) {
        if (!raw.is_object()) {
            parseFailures.push_back({{"formKey", ""}, {"source", ""}, {"reason", "item is not an object"}});
            continue;
        }
        std::string source;
        std::string formKeyText;
        if (!stringFieldIfPresent(raw, "source", source) ||
            !stringFieldIfPresent(raw, "formKey", formKeyText)) {
            parseFailures.push_back({
                {"formKey", ""},
                {"source", ""},
                {"reason", "source and formKey must be strings"}
            });
            continue;
        }
        if (source.empty() || formKeyText.empty()) {
            parseFailures.push_back({
                {"formKey", formKeyText},
                {"source", source},
                {"reason", "source and formKey are required"}
            });
            continue;
        }

        try {
            auto key = fo4::FormKey::parse(formKeyText);
            if (!seenKeys.insert(key).second) {
                parseFailures.push_back({
                    {"formKey", formKeyText},
                    {"source", source},
                    {"reason", "duplicate FormKey in batch"}
                });
                continue;
            }
            items.push_back({source, formKeyText, std::move(key)});
        } catch (const std::exception& error) {
            parseFailures.push_back({
                {"formKey", formKeyText},
                {"source", source},
                {"reason", error.what()}
            });
        }
    }

    std::vector<std::string_view> owners{targetName};
    for (const auto& item : items) owners.push_back(item.source);
    OpLock lock(*this, OpLock::Mode::Write, owners);
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
            "Refusing to write overrides into protected or game-Data plugin: " + target->name
        });
    }
    if (!fileUnchangedSinceLoad(*target)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Target plugin was modified on disk after it was loaded; reload it before writing: " +
                target->path.string()
        });
    }

    nlohmann::json existing = nlohmann::json::array();
    if (!overwrite) {
        for (const auto& item : items) {
            if (fo4::find_record(target->plugin, item.key) == nullptr) continue;
            existing.push_back({
                {"formKey", item.formKeyText},
                {"source", item.source}
            });
        }
        if (!existing.empty()) {
            return nlohmann::json(nlohmann::json{
                {"total", rawItems.size()},
                {"ok", 0},
                {"failed", parseFailures.size()},
                {"failures", std::move(parseFailures)},
                {"requiresOverwrite", true},
                {"existing", std::move(existing)},
                {"requiresLoadOrderConfirmation", false},
                {"losing", nlohmann::json::array()}
            }.dump());
        }
    }

    if (!allowLosing) {
        nlohmann::json losing = nlohmann::json::array();
        for (const auto& item : items) {
            auto later = laterOverridesLocked(*target, item.key);
            if (later.empty()) continue;
            losing.push_back({
                {"formKey", item.formKeyText},
                {"source", item.source},
                {"loadsAfter", std::move(later)}
            });
        }
        if (!losing.empty()) {
            return nlohmann::json(nlohmann::json{
                {"total", rawItems.size()},
                {"ok", 0},
                {"failed", parseFailures.size()},
                {"failures", std::move(parseFailures)},
                {"requiresOverwrite", false},
                {"existing", nlohmann::json::array()},
                {"requiresLoadOrderConfirmation", true},
                {"losing", std::move(losing)}
            }.dump());
        }
    }

    auto mutated = target->plugin;
    std::size_t copied = 0;
    nlohmann::json failures = std::move(parseFailures);

    for (const auto& item : items) {
        const auto* source = findPlugin(item.source);
        if (source == nullptr) {
            failures.push_back({
                {"formKey", item.formKeyText},
                {"source", item.source},
                {"reason", "source plugin is not loaded"}
            });
            continue;
        }

        try {
            const auto status = fo4::copy_record_as_override(
                mutated, source->plugin, item.key, overwrite);
            switch (status) {
                case fo4::RecordCopyStatus::inserted:
                case fo4::RecordCopyStatus::replaced:
                    ++copied;
                    break;
                case fo4::RecordCopyStatus::already_exists:
                    failures.push_back({
                        {"formKey", item.formKeyText},
                        {"source", item.source},
                        {"reason", "target already contains the record"}
                    });
                    break;
                case fo4::RecordCopyStatus::source_missing:
                    failures.push_back({
                        {"formKey", item.formKeyText},
                        {"source", item.source},
                        {"reason", "record is not present in source plugin"}
                    });
                    break;
                case fo4::RecordCopyStatus::source_containment_unsupported:
                    failures.push_back({
                        {"formKey", item.formKeyText},
                        {"source", item.source},
                        {"reason", "record requires nested source GRUP containment that native insertion does not support yet"}
                    });
                    break;
            }
        } catch (const std::exception& error) {
            failures.push_back({
                {"formKey", item.formKeyText},
                {"source", item.source},
                {"reason", error.what()}
            });
        }
    }

    if (copied != 0) {
        try {
            stagePluginMutationLocked(*target, std::move(mutated));
        } catch (const std::exception& error) {
            return std::unexpected(Error{"BatchCopyWriteFailed", error.what()});
        }
    }

    return nlohmann::json(nlohmann::json{
        {"total", rawItems.size()},
        {"ok", copied},
        {"failed", failures.size()},
        {"failures", std::move(failures)},
        {"requiresOverwrite", false},
        {"existing", nlohmann::json::array()},
        {"requiresLoadOrderConfirmation", false},
        {"losing", nlohmann::json::array()}
    }.dump());
}

}
