#include "RadiclePluginLifecycleFields.hpp"
#include "RadiclePluginSupport.hpp"

#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/schema/preflight.hpp>
#include <fo4/schema/record_constraints.hpp>

#include <span>
#include <vector>

namespace fo4ide::plugin {

using namespace lifecycle;

namespace {

Result<fo4::FormKey> createRichSchemaRecordLocked(fo4::Plugin& mutated,
                                                  std::string_view signature,
                                                  const nlohmann::json& payload) {
    const auto* editor = editorIdPayloadField(payload);
    if (editor == nullptr || !editor->is_string() ||
        editor->get_ref<const std::string&>().empty()) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            std::string(signature) + " creation requires a non-empty string editorId"});
    }
    const auto editorId = support::encodeEditorId(editor->get_ref<const std::string&>());
    if (!editorId) return std::unexpected(editorId.error());

    const auto record = fo4::Signature::from_string(signature);
    std::vector<fo4::schema::SubrecordAuthoringConstraint> rules;
    bool explicitRules = false;
    if (signature == "GLOB") {
        const auto type = payload.find("type");
        if (type == payload.end() || !type->is_string()) {
            return std::unexpected(Error{
                "InvalidRecordCreationPayload",
                "GLOB creation requires a string type: short, long, float or boolean"});
        }
        static constexpr std::uint8_t codes[] = {115, 108, 102, 98};  // Short, Long, Float, Boolean
        const auto name = type->get_ref<const std::string&>();
        const int index = name == "short"   ? 0
                          : name == "long"    ? 1
                          : name == "float"   ? 2
                          : name == "boolean" ? 3
                                              : -1;
        const std::uint8_t* data = index < 0 ? nullptr : &codes[static_cast<std::size_t>(index)];
        if (data == nullptr) {
            return std::unexpected(Error{
                "InvalidRecordCreationPayload",
                "GLOB type must be short, long, float or boolean"});
        }
        const auto constraint = fo4::schema::record_authoring_constraint(record);
        if (!constraint) {
            return std::unexpected(Error{
                "RecordCreationRequiresSchema",
                "No authoring schema is registered for GLOB"});
        }
        const auto generated = fo4::schema::authoring_subrecords(*constraint);
        rules.assign(generated.begin(), generated.end());
        bool found = false;
        for (auto& rule : rules) {
            if (rule.signature != fo4::Signature::from_string("FNAM")) continue;
            rule.minimum = 1;
            rule.maximum = 1;
            rule.default_data = data;
            rule.default_size = 1;
            found = true;
        }
        if (!found) {
            return std::unexpected(Error{
                "RecordCreationRequiresSchema",
                "GLOB's FNAM type subrecord is not in the authoring schema"});
        }
        explicitRules = true;
    }

    const auto created = explicitRules
        ? fo4::create_record(mutated, record, *editorId, std::span<const fo4::schema::SubrecordAuthoringConstraint>(rules))
        : fo4::create_record(mutated, record, *editorId);
    switch (created.status) {
        case fo4::RecordCreateStatus::created:
            break;
        case fo4::RecordCreateStatus::not_authorable:
            return std::unexpected(Error{
                "RecordCreationRequiresSchema",
                std::string(signature) + " has no independent schema creation rule"});
        case fo4::RecordCreateStatus::no_default:
            return std::unexpected(Error{
                "RecordNoDefault",
                std::string(signature) + " requires a subrecord with no usable default"});
        case fo4::RecordCreateStatus::invalid_editor_id:
            return std::unexpected(Error{"InvalidEditorId", "Editor ID is not valid for this record"});
        case fo4::RecordCreateStatus::preflight_failed:
            return std::unexpected(Error{"RecordPreflightFailed", "Generated record failed schema validation"});
    }
    for (const auto& issue : fo4::schema::preflight_authoring_record(mutated, created.key)) {
        if (issue.severity != fo4::schema::PreflightSeverity::error) continue;
        return std::unexpected(Error{
            "RecordPreflightFailed",
            issue.path.empty() ? issue.message : issue.path + ": " + issue.message});
    }
    return created.key;
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
    const bool schemaCreation = payload != nullptr && payload->contains("schema") &&
        payload->at("schema").is_boolean() && payload->at("schema").get<bool>();
    if (!schemaCreation && !generatedLayoutCreationSupported(signatureText) &&
        signatureText != "ARMA" && signatureText != "ARMO" &&
        signatureText != "CELL" && signatureText != "EXPL" &&
        signatureText != "EYES" && signatureText != "FACT" &&
        signatureText != "HAZD" && signatureText != "LVLI" &&
        signatureText != "LVLN" && signatureText != "LVSP" &&
        signatureText != "PERK" && signatureText != "PROJ" && signatureText != "RACE") {
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

        if (schemaCreation) {
            const auto* editor = editorIdPayloadField(*payload);
            if (editor != nullptr && !editor->is_string()) {
                return std::unexpected(Error{"InvalidRecordCreationPayload", "Schema editorId must be a string"});
            }
            const auto requested = editor == nullptr ? std::string{} : editor->get<std::string>();
            const auto editorId = support::encodeEditorId(requested);
            if (!editorId) return std::unexpected(editorId.error());
            const auto created = fo4::create_record(mutated, fo4::Signature::from_string(signatureText), *editorId);
            switch (created.status) {
                case fo4::RecordCreateStatus::created: key = created.key; break;
                case fo4::RecordCreateStatus::not_authorable:
                    return std::unexpected(Error{"RecordNotAuthorable", "Record has no independent schema creation rule"});
                case fo4::RecordCreateStatus::no_default:
                    return std::unexpected(Error{"RecordNoDefault", "Required subrecords have no usable default"});
                case fo4::RecordCreateStatus::invalid_editor_id:
                    return std::unexpected(Error{"InvalidEditorId", "Editor ID is not valid for this record"});
                case fo4::RecordCreateStatus::preflight_failed:
                    return std::unexpected(Error{"RecordPreflightFailed", "Generated record failed schema validation"});
            }
        } else if (signatureText == "CELL") {
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
            const auto editorId = optionalEditorIdField(*payload, editorValid);
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
            const auto editorId = optionalEditorIdField(*payload, editorValid);
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

                const auto* winner = (*graph_).winner(entry.spell);
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
            const auto editorId = optionalEditorIdField(*payload, editorValid);
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
        } else if (signatureText == "LVLI" || signatureText == "LVLN") {
            std::uint8_t chanceNone = 0;
            std::uint8_t maxCount = 0;
            std::uint8_t flags = 0;
            if (!lifecycleUnsignedField(*payload, "chanceNone", std::uint8_t{0}, chanceNone) ||
                !lifecycleUnsignedField(*payload, "maxCount", std::uint8_t{0}, maxCount) ||
                !lifecycleUnsignedField(*payload, "flags", std::uint8_t{0}, flags)) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    signatureText + " chanceNone, maxCount, and flags must be 0..255"
                });
            }
            bool editorValid = false;
            const auto editorId = optionalEditorIdField(*payload, editorValid);
            if (!editorValid) {
                return std::unexpected(Error{
                    "InvalidRecordCreationPayload",
                    signatureText + " editorId must be a string or null"
                });
            }
            key = fo4::forms::create_leveled_list(
                mutated, fo4::Signature::from_string(signatureText), optionalStringView(editorId),
                chanceNone, maxCount, flags);
        } else if (signatureText == "FACT") {
            bool valid = false;
            const auto editorId = optionalEditorIdField(*payload, valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "FACT editorId must be a string or null"});
            const auto fullName = optionalStringField(*payload, "fullName", valid);
            if (!valid) return std::unexpected(Error{"InvalidRecordCreationPayload", "FACT fullName must be a string or null"});
            std::uint32_t flags = 0;
            if (!lifecycleUnsignedField(*payload, "flags", std::uint32_t{0}, flags)) {
                return std::unexpected(Error{"InvalidRecordCreationPayload", "FACT flags must be an unsigned 32-bit value"});
            }
            key = fo4::forms::create_faction(
                mutated, optionalStringView(editorId), optionalStringView(fullName), flags);
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
            const auto editorId = optionalEditorIdField(*payload, valid);
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
            const auto editorId = optionalEditorIdField(*payload, valid);
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
            const auto editorId = optionalEditorIdField(*payload, valid);
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
        } else if (generatedLayoutCreationSupported(signatureText)) {
            const auto created = createRichSchemaRecordLocked(mutated, signatureText, *payload);
            if (!created) return std::unexpected(created.error());
            key = *created;
        } else if (signatureText == "PERK") {
            const auto created = createPerkLocked(mutated, *payload);
            if (!created) return std::unexpected(created.error());
            key = *created;
        } else if (signatureText == "PROJ" || signatureText == "EXPL" || signatureText == "HAZD") {
            const auto created = signatureText == "PROJ" ? createProjectileLocked(mutated, *payload)
                : signatureText == "EXPL"                ? createExplosionLocked(mutated, *payload)
                                                         : createHazardLocked(mutated, *payload);
            if (!created) return std::unexpected(created.error());
            key = *created;
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
    } catch (const fo4::forms::LeveledListError& error) {
        return std::unexpected(Error{
            signatureText == "LVSP" ? "LeveledSpellCreateFailed" : "LeveledListCreateFailed",
            error.what()});
    } catch (const fo4::forms::FactionError& error) {
        return std::unexpected(Error{"FactionCreateFailed", error.what()});
    } catch (const fo4::forms::RaceError& error) {
        return std::unexpected(Error{"RaceCreateFailed", error.what()});
    } catch (const fo4::forms::ProjExplHazdError& error) {
        return std::unexpected(Error{"ProjExplHazdCreateFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        const auto code = signatureText == "CELL" ? "CellCreateFailed"
            : signatureText == "EYES"             ? "EyesCreateFailed"
            : signatureText == "RACE"             ? "RaceCreateFailed"
            : (signatureText == "ARMO" || signatureText == "ARMA") ? "ArmorCreateFailed"
            : signatureText == "LVSP"             ? "LeveledSpellCreateFailed"
            : (signatureText == "LVLI" || signatureText == "LVLN") ? "LeveledListCreateFailed"
            : signatureText == "FACT"             ? "FactionCreateFailed"
                                                  : "ProjExplHazdCreateFailed";
        return std::unexpected(Error{code, error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordCreateFailed", error.what()});
    }
}

std::optional<Error> RadiclePluginBackend::requireReferenceCoverage(
    const fo4::FormKey& key,
    std::string_view operation) const {
    for (const auto& candidate : plugins_) {
        auto coverageProbe = candidate.plugin;
        try {
            fo4::rekey_form_links(coverageProbe, key, key);
        } catch (const std::exception& error) {
            return Error{
                "ReferenceCoverageIncomplete",
                "Cannot prove " + std::string(operation) + " safety while scanning " +
                    candidate.name + ": " + error.what()
            };
        }
    }
    return std::nullopt;
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
        if (const auto error = requireReferenceCoverage(key, "delete")) {
            return std::unexpected(*error);
        }

        for (const auto& version : (*graph_).versions(key)) {
            if (version.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot delete a record that has an override in another loaded plugin: " +
                        std::string(version.plugin.filename())
                });
            }
        }
        if (!(*graph_).referenced_by(key).empty()) {
            return std::unexpected(Error{
                "RecordReferenced",
                "Cannot delete a record while loaded records still reference it: " + formKeyText
            });
        }
        for (const auto& removed : fo4::records_removed_by_delete(loaded->plugin, key)) {
            const bool ownedElsewhere = removed.owner() != loaded->plugin.key;
            for (const auto& version : (*graph_).versions(removed)) {
                if (ownedElsewhere || version.plugin == loaded->plugin.key) continue;
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot delete " + formKeyText + ": " + std::string(version.plugin.filename()) +
                        " overrides " + removed.to_string() + " inside it"
                });
            }
            const bool masterKeepsRecord = ownedElsewhere && std::ranges::any_of(
                (*graph_).versions(removed),
                [&](const auto& version) { return version.plugin == removed.owner(); });
            for (const auto& reference : (*graph_).referenced_by(removed)) {
                if (reference.plugin == loaded->plugin.key || masterKeepsRecord) continue;
                return std::unexpected(Error{
                    "RecordReferenced",
                    "Cannot delete " + formKeyText + ": " + reference.source.to_string() + " in " +
                        std::string(reference.plugin.filename()) + " refers to " + removed.to_string() + " inside it"
                });
            }
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
        if (const auto error = requireReferenceCoverage(oldKey, "renumber")) {
            return std::unexpected(*error);
        }

        for (const auto& version : (*graph_).versions(oldKey)) {
            if (version.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot renumber a record that has an override in another loaded plugin: " +
                        std::string(version.plugin.filename())
                });
            }
        }
        for (const auto& reference : (*graph_).referenced_by(oldKey)) {
            if (reference.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordReferencedExternally",
                    "Cannot renumber a record referenced by another loaded plugin: " +
                        std::string(reference.plugin.filename())
                });
            }
        }

        auto mutated = loaded->plugin;
        if (newKey != oldKey && fo4::find_record(mutated, newKey) == nullptr) {
            if (const auto refusal = support::parentGroupRelabelRefusal(mutated, oldKey)) return std::unexpected(*refusal);
        }
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
