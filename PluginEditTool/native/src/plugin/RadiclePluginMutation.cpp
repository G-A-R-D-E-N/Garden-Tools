#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include "RadiclePluginSupport.hpp"

#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4/forms/eyes.hpp>
#include <fo4/forms/leveled_spell.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/condition_scopes.hpp>
#include <fo4/schema/ctda.hpp>

namespace fo4ide::plugin {

using support::editorId;
using support::joinNames;

namespace {

bool optionalBoolArgument(const nlohmann::json& args, std::size_t index, bool& value) {
    value = false;
    if (!args.is_array() || index >= args.size() || args[index].is_null()) return true;
    if (!args[index].is_boolean()) return false;
    value = args[index].get<bool>();
    return true;
}

constexpr std::uint32_t kCopyInitiallyDisabled = 0x00000800u;

constexpr std::array kInitiallyDisabledSignatures{
    fo4::Signature::from_string("REFR"), fo4::Signature::from_string("ACHR"),
    fo4::Signature::from_string("NAVM"), fo4::Signature::from_string("PARW"),
    fo4::Signature::from_string("PBAR"), fo4::Signature::from_string("PBEA"),
    fo4::Signature::from_string("PCON"), fo4::Signature::from_string("PFLA"),
    fo4::Signature::from_string("PGRE"), fo4::Signature::from_string("PHZD"),
    fo4::Signature::from_string("PMIS"),
};

bool supportsInitiallyDisabled(fo4::Signature signature) {
    return std::find(kInitiallyDisabledSignatures.begin(), kInitiallyDisabledSignatures.end(),
                     signature) != kInitiallyDisabledSignatures.end();
}

bool markCopyInitiallyDisabled(fo4::Plugin& plugin, const fo4::FormKey& key) {
    bool found = false;
    fo4::visit_records(plugin, [&](fo4::Record& record) {
        if (record.header.signature == fo4::sig_tes4 || record.form_key != key) return;
        record.header.flags = fo4::RecordFlags{record.header.flags.value() | kCopyInitiallyDisabled};
        record.original_range.clear();
        record.decompressed_payload.clear();
        record.header.data_size = 0;
        record.uncompressed_size = 0;
        found = true;
    });
    if (found) plugin.original_range.clear();
    return found;
}

std::string asciiLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

bool officialPluginName(std::string_view name) {
    const auto lower = asciiLower(name);
    if (lower == "fallout4.esm" ||
        lower == "dlcrobot.esm" ||
        lower == "dlccoast.esm" ||
        lower == "dlcworkshop01.esm" ||
        lower == "dlcworkshop02.esm" ||
        lower == "dlcworkshop03.esm" ||
        lower == "dlcnukaworld.esm" ||
        lower == "dlcultrahighresolution.esm") {
        return true;
    }
    return lower.starts_with("cc") &&
           (lower.ends_with(".esm") || lower.ends_with(".esp") || lower.ends_with(".esl"));
}

template <class UInt>
bool parseUnsigned(std::string_view text, UInt& value) {
    static_assert(std::is_unsigned_v<UInt>);
    if (text.empty()) return false;
    std::uint64_t parsed = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed, 10);
    if (error != std::errc{} || ptr != end ||
        parsed > static_cast<std::uint64_t>(std::numeric_limits<UInt>::max())) {
        return false;
    }
    value = static_cast<UInt>(parsed);
    return true;
}

enum class LvspEntryField {
    level,
    spell,
    count,
    chanceNone,
};

struct LvspEntryPath {
    std::size_t index{0};
    LvspEntryField field{LvspEntryField::level};
};

bool parseLvspEntryPath(std::string_view field, LvspEntryPath& result) {
    constexpr std::string_view prefix = "LVSP\\Entries[";
    if (!field.starts_with(prefix)) return false;
    const auto close = field.find(']', prefix.size());
    if (close == std::string_view::npos || close + 2 >= field.size() ||
        field[close + 1] != '\\') {
        return false;
    }

    std::size_t index = 0;
    if (!parseUnsigned(field.substr(prefix.size(), close - prefix.size()), index)) return false;
    const auto member = field.substr(close + 2);
    if (member == "Level") result.field = LvspEntryField::level;
    else if (member == "Spell") result.field = LvspEntryField::spell;
    else if (member == "Count") result.field = LvspEntryField::count;
    else if (member == "ChanceNone") result.field = LvspEntryField::chanceNone;
    else return false;
    result.index = index;
    return true;
}

bool isLvspField(std::string_view field) {
    LvspEntryPath ignored;
    return field == "LVSP\\ChanceNone" || field == "LVSP\\MaxCount" ||
           field == "LVSP\\Flags" || parseLvspEntryPath(field, ignored);
}

bool isEyesField(std::string_view field) {
    return field == "EYES\\Full" || field == "EYES\\Icon" ||
           field == "EYES\\DataFlags";
}

}

