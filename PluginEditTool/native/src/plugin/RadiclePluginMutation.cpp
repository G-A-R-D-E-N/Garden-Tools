#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
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

#include <fo4/forms/eyes.hpp>
#include <fo4/forms/leveled_spell.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool boolArgument(const nlohmann::json& args, std::size_t index, bool& value) {
    if (!args.is_array() || index >= args.size() || !args[index].is_boolean()) return false;
    value = args[index].get<bool>();
    return true;
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
    return !officialPluginName(loaded.name);
}

Result<nlohmann::json> RadiclePluginBackend::invokeBackend(
    std::string_view method,
    const nlohmann::json& arguments) {
    // All write guards live inside the mutating functions themselves, so this
    // shim is a pure name mapping and cannot be bypassed to skip protection.
    // SetField routes through the scalar-schema-aware entry point, identical
    // to invoke("backend.SetField").
    if (method == "SetField") return setFieldValue(arguments);
    if (method == "CopyAsOverride") return copyAsOverride(arguments);
    if (method == "CopyAsOverrideMany") return copyAsOverrideMany(arguments);
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

Result<nlohmann::json> RadiclePluginBackend::setField(const nlohmann::json& arguments) {
    const auto pluginName = stringArgument(arguments, 0);
    const auto formKeyText = stringArgument(arguments, 1);
    const auto field = stringArgument(arguments, 2);
    const auto value = stringArgument(arguments, 3);

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

    std::scoped_lock lock(mutex_);
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

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
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
    const auto sourceName = stringArgument(arguments, 0);
    const auto formKeyText = stringArgument(arguments, 1);
    const auto targetName = stringArgument(arguments, 2);
    bool overwrite = false;

    if (sourceName.empty()) {
        return std::unexpected(Error{"SourcePluginMissing", "Source plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (targetName.empty()) {
        return std::unexpected(Error{"TargetPluginMissing", "Target plugin name is empty"});
    }
    if (!boolArgument(arguments, 3, overwrite)) {
        return std::unexpected(Error{"InvalidOverwriteFlag", "Overwrite flag must be boolean"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
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
    if (target->plugin.header.tes4_flags.is_master()) {
        return std::unexpected(Error{
            "MasterOverrideWriteUnsupported",
            "Native override insertion into master-flagged targets is disabled until TES4 ONAM override metadata is maintained"
        });
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
            case fo4::RecordCopyStatus::already_exists:
                return nlohmann::json(
                    "EXISTS: " + targetName + " already contains " + formKeyText + ".");
            case fo4::RecordCopyStatus::inserted:
            case fo4::RecordCopyStatus::replaced:
                break;
        }

        fo4::write_plugin_file(mutated, target->path);
        target->plugin = std::move(mutated);
        {
            std::error_code ec;
            target->loadedTime = std::filesystem::last_write_time(target->path, ec);
        }
        rebuildGraphLocked();

        return nlohmann::json(
            std::string(status == fo4::RecordCopyStatus::replaced ? "Replaced " : "Copied ") +
            formKeyText + " as override in " + targetName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CopyOverrideFailed", error.what()});
    }
}

}
