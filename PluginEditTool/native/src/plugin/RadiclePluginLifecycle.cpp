#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/forms/armor.hpp>
#include <fo4/forms/cell.hpp>
#include <fo4/forms/eyes.hpp>
#include <fo4/forms/leveled_spell.hpp>
#include <fo4/forms/proj_expl_hazd.hpp>
#include <fo4/forms/race.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/schema/scalar.hpp>
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

std::optional<Error> parseFormKeyField(const nlohmann::json& object,
                                       const char* key,
                                       fo4::FormKey& value) {
    bool valid = true;
    const auto text = optionalStringField(object, key, valid);
    if (!valid) {
        return Error{
            "InvalidRecordCreationPayload",
            std::string(key) + " must be a FormKey string or null"
        };
    }
    if (!text.has_value() || text->empty()) {
        value = fo4::FormKey{};
        return std::nullopt;
    }
    try {
        value = fo4::FormKey::parse(*text);
        return std::nullopt;
    } catch (const std::exception& error) {
        return Error{"InvalidFormKey", std::string(key) + ": " + error.what()};
    }
}

bool lifecycleBoundsField(const nlohmann::json& object, fo4::forms::ObjectBounds& bounds) {
    const auto found = object.find("bounds");
    if (found == object.end() || found->is_null()) return true;
    if (!found->is_array() || found->size() != 6) return false;
    for (std::size_t i = 0; i < 6; ++i) {
        std::int16_t value = 0;
        const auto& item = (*found)[i];
        if (item.is_number_unsigned()) {
            const auto parsed = item.get<std::uint64_t>();
            if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int16_t>::max())) {
                return false;
            }
            value = static_cast<std::int16_t>(parsed);
        } else if (item.is_number_integer()) {
            const auto parsed = item.get<std::int64_t>();
            if (parsed < std::numeric_limits<std::int16_t>::min() ||
                parsed > std::numeric_limits<std::int16_t>::max()) {
                return false;
            }
            value = static_cast<std::int16_t>(parsed);
        } else {
            return false;
        }
        bounds[i] = value;
    }
    return true;
}

bool lifecycleVec3Field(const nlohmann::json& object,
                        const char* key,
                        std::array<float, 3>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) return true;
    if (!found->is_array() || found->size() != 3) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(*found)[i].is_number()) return false;
        const auto parsed = (*found)[i].get<double>();
        if (!std::isfinite(parsed) ||
            parsed < -static_cast<double>(std::numeric_limits<float>::max()) ||
            parsed > static_cast<double>(std::numeric_limits<float>::max())) {
            return false;
        }
        value[i] = static_cast<float>(parsed);
        if (!std::isfinite(value[i])) return false;
    }
    return true;
}

std::optional<Error> checkLoadedSlot(const fo4::ConflictGraph& graph,
                                     const fo4::FormKey& key,
                                     std::initializer_list<std::string_view> accepted,
                                     std::string_view slotName) {
    if (key.owner().empty()) return std::nullopt;
    const auto* winner = graph.winner(key);
    if (winner == nullptr) {
        return Error{
            "InvalidTypedFormKey",
            std::string(slotName) + " must resolve to a loaded record: " + key.to_string()
        };
    }
    if (accepted.size() == 0) return std::nullopt;
    const auto signature = winner->signature.to_string();
    for (const auto name : accepted) {
        if (signature == name) return std::nullopt;
    }
    std::string expected;
    for (const auto name : accepted) {
        if (!expected.empty()) expected += " or ";
        expected += std::string(name);
    }
    return Error{
        "InvalidTypedFormKey",
        std::string(slotName) + " " + key.to_string() + " is a " + signature +
            ", but this slot holds a " + expected
    };
}

std::optional<Error> validateProvidedScalar(const nlohmann::json& object,
                                            const char* jsonKey,
                                            std::string_view path) {
    const auto found = object.find(jsonKey);
    if (found == object.end() || found->is_null()) return std::nullopt;
    if (!found->is_number()) {
        return Error{
            "InvalidRecordCreationPayload",
            std::string(jsonKey) + " must be a number"
        };
    }
    const auto field = fo4::schema::scalar_field_descriptor(path);
    if (!field.has_value()) {
        return Error{
            "InvalidRecordCreationPayload",
            std::string(path) + " is not a generated scalar field"
        };
    }
    try {
        fo4::schema::encode_scalar(found->dump(), field->scalar);
    } catch (const fo4::schema::ScalarError& error) {
        return Error{
            "InvalidRecordCreationPayload",
            std::string(path) + ": " + error.what()
        };
    }
    return std::nullopt;
}