bool RadiclePluginBackend::isEditable(const LoadedPlugin& loaded) const {
    if (asciiLower(loaded.source) == "data") return false;
    if (!dataDirectory_.empty()) {
        std::error_code dataEc;
        std::error_code pluginEc;
        const auto dataDir = std::filesystem::weakly_canonical(dataDirectory_, dataEc);
        const auto pluginPath = std::filesystem::weakly_canonical(loaded.path, pluginEc);
        if (dataEc || pluginEc || dataDir.empty() || pluginPath.empty()) {
            return false;
        }
        const auto [dataEnd, nothing] = std::mismatch(
            dataDir.begin(), dataDir.end(), pluginPath.begin(), pluginPath.end());
        if (dataEnd == dataDir.end()) return false;
    }
    return !officialPluginName(loaded.name);
}

void RadiclePluginBackend::setDataDirectory(std::filesystem::path dataDirectory) {
    OpLock lock(*this, OpLock::Mode::Lifecycle);
    dataDirectory_ = std::move(dataDirectory);
}

Result<nlohmann::json> RadiclePluginBackend::invokeBackend(
    std::string_view method,
    const nlohmann::json& arguments) {
    if (method == "CreateRecord") return createRecord(arguments);
    if (method == "DeleteRecord") return deleteRecord(arguments);
    if (method == "RenumberRecord") return renumberRecord(arguments);
    if (method == "SetField") return setFieldValue(arguments);
    if (method == "InsertFormLinkListElement") return insertFormLinkListElement(arguments);
    if (method == "RemoveFormLinkListElement") return removeFormLinkListElement(arguments);
    if (method == "AddListItem") return addListItem(arguments);
    if (method == "RemoveListItem") return removeListItem(arguments);
    if (method == "SetComponents") return setComponents(arguments);
    if (method == "SetQuestAliases") return setQuestAliases(arguments);
    if (method == "SetQuestStages") return setQuestStages(arguments);
    if (method == "CopyAsOverride") return copyAsOverride(arguments);
    if (method == "CopyAsOverrideMany") return copyAsOverrideMany(arguments);
    if (method == "CopyElementToSelected") return copyElementToSelected(arguments);
    if (method == "RemoveElementFromSelected") return removeElementFromSelected(arguments);
    if (method == "AddMasters") return addMasters(arguments);
    if (method == "CheckEslEligibility") return checkEslEligibility(arguments);
    if (method == "GetPluginRecordTypes") return getPluginRecordTypes(arguments);
    if (method == "GetRecordsOfType") return getRecordsOfType(arguments);
    if (method == "GetRecordsGrid") return getRecordsGrid(arguments);
    if (method == "GetRecordDetails") return getRecordDetails(arguments);
    if (method == "GetRecordPluginMatrix") return getRecordPluginMatrix(arguments);
    if (method == "GetContainmentPath") return getContainmentPath(arguments);

    return invoke("backend." + std::string(method), arguments);
}



