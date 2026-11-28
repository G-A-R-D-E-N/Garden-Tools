#include "RadiclePluginQuestInternal.hpp"
#include "RadiclePluginSupport.hpp"
#include "RadiclePluginCellReferenceInternal.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/environment/cell_view.hpp>
#include <fo4/environment/placed.hpp>
#include <fo4/flags.hpp>
#include <fo4/pex/pex.hpp>
#include <fo4/schema/vmad.hpp>

namespace fo4ide::plugin {

namespace {

using fo4::env::Environment;
using fo4::env::PlacedReferenceData;
using fo4::env::RecordRef;

struct NamedBit {
    std::uint32_t bit;
    const char* name;
};

constexpr NamedBit k_default_flags[] = {
    {4, "Ground Piece"}, {8, "LOD Respects Enable State"}, {10, "Persistent"}, {11, "Initially Disabled"},
    {16, "Is Full LOD"}, {26, "Filter (Collision Geometry)"}, {27, "Bounding Box (Collision Geometry)"},
    {28, "Reflected By Auto Water"}, {30, "Ground"}, {31, "Multibound"},
};

constexpr NamedBit k_static_flags[] = {
    {4, "Ground Piece"}, {8, "LOD Respects Enable State"}, {9, "Hidden From Local Map"}, {10, "Persistent"},
    {11, "Initially Disabled"}, {15, "Visible when distant"}, {16, "Is Full LOD"},
    {26, "Filter (Collision Geometry)"}, {27, "Bounding Box (Collision Geometry)"},
    {28, "Reflected By Auto Water"}, {29, "Don't Havok Settle"}, {30, "No Respawn"}, {31, "Multibound"},
};

constexpr NamedBit k_container_flags[] = {
    {4, "Ground Piece"}, {10, "Persistent"}, {11, "Initially Disabled"}, {16, "Is Full LOD"},
    {25, "No AI Acquire"}, {26, "Filter (Collision Geometry)"}, {27, "Bounding Box (Collision Geometry)"},
    {29, "Don't Havok Settle"}, {30, "Ground"}, {31, "Multibound"},
};

constexpr NamedBit k_door_flags[] = {
    {2, "Minimal Use Door"}, {6, "Hidden From Local Map"}, {8, "Inaccessible"}, {10, "Persistent"},
    {11, "Initially Disabled"}, {16, "Is Full LOD"}, {26, "Filter (Collision Geometry)"},
    {27, "Bounding Box (Collision Geometry)"}, {29, "Don't Havok Settle"}, {30, "No Respawn"}, {31, "Multibound"},
};

constexpr NamedBit k_light_flags[] = {
    {8, "Doesn't Light Water"}, {9, "Casts Shadows"}, {10, "Persistent"}, {11, "Initially Disabled"},
    {16, "Never Fades"}, {17, "Doesn't Light Landscape"}, {25, "No AI Acquire"},
    {28, "Reflected By Auto Water"}, {29, "Don't Havok Settle"}, {30, "No Respawn"}, {31, "Multibound"},
};

constexpr NamedBit k_movable_static_flags[] = {
    {4, "Ground Piece"}, {9, "Motion Blur"}, {10, "Persistent"}, {11, "Initially Disabled"},
    {16, "Is Full LOD"}, {26, "Filter (Collision Geometry)"}, {27, "Bounding Box (Collision Geometry)"},
    {28, "Reflected By Auto Water"}, {29, "Don't Havok Settle"}, {30, "No Respawn"}, {31, "Multibound"},
};

constexpr NamedBit k_addon_node_flags[] = {
    {10, "Persistent"}, {11, "Initially Disabled"}, {16, "Is Full LOD"}, {28, "Reflected By Auto Water"},
    {29, "Don't Havok Settle"}, {30, "No Respawn"}, {31, "Multibound"},
};

constexpr NamedBit k_item_flags[] = {
    {4, "Ground Piece"}, {10, "Persistent"}, {11, "Initially Disabled"}, {16, "Is Full LOD"},
    {25, "No AI Acquire"}, {28, "Reflected By Auto Water"}, {29, "Don't Havok Settle"},
    {30, "No Respawn"}, {31, "Multibound"},
};

constexpr NamedBit k_placed_npc_flags[] = {
    {9, "Starts Dead"}, {10, "Persistent"}, {11, "Initially Disabled"}, {13, "Starts Unconscious"},
    {25, "No AI Acquire"}, {29, "Don't Havok Settle"},
};

constexpr std::string_view k_no_respawn_name = "No Respawn";

std::span<const NamedBit> tableFor(const fo4::Signature refSignature, std::string_view baseSignature) {
    if (refSignature.to_string() == "ACHR") return k_placed_npc_flags;
    if (baseSignature == "STAT" || baseSignature == "ACTI" || baseSignature == "SCOL" || baseSignature == "TREE") {
        return k_static_flags;
    }
    if (baseSignature == "CONT" || baseSignature == "TERM") return k_container_flags;
    if (baseSignature == "DOOR") return k_door_flags;
    if (baseSignature == "LIGH") return k_light_flags;
    if (baseSignature == "MSTT") return k_movable_static_flags;
    if (baseSignature == "ADDN") return k_addon_node_flags;
    if (baseSignature == "ALCH" || baseSignature == "BOOK" || baseSignature == "AMMO" || baseSignature == "ARMO" ||
        baseSignature == "INGR" || baseSignature == "KEYM" || baseSignature == "MISC" || baseSignature == "FURN" ||
        baseSignature == "WEAP") {
        return k_item_flags;
    }
    return k_default_flags;
}

constexpr std::uint32_t k_locked_record_flags = fo4::RecordFlags::persistent | fo4::RecordFlags::deleted;

std::uint32_t editableMask(std::span<const NamedBit> table) {
    std::uint32_t mask = 0;
    for (const auto& flag : table) {
        if (flag.bit < 32) mask |= 1u << flag.bit;
    }
    return mask & ~k_locked_record_flags;
}

const char* lockLevelName(std::uint8_t level) {
    switch (level) {
        case 0: return "None";
        case 1: return "Novice 1";
        case 25: return "Novice 25";
        case 50: return "Advanced";
        case 75: return "Expert";
        case 100: return "Master";
        case 251: return "Barred";
        case 252: return "Chained";
        case 253: return "Requires Terminal";
        case 254: return "Inaccessible";
        case 255: return "Requires Key";
        default: return nullptr;
    }
}

const char* vmadTypeName(std::uint8_t type) {
    switch (type) {
        case 0: return "none";
        case 1: return "object";
        case 2: return "string";
        case 3: return "int";
        case 4: return "float";
        case 5: return "bool";
        case 6: return "variable";
        case 7: return "struct";
        case 11: return "objectArray";
        case 12: return "stringArray";
        case 13: return "intArray";
        case 14: return "floatArray";
        case 15: return "boolArray";
        case 16: return "variableArray";
        case 17: return "structArray";
        default: return "unknown";
    }
}

std::uint32_t readU32(const std::vector<std::byte>& data, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
    }
    return value;
}

const fo4::Subrecord* findSubrecord(const RecordRef& ref, std::string_view signature) {
    if (ref.record == nullptr) return nullptr;
    for (const auto& sub : ref.record->subrecords) {
        if (sub.signature.to_string() == signature) return &sub;
    }
    return nullptr;
}

nlohmann::json vec3Json(const fo4::env::Vec3& value) {
    return {{"x", value[0]}, {"y", value[1]}, {"z", value[2]}};
}

nlohmann::json linkJson(const Environment& env, const fo4::FormKey& key) {
    if (key.owner().empty()) return nullptr;
    nlohmann::json out{{"formKey", key.to_string()}, {"editorId", nullptr}, {"signature", nullptr}};
    if (const auto* winner = env.winner(key)) {
        const auto meta = env.metadata(*winner);
        if (meta.editor_id) out["editorId"] = std::string(*meta.editor_id);
        out["signature"] = meta.signature.to_string();
    }
    return out;
}

nlohmann::json optionalLinkJson(const Environment& env, const std::optional<fo4::FormKey>& key) {
    if (!key) return nullptr;
    return linkJson(env, *key);
}

nlohmann::json flagsJson(std::uint32_t raw, std::span<const NamedBit> table) {
    nlohmann::json names = nlohmann::json::array();
    nlohmann::json options = nlohmann::json::array();
    nlohmann::json respawns = nullptr;
    const auto editable = editableMask(table);
    for (const auto& flag : table) {
        const bool set = flag.bit < 32 && (raw & (1u << flag.bit)) != 0;
        if (set) names.push_back({{"name", flag.name}, {"bit", flag.bit}});
        options.push_back({{"name", flag.name}, {"bit", flag.bit}, {"editable", (editable & (1u << flag.bit)) != 0}});
        if (std::string_view(flag.name) == k_no_respawn_name) respawns = !set;
    }
    return {
        {"raw", raw},
        {"deleted", (raw & fo4::RecordFlags::deleted) != 0},
        {"persistent", (raw & fo4::RecordFlags::persistent) != 0},
        {"initiallyDisabled", (raw & fo4::RecordFlags::initially_disabled) != 0},
        {"respawns", std::move(respawns)},
        {"names", std::move(names)},
        {"options", std::move(options)},
    };
}

nlohmann::json objectElementJson(const Environment& env, const RecordRef& context,
                                 const fo4::VmadObjectElement& element) {
    return {
        {"object", linkJson(env, env.resolve_link(context, element.object))},
        {"alias", element.alias},
    };
}

nlohmann::json structMemberJson(const Environment& env, const RecordRef& context,
                                const fo4::VmadStructMember& member) {
    nlohmann::json value = nullptr;
    if (member.object) value = objectElementJson(env, context, *member.object);
    else if (member.text) value = *member.text;
    else if (member.integer) value = *member.integer;
    else if (member.real) value = *member.real;
    else if (member.boolean) value = *member.boolean;
    return {
        {"name", member.name},
        {"type", member.type},
        {"typeName", vmadTypeName(member.type)},
        {"flags", member.flags},
        {"value", std::move(value)},
    };
}

nlohmann::json propertyJson(const Environment& env, const RecordRef& context,
                            const fo4::VmadProperty& property) {
    nlohmann::json value = nullptr;
    nlohmann::json elements = nullptr;
    if (property.object) {
        value = objectElementJson(env, context, {*property.object, property.alias.value_or(0)});
    } else if (property.text) {
        value = *property.text;
    } else if (property.integer) {
        value = *property.integer;
    } else if (property.real) {
        value = *property.real;
    } else if (property.boolean) {
        value = *property.boolean;
    }
    if (property.object_elements) {
        elements = nlohmann::json::array();
        for (const auto& element : *property.object_elements) {
            elements.push_back(objectElementJson(env, context, element));
        }
    } else if (property.text_elements) {
        elements = *property.text_elements;
    } else if (property.integer_elements) {
        elements = *property.integer_elements;
    } else if (property.real_elements) {
        elements = *property.real_elements;
    } else if (property.boolean_elements) {
        elements = *property.boolean_elements;
    } else if (property.struct_elements) {
        elements = nlohmann::json::array();
        for (const auto& structure : *property.struct_elements) {
            nlohmann::json memberList = nlohmann::json::array();
            for (const auto& member : structure.members) {
                memberList.push_back(structMemberJson(env, context, member));
            }
            elements.push_back({{"members", std::move(memberList)}});
        }
    } else if (property.element_count && *property.element_count == 0 && property.type >= 11 && property.type <= 17) {
        elements = nlohmann::json::array();
    }
    const bool undecoded = value.is_null() && elements.is_null() && property.type != 0;
    return {
        {"name", property.name},
        {"type", property.type},
        {"typeName", vmadTypeName(property.type)},
        {"flags", property.flags},
        {"elementCount", property.element_count ? nlohmann::json(*property.element_count) : nlohmann::json(nullptr)},
        {"value", std::move(value)},
        {"elements", std::move(elements)},
        {"undecoded", undecoded},
    };
}

// Constraint: user flag index 1 is Conditional in the Fallout 4 user flag table, so its mask is 0x2.
constexpr std::uint32_t k_conditional_user_flag = 1u << 1;
constexpr std::size_t k_declared_depth_limit = 8;

struct DeclaredScript {
    std::string name;
    std::string parent;
    std::vector<fo4::pex::PexProperty> properties;
    std::vector<fo4::pex::PexVariable> variables;
};

using DeclaredLoad = std::pair<std::optional<DeclaredScript>, std::string>;

struct DeclarationContext {
    std::filesystem::path dataDirectory;
    std::vector<std::string> pluginNames;
    std::map<std::string, DeclaredLoad> cache;
};

// Invariant: each script is decoded once per details call; a failure is cached with its message.
const DeclaredLoad& loadDeclared(DeclarationContext& ctx, const std::string& name) {
    const auto key = support::asciiLower(name);
    if (const auto found = ctx.cache.find(key); found != ctx.cache.end()) return found->second;
    DeclaredLoad entry;
    if (ctx.dataDirectory.empty()) {
        entry.second = "game Data directory is not configured";
    } else {
        try {
            std::vector<std::string> notes;
            const auto bytes = questinternal::compiledScriptBytes(ctx.dataDirectory, ctx.pluginNames, name, notes);
            if (!bytes) {
                entry.second = "no compiled script " + name + " in the Data archives or Scripts folder";
            } else {
                const auto pex = fo4::pex::PexFile::fromBytes(*bytes);
                if (pex.objects.empty() || support::asciiLower(pex.objects.front().name) != key) {
                    entry.second = "compiled script does not declare " + name;
                } else {
                    const auto& object = pex.objects.front();
                    entry.first = DeclaredScript{object.name, object.parentClassName, object.properties, object.variables};
                }
            }
        } catch (const std::exception& error) {
            entry.second = error.what();
        }
    }
    return ctx.cache.emplace(key, std::move(entry)).first->second;
}

nlohmann::json pexValueJson(const fo4::pex::PexValue& value) {
    switch (value.type) {
        case fo4::pex::PexValueType::Integer: return value.int_;
        case fo4::pex::PexValueType::Float: return value.float_;
        case fo4::pex::PexValueType::Bool: return value.bool_;
        case fo4::pex::PexValueType::String:
        case fo4::pex::PexValueType::Identifier: return value.str;
        default: return nullptr;
    }
}

// Invariant: an auto property defaults to its backing variable; a manual one only to a literal returned by its getter.
std::optional<fo4::pex::PexValue> declaredDefault(const DeclaredScript& script, const fo4::pex::PexProperty& property) {
    if (property.isAutoVar()) {
        const auto backing = support::asciiLower(property.autoVarName);
        for (const auto& variable : script.variables) {
            if (support::asciiLower(variable.name) == backing) return variable.defaultValue;
        }
        return std::nullopt;
    }
    if (property.readHandler) {
        for (const auto& instruction : property.readHandler->instructions) {
            if (instruction.mnemonic == "return" && !instruction.args.empty() &&
                instruction.args[0].type != fo4::pex::PexValueType::Identifier) {
                return instruction.args[0];
            }
        }
    }
    return std::nullopt;
}

nlohmann::json declaredScriptJson(DeclarationContext& ctx, const std::string& scriptName,
                                  const std::set<std::string>& storedNames) {
    nlohmann::json declared = nlohmann::json::array();
    nlohmann::json parent = nullptr;
    nlohmann::json error = nullptr;
    std::set<std::string> emitted;
    std::set<std::string> visited;
    std::string current = scriptName;
    for (std::size_t depth = 0;; ++depth) {
        visited.insert(support::asciiLower(current));
        const auto& [script, message] = loadDeclared(ctx, current);
        if (!script) {
            error = depth == 0 ? message : "parent " + current + ": " + message;
            break;
        }
        if (depth == 0 && !script->parent.empty()) parent = script->parent;
        for (const auto& property : script->properties) {
            if (!emitted.insert(support::asciiLower(property.name)).second) continue;
            const bool stored = storedNames.contains(support::asciiLower(property.name));
            const auto value = declaredDefault(*script, property);
            declared.push_back({
                {"name", property.name},
                {"type", property.type},
                {"auto", property.isAutoVar()},
                {"conditional", (property.userFlags & k_conditional_user_flag) != 0},
                {"readOnly", !property.canWrite()},
                {"default", value ? pexValueJson(*value) : nlohmann::json(nullptr)},
                {"status", stored ? "stored" : "default"},
                {"declaredBy", script->name},
            });
        }
        if (script->parent.empty()) break;
        if (depth + 1 >= k_declared_depth_limit) {
            error = "parent chain is deeper than " + std::to_string(k_declared_depth_limit);
            break;
        }
        if (visited.contains(support::asciiLower(script->parent))) break;
        current = script->parent;
    }
    return {{"declared", std::move(declared)}, {"parent", std::move(parent)}, {"declaredError", std::move(error)}};
}

nlohmann::json scriptsJson(const Environment& env, const RecordRef& context, const fo4::VmadScripts& raw,
                           DeclarationContext& declarations) {
    nlohmann::json scripts = nlohmann::json::array();
    for (const auto& script : raw.scripts) {
        nlohmann::json properties = nlohmann::json::array();
        std::set<std::string> storedNames;
        for (const auto& property : script.properties) {
            properties.push_back(propertyJson(env, context, property));
            storedNames.insert(support::asciiLower(property.name));
        }
        auto entry = nlohmann::json{
            {"name", script.name},
            {"flags", script.flags},
            {"properties", std::move(properties)},
        };
        entry.update(declaredScriptJson(declarations, script.name, storedNames));
        scripts.push_back(std::move(entry));
    }
    return {
        {"version", raw.version},
        {"objectFormat", raw.object_format},
        {"unsupported", raw.unsupported},
        {"fragmentBytes", raw.trailing_bytes},
        {"scripts", std::move(scripts)},
    };
}

nlohmann::json baseScriptsJson(const Environment& env, const RecordRef& base, DeclarationContext& declarations) {
    const auto* vmad = findSubrecord(base, "VMAD");
    if (vmad == nullptr) return nullptr;
    try {
        return scriptsJson(env, base, fo4::read_vmad_scripts(vmad->data), declarations);
    } catch (const std::exception& error) {
        return {{"error", std::string(error.what())}};
    }
}

nlohmann::json lockJson(const Environment& env, const RecordRef& ref) {
    const auto* xloc = findSubrecord(ref, "XLOC");
    if (xloc == nullptr || xloc->data.size() < 16) return nullptr;
    const auto level = std::to_integer<std::uint8_t>(xloc->data[0]);
    const auto flags = std::to_integer<std::uint8_t>(xloc->data[8]);
    const auto levelName = lockLevelName(level);
    return {
        {"level", level},
        {"levelName", levelName != nullptr ? nlohmann::json(levelName) : nlohmann::json(nullptr)},
        {"key", linkJson(env, env.resolve_link(ref, fo4::RawFormId{readU32(xloc->data, 4)}))},
        {"flags", flags},
    };
}

nlohmann::json enableParentJson(const Environment& env, const fo4::env::EnableParent& parent) {
    nlohmann::json out{
        {"reference", linkJson(env, parent.reference)},
        {"flags", nullptr},
        {"setOppositeToParent", nullptr},
        {"popIn", nullptr},
    };
    if (parent.flags) {
        out["flags"] = *parent.flags;
        out["setOppositeToParent"] = (*parent.flags & 0x01u) != 0;
        out["popIn"] = (*parent.flags & 0x02u) != 0;
    }
    return out;
}

nlohmann::json placedDataJson(const Environment& env, const RecordRef& ref, const PlacedReferenceData& data,
                              DeclarationContext& declarations) {
    nlohmann::json out = nlohmann::json::object();

    nlohmann::json transform{
        {"position", data.position ? vec3Json(*data.position) : nlohmann::json(nullptr)},
        {"rotation", data.rotation ? vec3Json(*data.rotation) : nlohmann::json(nullptr)},
        {"scale", data.scale ? nlohmann::json(*data.scale) : nlohmann::json(nullptr)},
    };
    out["transform"] = std::move(transform);

    out["layer"] = optionalLinkJson(env, data.layer);
    out["encounterZone"] = optionalLinkJson(env, data.encounter_zone);
    out["emittance"] = optionalLinkJson(env, data.emittance);
    out["radius"] = data.radius ? nlohmann::json(*data.radius) : nlohmann::json(nullptr);
    out["count"] = data.count ? nlohmann::json(*data.count) : nlohmann::json(nullptr);
    out["levelModifier"] = data.level_modifier ? nlohmann::json(*data.level_modifier) : nlohmann::json(nullptr);
    out["locationReference"] = optionalLinkJson(env, data.location_reference);
    nlohmann::json locationTypes = nlohmann::json::array();
    for (const auto& type : data.location_ref_types) locationTypes.push_back(linkJson(env, type));
    out["locationReferenceTypes"] = std::move(locationTypes);
    out["attachRef"] = optionalLinkJson(env, data.attach_ref);

    out["enableParent"] = data.enable_parent ? enableParentJson(env, *data.enable_parent) : nlohmann::json(nullptr);

    if (data.ownership) {
        out["ownership"] = {
            {"owner", linkJson(env, data.ownership->owner)},
            {"noCrime", data.ownership->no_crime ? nlohmann::json(*data.ownership->no_crime) : nlohmann::json(nullptr)},
        };
    } else {
        out["ownership"] = nullptr;
    }
    out["factionRank"] = data.faction_rank ? nlohmann::json(*data.faction_rank) : nlohmann::json(nullptr);

    nlohmann::json linked = nlohmann::json::array();
    for (const auto& entry : data.linked_references) {
        linked.push_back({
            {"keyword", linkJson(env, entry.keyword)},
            {"reference", linkJson(env, entry.reference)},
        });
    }
    out["linkedReferences"] = std::move(linked);

    if (data.activate_parent_flags || !data.activate_parents.empty()) {
        nlohmann::json parents = nlohmann::json::array();
        for (const auto& parent : data.activate_parents) {
            parents.push_back({{"reference", linkJson(env, parent.reference)}, {"delay", parent.delay}});
        }
        out["activateParents"] = {
            {"flags", data.activate_parent_flags ? nlohmann::json(*data.activate_parent_flags) : nlohmann::json(nullptr)},
            {"parents", std::move(parents)},
        };
    } else {
        out["activateParents"] = nullptr;
    }

    if (data.teleport) {
        const auto& teleport = *data.teleport;
        out["teleport"] = {
            {"door", linkJson(env, teleport.door)},
            {"position", vec3Json(teleport.position)},
            {"rotation", vec3Json(teleport.rotation)},
            {"flags", teleport.flags},
            {"transitionInterior", linkJson(env, teleport.transition_interior)},
        };
    } else {
        out["teleport"] = nullptr;
    }

    if (data.primitive) {
        const auto& primitive = *data.primitive;
        out["primitive"] = {
            {"bounds", vec3Json(primitive.bounds)},
            {"color", primitive.color},
            {"type", primitive.type},
        };
    } else {
        out["primitive"] = nullptr;
    }

    if (data.spline) {
        const auto& spline = *data.spline;
        out["spline"] = {
            {"slack", spline.slack},
            {"thickness", spline.thickness},
            {"halfExtents", vec3Json(spline.half_extents)},
            {"windDetachedEnd", spline.wind_detached_end ? nlohmann::json(*spline.wind_detached_end) : nlohmann::json(nullptr)},
        };
    } else {
        out["spline"] = nullptr;
    }

    if (data.room_portal) {
        const auto& portal = *data.room_portal;
        out["roomPortal"] = {
            {"width", portal.width},
            {"height", portal.height},
            {"position", vec3Json(portal.position)},
            {"rotation", portal.rotation},
        };
    } else {
        out["roomPortal"] = nullptr;
    }

    if (data.room) {
        const auto& room = *data.room;
        nlohmann::json rooms = nlohmann::json::array();
        for (const auto& linkedRoom : room.linked_rooms) rooms.push_back(linkJson(env, linkedRoom));
        out["room"] = {
            {"linkedRoomCount", room.linked_room_count},
            {"flags", room.flags},
            {"lightingTemplate", optionalLinkJson(env, room.lighting_template)},
            {"imageSpace", optionalLinkJson(env, room.image_space)},
            {"linkedRooms", std::move(rooms)},
        };
    } else {
        out["room"] = nullptr;
    }

    nlohmann::json portals = nlohmann::json::array();
    for (const auto& portal : data.portals) {
        portals.push_back({{"origin", linkJson(env, portal.origin)}, {"destination", linkJson(env, portal.destination)}});
    }
    out["portals"] = std::move(portals);

    if (data.light_data) {
        const auto& light = *data.light_data;
        out["lightData"] = {
            {"fov", light.fov},
            {"fade", light.fade},
            {"endDistanceCap", light.end_distance_cap},
            {"shadowDepthBias", light.shadow_depth_bias},
            {"nearClip", light.near_clip ? nlohmann::json(*light.near_clip) : nlohmann::json(nullptr)},
            {"volumetricIntensity", light.volumetric_intensity ? nlohmann::json(*light.volumetric_intensity) : nlohmann::json(nullptr)},
        };
    } else {
        out["lightData"] = nullptr;
    }

    if (data.map_marker) {
        const auto& marker = *data.map_marker;
        nlohmann::json name = nullptr;
        nlohmann::json stringId = nullptr;
        if (marker.name.kind == fo4::env::NameKind::inline_text) {
            name = std::string(marker.name.text);
        } else if (marker.name.kind == fo4::env::NameKind::string_id) {
            stringId = marker.name.string_id;
        }
        out["mapMarker"] = {
            {"flags", marker.flags ? nlohmann::json(*marker.flags) : nlohmann::json(nullptr)},
            {"name", std::move(name)},
            {"nameStringId", std::move(stringId)},
            {"type", marker.type ? nlohmann::json(*marker.type) : nlohmann::json(nullptr)},
        };
    } else {
        out["mapMarker"] = nullptr;
    }

    nlohmann::json patrols = nlohmann::json::array();
    for (const auto& patrol : data.patrols) {
        nlohmann::json topics = nlohmann::json::array();
        for (const auto& topic : patrol.topics) {
            topics.push_back({
                {"type", topic.type},
                {"topic", topic.type == 0 ? linkJson(env, topic.topic) : nlohmann::json(nullptr)},
                {"subtype", topic.type == 1 ? nlohmann::json(topic.subtype) : nlohmann::json(nullptr)},
            });
        }
        patrols.push_back({
            {"idleTime", patrol.idle_time},
            {"idle", optionalLinkJson(env, patrol.idle)},
            {"topic", optionalLinkJson(env, patrol.topic)},
            {"topics", std::move(topics)},
        });
    }
    out["patrols"] = std::move(patrols);

    out["lock"] = lockJson(env, ref);
    out["comments"] = data.comments ? nlohmann::json(std::string(*data.comments)) : nlohmann::json(nullptr);
    out["openByDefault"] = data.open_by_default;
    out["ignoredBySandbox"] = data.ignored_by_sandbox;

    out["scripts"] = data.scripts ? scriptsJson(env, ref, data.scripts->raw, declarations) : nlohmann::json(nullptr);
    return out;
}

std::string baseNameText(const fo4::env::RecordMetadata& meta) {
    if (meta.name.kind != fo4::env::NameKind::inline_text) return {};
    return std::string(meta.name.text);
}

// Constraint: record text is Windows-1252 unless it is valid UTF-8, and a raw non-UTF-8 byte makes the JSON dump throw.
void decodeRecordText(nlohmann::json& node) {
    if (node.is_string()) {
        const auto& raw = node.get_ref<const std::string&>();
        if (!fo4::text::is_valid_utf8(raw)) node = fo4::text::windows1252_to_utf8(raw);
        return;
    }
    if (node.is_array() || node.is_object()) {
        for (auto& child : node) decodeRecordText(child);
    }
}

}  // namespace

