#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/forms/armor.hpp>
#include <fo4/forms/eyes.hpp>
#include <fo4/forms/leveled_spell.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
namespace {

std::string lifecycleStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

const nlohmann::json* lifecycleObjectArgument(const nlohmann::json& args,
                                              std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_object()) return nullptr;
    return &args[index];
}

template <class UInt>
bool lifecycleUnsignedField(const nlohmann::json& object,
                            const char* key,
                            UInt defaultValue,
                            UInt& value) {
    const auto found = object.find(key);
    if (found == object.end()) {
        value = defaultValue;
        return true;
    }
    std::uint64_t parsed = 0;
    if (found->is_number_unsigned()) {
        parsed = found->get<std::uint64_t>();
    } else if (found->is_number_integer()) {
        const auto signedValue = found->get<std::int64_t>();
        if (signedValue < 0) return false;
        parsed = static_cast<std::uint64_t>(signedValue);
    } else {
        return false;
    }
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<UInt>::max())) return false;
    value = static_cast<UInt>(parsed);
    return true;
}

template <class SInt>
bool lifecycleSignedField(const nlohmann::json& object,
                          const char* key,
                          SInt defaultValue,
                          SInt& value) {
    const auto found = object.find(key);
    if (found == object.end()) {
        value = defaultValue;
        return true;
    }
    if (!found->is_number_integer()) return false;
    const auto parsed = found->get<std::int64_t>();
    if (parsed < static_cast<std::int64_t>(std::numeric_limits<SInt>::min()) ||
        parsed > static_cast<std::int64_t>(std::numeric_limits<SInt>::max())) {
        return false;
    }
    value = static_cast<SInt>(parsed);
    return true;
}

bool lifecycleFloatField(const nlohmann::json& object,
                         const char* key,
                         float defaultValue,
                         float& value) {
    const auto found = object.find(key);
    if (found == object.end()) {
        value = defaultValue;
        return true;
    }
    if (!found->is_number()) return false;
    const auto parsed = found->get<double>();
    if (!std::isfinite(parsed) ||
        parsed < -static_cast<double>(std::numeric_limits<float>::max()) ||
        parsed > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    value = static_cast<float>(parsed);
    return std::isfinite(value);
}

std::optional<std::string> optionalStringField(const nlohmann::json& object,
                                               const char* key,
                                               bool& valid) {
    valid = true;
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) return std::nullopt;
    if (!found->is_string()) {
        valid = false;
        return std::nullopt;
    }
    return found->get<std::string>();
}