Result<nlohmann::json> RadiclePluginBackend::setConditionsAtPath(
    const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto recordId = support::stringArgument(arguments, 1);
    const auto path = support::stringArgument(arguments, 2);
    const auto conditionsJson = support::stringArgument(arguments, 3);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordId.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record FormKey is empty"});
    }
    if (conditionsJson.empty()) {
        return std::unexpected(Error{
            "InvalidConditionsJson",
            "conditions JSON is empty; pass [] to clear"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{
            "PluginNotLoaded",
            "Plugin is not loaded: " + pluginName
        });
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to edit protected or game-Data plugin: " + loaded->name
        });
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                loaded->path.string()
        });
    }

    const auto resolveEditorId = [&](std::string_view text)
        -> std::optional<fo4::RawFormId> {
        for (const auto& candidate : plugins_) {
            for (const auto* record : fo4::collect_records(candidate.plugin)) {
                const auto id = editorId(*record);
                if (id.empty()) continue;
                std::string lowerId = id;
                std::string lowerText(text);
                std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                if (lowerId == lowerText) {
                    try {
                        const auto raw = fo4::encode_plugin_form_id(
                            record->form_key, fo4::plugin_form_id_context(candidate.plugin));
                        return fo4::RawFormId{raw.value()};
                    } catch (const std::exception&) {
                        return std::nullopt;
                    }
                }
            }
        }
        return std::nullopt;
    };

    nlohmann::json document;
    std::size_t writtenCount = 0;
    try {
        document = nlohmann::json::parse(conditionsJson);
        if (!document.is_array()) {
            return std::unexpected(Error{"InvalidConditionsJson", "Could not parse conditions JSON: expected an array."});
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidConditionsJson", "Could not parse conditions JSON: " + std::string(error.what())});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(recordId);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto* record = fo4::find_record(mutated, key);
        if (record == nullptr) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Record '" + recordId + "' not found in " + pluginName + "."
            });
        }
        const auto scoped =
            fo4::schema::scoped_condition_path(record->header.signature, path);
        if (!scoped.has_value()) {
            return std::unexpected(Error{
                "ConditionPathUnsupported",
                "Native condition editing does not represent path '" + path +
                    "' on " + record->header.signature.to_string() + " records"
            });
        }
        const auto existing = fo4::schema::read_scoped_conditions(
            *record, scoped->descriptor, scoped->container_index);
        auto status = existing.error;
        if (existing.ok) {
            std::string failure;
            const auto built = buildConditions(document, loaded->plugin, existing.conditions, resolveEditorId, failure);
            if (!built) return std::unexpected(Error{"InvalidConditionsJson", failure});
            writtenCount = built->size();
            status = fo4::schema::set_record_scoped_conditions(
                mutated, key, scoped->descriptor, scoped->container_index,
                std::span<const fo4::CtdaCondition>{*built});
        }
        if (status == fo4::schema::ScopedConditionStatus::unrepresentable) {
            return std::unexpected(Error{"ConditionPayloadUnsupported", "Condition bytes cannot round-trip in this scope"});
        }
        if (status == fo4::schema::ScopedConditionStatus::container_index_out_of_range) {
            return std::unexpected(Error{
                "ConditionScopeOutOfRange",
                "Container index for '" + path + "' is out of range on record '" +
                    recordId + "'"
            });
        }
        if (status == fo4::schema::ScopedConditionStatus::record_missing ||
            status == fo4::schema::ScopedConditionStatus::record_mismatch) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Record '" + recordId + "' not found in " + pluginName + "."
            });
        }
        if (status == fo4::schema::ScopedConditionStatus::element_not_conditioned) {
            return std::unexpected(Error{
                "ConditionScopeNotConditioned",
                "'" + path + "' addresses an element that carries no conditions on record '" +
                    recordId + "'"
            });
        }
        if (status == fo4::schema::ScopedConditionStatus::alias_structure_invalid)
            return std::unexpected(Error{"ConditionAliasMalformed", "Record '" + recordId + "' has a malformed alias array for path '" + path + "'"});
        if (status == fo4::schema::ScopedConditionStatus::multiple_lists)
            return std::unexpected(Error{"ConditionScopeMultipleLists", "'" + path + "' spans more than one condition list on record '" + recordId + "'"});
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetConditionsFailed", error.what()});
    }

    auto message = "Set " + std::to_string(writtenCount) +
                   " condition(s) at " + path + " on " + recordId + " in " +
                   pluginName + ". SavePlugin to persist.";
    return nlohmann::json(std::move(message));
}