namespace referenceinternal {

std::optional<std::string> referenceEditRefusal(const Environment& env,
                                                const fo4::FormKey& reference,
                                                const fo4::forms::PlacedReferenceEdit& edit) {
    if (!edit.record_flags && !edit.lock && !edit.ownership && !edit.linked_references && !edit.teleport) return std::nullopt;
    if (edit.teleport) {
        const auto* door = env.winner(edit.teleport->door);
        if (door == nullptr || door->record == nullptr) {
            return "Teleport door " + edit.teleport->door.to_string() + " was not found";
        }
        if (env.metadata(*door).signature.to_string() != "DOOR") {
            return "Teleport door " + edit.teleport->door.to_string() + " is not a DOOR record";
        }
    }
    if (edit.linked_references) {
        for (const auto& link : *edit.linked_references) {
            if (link.keyword) {
                const auto* keyword = env.winner(*link.keyword);
                if (keyword == nullptr || keyword->record == nullptr) {
                    return "Linked keyword " + link.keyword->to_string() + " was not found";
                }
                if (env.metadata(*keyword).signature.to_string() != "KYWD") {
                    return "Linked keyword " + link.keyword->to_string() + " is not a KYWD record";
                }
            }
            const auto* target = env.winner(link.reference);
            if (target == nullptr || target->record == nullptr) {
                return "Linked reference " + link.reference.to_string() + " was not found";
            }
            if (!fo4::is_placed_reference_signature(target->record->header.signature)) {
                return "Linked reference " + link.reference.to_string() + " is not a placed reference";
            }
        }
    }
    if (edit.ownership && edit.ownership->owner) {
        const auto& owner = *edit.ownership->owner;
        const auto* ownerWinner = env.winner(owner);
        if (ownerWinner == nullptr || ownerWinner->record == nullptr) {
            return "Owner " + owner.to_string() + " was not found";
        }
        const auto ownerSignature = env.metadata(*ownerWinner).signature.to_string();
        if (ownerSignature != "NPC_" && ownerSignature != "FACT") {
            return "Owner " + owner.to_string() + " is a " + ownerSignature + " record; the owner must be an NPC_ or FACT";
        }
        if (edit.ownership->faction_rank && ownerSignature != "FACT") {
            return std::string("Faction rank can only be set when the owner is a FACT");
        }
    } else if (edit.ownership && edit.ownership->faction_rank) {
        return std::string("Faction rank can only be set together with an owner");
    }
    const auto* winner = env.winner(reference);
    if (winner == nullptr || winner->record == nullptr) return std::nullopt;
    if (!fo4::is_placed_reference_signature(winner->record->header.signature)) return std::nullopt;

    if (edit.record_flags) {
        const auto data = fo4::env::decode_placed_reference(env, *winner);
        std::string baseSignature;
        if (data.base && !data.base->owner().empty()) {
            if (const auto info = fo4::env::describe_base_object(env, *data.base)) {
                baseSignature = info->signature.to_string();
            }
        }
        const auto table = tableFor(data.signature, baseSignature);
        const auto changed = winner->record->header.flags.value() ^ *edit.record_flags;
        if ((changed & k_locked_record_flags) != 0) {
            return std::string("The persistent and deleted record flags cannot be changed");
        }
        const auto unknown = changed & ~editableMask(table);
        if (unknown != 0) {
            return "Record flag bit " + std::to_string(std::countr_zero(unknown)) + " is not editable on this reference";
        }
    }

    if (edit.lock && edit.lock->has_value()) {
        const auto& lock = **edit.lock;
        if (lockLevelName(lock.level) == nullptr) {
            return "Lock level " + std::to_string(lock.level) + " is not a named lock level";
        }
        if (lock.key) {
            const auto* keyWinner = env.winner(*lock.key);
            if (keyWinner == nullptr || keyWinner->record == nullptr) {
                return "Lock key " + lock.key->to_string() + " was not found";
            }
            if (env.metadata(*keyWinner).signature.to_string() != "KEYM") {
                return "Lock key " + lock.key->to_string() + " is not a KEYM record";
            }
        }
    }
    return std::nullopt;
}

}  // namespace referenceinternal