std::optional<std::string_view> optionalStringView(
    const std::optional<std::string>& value) {
    if (!value.has_value()) return std::nullopt;
    return std::string_view{*value};
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::createRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto signatureText = lifecycleStringArgument(arguments, 1);
    const auto* payload = lifecycleObjectArgument(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (signatureText.size() != 4) {
        return std::unexpected(Error{
            "InvalidRecordSignature",
            "Record signature must contain exactly four characters"
        });
    }
    if (signatureText != "ARMA" && signatureText != "ARMO" &&
        signatureText != "EYES" && signatureText != "LVSP") {
        return std::unexpected(Error{
            "RecordCreationRequiresSchema",
            "Raw record creation is disabled until the schema pre-flight layer defines required fields and insertion order for " +
                signatureText
        });
    }
    if (payload == nullptr) {
        return std::unexpected(Error{
            "RecordCreationPayloadMissing",
            "Typed " + signatureText + " creation requires an object payload"
        });
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (loaded->plugin.header.tes4_flags.is_small_file()) {
        return std::unexpected(Error{
            "LightPluginLifecycleUnsupported",
            "Creating records in light plugins is disabled until the engine's small-file FormID predicate is proven"
        });
    }

    try {
        auto mutated = loaded->plugin;
        fo4::FormKey key;

        if (signatureText == "EYES") {
            const auto full = payload->find("fullText");
            const auto icon = payload->find("icon");
            if (full == payload->end() || !full->is_string() ||
                icon == payload->end() || !icon->is_string()) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EYES creation requires string fields fullText and icon"
                });
            }

            bool editorValid = false;
            const auto editorId = optionalStringField(*payload, "editorId", editorValid);
            if (!editorValid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EYES editorId must be a string or null"
                });
            }

            std::uint8_t dataFlags = 0;
            std::uint32_t recordFlags = 0;
            if (!lifecycleUnsignedField(*payload, "dataFlags", std::uint8_t{0}, dataFlags) ||
                !lifecycleUnsignedField(*payload, "recordFlags", std::uint32_t{0}, recordFlags)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EYES dataFlags must be 0..255 and recordFlags must be an unsigned 32-bit value"
                });
            }

            key = fo4::forms::create_eyes(
                mutated,
                optionalStringView(editorId),
                full->get_ref<const std::string&>(),
                icon->get_ref<const std::string&>(),
                dataFlags,
                fo4::RecordFlags{recordFlags});
        } else if (signatureText == "LVSP") {
            std::uint8_t chanceNone = 0;
            std::uint8_t maxCount = 0;
            std::uint8_t flags = 0;
            if (!lifecycleUnsignedField(*payload, "chanceNone", std::uint8_t{0}, chanceNone) ||
                !lifecycleUnsignedField(*payload, "maxCount", std::uint8_t{0}, maxCount) ||
                !lifecycleUnsignedField(*payload, "flags", std::uint8_t{0}, flags)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "LVSP chanceNone, maxCount, and flags must be 0..255"
                });
            }

            const auto entriesField = payload->find("entries");
            if (entriesField == payload->end() || !entriesField->is_array()) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "LVSP creation requires an entries array"
                });
            }

            std::vector<fo4::forms::LeveledSpellEntry> entries;
            entries.reserve(entriesField->size());
            for (const auto& item : *entriesField) {
                if (!item.is_object()) {
                    return std::unexpected(Error{
                        "InvalidRecordCreationPayload",
                        "Every LVSP entry must be an object"
                    });
                }
                const auto spell = item.find("spell");
                if (spell == item.end() || !spell->is_string()) {
                    return std::unexpected(Error{
                        "InvalidRecordCreationPayload",
                        "Every LVSP entry requires a spell FormKey string"
                    });
                }

                fo4::forms::LeveledSpellEntry entry;
                if (!lifecycleUnsignedField(item, "level", std::uint16_t{1}, entry.level) ||
                    !lifecycleUnsignedField(item, "count", std::uint16_t{1}, entry.count) ||
                    !lifecycleUnsignedField(item, "chanceNone", std::uint8_t{0}, entry.chance_none)) {
                    return std::unexpected(Error{
                        "InvalidRecordCreationPayload",
                        "LVSP entry level/count must be 0..65535 and chanceNone must be 0..255"
                    });
                }
                try {
                    entry.spell = fo4::FormKey::parse(spell->get_ref<const std::string&>());
                } catch (const std::exception& error) {
                    return std::unexpected(Error{"InvalidLeveledSpellReference", error.what()});
                }

                const auto* winner = graph_.winner(entry.spell);
                if (winner == nullptr ||
                    (winner->signature != fo4::Signature::from_string("SPEL") &&
                     winner->signature != fo4::Signature::from_string("LVSP"))) {
                    return std::unexpected(Error{
                        "InvalidLeveledSpellReference",
                        "LVSP entry spell must resolve to a loaded SPEL or LVSP record: " +
                            entry.spell.to_string()
                    });
                }
                entries.push_back(std::move(entry));
            }

            bool editorValid = false;
            const auto editorId = optionalStringField(*payload, "editorId", editorValid);
            if (!editorValid ||
                (editorId.has_value() && editorId->find('\0') != std::string::npos)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "LVSP editorId must be a string without embedded NUL bytes or null"
                });
            }

            key = fo4::forms::create_leveled_spell(
                mutated, chanceNone, maxCount, flags, entries);
            if (editorId.has_value() &&
                !fo4::set_record_editor_id(mutated, key, *editorId)) {
                return std::unexpected(Error{
                    "RecordCreateFailed",
                    "Newly created LVSP record could not accept its editor ID"
                });
            }
        } else if (signatureText == "ARMO") {
            bool valid = false;
            const auto editorId = optionalStringField(*payload, "editorId", valid);
            if (!valid) {
                return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMO editorId must be a string or null"});
            }
            const auto maleWorld = optionalStringField(*payload, "maleWorldModel", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMO maleWorldModel must be a string or null"});
            const auto maleIcon = optionalStringField(*payload, "maleIcon", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMO maleIcon must be a string or null"});
            const auto maleMessage = optionalStringField(*payload, "maleMessageIcon", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMO maleMessageIcon must be a string or null"});
            const auto femaleWorld = optionalStringField(*payload, "femaleWorldModel", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMO femaleWorldModel must be a string or null"});
            const auto femaleIcon = optionalStringField(*payload, "femaleIcon", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMO femaleIcon must be a string or null"});
            const auto femaleMessage = optionalStringField(*payload, "femaleMessageIcon", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMO femaleMessageIcon must be a string or null"});

            fo4::forms::ArmorCreation creation;
            creation.editor_id = optionalStringView(editorId);
            creation.male_world_model = optionalStringView(maleWorld);
            creation.male_icon = optionalStringView(maleIcon);
            creation.male_message_icon = optionalStringView(maleMessage);
            creation.female_world_model = optionalStringView(femaleWorld);
            creation.female_icon = optionalStringView(femaleIcon);
            creation.female_message_icon = optionalStringView(femaleMessage);
            if (!lifecycleSignedField(*payload, "value", std::int32_t{0}, creation.value) ||
                !lifecycleFloatField(*payload, "weight", 0.0f, creation.weight) ||
                !lifecycleUnsignedField(*payload, "health", std::uint32_t{0}, creation.health)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "ARMO value must be int32, weight must be finite f32, and health must be uint32"
                });
            }
            key = fo4::forms::create_armor(mutated, creation);
        } else {
            bool valid = false;
            const auto editorId = optionalStringField(*payload, "editorId", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMA editorId must be a string or null"});
            const auto maleBiped = optionalStringField(*payload, "maleBipedModel", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMA maleBipedModel must be a string or null"});
            const auto femaleBiped = optionalStringField(*payload, "femaleBipedModel", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMA femaleBipedModel must be a string or null"});
            const auto maleFirst = optionalStringField(*payload, "maleFirstPersonModel", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMA maleFirstPersonModel must be a string or null"});
            const auto femaleFirst = optionalStringField(*payload, "femaleFirstPersonModel", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "ARMA femaleFirstPersonModel must be a string or null"});

            fo4::forms::ArmorAddonCreation creation;
            creation.editor_id = optionalStringView(editorId);
            creation.male_biped_model = optionalStringView(maleBiped);
            creation.female_biped_model = optionalStringView(femaleBiped);
            creation.male_first_person_model = optionalStringView(maleFirst);
            creation.female_first_person_model = optionalStringView(femaleFirst);
            if (!lifecycleUnsignedField(*payload, "malePriority", std::uint8_t{0}, creation.male_priority) ||
                !lifecycleUnsignedField(*payload, "femalePriority", std::uint8_t{0}, creation.female_priority) ||
                !lifecycleUnsignedField(*payload, "maleWeightSlider", std::uint8_t{0}, creation.male_weight_slider) ||
                !lifecycleUnsignedField(*payload, "femaleWeightSlider", std::uint8_t{0}, creation.female_weight_slider) ||
                !lifecycleUnsignedField(*payload, "detectionSoundValue", std::uint8_t{0}, creation.detection_sound_value) ||
                !lifecycleFloatField(*payload, "weaponAdjust", 0.0f, creation.weapon_adjust)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "ARMA priority/slider/detection values must be uint8 and weaponAdjust must be finite f32"
                });
            }
            key = fo4::forms::create_armor_addon(mutated, creation);
        }

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"signature", signatureText},
            {"formKey", key.to_string()}
        }.dump());
    } catch (const fo4::forms::ArmorError& error) {
        return std::unexpected(Error{"ArmorCreateFailed", error.what()});
    } catch (const fo4::forms::EyesError& error) {
        return std::unexpected(Error{"EyesCreateFailed", error.what()});
    } catch (const fo4::forms::LeveledSpellError& error) {
        return std::unexpected(Error{"LeveledSpellCreateFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordCreateFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::deleteRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto formKeyText = lifecycleStringArgument(arguments, 1);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
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
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (key.owner() != loaded->plugin.key) {
        return std::unexpected(Error{
            "RecordNotLocal",
            "Only records owned by the target plugin can be deleted: " + formKeyText
        });
    }

    try {
        // The graph is only as complete as the schema scanner. Strictly walk
        // every loaded plugin first so unsupported VMAD or malformed NAVM/NAVI
        // cannot hide an inbound reference to the record being deleted.
        for (const auto& candidate : plugins_) {
            auto coverageProbe = candidate.plugin;
            try {
                fo4::rekey_form_links(coverageProbe, key, key);
            } catch (const std::exception& error) {
                return std::unexpected(Error{
                    "ReferenceCoverageIncomplete",
                    "Cannot prove delete safety while scanning " + candidate.name + ": " + error.what()
                });
            }
        }

        for (const auto& version : graph_.versions(key)) {
            if (version.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot delete a record that has an override in another loaded plugin: " +
                        std::string(version.plugin.filename())
                });
            }
        }
        if (!graph_.referenced_by(key).empty()) {
            return std::unexpected(Error{
                "RecordReferenced",
                "Cannot delete a record while loaded records still reference it: " + formKeyText
            });
        }

        auto mutated = loaded->plugin;
        const auto status = fo4::delete_local_record(mutated, key);
        switch (status) {
            case fo4::RecordDeleteStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
            case fo4::RecordDeleteStatus::not_local:
                return std::unexpected(Error{"RecordNotLocal", "Record is not owned by " + pluginName});
            case fo4::RecordDeleteStatus::referenced:
                return std::unexpected(Error{"RecordReferenced", "Record is still referenced: " + formKeyText});
            case fo4::RecordDeleteStatus::deleted:
                break;
        }

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"deleted", formKeyText}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordDeleteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::renumberRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto oldFormKeyText = lifecycleStringArgument(arguments, 1);
    const auto newFormKeyText = lifecycleStringArgument(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (oldFormKeyText.empty() || newFormKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Old and new record FormKeys are required"});
    }

    fo4::FormKey oldKey;
    fo4::FormKey newKey;
    try {
        oldKey = fo4::FormKey::parse(oldFormKeyText);
        newKey = fo4::FormKey::parse(newFormKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (loaded->plugin.header.tes4_flags.is_small_file()) {
        return std::unexpected(Error{
            "LightPluginLifecycleUnsupported",
            "Renumbering records in light plugins is disabled until the engine's small-file FormID predicate is proven"
        });
    }
    if (oldKey.owner() != loaded->plugin.key || newKey.owner() != loaded->plugin.key) {
        return std::unexpected(Error{
            "RecordNotLocal",
            "Renumbering requires both old and new FormKeys to be owned by " + pluginName
        });
    }

    try {
        for (const auto& candidate : plugins_) {
            auto coverageProbe = candidate.plugin;
            try {
                fo4::rekey_form_links(coverageProbe, oldKey, oldKey);
            } catch (const std::exception& error) {
                return std::unexpected(Error{
                    "ReferenceCoverageIncomplete",
                    "Cannot prove renumber safety while scanning " + candidate.name + ": " + error.what()
                });
            }
        }

        for (const auto& version : graph_.versions(oldKey)) {
            if (version.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot renumber a record that has an override in another loaded plugin: " +
                        std::string(version.plugin.filename())
                });
            }
        }
        for (const auto& reference : graph_.referenced_by(oldKey)) {
            if (reference.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordReferencedExternally",
                    "Cannot renumber a record referenced by another loaded plugin: " +
                        std::string(reference.plugin.filename())
                });
            }
        }

        auto mutated = loaded->plugin;
        const auto status = fo4::renumber_local_record(mutated, oldKey, newKey.local());
        switch (status) {
            case fo4::RecordRenumberStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present: " + oldFormKeyText});
            case fo4::RecordRenumberStatus::not_local:
                return std::unexpected(Error{"RecordNotLocal", "Record is not owned by " + pluginName});
            case fo4::RecordRenumberStatus::target_exists:
                return std::unexpected(Error{"RecordFormIdExists", "Target FormKey already exists: " + newFormKeyText});
            case fo4::RecordRenumberStatus::unchanged:
                return nlohmann::json(nlohmann::json{
                    {"ok", true},
                    {"plugin", pluginName},
                    {"oldFormKey", oldFormKeyText},
                    {"newFormKey", newFormKeyText},
                    {"changed", false}
                }.dump());
            case fo4::RecordRenumberStatus::renumbered:
                break;
        }

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"oldFormKey", oldFormKeyText},
            {"newFormKey", newFormKeyText},
            {"changed", true}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordRenumberFailed", error.what()});
    }
}

}