Result<nlohmann::json> RadiclePluginBackend::setField(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto field = support::stringArgument(arguments, 2);
    const auto value = support::stringArgument(arguments, 3);

    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    const bool edidField = field == "Subrecords\\EDID[0]" || field == "EDID";
    const bool eyesField = isEyesField(field);
    const bool lvspField = isLvspField(field);
    if (!edidField && !eyesField && !lvspField) {
        return std::unexpected(Error{
            "FieldWriteUnsupported",
            "Native Radicle editing supports EDID plus typed EYES and LVSP fields only"
        });
    }
    if (edidField && value.find('\0') != std::string::npos) {
        return std::unexpected(Error{"InvalidEditorId", "EDID cannot contain an embedded NUL"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{
            "PluginNotLoaded",
            "Plugin is not loaded: " + pluginName
        });
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to edit protected or game-Data plugin: " + loaded->name
        });
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                loaded->path.string()
        });
    }

    try {
        auto mutated = loaded->plugin;
        std::string status;

        if (edidField) {
            if (!fo4::set_record_editor_id(mutated, key, value)) {
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + pluginName + ": " + formKeyText
                });
            }
            status = "Updated EDID for " + formKeyText + " in " + pluginName + ".";
        } else if (eyesField) {
            if (field == "EYES\\Full") {
                fo4::forms::set_eyes_text_fields(
                    mutated, key, std::nullopt, std::string_view{value}, std::nullopt);
            } else if (field == "EYES\\Icon") {
                fo4::forms::set_eyes_text_fields(
                    mutated, key, std::nullopt, std::nullopt, std::string_view{value});
            } else {
                std::uint8_t parsed = 0;
                if (!parseUnsigned(value, parsed)) {
                    return std::unexpected(Error{
                        "InvalidFieldValue",
                        "EYES DATA flags must be 0..255"
                    });
                }
                fo4::forms::set_eyes_data_flags(mutated, key, parsed);
            }
            status = "Updated " + field + " for " + formKeyText + " in " + pluginName + ".";
        } else {
            auto model = fo4::forms::decode_leveled_spell(mutated, key);
            if (field == "LVSP\\ChanceNone") {
                std::uint8_t parsed = 0;
                if (!parseUnsigned(value, parsed)) {
                    return std::unexpected(Error{"InvalidFieldValue", "LVSP ChanceNone must be 0..255"});
                }
                fo4::forms::set_leveled_spell_settings(
                    mutated, key, parsed, model.max_count, model.flags);
            } else if (field == "LVSP\\MaxCount") {
                std::uint8_t parsed = 0;
                if (!parseUnsigned(value, parsed)) {
                    return std::unexpected(Error{"InvalidFieldValue", "LVSP MaxCount must be 0..255"});
                }
                fo4::forms::set_leveled_spell_settings(
                    mutated, key, model.chance_none, parsed, model.flags);
            } else if (field == "LVSP\\Flags") {
                std::uint8_t parsed = 0;
                if (!parseUnsigned(value, parsed) || (parsed & ~0x07u) != 0) {
                    return std::unexpected(Error{
                        "InvalidFieldValue",
                        "LVSP Flags must use only defined bits 0x01, 0x02, and 0x04"
                    });
                }
                fo4::forms::set_leveled_spell_settings(
                    mutated, key, model.chance_none, model.max_count, parsed);
            } else {
                LvspEntryPath path;
                if (!parseLvspEntryPath(field, path) || path.index >= model.entries.size()) {
                    return std::unexpected(Error{
                        "FieldWriteUnsupported",
                        "LVSP entry field path is invalid or outside the entry list"
                    });
                }
                auto& entry = model.entries[path.index];
                switch (path.field) {
                    case LvspEntryField::level: {
                        std::uint16_t parsed = 0;
                        if (!parseUnsigned(value, parsed)) {
                            return std::unexpected(Error{"InvalidFieldValue", "LVSP entry Level must be 0..65535"});
                        }
                        entry.level = parsed;
                        break;
                    }
                    case LvspEntryField::count: {
                        std::uint16_t parsed = 0;
                        if (!parseUnsigned(value, parsed)) {
                            return std::unexpected(Error{"InvalidFieldValue", "LVSP entry Count must be 0..65535"});
                        }
                        entry.count = parsed;
                        break;
                    }
                    case LvspEntryField::chanceNone: {
                        std::uint8_t parsed = 0;
                        if (!parseUnsigned(value, parsed)) {
                            return std::unexpected(Error{"InvalidFieldValue", "LVSP entry ChanceNone must be 0..255"});
                        }
                        entry.chance_none = parsed;
                        break;
                    }
                    case LvspEntryField::spell: {
                        fo4::FormKey target;
                        try {
                            target = fo4::FormKey::parse(value);
                        } catch (const std::exception& error) {
                            return std::unexpected(Error{"InvalidFieldValue", error.what()});
                        }
                        const auto* winner = graph_.winner(target);
                        if (winner == nullptr ||
                            (winner->signature != fo4::Signature::from_string("SPEL") &&
                             winner->signature != fo4::Signature::from_string("LVSP"))) {
                            return std::unexpected(Error{
                                "InvalidLeveledSpellReference",
                                "LVSP entry Spell must resolve to a loaded SPEL or LVSP record"
                            });
                        }
                        entry.spell = std::move(target);
                        break;
                    }
                }
                fo4::forms::replace_leveled_spell_entries(mutated, key, model.entries);
            }
            status = "Updated " + field + " for " + formKeyText + " in " + pluginName + ".";
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(std::move(status));
    } catch (const fo4::forms::EyesError& error) {
        return std::unexpected(Error{"EyesWriteFailed", error.what()});
    } catch (const fo4::forms::LeveledSpellError& error) {
        return std::unexpected(Error{"LeveledSpellWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::copyAsOverride(
    const nlohmann::json& arguments) {
    const auto sourceName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto targetName = support::stringArgument(arguments, 2);

    if (sourceName.empty()) {
        return std::unexpected(Error{"SourcePluginMissing", "Source plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (targetName.empty()) {
        return std::unexpected(Error{"TargetPluginMissing", "Target plugin name is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {targetName, sourceName});
    return copyAsOverrideLocked(arguments, false);
}

Result<nlohmann::json> RadiclePluginBackend::copyAsOverrideLocked(
    const nlohmann::json& arguments, bool initiallyDisabled) {
    const auto sourceName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto targetName = support::stringArgument(arguments, 2);
    bool overwrite = false;

    if (!support::tryBoolArgument(arguments, 3, overwrite)) {
        return std::unexpected(Error{"InvalidOverwriteFlag", "Overwrite flag must be boolean"});
    }
    bool allowLosing = false;
    if (!optionalBoolArgument(arguments, 4, allowLosing)) {
        return std::unexpected(Error{"InvalidAllowLosingFlag", "Allow-losing flag must be boolean"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

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
            "Refusing to write override into protected or game-Data plugin: " + target->name
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
    if (initiallyDisabled && !supportsInitiallyDisabled(sourceRecord->header.signature)) {
        return std::unexpected(Error{
            "DisabledOverrideUnsupported",
            "Initially Disabled applies only to placed references and NAVM, not " +
                sourceRecord->header.signature.to_string() + ": " + formKeyText
        });
    }
    if (!allowLosing) {
        const auto later = laterOverridesLocked(*target, key);
        if (!later.empty()) {
            return nlohmann::json(
                "LOSES: " + target->name + " loads before " + joinNames(later) +
                ", which also override " + formKeyText + ". The copied override will not win "
                "until " + target->name + " loads after them.");
        }
    }

    try {
        auto mutated = target->plugin;
        const auto status = fo4::copy_record_as_override(mutated, source->plugin, key, overwrite);
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
            case fo4::RecordCopyStatus::already_exists:
                return nlohmann::json(
                    "EXISTS: " + targetName + " already contains " + formKeyText + ".");
            case fo4::RecordCopyStatus::inserted:
            case fo4::RecordCopyStatus::replaced:
                break;
        }

        if (initiallyDisabled && !markCopyInitiallyDisabled(mutated, key)) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Copied override not found in " + targetName
            });
        }
        stagePluginMutationLocked(*target, std::move(mutated));

        return nlohmann::json(
            std::string(status == fo4::RecordCopyStatus::replaced ? "Replaced " : "Copied ") +
            formKeyText + " as override in " + targetName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CopyOverrideFailed", error.what()});
    }
}

std::vector<std::string> RadiclePluginBackend::laterOverridesLocked(
    const LoadedPlugin& target,
    const fo4::FormKey& key) const {
    const auto targetIndex = static_cast<std::size_t>(&target - plugins_.data());
    std::vector<std::string> later;
    for (const auto& version : graph_.versions(key)) {
        if (version.load_order > targetIndex) later.emplace_back(version.plugin.filename());
    }
    return later;
}

}
