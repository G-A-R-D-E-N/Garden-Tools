#include <fo4ide/app/SwfTarget.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/c_api.h>
#include <fo4/c_api_swf.h>
#include <fo4/c_api_swf_verbs.h>
#include <fo4ide/core/Base64.hpp>
#include <fo4ide/core/JsonInt.hpp>

#include "ServiceTargetSupport.hpp"
#include "SwfTargetSupport.hpp"

namespace fo4ide::app {
namespace {

std::string stringArg(const nlohmann::json& args, std::size_t index) {
    return support::stringArgument(args, index);
}

long long intArg(const nlohmann::json& args, std::size_t index) {
    const nlohmann::json* value = support::argument(args, index);
    return value == nullptr ? 0 : core::jsonInt(*value).value_or(0);
}

std::size_t sizeArg(const nlohmann::json& args, std::size_t index) {
    return static_cast<std::size_t>(intArg(args, index));
}

std::uint32_t u32Arg(const nlohmann::json& args, std::size_t index) {
    return static_cast<std::uint32_t>(intArg(args, index));
}

std::uint16_t u16Arg(const nlohmann::json& args, std::size_t index) {
    return static_cast<std::uint16_t>(intArg(args, index));
}

int int32Arg(const nlohmann::json& args, std::size_t index) {
    return static_cast<int>(intArg(args, index));
}

const char* orEmpty(const char* value) {
    return value != nullptr ? value : "";
}

const char* kindName(std::uint8_t kind) {
    // Constraint: the names must match SwfCharacterKind.ToString, because the panel reads the kind field.
    switch (kind) {
        case 0: return "Shape";
        case 1: return "Morph";
        case 2: return "Bitmap";
        case 3: return "Text";
        case 4: return "EditText";
        case 5: return "Font";
        case 6: return "Sprite";
        case 7: return "Button";
        case 8: return "Sound";
        case 9: return "Video";
        case 10: return "Imported";
        default: break;
    }
    return nullptr;
}

std::string hex8(std::uint32_t value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out(8, '0');
    for (int index = 7; index >= 0; --index) {
        out[static_cast<std::size_t>(index)] = kHex[value & 0xF];
        value >>= 4;
    }
    return out;
}

bool splitRow(const std::string& row, long long& first, unsigned long long& second, std::string& rest) {
    const auto tab = row.find('\t');
    if (tab == std::string::npos) return false;
    const auto tab2 = row.find('\t', tab + 1);
    if (tab2 == std::string::npos) return false;
    try {
        first = std::stoll(row.substr(0, tab));
        second = std::stoull(row.substr(tab + 1, tab2 - tab - 1));
    } catch (const std::exception&) {
        return false;
    }
    rest = row.substr(tab2 + 1);
    return true;
}

// Constraint: the kinds are index i, uint16 h, uint32 u, int32 z and string s, checked before dispatch so no edit runs on a defaulted argument.
std::optional<std::string_view> expectedArguments(std::string_view method) {
    static const std::map<std::string_view, std::string_view> kinds{
        {"Open", "s"},
        {"Timeline", "i"},
        {"FrameLabels", "h"},
        {"Frame", "ii"},
        {"RenameLabel", "his"},
        {"AddLabel", "ush"},
        {"RemoveLabel", "hi"},
        {"ReadEditText", "i"},
        {"EditEditText", "isszzzz"},
        {"SetDepth", "ih"},
        {"PlacementMatrix", "i"},
        {"SetTransform", "izzzzzz"},
        {"InsertKeyframe", "iu"},
        {"InsertAbcInstruction", "iuis"},
        {"ReplaceAbcInstruction", "iuis"},
        {"Disassemble", "ii"},
        {"ShapeSvg", "h"},
        {"FrameSvg", "u"},
        {"BitmapPng", "i"},
        {"BitmapJpeg", "i"},
        {"Avm1Disassemble", "i"},
        {"AbcBlocks", ""},
        {"ActionTags", ""},
        {"Characters", ""},
        {"InspectText", ""},
        {"Placements", ""},
        {"Redo", ""},
        {"Save", ""},
        {"State", ""},
        {"Undo", ""},
    };
    const auto found = kinds.find(method);
    if (found == kinds.end()) return std::nullopt;
    return found->second;
}

std::optional<std::string> argumentRefusal(std::string_view method, const nlohmann::json& args) {
    const std::string_view kinds = expectedArguments(method).value_or(std::string_view{});
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const std::string position = "argument " + std::to_string(index);
        const nlohmann::json* value = support::argument(args, index);
        if (value == nullptr) return position + " is missing";
        if (kinds[index] == 's') {
            if (!value->is_string()) return position + " must be a string";
            continue;
        }
        const auto number = core::jsonInt(*value);
        if (!number) return position + " must be an integer";
        if (kinds[index] == 'h' && (*number < 0 || *number > 65535)) {
            return position + " must be an integer from 0..65535";
        }
        if ((kinds[index] == 'i' || kinds[index] == 'u') && *number < 0) {
            return position + " must not be negative";
        }
    }
    return std::nullopt;
}

}  // namespace