std::optional<Error> validateProvidedScalars(
    const nlohmann::json& object,
    std::initializer_list<std::pair<const char*, const char*>> fields) {
    for (const auto& [jsonKey, path] : fields) {
        if (const auto error = validateProvidedScalar(object, jsonKey, path)) {
            return error;
        }
    }
    return std::nullopt;
}

std::optional<Error> validateProvidedVec3(const nlohmann::json& object,
                                          const char* jsonKey,
                                          std::string_view pathX,
                                          std::string_view pathY,
                                          std::string_view pathZ) {
    const auto found = object.find(jsonKey);
    if (found == object.end() || found->is_null()) return std::nullopt;
    if (!found->is_array() || found->size() != 3) {
        return Error{
            "InvalidRecordCreationPayload",
            std::string(jsonKey) + " must be an array of three numbers"
        };
    }
    const std::array<std::string_view, 3> paths{pathX, pathY, pathZ};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(*found)[i].is_number()) {
            return Error{
                "InvalidRecordCreationPayload",
                std::string(jsonKey) + " must be an array of three numbers"
            };
        }
        const auto field = fo4::schema::scalar_field_descriptor(paths[i]);
        if (!field.has_value()) {
            return Error{
                "InvalidRecordCreationPayload",
                std::string(paths[i]) + " is not a generated scalar field"
            };
        }
        try {
            fo4::schema::encode_scalar((*found)[i].dump(), field->scalar);
        } catch (const fo4::schema::ScalarError& error) {
            return Error{
                "InvalidRecordCreationPayload",
                std::string(paths[i]) + ": " + error.what()
            };
        }
    }
    return std::nullopt;
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
        signatureText != "CELL" && signatureText != "EXPL" &&
        signatureText != "EYES" && signatureText != "HAZD" &&
        signatureText != "LVSP" && signatureText != "PROJ" &&
        signatureText != "RACE") {
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

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    // Light plugins are allowed: the small-file FormID range is proven from
    // TESFile::AdjustFormIDFileIndex (see Radicle/docs/ESL_FORMIDS.md, #26) and
    // fo4::create_local_record enforces the 0x001..0xFFF bounds itself.

    try {
        auto mutated = loaded->plugin;
        fo4::FormKey key;

        if (signatureText == "CELL") {
            const auto worldspace = payload->find("worldspace");
            if (worldspace == payload->end() || !worldspace->is_string() ||
                !payload->contains("x") || !payload->contains("y")) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "CELL creation requires worldspace FormKey, x, and y"
                });
            }

            fo4::forms::ExteriorCellCreation creation;
            try {
                creation.worldspace =
                    fo4::FormKey::parse(worldspace->get_ref<const std::string&>());
            } catch (const std::exception& error) {
                return std::unexpected(Error{"InvalidWorldspaceFormKey", error.what()});
            }
            if (!lifecycleSignedField(*payload, "x", std::int32_t{0}, creation.x) ||
                !lifecycleSignedField(*payload, "y", std::int32_t{0}, creation.y)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "CELL x and y must fit signed 32-bit storage"
                });
            }
            bool editorValid = false;
            const auto editorId = optionalStringField(*payload, "editorId", editorValid);
            if (!editorValid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "CELL editorId must be a string or null"
                });
            }
            creation.editor_id = optionalStringView(editorId);
            key = fo4::forms::create_exterior_cell(mutated, creation);
        } else if (signatureText == "EYES") {
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
        } else if (signatureText == "RACE") {
            const auto description = payload->find("description");
            if (description == payload->end() || !description->is_string() ||
                !payload->contains("maleHeight") || !payload->contains("femaleHeight")) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "RACE creation requires description, maleHeight, and femaleHeight"
                });
            }

            bool valid = false;
            const auto editorId = optionalStringField(*payload, "editorId", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "RACE editorId must be a string or null"});
            const auto fullName = optionalStringField(*payload, "fullName", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "RACE fullName must be a string or null"});

            fo4::forms::RaceCreation creation;
            creation.editor_id = optionalStringView(editorId);
            creation.full_name = optionalStringView(fullName);
            creation.description = description->get_ref<const std::string&>();
            if (!lifecycleFloatField(*payload, "maleHeight", 0.0f, creation.male_height) ||
                !lifecycleFloatField(*payload, "femaleHeight", 0.0f, creation.female_height) ||
                !lifecycleUnsignedField(*payload, "flags", std::uint32_t{0}, creation.flags) ||
                !lifecycleUnsignedField(*payload, "flags2", std::uint32_t{0}, creation.flags2) ||
                !lifecycleUnsignedField(*payload, "size", std::uint32_t{0}, creation.size) ||
                !lifecycleFloatField(*payload, "accelerationRate", 0.0f, creation.acceleration_rate) ||
                !lifecycleFloatField(*payload, "decelerationRate", 0.0f, creation.deceleration_rate) ||
                !lifecycleFloatField(*payload, "injuredHealthPct", 0.0f, creation.injured_health_pct)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "RACE numeric fields must fit their current form-131 storage"
                });
            }
            key = fo4::forms::create_race(mutated, creation);
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
        } else if (signatureText == "ARMA") {
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
        } else if (signatureText == "PROJ") {
            bool valid = false;
            const auto editorId = optionalStringField(*payload, "editorId", valid);
            if (!valid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "PROJ editorId must be a string or null"
                });
            }
            const auto name = optionalStringField(*payload, "name", valid);
            if (!valid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "PROJ name must be a string or null"
                });
            }
            const auto muzzleFlashModel = optionalStringField(*payload, "muzzleFlashModel", valid);
            if (!valid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "PROJ muzzleFlashModel must be a string or null"
                });
            }

            fo4::forms::ProjectileCreation creation;
            creation.editor_id = editorId.value_or(std::string{});
            creation.name = name;
            creation.muzzle_flash_model = muzzleFlashModel.value_or(std::string{});
            if (!lifecycleBoundsField(*payload, creation.bounds) ||
                !lifecycleUnsignedField(*payload, "flags", creation.flags, creation.flags) ||
                !lifecycleFloatField(*payload, "gravity", creation.gravity, creation.gravity) ||
                !lifecycleFloatField(*payload, "speed", creation.speed, creation.speed) ||
                !lifecycleFloatField(*payload, "range", creation.range, creation.range) ||
                !lifecycleFloatField(
                    *payload, "explosionProximity", creation.explosion_proximity,
                    creation.explosion_proximity) ||
                !lifecycleFloatField(
                    *payload, "explosionTimer", creation.explosion_timer, creation.explosion_timer) ||
                !lifecycleFloatField(
                    *payload, "muzzleFlashDuration", creation.muzzle_flash_duration,
                    creation.muzzle_flash_duration) ||
                !lifecycleFloatField(
                    *payload, "fadeOutTime", creation.fade_out_time, creation.fade_out_time) ||
                !lifecycleFloatField(*payload, "force", creation.force, creation.force) ||
                !lifecycleFloatField(
                    *payload, "coneSpread", creation.cone_spread, creation.cone_spread) ||
                !lifecycleFloatField(
                    *payload, "collisionRadius", creation.collision_radius,
                    creation.collision_radius) ||
                !lifecycleFloatField(*payload, "lifetime", creation.lifetime, creation.lifetime) ||
                !lifecycleFloatField(
                    *payload, "relaunchInterval", creation.relaunch_interval,
                    creation.relaunch_interval) ||
                !lifecycleUnsignedField(
                    *payload, "tracerFrequency", creation.tracer_frequency,
                    creation.tracer_frequency) ||
                !lifecycleUnsignedField(
                    *payload, "soundLevel", creation.sound_level, creation.sound_level)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "PROJ numeric fields must fit their measured DNAM storage"
                });
            }
            if (const auto error = validateProvidedScalars(*payload, {
                    {"flags", "PROJ\\Flags"},
                    {"gravity", "PROJ\\Gravity"},
                    {"speed", "PROJ\\Speed"},
                    {"range", "PROJ\\Range"},
                    {"explosionProximity", "PROJ\\ExplosionProximity"},
                    {"explosionTimer", "PROJ\\ExplosionTimer"},
                    {"muzzleFlashDuration", "PROJ\\MuzzleFlashDuration"},
                    {"fadeOutTime", "PROJ\\FadeOutTime"},
                    {"force", "PROJ\\Force"},
                    {"coneSpread", "PROJ\\ConeSpread"},
                    {"collisionRadius", "PROJ\\CollisionRadius"},
                    {"lifetime", "PROJ\\Lifetime"},
                    {"relaunchInterval", "PROJ\\RelaunchInterval"},
                    {"tracerFrequency", "PROJ\\TracerFrequency"},
                    {"soundLevel", "PROJ\\SoundLevel"}})) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(*payload, "light", creation.light)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "muzzleFlashLight", creation.muzzle_flash_light)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "explosionType", creation.explosion_type)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "activeSoundLoop", creation.active_sound_loop)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "countdownSound", creation.countdown_sound)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "deactivateSound", creation.deactivate_sound)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "defaultWeaponSource", creation.default_weapon_source)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(*payload, "decalData", creation.decal_data)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "collisionLayer", creation.collision_layer)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "vatsProjectile", creation.vats_projectile)) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(graph_, creation.light, {"LIGH"}, "light")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.muzzle_flash_light, {"LIGH"}, "muzzleFlashLight")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.explosion_type, {"EXPL"}, "explosionType")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.active_sound_loop, {"SNDR"}, "activeSoundLoop")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.countdown_sound, {"SNDR"}, "countdownSound")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.deactivate_sound, {"SNDR"}, "deactivateSound")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.default_weapon_source, {"WEAP"}, "defaultWeaponSource")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.decal_data, {"TXST"}, "decalData")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.collision_layer, {"COLL"}, "collisionLayer")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.vats_projectile, {"PROJ"}, "vatsProjectile")) {
                return std::unexpected(*error);
            }

            ensureOwnerMaster(mutated, creation.light.owner());
            ensureOwnerMaster(mutated, creation.muzzle_flash_light.owner());
            ensureOwnerMaster(mutated, creation.explosion_type.owner());
            ensureOwnerMaster(mutated, creation.active_sound_loop.owner());
            ensureOwnerMaster(mutated, creation.countdown_sound.owner());
            ensureOwnerMaster(mutated, creation.deactivate_sound.owner());
            ensureOwnerMaster(mutated, creation.default_weapon_source.owner());
            ensureOwnerMaster(mutated, creation.decal_data.owner());
            ensureOwnerMaster(mutated, creation.collision_layer.owner());
            ensureOwnerMaster(mutated, creation.vats_projectile.owner());
            key = fo4::forms::create_projectile(mutated, creation);
        } else if (signatureText == "EXPL") {
            bool valid = false;
            const auto editorId = optionalStringField(*payload, "editorId", valid);
            if (!valid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EXPL editorId must be a string or null"
                });
            }
            const auto name = optionalStringField(*payload, "name", valid);
            if (!valid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EXPL name must be a string or null"
                });
            }

            fo4::forms::ExplosionCreation creation;
            creation.editor_id = editorId.value_or(std::string{});
            creation.name = name;
            if (!lifecycleBoundsField(*payload, creation.bounds) ||
                !lifecycleFloatField(*payload, "force", creation.force, creation.force) ||
                !lifecycleFloatField(*payload, "damage", creation.damage, creation.damage) ||
                !lifecycleFloatField(
                    *payload, "innerRadius", creation.inner_radius, creation.inner_radius) ||
                !lifecycleFloatField(
                    *payload, "outerRadius", creation.outer_radius, creation.outer_radius) ||
                !lifecycleFloatField(
                    *payload, "imageSpaceRadius", creation.image_space_radius,
                    creation.image_space_radius) ||
                !lifecycleFloatField(
                    *payload, "verticalOffsetMult", creation.vertical_offset_mult,
                    creation.vertical_offset_mult) ||
                !lifecycleUnsignedField(*payload, "flags", creation.flags, creation.flags) ||
                !lifecycleUnsignedField(
                    *payload, "soundLevel", creation.sound_level, creation.sound_level) ||
                !lifecycleFloatField(
                    *payload, "placedObjectFadeDelay", creation.placed_object_fade_delay,
                    creation.placed_object_fade_delay) ||
                !lifecycleUnsignedField(
                    *payload, "staggerMagnitude", creation.stagger_magnitude,
                    creation.stagger_magnitude) ||
                !lifecycleVec3Field(
                    *payload, "spawnProjectileVector", creation.spawn_projectile_vector) ||
                !lifecycleFloatField(
                    *payload, "projectileSpread", creation.projectile_spread,
                    creation.projectile_spread) ||
                !lifecycleUnsignedField(
                    *payload, "projectileCount", creation.projectile_count,
                    creation.projectile_count)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "EXPL numeric fields must fit their measured DATA storage"
                });
            }
            if (const auto error = validateProvidedScalars(*payload, {
                    {"force", "EXPL\\Force"},
                    {"damage", "EXPL\\Damage"},
                    {"innerRadius", "EXPL\\InnerRadius"},
                    {"outerRadius", "EXPL\\OuterRadius"},
                    {"imageSpaceRadius", "EXPL\\ImageSpaceRadius"},
                    {"verticalOffsetMult", "EXPL\\VerticalOffsetMult"},
                    {"flags", "EXPL\\Flags"},
                    {"soundLevel", "EXPL\\SoundLevel"},
                    {"placedObjectFadeDelay", "EXPL\\PlacedObjectFadeDelay"},
                    {"staggerMagnitude", "EXPL\\StaggerMagnitude"},
                    {"projectileSpread", "EXPL\\ProjectileSpread"},
                    {"projectileCount", "EXPL\\ProjectileCount"}})) {
                return std::unexpected(*error);
            }
            if (const auto error = validateProvidedVec3(
                    *payload,
                    "spawnProjectileVector",
                    "EXPL\\SpawnProjectileVectorX",
                    "EXPL\\SpawnProjectileVectorY",
                    "EXPL\\SpawnProjectileVectorZ")) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(*payload, "light", creation.light)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(*payload, "sound1", creation.sound1)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(*payload, "sound2", creation.sound2)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "impactDataSet", creation.impact_data_set)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "impactPlacedObject", creation.impact_placed_object)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "spawnProjectile", creation.spawn_projectile)) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(graph_, creation.light, {"LIGH"}, "light")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(graph_, creation.sound1, {"SNDR"}, "sound1")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(graph_, creation.sound2, {"SNDR"}, "sound2")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.impact_data_set, {"IPDS"}, "impactDataSet")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.impact_placed_object, {}, "impactPlacedObject")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.spawn_projectile, {"PROJ"}, "spawnProjectile")) {
                return std::unexpected(*error);
            }

            ensureOwnerMaster(mutated, creation.light.owner());
            ensureOwnerMaster(mutated, creation.sound1.owner());
            ensureOwnerMaster(mutated, creation.sound2.owner());
            ensureOwnerMaster(mutated, creation.impact_data_set.owner());
            ensureOwnerMaster(mutated, creation.impact_placed_object.owner());
            ensureOwnerMaster(mutated, creation.spawn_projectile.owner());
            key = fo4::forms::create_explosion(mutated, creation);
        } else if (signatureText == "HAZD") {
            bool valid = false;
            const auto editorId = optionalStringField(*payload, "editorId", valid);
            if (!valid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "HAZD editorId must be a string or null"
                });
            }
            const auto name = optionalStringField(*payload, "name", valid);
            if (!valid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "HAZD name must be a string or null"
                });
            }
            const auto model = optionalStringField(*payload, "model", valid);
            if (!valid || !model.has_value() || model->empty()) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "HAZD creation requires a non-empty model path"
                });
            }

            fo4::forms::HazardCreation creation;
            creation.editor_id = editorId.value_or(std::string{});
            creation.name = name;
            creation.model = *model;
            if (!lifecycleBoundsField(*payload, creation.bounds) ||
                !lifecycleUnsignedField(*payload, "limit", creation.limit, creation.limit) ||
                !lifecycleFloatField(*payload, "radius", creation.radius, creation.radius) ||
                !lifecycleFloatField(*payload, "lifetime", creation.lifetime, creation.lifetime) ||
                !lifecycleFloatField(
                    *payload, "imageSpaceRadius", creation.image_space_radius,
                    creation.image_space_radius) ||
                !lifecycleFloatField(
                    *payload, "targetInterval", creation.target_interval,
                    creation.target_interval) ||
                !lifecycleUnsignedField(*payload, "flags", creation.flags, creation.flags) ||
                !lifecycleFloatField(
                    *payload, "fullEffectRadius", creation.full_effect_radius,
                    creation.full_effect_radius) ||
                !lifecycleFloatField(
                    *payload, "taperWeight", creation.taper_weight, creation.taper_weight) ||
                !lifecycleFloatField(
                    *payload, "taperCurve", creation.taper_curve, creation.taper_curve)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    "HAZD numeric fields must fit their measured DNAM storage"
                });
            }
            if (const auto error = validateProvidedScalars(*payload, {
                    {"limit", "HAZD\\Limit"},
                    {"radius", "HAZD\\Radius"},
                    {"lifetime", "HAZD\\Lifetime"},
                    {"imageSpaceRadius", "HAZD\\ImageSpaceRadius"},
                    {"targetInterval", "HAZD\\TargetInterval"},
                    {"flags", "HAZD\\Flags"},
                    {"fullEffectRadius", "HAZD\\FullEffectRadius"},
                    {"taperWeight", "HAZD\\TaperWeight"},
                    {"taperCurve", "HAZD\\TaperCurve"}})) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(*payload, "spell", creation.spell)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(*payload, "light", creation.light)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(
                    *payload, "impactDataSet", creation.impact_data_set)) {
                return std::unexpected(*error);
            }
            if (const auto error = parseFormKeyField(*payload, "sound", creation.sound)) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.spell, {"SPEL", "ENCH"}, "spell")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(graph_, creation.light, {"LIGH"}, "light")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(
                    graph_, creation.impact_data_set, {"IPDS"}, "impactDataSet")) {
                return std::unexpected(*error);
            }
            if (const auto error = checkLoadedSlot(graph_, creation.sound, {"SNDR"}, "sound")) {
                return std::unexpected(*error);
            }

            ensureOwnerMaster(mutated, creation.spell.owner());
            ensureOwnerMaster(mutated, creation.light.owner());
            ensureOwnerMaster(mutated, creation.impact_data_set.owner());
            ensureOwnerMaster(mutated, creation.sound.owner());
            key = fo4::forms::create_hazard(mutated, creation);
        } else {
            return std::unexpected(Error{
                "RecordCreationRequiresSchema",
                "Raw record creation is disabled until the schema pre-flight layer defines required fields and insertion order for " +
                    signatureText
            });
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"signature", signatureText},
            {"formKey", key.to_string()}
        }.dump());
    } catch (const fo4::forms::ArmorError& error) {
        return std::unexpected(Error{"ArmorCreateFailed", error.what()});
    } catch (const fo4::forms::CellError& error) {
        return std::unexpected(Error{"CellCreateFailed", error.what()});
    } catch (const fo4::forms::EyesError& error) {
        return std::unexpected(Error{"EyesCreateFailed", error.what()});
    } catch (const fo4::forms::LeveledSpellError& error) {
        return std::unexpected(Error{"LeveledSpellCreateFailed", error.what()});
    } catch (const fo4::forms::RaceError& error) {
        return std::unexpected(Error{"RaceCreateFailed", error.what()});
    } catch (const fo4::forms::ProjExplHazdError& error) {
        return std::unexpected(Error{"ProjExplHazdCreateFailed", error.what()});
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

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
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

        stagePluginMutationLocked(*loaded, std::move(mutated));
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

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    // Light plugins are allowed; fo4::renumber_local_record enforces the proven
    // 0x001..0xFFF small-file range. See Radicle/docs/ESL_FORMIDS.md and #26.
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

        stagePluginMutationLocked(*loaded, std::move(mutated));
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