Result<nlohmann::json> RadiclePluginBackend::getReferenceDetails(const nlohmann::json& arguments) const {
    const auto formKeyText = support::stringArgument(arguments, 0);
    if (formKeyText.empty()) {
        return std::unexpected(Error{"ReferenceFormKeyMissing", "GetReferenceDetails requires a FormKey"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto env = borrowedEnvironment();
        const auto* winner = env->winner(key);
        if (winner == nullptr || winner->record == nullptr) {
            return nlohmann::json(nlohmann::json{{"error", "Reference not found: " + formKeyText}}.dump());
        }
        if (!fo4::is_placed_reference_signature(winner->record->header.signature)) {
            return nlohmann::json(nlohmann::json{
                {"error", formKeyText + " is not a placed reference (" + winner->record->header.signature.to_string() + ")"}
            }.dump());
        }

        const auto data = fo4::env::decode_placed_reference(*env, *winner);
        const auto meta = env->metadata(*winner);

        nlohmann::json versions = nlohmann::json::array();
        for (const auto& version : env->versions(key)) {
            versions.push_back(std::string(env->plugins().at(version.plugin).key.filename()));
        }

        DeclarationContext declarations{dataDirectory_, archivePluginNamesLocked(), {}};
        nlohmann::json base = nullptr;
        nlohmann::json baseScripts = nullptr;
        std::string baseSignature;
        if (data.base && !data.base->owner().empty()) {
            const auto info = fo4::env::describe_base_object(*env, *data.base);
            const auto* baseRef = env->winner(*data.base);
            base = linkJson(*env, *data.base);
            if (info) baseSignature = info->signature.to_string();
            base["signature"] = baseSignature.empty() ? nlohmann::json(nullptr) : nlohmann::json(baseSignature);
            base["type"] = info ? nlohmann::json(info->type_name) : nlohmann::json(nullptr);
            base["model"] = info && !info->model_path.empty() ? nlohmann::json(info->model_path) : nlohmann::json(nullptr);
            base["missing"] = baseRef == nullptr;
            if (baseRef != nullptr && baseRef->record != nullptr) {
                const auto baseMeta = env->metadata(*baseRef);
                const auto name = baseNameText(baseMeta);
                base["name"] = name.empty() ? nlohmann::json(nullptr) : nlohmann::json(name);
                baseScripts = baseScriptsJson(*env, *baseRef, declarations);
            }
        }

        nlohmann::json out = {
            {"formKey", meta.form_key.to_string()},
            {"editorId", meta.editor_id ? nlohmann::json(std::string(*meta.editor_id)) : nlohmann::json(nullptr)},
            {"signature", data.signature.to_string()},
            {"winningPlugin", std::string(env->plugins().at(winner->plugin).key.filename())},
            {"pluginsWithRecord", std::move(versions)},
            {"cell", optionalLinkJson(*env, env->cell_of(*winner))},
            {"flags", flagsJson(winner->record->header.flags.value(), tableFor(data.signature, baseSignature))},
            {"base", std::move(base)},
            {"baseScripts", std::move(baseScripts)},
        };
        out.update(placedDataJson(*env, *winner, data, declarations));
        decodeRecordText(out);
        return nlohmann::json(out.dump());
    } catch (const fo4::env::WorldDecodeError& error) {
        return nlohmann::json(nlohmann::json{
            {"error", "Reference record is malformed: " + std::string(error.what())}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ReferenceReadFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
