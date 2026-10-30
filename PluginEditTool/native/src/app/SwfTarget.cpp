#include <fo4ide/app/SwfTarget.hpp>

#include <cctype>
#include <fstream>
#include <memory>

#include <fo4/c_api.h>
#include <fo4/c_api_swf.h>
#include <fo4/c_api_swf_verbs.h>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {
namespace {

const char* statusName(radicle_status status) {
    // Constraint: the names must match Status.ToString, because the panel reads the status field.
    switch (status) {
        case RADICLE_STATUS_OK: return "Ok";
        case RADICLE_STATUS_INVALID_ARGUMENT: return "InvalidArgument";
        case RADICLE_STATUS_IO_ERROR: return "IoError";
        case RADICLE_STATUS_PARSE_ERROR: return "ParseError";
        case RADICLE_STATUS_NOT_FOUND: return "NotFound";
        case RADICLE_STATUS_OUT_OF_MEMORY: return "OutOfMemory";
        case RADICLE_STATUS_INTERNAL_ERROR: return "InternalError";
    }
    return "InternalError";
}

nlohmann::json errorText(const std::string& message) {
    return nlohmann::json(nlohmann::json{{"error", message}}.dump());
}

nlohmann::json radicleError(radicle_status status) {
    const char* detail = radicle_last_error();
    nlohmann::json body{{"error", detail ? detail : "unknown Radicle error"}};
    if (status != RADICLE_STATUS_OK) body["status"] = statusName(status);
    return nlohmann::json(body.dump());
}

std::string stringArg(const nlohmann::json& args, std::size_t index) {
    return support::stringArgument(args, index);
}

long long intArg(const nlohmann::json& args, std::size_t index) {
    // Constraint: the C# host narrows with an unchecked cast, so negatives must wrap rather than refuse.
    const nlohmann::json* value = support::argument(args, index);
    if (value == nullptr) return 0;
    if (value->is_number_integer()) return value->get<long long>();
    if (value->is_number_unsigned()) return static_cast<long long>(value->get<unsigned long long>());
    if (value->is_number_float()) return static_cast<long long>(value->get<double>());
    return 0;
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

struct BytesFree {
    void operator()(std::uint8_t* value) const noexcept {
        if (value != nullptr) radicle_free_bytes(value);
    }
};

using BytesPtr = std::unique_ptr<std::uint8_t, BytesFree>;

std::string takeText(std::uint8_t* bytes, std::size_t size) {
    BytesPtr holder(bytes);
    if (bytes == nullptr) return {};
    return std::string(reinterpret_cast<const char*>(bytes), size);
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

std::string base64Encode(const std::uint8_t* data, std::size_t size) {
    static constexpr char kDigits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (std::size_t index = 0; index < size;) {
        std::uint32_t group = 0;
        int count = 0;
        for (int part = 0; part < 3; ++part) {
            group <<= 8;
            if (index < size) {
                group |= data[index++];
                ++count;
            }
        }
        out.push_back(kDigits[(group >> 18) & 0x3F]);
        out.push_back(kDigits[(group >> 12) & 0x3F]);
        out.push_back(count > 1 ? kDigits[(group >> 6) & 0x3F] : '=');
        out.push_back(count > 2 ? kDigits[group & 0x3F] : '=');
    }
    return out;
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

}  // namespace

SwfTarget::SwfTarget() = default;

SwfTarget::~SwfTarget() {
    if (history_ != nullptr) radicle_swf_history_destroy(history_);
    if (movie_ != nullptr) radicle_swf_destroy(movie_);
}

Result<nlohmann::json> SwfTarget::open(const nlohmann::json& args) {
    const std::string path = stringArg(args, 0);
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return errorText("no SWF file was found at that path: " + path);
    const std::vector<std::byte> bytes((std::istreambuf_iterator<char>(stream)),
                                       std::istreambuf_iterator<char>());
    if (stream.bad()) return errorText("the SWF file could not be read: " + path);

    radicle_swf* movie = nullptr;
    const auto* data = bytes.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(bytes.data());
    radicle_status status =
        radicle_swf_open_bytes(data, bytes.size(), &movie);
    if (status != RADICLE_STATUS_OK || movie == nullptr) {
        if (movie != nullptr) radicle_swf_destroy(movie);
        return radicleError(status);
    }
    radicle_swf_history* history = nullptr;
    status = radicle_swf_history_create(movie, &history);
    if (status != RADICLE_STATUS_OK || history == nullptr) {
        if (history != nullptr) radicle_swf_history_destroy(history);
        radicle_swf_destroy(movie);
        return radicleError(status);
    }
    if (history_ != nullptr) radicle_swf_history_destroy(history_);
    if (movie_ != nullptr) radicle_swf_destroy(movie_);
    movie_ = movie;
    history_ = history;
    path_ = path;
    saved_.assign(bytes.begin(), bytes.end());
    current_.assign(bytes.begin(), bytes.end());
    return snapshot(false);
}

Result<nlohmann::json> SwfTarget::snapshot(bool changed) {
    if (history_ == nullptr) return errorText("no SWF document is open");
    std::uint8_t* raw = nullptr;
    std::size_t size = 0;
    const radicle_status status = radicle_swf_history_write_bytes(history_, &raw, &size);
    if (status != RADICLE_STATUS_OK) {
        if (raw != nullptr) radicle_free_bytes(raw);
        return radicleError(status);
    }
    BytesPtr holder(raw);
    const std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(raw),
                                       reinterpret_cast<const std::byte*>(raw) + size);
    current_.assign(bytes.begin(), bytes.end());
    std::size_t undo = 0;
    std::size_t redo = 0;
    if (radicle_swf_history_undo_count(history_, &undo) != RADICLE_STATUS_OK ||
        radicle_swf_history_redo_count(history_, &redo) != RADICLE_STATUS_OK) {
        return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
    }
    nlohmann::json body{{"changed", changed},
                        {"revision", radicle_swf_history_revision(history_)},
                        {"undo", undo},
                        {"redo", redo},
                        {"dirty", current_ != saved_}};
    return nlohmann::json(body.dump());
}

bool SwfTarget::refreshFromHistory(std::string& message) {
    std::uint8_t* raw = nullptr;
    std::size_t size = 0;
    if (radicle_swf_history_write_bytes(history_, &raw, &size) != RADICLE_STATUS_OK) {
        if (raw != nullptr) radicle_free_bytes(raw);
        const char* detail = radicle_last_error();
        message = detail ? detail : "the history bytes could not be read";
        return false;
    }
    BytesPtr holder(raw);
    radicle_swf* next = nullptr;
    const radicle_status status = radicle_swf_open_bytes(raw, size, &next);
    if (status != RADICLE_STATUS_OK || next == nullptr) {
        if (next != nullptr) radicle_swf_destroy(next);
        const char* detail = radicle_last_error();
        message = detail ? detail : "the edited movie did not decode";
        return false;
    }
    if (movie_ != nullptr) radicle_swf_destroy(movie_);
    movie_ = next;
    current_.assign(reinterpret_cast<const std::byte*>(raw),
                    reinterpret_cast<const std::byte*>(raw) + size);
    return true;
}

Result<nlohmann::json> SwfTarget::invoke(std::string_view method, const nlohmann::json& args) {
    std::lock_guard<std::mutex> guard(mutex_);
    const std::string name(method);

    if (name == "Open") return open(args);

    if (movie_ == nullptr || history_ == nullptr) return errorText("no SWF document is open");

    if (name == "State") return snapshot(false);

    if (name == "Characters") {
        std::size_t count = 0;
        if (radicle_swf_character_count(movie_, &count) != RADICLE_STATUS_OK)
            return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
        nlohmann::json characters = nlohmann::json::array();
        for (std::size_t index = 0; index < count; ++index) {
            radicle_swf_character raw{};
            if (radicle_swf_character_at(movie_, index, &raw) != RADICLE_STATUS_OK)
                return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
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
        return nlohmann::json(nlohmann::json{{"characters", std::move(characters)}}.dump());
    }

    if (name == "Timeline") {
        radicle_swf_frames* frames = nullptr;
        if (radicle_swf_frames_open(movie_, &frames) != RADICLE_STATUS_OK || frames == nullptr) {
            if (frames != nullptr) radicle_swf_frames_destroy(frames);
            return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
        }
        std::uint16_t character = 0;
        std::size_t frameCount = 0;
        const radicle_status status =
            radicle_swf_frames_timeline_at(frames, sizeArg(args, 0), &character, &frameCount);
        radicle_swf_frames_destroy(frames);
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        return nlohmann::json(
            nlohmann::json{{"characterId", character}, {"frameCount", frameCount}}.dump());
    }

    if (name == "FrameLabels") {
        char** rows = nullptr;
        std::size_t count = 0;
        const radicle_status status = radicle_swf_frame_label_rows(
            history_, u16Arg(args, 0), &rows, &count);
        if (status != RADICLE_STATUS_OK) {
            if (rows != nullptr) radicle_free_string_array(rows, count);
            return radicleError(status);
        }
        nlohmann::json labels = nlohmann::json::array();
        for (std::size_t index = 0; index < count; ++index) {
            const std::string row(rows[index] != nullptr ? rows[index] : "");
            long long labelIndex = 0;
            unsigned long long frame = 0;
            std::string labelName;
            if (!splitRow(row, labelIndex, frame, labelName)) {
                radicle_free_string_array(rows, count);
                return errorText("the frame label row did not parse");
            }
            labels.push_back(
                nlohmann::json{{"labelIndex", labelIndex}, {"frame", frame}, {"name", labelName}});
        }
        radicle_free_string_array(rows, count);
        return nlohmann::json(nlohmann::json{{"labels", std::move(labels)}}.dump());
    }

    if (name == "Frame") {
        radicle_swf_frames* frames = nullptr;
        if (radicle_swf_frames_open(movie_, &frames) != RADICLE_STATUS_OK || frames == nullptr) {
            if (frames != nullptr) radicle_swf_frames_destroy(frames);
            return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
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
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        return nlohmann::json(nlohmann::json{{"rows", std::move(rows)}}.dump());
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
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        std::string message;
        if (!refreshFromHistory(message)) return errorText(message);
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
            return radicleError(status);
        }
        const std::string text = takeText(raw, size);
        nlohmann::json body{{"id", info.character_id},
                            {"text", text},
                            {"color", info.has_color != 0 ? hex8(info.color) : ""},
                            {"x0", info.bounds.x_min},
                            {"x1", info.bounds.x_max},
                            {"y0", info.bounds.y_min},
                            {"y1", info.bounds.y_max}};
        return nlohmann::json(body.dump());
    }

    if (name == "EditEditText") {
        const std::string text = stringArg(args, 1);
        std::string colorHex = stringArg(args, 2);
        while (!colorHex.empty() && colorHex.front() == '#') colorHex.erase(colorHex.begin());
        while (!colorHex.empty() && std::isspace(static_cast<unsigned char>(colorHex.front())))
            colorHex.erase(colorHex.begin());
        while (!colorHex.empty() && std::isspace(static_cast<unsigned char>(colorHex.back())))
            colorHex.pop_back();
        radicle_swf_edit_text_fields fields{};
        fields.has_text = 1;
        fields.text = text.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(text.data());
        fields.text_size = text.size();
        if (!colorHex.empty()) {
            try {
                fields.has_color = 1;
                fields.color = static_cast<std::uint32_t>(std::stoul(colorHex, nullptr, 16));
            } catch (const std::exception&) {
                return errorText("the color is not hexadecimal: " + stringArg(args, 2));
            }
        }
        fields.has_bounds = 1;
        fields.bounds.x_min = int32Arg(args, 3);
        fields.bounds.x_max = int32Arg(args, 4);
        fields.bounds.y_min = int32Arg(args, 5);
        fields.bounds.y_max = int32Arg(args, 6);
        radicle_swf_plan_result result{};
        const radicle_status status =
            radicle_swf_edit_text_apply(history_, sizeArg(args, 0), &fields, &result);
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        std::string message;
        if (!refreshFromHistory(message)) return errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "Placements") {
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        if (radicle_swf_placements_json(history_, &raw, &size) != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
        }
        const std::string text = takeText(raw, size);
        nlohmann::json parsed = nlohmann::json::array();
        try {
            parsed = nlohmann::json::parse(text);
        } catch (const std::exception&) {
            return errorText("the placement list did not parse");
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
        return nlohmann::json(nlohmann::json{{"placements", std::move(placements)}}.dump());
    }

    if (name == "SetDepth") {
        radicle_swf_plan_result result{};
        const radicle_status status = radicle_swf_set_placement_depth(history_, sizeArg(args, 0),
                                                                      u16Arg(args, 1), &result);
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        std::string message;
        if (!refreshFromHistory(message)) return errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "PlacementMatrix") {
        radicle_swf_placement_matrix_fields matrix{};
        const radicle_status status =
            radicle_swf_placement_matrix_at(history_, sizeArg(args, 0), &matrix);
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        nlohmann::json body{{"hasScale", matrix.has_scale != 0},
                            {"scaleX", matrix.has_scale != 0 ? matrix.scale_x : 0x10000},
                            {"scaleY", matrix.has_scale != 0 ? matrix.scale_y : 0x10000},
                            {"hasRotate", matrix.has_rotate != 0},
                            {"rotate0", matrix.rotate0},
                            {"rotate1", matrix.rotate1},
                            {"x", matrix.translate_x},
                            {"y", matrix.translate_y}};
        return nlohmann::json(body.dump());
    }

    if (name == "SetTransform") {
        radicle_swf_plan_result result{};
        const radicle_status status = radicle_swf_set_placement_transform(
            history_, sizeArg(args, 0), int32Arg(args, 1), int32Arg(args, 2), int32Arg(args, 3),
            int32Arg(args, 4), int32Arg(args, 5), int32Arg(args, 6), &result);
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        std::string message;
        if (!refreshFromHistory(message)) return errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "InsertKeyframe") {
        radicle_swf_plan_result result{};
        const radicle_status status = radicle_swf_insert_placement_keyframe(
            history_, sizeArg(args, 0), u32Arg(args, 1), &result);
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        std::string message;
        if (!refreshFromHistory(message)) return errorText(message);
        return snapshot(result.changed != 0);
    }

    if (name == "AbcBlocks") {
        const std::size_t blocks = radicle_swf_abc_count(movie_);
        nlohmann::json list = nlohmann::json::array();
        for (std::size_t abc = 0; abc < blocks; ++abc) {
            radicle_swf_abc_info info{};
            if (radicle_swf_abc_info_at(movie_, abc, &info) != RADICLE_STATUS_OK)
                return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
            nlohmann::json classes = nlohmann::json::array();
            const std::size_t classCount = radicle_swf_abc_class_count(movie_, abc);
            for (std::size_t index = 0; index < classCount; ++index) {
                radicle_swf_class raw{};
                if (radicle_swf_abc_class_at(movie_, abc, index, &raw) != RADICLE_STATUS_OK)
                    return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
                classes.push_back(orEmpty(raw.name));
            }
            nlohmann::json scripts = nlohmann::json::array();
            const std::size_t scriptCount = radicle_swf_abc_script_count(movie_, abc);
            for (std::size_t index = 0; index < scriptCount; ++index) {
                radicle_swf_method raw{};
                if (radicle_swf_abc_script_at(movie_, abc, index, &raw) != RADICLE_STATUS_OK)
                    return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
                scripts.push_back(orEmpty(raw.name));
            }
            nlohmann::json methods = nlohmann::json::array();
            const std::size_t methodCount = radicle_swf_abc_method_count(movie_, abc);
            for (std::size_t index = 0; index < methodCount; ++index) {
                radicle_swf_method_pool raw{};
                if (radicle_swf_abc_method_at(movie_, abc, index, &raw) != RADICLE_STATUS_OK)
                    return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
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
        return nlohmann::json(nlohmann::json{{"blocks", std::move(list)}}.dump());
    }

    if (name == "Disassemble") {
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status = radicle_swf_abc_method_disassemble(movie_, sizeArg(args, 0),
                                                                         sizeArg(args, 1), &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"text", takeText(raw, size)}}.dump());
    }

    if (name == "InsertAbcInstruction" || name == "ReplaceAbcInstruction") {
        const std::size_t abc = sizeArg(args, 0);
        const std::uint32_t body = u32Arg(args, 1);
        const std::size_t offset = sizeArg(args, 2);
        const std::string text = stringArg(args, 3);
        radicle_swf_abc_info info{};
        if (radicle_swf_abc_info_at(movie_, abc, &info) != RADICLE_STATUS_OK)
            return radicleError(RADICLE_STATUS_NOT_FOUND);
        std::uint8_t* current = nullptr;
        std::size_t currentSize = 0;
        if (radicle_swf_history_write_bytes(history_, &current, &currentSize) !=
            RADICLE_STATUS_OK) {
            if (current != nullptr) radicle_free_bytes(current);
            return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
        }
        BytesPtr currentHolder(current);
        radicle_swf* scratch = nullptr;
        if (radicle_swf_open_bytes(current, currentSize, &scratch) != RADICLE_STATUS_OK ||
            scratch == nullptr) {
            if (scratch != nullptr) radicle_swf_destroy(scratch);
            return radicleError(RADICLE_STATUS_PARSE_ERROR);
        }
        const auto* input =
            text.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(text.data());
        radicle_status status =
            (name == "InsertAbcInstruction")
                ? radicle_swf_abc_insert_instruction(scratch, abc, body, offset, input, text.size())
                : radicle_swf_abc_replace_instruction(scratch, abc, body, offset, input,
                                                      text.size());
        radicle_swf_plan_result result{};
        if (status == RADICLE_STATUS_OK) {
            std::uint8_t* payload = nullptr;
            std::size_t payloadSize = 0;
            status = radicle_swf_tag_payload(scratch, info.tag_index, &payload, &payloadSize);
            if (status == RADICLE_STATUS_OK && payload != nullptr) {
                BytesPtr payloadHolder(payload);
                radicle_swf_tag_plan plan{};
                radicle_swf_tag_replacement replacement{};
                replacement.tag_index = info.tag_index;
                replacement.payload = payload;
                replacement.size = payloadSize;
                plan.replacements = &replacement;
                plan.replacement_count = 1;
                status = radicle_swf_history_apply(history_, &plan, &result);
            } else {
                if (payload != nullptr) radicle_free_bytes(payload);
                if (status == RADICLE_STATUS_OK) status = RADICLE_STATUS_INTERNAL_ERROR;
            }
        }
        radicle_swf_destroy(scratch);
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        std::string message;
        if (!refreshFromHistory(message)) return errorText(message);
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
            return radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"text", takeText(raw, size)}}.dump());
    }

    if (name == "ActionTags") {
        const std::size_t count = radicle_swf_tag_count(movie_);
        nlohmann::json tags = nlohmann::json::array();
        for (std::size_t index = 0; index < count; ++index) {
            radicle_swf_tag tag{};
            if (radicle_swf_tag_at(movie_, index, &tag) != RADICLE_STATUS_OK)
                return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
            if (tag.code != 12 && tag.code != 59) continue;
            tags.push_back(nlohmann::json{{"tagIndex", index},
                                           {"code", tag.code},
                                           {"insideSprite", tag.inside_sprite != 0}});
        }
        return nlohmann::json(nlohmann::json{{"tags", std::move(tags)}}.dump());
    }

    if (name == "Avm1Disassemble") {
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status =
            radicle_swf_avm1_disassemble(movie_, sizeArg(args, 0), &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"text", takeText(raw, size)}}.dump());
    }

    if (name == "ShapeSvg") {
        const long long character = intArg(args, 0);
        if (character < 0 || character > 65535)
            return errorText("character id must be 0..65535");
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status = radicle_swf_shape_svg(
            movie_, static_cast<std::uint16_t>(character), &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"svg", takeText(raw, size)}}.dump());
    }

    if (name == "FrameSvg") {
        const long long frame = intArg(args, 0);
        if (frame < 0) return errorText("frame must be 0 or more");
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        const radicle_status status = radicle_swf_frame_svg(movie_, static_cast<std::uint32_t>(frame),
                                                            &raw, &size);
        if (status != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return radicleError(status);
        }
        return nlohmann::json(nlohmann::json{{"svg", takeText(raw, size)}}.dump());
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
            return radicleError(status);
        }
        BytesPtr holder(raw);
        const std::string encoded = base64Encode(raw, size);
        if (name == "BitmapPng") return nlohmann::json(nlohmann::json{{"png", encoded}}.dump());
        return nlohmann::json(nlohmann::json{{"jpeg", encoded}}.dump());
    }

    if (name == "Undo" || name == "Redo") {
        radicle_swf_plan_result result{};
        const radicle_status status = (name == "Undo") ? radicle_swf_history_undo(history_, &result)
                                                       : radicle_swf_history_redo(history_, &result);
        if (status != RADICLE_STATUS_OK) return radicleError(status);
        std::string message;
        if (!refreshFromHistory(message)) return errorText(message);
        return snapshot(false);
    }

    if (name == "Save") {
        if (!path_) return errorText("the document has no path to save to");
        std::uint8_t* raw = nullptr;
        std::size_t size = 0;
        if (radicle_swf_history_write_bytes(history_, &raw, &size) != RADICLE_STATUS_OK) {
            if (raw != nullptr) radicle_free_bytes(raw);
            return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
        }
        BytesPtr holder(raw);
        std::ofstream stream(*path_, std::ios::binary | std::ios::trunc);
        if (!stream) return errorText("the SWF file could not be written: " + *path_);
        stream.write(reinterpret_cast<const char*>(raw), static_cast<std::streamsize>(size));
        stream.close();
        if (!stream) return errorText("the SWF file could not be written: " + *path_);
        saved_.assign(reinterpret_cast<const std::byte*>(raw),
                      reinterpret_cast<const std::byte*>(raw) + size);
        current_.assign(saved_.begin(), saved_.end());
        return nlohmann::json(nlohmann::json{{"saved", *path_}}.dump());
    }

    return std::unexpected(Error{"UnknownMethod", "Unknown swf RPC method: " + name});
}

}  // namespace fo4ide::app