SwfTarget::SwfTarget(SwfRootsProvider roots) : roots_(std::move(roots)) {}

SwfTarget::~SwfTarget() { closeDocument(); }

Result<nlohmann::json> SwfTarget::invoke(std::string_view method, const nlohmann::json& args) {
    std::lock_guard<std::mutex> guard(mutex_);
    const std::string name(method);

    if (const auto refusal = argumentRefusal(name, args)) return swftarget::errorText(*refusal);

    if (name == "Open") return open(args);

    if ((movie_ == nullptr || history_ == nullptr) && expectedArguments(name)) return swftarget::errorText("no SWF document is open");

    if (name == "State") return snapshot(false);

    if (name == "Characters") {
        std::size_t count = 0;
        if (radicle_swf_character_count(movie_, &count) != RADICLE_STATUS_OK)
            return swftarget::radicleError(RADICLE_STATUS_INTERNAL_ERROR);
        nlohmann::json characters = nlohmann::json::array();
        for (std::size_t index = 0; index < count; ++index) {
            radicle_swf_character raw{};
            if (radicle_swf_character_at(movie_, index, &raw) != RADICLE_STATUS_OK)
                return swftarget::radicleError(RADICLE_STATUS_INTERNAL_ERROR);
            const char* kind = kindName(raw.kind);
            nlohmann::json item{{"id", raw.character_id},
                                {"tagIndex", raw.tag_index},
                                {"name", orEmpty(raw.name)},
                                {"imported", orEmpty(raw.imported_name)}};
            if (kind != nullptr) {
                item["kind"] = kind;
            } else {
                item["kind"] = std::to_string(static_cast<unsigned>(raw.kind));
            }
            characters.push_back(std::move(item));
        }
        return nlohmann::json(nlohmann::json{{"characters", std::move(characters)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "Timeline") {
        radicle_swf_frames* frames = nullptr;
        if (radicle_swf_frames_open(movie_, &frames) != RADICLE_STATUS_OK || frames == nullptr) {
            if (frames != nullptr) radicle_swf_frames_destroy(frames);
            return swftarget::radicleError(RADICLE_STATUS_INTERNAL_ERROR);
        }
        std::uint16_t character = 0;
        std::size_t frameCount = 0;
        const radicle_status status =
            radicle_swf_frames_timeline_at(frames, sizeArg(args, 0), &character, &frameCount);
        radicle_swf_frames_destroy(frames);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        return nlohmann::json(
            nlohmann::json{{"characterId", character}, {"frameCount", frameCount}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "FrameLabels") {
        char** rows = nullptr;
        std::size_t count = 0;
        const radicle_status status = radicle_swf_frame_label_rows(
            history_, u16Arg(args, 0), &rows, &count);
        if (status != RADICLE_STATUS_OK) {
            if (rows != nullptr) radicle_free_string_array(rows, count);
            return swftarget::radicleError(status);
        }
        nlohmann::json labels = nlohmann::json::array();
        for (std::size_t index = 0; index < count; ++index) {
            const std::string row(rows[index] != nullptr ? rows[index] : "");
            long long labelIndex = 0;
            unsigned long long frame = 0;
            std::string labelName;
            if (!splitRow(row, labelIndex, frame, labelName)) {
                radicle_free_string_array(rows, count);
                return swftarget::errorText("the frame label row did not parse");
            }
            labels.push_back(
                nlohmann::json{{"labelIndex", labelIndex}, {"frame", frame}, {"name", labelName}});
        }
        radicle_free_string_array(rows, count);
        return nlohmann::json(nlohmann::json{{"labels", std::move(labels)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "Frame") {
        radicle_swf_frames* frames = nullptr;
        if (radicle_swf_frames_open(movie_, &frames) != RADICLE_STATUS_OK || frames == nullptr) {
            if (frames != nullptr) radicle_swf_frames_destroy(frames);
            return swftarget::radicleError(RADICLE_STATUS_INTERNAL_ERROR);
        }
        const std::size_t timeline = sizeArg(args, 0);
        const std::size_t frame = sizeArg(args, 1);
        std::size_t count = 0;
        radicle_status status = radicle_swf_frames_instance_count(frames, timeline, frame, &count);
        nlohmann::json rows = nlohmann::json::array();
        for (std::size_t index = 0; status == RADICLE_STATUS_OK && index < count; ++index) {
            radicle_swf_frames_instance raw{};
            status = radicle_swf_frames_instance_at(frames, timeline, frame, index, &raw);
            if (status != RADICLE_STATUS_OK) break;
            rows.push_back(nlohmann::json{{"depth", raw.depth},
                                          {"character", raw.character_id},
                                          {"name", orEmpty(raw.name)},
                                          {"x", raw.transform.translate_x},
                                          {"y", raw.transform.translate_y},
                                          {"scaleX", raw.transform.scale_x},
                                          {"scaleY", raw.transform.scale_y},
                                          {"rotation", raw.transform.rotate_skew0},
                                          {"instanceId", raw.instance_id},
                                          {"boundsX", raw.bounds.x},
                                          {"boundsY", raw.bounds.y},
                                          {"boundsWidth", raw.bounds.width},
                                          {"boundsHeight", raw.bounds.height},
                                          {"hasBounds", raw.has_bounds != 0},
                                          {"clipDepth", raw.clip_depth}});
        }
        radicle_swf_frames_destroy(frames);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        return nlohmann::json(nlohmann::json{{"rows", std::move(rows)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "RenameLabel" || name == "AddLabel" || name == "RemoveLabel") {
        radicle_swf_plan_result result{};
        radicle_status status = RADICLE_STATUS_OK;
        if (name == "RenameLabel") {
            status = radicle_swf_frame_label_rename(history_, u16Arg(args, 0), sizeArg(args, 1),
                                                    stringArg(args, 2).c_str(), &result);
        } else if (name == "AddLabel") {
            // Constraint: the argument order must match SwfInterop.AddLabel, which takes the frame first.
            status = radicle_swf_frame_label_add(history_, u16Arg(args, 2), u32Arg(args, 0),
                                                 stringArg(args, 1).c_str(), &result);
        } else {
            status = radicle_swf_frame_label_remove(history_, u16Arg(args, 0), sizeArg(args, 1),
                                                    &result);
        }
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        std::string message;
        if (!refreshFromHistory(message, false)) return swftarget::errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "ReadEditText") {
        radicle_swf_edit_text_info info{};
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status =
            radicle_swf_edit_text_read(history_, sizeArg(args, 0), &info, &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(status);
        }
        const std::string text = swftarget::takeText(raw, size);
        nlohmann::json body{{"id", info.character_id},
                            {"text", text},
                            {"color", info.has_color != 0 ? hex8(info.color) : ""},
                            {"x0", info.bounds.x_min},
                            {"x1", info.bounds.x_max},
                            {"y0", info.bounds.y_min},
                            {"y1", info.bounds.y_max}};
        return nlohmann::json(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "EditEditText") {
        const std::string text = stringArg(args, 1);
        std::string colorHex = support::trim(stringArg(args, 2));
        while (!colorHex.empty() && colorHex.front() == '#') colorHex.erase(colorHex.begin());
        if (colorHex.size() > 2 && colorHex[0] == '0' && (colorHex[1] | 0x20) == 'x') colorHex.erase(0, 2);
        radicle_swf_edit_text_fields fields{};
        fields.has_text = 1;
        fields.text = text.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(text.data());
        fields.text_size = text.size();
        if (!colorHex.empty()) {
            const char* last = colorHex.data() + colorHex.size();
            const auto parsed = std::from_chars(colorHex.data(), last, fields.color, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != last) {
                return swftarget::errorText("the color is not hexadecimal: " + stringArg(args, 2));
            }
            fields.has_color = 1;
        }
        fields.has_bounds = 1;
        fields.bounds.x_min = int32Arg(args, 3);
        fields.bounds.x_max = int32Arg(args, 4);
        fields.bounds.y_min = int32Arg(args, 5);
        fields.bounds.y_max = int32Arg(args, 6);
        radicle_swf_plan_result result{};
        const radicle_status status =
            radicle_swf_edit_text_apply(history_, sizeArg(args, 0), &fields, &result);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        std::string message;
        if (!refreshFromHistory(message, false)) return swftarget::errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "Placements") {
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status placementsStatus = radicle_swf_placements_json(history_, &raw, &size);
        if (placementsStatus != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(placementsStatus);
        }
        const std::string text = swftarget::takeText(raw, size);
        nlohmann::json parsed = nlohmann::json::array();
        try {
            parsed = nlohmann::json::parse(text);
        } catch (const std::exception&) {
            return swftarget::errorText("the placement list did not parse");
        }
        nlohmann::json placements = nlohmann::json::array();
        for (const auto& item : parsed) {
            nlohmann::json row{{"tagIndex", item.value("tag_index", 0)},
                               {"depth", item.value("depth", 0)},
                               {"insideSprite", item.value("inside_sprite", false)}};
            if (item.contains("character_id") && !item.at("character_id").is_null()) {
                row["character"] = item.at("character_id");
            } else {
                row["character"] = nullptr;
            }
            placements.push_back(std::move(row));
        }
        return nlohmann::json(nlohmann::json{{"placements", std::move(placements)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "SetDepth") {
        radicle_swf_plan_result result{};
        const radicle_status status = radicle_swf_set_placement_depth(history_, sizeArg(args, 0),
                                                                      u16Arg(args, 1), &result);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        std::string message;
        if (!refreshFromHistory(message, false)) return swftarget::errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "PlacementMatrix") {
        radicle_swf_placement_matrix_fields matrix{};
        const radicle_status status =
            radicle_swf_placement_matrix_at(history_, sizeArg(args, 0), &matrix);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        nlohmann::json body{{"hasScale", matrix.has_scale != 0},
                            {"scaleX", matrix.scale_x},
                            {"scaleY", matrix.scale_y},
                            {"hasRotate", matrix.has_rotate != 0},
                            {"rotate0", matrix.rotate0},
                            {"rotate1", matrix.rotate1},
                            {"x", matrix.translate_x},
                            {"y", matrix.translate_y}};
        return nlohmann::json(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "SetTransform") {
        radicle_swf_plan_result result{};
        const radicle_status status = radicle_swf_set_placement_transform(
            history_, sizeArg(args, 0), int32Arg(args, 1), int32Arg(args, 2), int32Arg(args, 3),
            int32Arg(args, 4), int32Arg(args, 5), int32Arg(args, 6), &result);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        std::string message;
        if (!refreshFromHistory(message, false)) return swftarget::errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "InsertKeyframe") {
        radicle_swf_plan_result result{};
        const radicle_status status = radicle_swf_insert_placement_keyframe(
            history_, sizeArg(args, 0), u32Arg(args, 1), &result);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        std::string message;
        if (!refreshFromHistory(message, false)) return swftarget::errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "AbcBlocks") {
        const std::size_t blocks = radicle_swf_abc_count(movie_);
        nlohmann::json list = nlohmann::json::array();
        for (std::size_t abc = 0; abc < blocks; ++abc) {
            radicle_swf_abc_info info{};
            if (radicle_swf_abc_info_at(movie_, abc, &info) != RADICLE_STATUS_OK)
                return swftarget::radicleError(RADICLE_STATUS_INTERNAL_ERROR);
            nlohmann::json classes = nlohmann::json::array();
            const std::size_t classCount = radicle_swf_abc_class_count(movie_, abc);
            for (std::size_t index = 0; index < classCount; ++index) {
                radicle_swf_class raw{};
                if (radicle_swf_abc_class_at(movie_, abc, index, &raw) != RADICLE_STATUS_OK)
                    return swftarget::radicleError(RADICLE_STATUS_INTERNAL_ERROR);
                classes.push_back(orEmpty(raw.name));
            }
            nlohmann::json scripts = nlohmann::json::array();
            const std::size_t scriptCount = radicle_swf_abc_script_count(movie_, abc);
            for (std::size_t index = 0; index < scriptCount; ++index) {
                radicle_swf_method raw{};
                if (radicle_swf_abc_script_at(movie_, abc, index, &raw) != RADICLE_STATUS_OK)
                    return swftarget::radicleError(RADICLE_STATUS_INTERNAL_ERROR);
                scripts.push_back(orEmpty(raw.name));
            }
            nlohmann::json methods = nlohmann::json::array();
            const std::size_t methodCount = radicle_swf_abc_method_count(movie_, abc);
            for (std::size_t index = 0; index < methodCount; ++index) {
                radicle_swf_method_pool raw{};
                if (radicle_swf_abc_method_at(movie_, abc, index, &raw) != RADICLE_STATUS_OK)
                    return swftarget::radicleError(RADICLE_STATUS_INTERNAL_ERROR);
                methods.push_back(nlohmann::json{{"index", index},
                                                 {"name", orEmpty(raw.name)},
                                                 {"returnType", orEmpty(raw.return_type)},
                                                 {"hasBody", raw.has_body != 0}});
            }
            list.push_back(nlohmann::json{{"abc", abc},
                                           {"name", orEmpty(info.name)},
                                           {"classes", std::move(classes)},
                                           {"scripts", std::move(scripts)},
                                           {"methods", std::move(methods)}});
        }
        return nlohmann::json(nlohmann::json{{"blocks", std::move(list)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "Disassemble") {
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status = radicle_swf_abc_method_disassemble(movie_, sizeArg(args, 0),
                                                                         sizeArg(args, 1), &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"text", swftarget::takeText(raw, size)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "InsertAbcInstruction" || name == "ReplaceAbcInstruction") {
        const std::size_t abc = sizeArg(args, 0);
        const std::uint32_t body = u32Arg(args, 1);
        const std::size_t offset = sizeArg(args, 2);
        const std::string text = stringArg(args, 3);
        const auto* input =
            text.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(text.data());
        radicle_swf_plan_result result{};
        // Constraint: one history verb must own the ABC edit, so the reader keeps no scratch movie.
        const radicle_status status = radicle_swf_history_abc_text_edit(
            history_, abc, body, offset, input, text.size(),
            name == "InsertAbcInstruction" ? 1 : 0, &result);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        std::string message;
        if (!refreshFromHistory(message, false)) return swftarget::errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "InspectText") {
        const std::string label = path_.value_or("<bytes>");
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status =
            radicle_swf_inspect_text(movie_, label.c_str(), &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"text", swftarget::takeText(raw, size)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "ActionTags") {
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status = radicle_swf_action_tags_json(history_, &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(status);
        }
        const std::string text = swftarget::takeText(raw, size);
        nlohmann::json parsed = nlohmann::json::array();
        try {
            parsed = nlohmann::json::parse(text);
        } catch (const std::exception&) {
            return swftarget::errorText("the action tag list did not parse");
        }
        nlohmann::json tags = nlohmann::json::array();
        for (const auto& item : parsed) {
            tags.push_back(nlohmann::json{{"tagIndex", item.value("tag_index", 0)},
                                           {"code", item.value("code", 0)},
                                           {"insideSprite", item.value("inside_sprite", false)}});
        }
        return nlohmann::json(nlohmann::json{{"tags", std::move(tags)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "Avm1Disassemble") {
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status =
            radicle_swf_avm1_disassemble(movie_, sizeArg(args, 0), &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"text", swftarget::takeText(raw, size)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "ShapeSvg") {
        const long long character = intArg(args, 0);
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status = radicle_swf_shape_svg(
            movie_, static_cast<std::uint16_t>(character), &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"svg", swftarget::takeText(raw, size)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "FrameSvg") {
        const long long frame = intArg(args, 0);
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status = radicle_swf_frame_svg(movie_, static_cast<std::uint32_t>(frame),
                                                            &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"svg", swftarget::takeText(raw, size)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "BitmapPng" || name == "BitmapJpeg") {
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status =
            (name == "BitmapPng")
                ? radicle_swf_bitmap_png(movie_, sizeArg(args, 0), &raw, &size)
                : radicle_swf_bitmap_jpeg(movie_, sizeArg(args, 0), &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return swftarget::radicleError(status);
        }
        swftarget::BytesPtr holder(raw);
        const std::string encoded = fo4ide::core::encodeBase64(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw), size));
        if (name == "BitmapPng") return nlohmann::json(nlohmann::json{{"png", encoded}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return nlohmann::json(nlohmann::json{{"jpeg", encoded}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    if (name == "Undo" || name == "Redo") {
        radicle_swf_plan_result result{};
        const radicle_status status = (name == "Undo") ? radicle_swf_history_undo(history_, &result)
                                                       : radicle_swf_history_redo(history_, &result);
        if (status != RADICLE_STATUS_OK) return swftarget::radicleError(status);
        std::string message;
        if (!refreshFromHistory(message, name == "Undo")) return swftarget::errorText(message);
        return snapshot(false);
    }

    if (name == "Save") return save();

    return std::unexpected(Error{"UnknownMethod", "Unknown swf RPC method: " + name});
}

}  // namespace fo4ide::app
