#pragma once

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
#include <fo4/forms/faction.hpp>
#include <fo4/forms/leveled_list.hpp>
#include <fo4/forms/leveled_spell.hpp>
#include <fo4/forms/proj_expl_hazd.hpp>
#include <fo4/forms/race.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin::lifecycle {

inline std::string lifecycleStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

inline const nlohmann::json* lifecycleObjectArgument(const nlohmann::json& args,
                                                     std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_object()) return nullptr;
    return &args[index];
}

template <class UInt>
inline bool lifecycleUnsignedField(const nlohmann::json& object,
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
inline bool lifecycleSignedField(const nlohmann::json& object,
                                 const char* key,
                                 SInt defaultValue,
                                 SInt& value) {
    const auto found = object.find(key);
    if (found == object.end()) {
        value = defaultValue;
        return true;
    }
    if (!found->is_number_integer()) return false;
    if (found->is_number_unsigned() &&
        found->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    const auto parsed = found->get<std::int64_t>();
    if (parsed < static_cast<std::int64_t>(std::numeric_limits<SInt>::min()) ||
        parsed > static_cast<std::int64_t>(std::numeric_limits<SInt>::max())) {
        return false;
    }
    value = static_cast<SInt>(parsed);
    return true;
}

inline bool lifecycleFloatField(const nlohmann::json& object,
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

inline std::optional<std::string> optionalStringField(const nlohmann::json& object,
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

inline std::optional<std::string_view> optionalStringView(
    const std::optional<std::string>& value) {
    if (!value.has_value()) return std::nullopt;
    return std::string_view{*value};
}

inline std::optional<std::string> optionalEditorIdField(const nlohmann::json& object, bool& valid) {
    auto editorId = optionalStringField(object, "editorId", valid);
    if (editorId.has_value() && editorId->empty()) return std::nullopt;
    return editorId;
}

// Constraint: these records create from generated defaults alone; payload adds only the editor ID.
inline bool generatedLayoutCreationSupported(std::string_view signature) {
    return signature == "QUST" || signature == "GLOB" || signature == "SPEL" ||
           signature == "MGEF" || signature == "FLST" || signature == "MESG";
}

// Constraint: `editorId` is the payload key for every typed creator; `editor_id` is an accepted alias.
inline const nlohmann::json* editorIdPayloadField(const nlohmann::json& payload) {
    auto found = payload.find("editorId");
    if (found == payload.end()) found = payload.find("editor_id");
    return found == payload.end() ? nullptr : &*found;
}

inline std::optional<Error> parseFormKeyField(const nlohmann::json& object,
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

inline bool lifecycleBoundsField(const nlohmann::json& object, fo4::forms::ObjectBounds& bounds) {
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

inline bool lifecycleVec3Field(const nlohmann::json& object,
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

inline std::optional<Error> checkLoadedSlot(const fo4::ConflictGraph& graph,
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

inline std::optional<Error> validateProvidedScalar(const nlohmann::json& object,
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

inline std::optional<Error> validateProvidedScalars(
    const nlohmann::json& object,
    std::initializer_list<std::pair<const char*, const char*>> fields) {
    for (const auto& [jsonKey, path] : fields) {
        if (const auto error = validateProvidedScalar(object, jsonKey, path)) {
            return error;
        }
    }
    return std::nullopt;
}

inline std::optional<Error> validateProvidedVec3(const nlohmann::json& object,
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

}
