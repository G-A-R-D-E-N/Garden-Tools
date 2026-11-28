#include <fo4ide/material/MaterialService.hpp>

#include <fo4/error.hpp>
#include <fo4/io/atomic_write.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>

namespace fo4ide::material {
namespace {

using fo4::material::FieldType;
using fo4::material::MaterialField;
using fo4::material::MaterialFile;
using fo4::material::RawBool;

struct Alias {
    std::string_view material;
    std::string_view shown;
};

constexpr std::array aliases{
    Alias{"ScreenSpaceReflections", "Ssr"},
    Alias{"WetnessControlScreenSpaceReflections", "WetSsr"},
    Alias{"EnvironmentMapping", "EnvMapping"},
    Alias{"EnvironmentMappingMaskScale", "EnvMappingMaskScale"},
    Alias{"EffectEnvironmentMapping", "EnvironmentMapping"},
    Alias{"EffectEnvironmentMappingMaskScale", "EnvironmentMappingMaskScale"},
};

std::string lower(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string_view shownName(std::string_view material) {
    for (const auto& alias : aliases) {
        if (alias.material == material) return alias.shown;
    }
    return material;
}

MaterialField* findShown(MaterialFile& file, std::string_view shown) {
    const auto wanted = lower(shown);
    for (auto& field : file.fields) {
        if (field.name != "TileFlags" && lower(shownName(field.name)) == wanted) return &field;
    }
    return nullptr;
}

std::string formatFloat(float value) {
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (converted.ec != std::errc{}) throw std::runtime_error("material float value cannot be formatted");
    std::string text(buffer.data(), converted.ptr);
    if (text == "-0") text = "0";
    return text;
}

std::string displayValue(const fo4::material::FieldValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, RawBool>) return item.raw != 0 ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint32_t>) return std::to_string(item);
        else if constexpr (std::is_same_v<T, float>) return formatFloat(item);
        else if constexpr (std::is_same_v<T, std::string>) return std::string(fo4::material::text(item));
        else return formatFloat(item.r) + ", " + formatFloat(item.g) + ", " + formatFloat(item.b);
    }, value);
}

const char* typeName(FieldType type) {
    switch (type) {
    case FieldType::boolean: return "bool";
    case FieldType::u8:
    case FieldType::u32: return "int";
    case FieldType::f32: return "float";
    case FieldType::color3: return "color";
    case FieldType::string: return "string";
    }
    return "string";
}

std::uint64_t parseWhole(std::string_view value) {
    std::size_t used = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(std::string(value), &used);
    } catch (const std::exception&) {
        throw std::runtime_error("expected a whole number");
    }
    if (used != value.size()) throw std::runtime_error("unexpected characters after a whole number");
    return parsed;
}

float parseFinite(std::string_view value) {
    std::size_t used = 0;
    float parsed = 0.0F;
    try {
        parsed = std::stof(std::string(value), &used);
    } catch (const std::exception&) {
        throw std::runtime_error("expected a number");
    }
    if (used != value.size()) throw std::runtime_error("unexpected characters after a number");
    if (!std::isfinite(parsed)) throw std::runtime_error("expected a finite number");
    return parsed;
}

bool parseBool(std::string value) {
    value = lower(value);
    if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
    if (value == "false" || value == "0" || value == "no" || value == "off") return false;
    throw std::runtime_error("expected true or false");
}

std::array<float, 3> parseColor(std::string value) {
    std::ranges::replace(value, ',', ' ');
    std::istringstream stream(value);
    std::array<float, 3> color{};
    if (!(stream >> color[0] >> color[1] >> color[2])) {
        throw std::runtime_error("expected 3 comma/space-separated numbers, e.g. '1.0, 0.5, 0.2'");
    }
    std::string extra;
    if (stream >> extra) throw std::runtime_error("expected exactly 3 color components");
    for (const float component : color) {
        if (!std::isfinite(component)) throw std::runtime_error("color components must be finite");
    }
    return color;
}

void assign(MaterialFile& file, const MaterialField& field, const std::string& value) {
    switch (field.type()) {
    case FieldType::boolean:
        fo4::material::set_field(file, field.name, parseBool(value));
        return;
    case FieldType::u8: {
        const auto parsed = parseWhole(value);
        if (parsed > 255U) throw std::runtime_error("value is outside byte range");
        fo4::material::set_field(file, field.name, static_cast<std::uint8_t>(parsed));
        return;
    }
    case FieldType::u32: {
        const auto parsed = parseWhole(value);
        if (parsed > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("value is outside uint32 range");
        fo4::material::set_field(file, field.name, static_cast<std::uint32_t>(parsed));
        return;
    }
    case FieldType::f32:
        fo4::material::set_field(file, field.name, parseFinite(value));
        return;
    case FieldType::color3: {
        const auto color = parseColor(value);
        fo4::material::set_field(file, field.name, fo4::material::Color3{color[0], color[1], color[2]});
        return;
    }
    case FieldType::string:
        if (value.find('\0') != std::string::npos) throw std::runtime_error("material string values cannot contain an embedded NUL byte");
        fo4::material::set_text(file, field.name, value);
        return;
    }
}

std::uint32_t tileMask(std::string_view name) {
    const auto key = lower(name);
    if (key == "tileu") return 2U;
    if (key == "tilev") return 1U;
    return 0U;
}

bool applyField(MaterialFile& file, std::string_view name, const std::string& value) {
    if (const auto mask = tileMask(name); mask != 0) {
        const auto flags = fo4::material::get_field<std::uint32_t>(file, "TileFlags");
        if (!flags) return false;
        const auto on = parseBool(value);
        return fo4::material::set_field(file, "TileFlags", on ? (*flags | mask) : (*flags & ~mask));
    }
    auto* field = findShown(file, name);
    if (field == nullptr) return false;
    if (field->name == "Version" || field->name == "Signature") {
        std::size_t used = 0;
        const auto requested = static_cast<std::uint32_t>(std::stoul(value, &used, 0));
        if (used != value.size()) throw std::runtime_error("unexpected characters after the version number");
        const auto current = fo4::material::get_field<std::uint32_t>(file, field->name).value_or(0U);
        if (requested != current) {
            throw std::runtime_error(field->name == "Version"
                ? "changing material version is not supported because it changes the binary layout"
                : "changing material signature is not supported");
        }
        return true;
    }
    assign(file, *field, value);
    return true;
}

nlohmann::json row(std::string name, const char* section, const char* type, std::string value) {
    return nlohmann::json{
        {"name", std::move(name)},
        {"section", section},
        {"type", type},
        {"value", std::move(value)}
    };
}

nlohmann::json fieldRow(const MaterialField& field, const char* section) {
    return row(std::string(shownName(field.name)), section, typeName(field.type()), displayValue(field.value));
}

nlohmann::json fieldRows(const MaterialFile& file) {
    const auto body = std::ranges::find_if(file.fields, [](const MaterialField& field) {
        return field.type() == FieldType::string;
    });
    auto rows = nlohmann::json::array();
    for (auto it = body; it != file.fields.end(); ++it) rows.push_back(fieldRow(*it, "material"));
    for (auto it = file.fields.begin(); it != body; ++it) {
        if (it->name != "TileFlags") {
            rows.push_back(fieldRow(*it, "header"));
            continue;
        }
        const auto flags = fo4::material::get_field<std::uint32_t>(file, "TileFlags").value_or(0U);
        rows.push_back(row("TileU", "header", "bool", (flags & 2U) != 0 ? "true" : "false"));
        rows.push_back(row("TileV", "header", "bool", (flags & 1U) != 0 ? "true" : "false"));
    }
    return rows;
}

Result<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::unexpected(Error{"MaterialReadFailed", "Could not open material file: " + path.string()});
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) return std::unexpected(Error{"MaterialReadFailed", "Could not determine material size"});
    constexpr std::streamoff kMaxMaterialBytes = 16 * 1024 * 1024;
    if (length > kMaxMaterialBytes) {
        return std::unexpected(Error{"MaterialFileTooLarge",
            "Material file is larger than the 16 MiB limit: " + path.string()});
    }
    stream.seekg(0, std::ios::beg);
    unsigned char magic[4]{};
    stream.read(reinterpret_cast<char*>(magic), 4);
    if (stream.gcount() != 4 || magic[0] != 'B' || magic[1] != 'G' ||
        (magic[2] != 'S' && magic[2] != 'E') || magic[3] != 'M') {
        return std::unexpected(Error{"MaterialParseFailed",
            "Not a BGSM or BGEM material: " + path.string()});
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream && !bytes.empty()) return std::unexpected(Error{"MaterialReadFailed", "Could not read complete material file"});
    return bytes;
}

}

Result<fo4::material::MaterialFile> MaterialService::load(const std::filesystem::path& path) const {
    if (path.empty()) return std::unexpected(Error{"MaterialPathMissing", "Provide a .bgsm or .bgem file path"});
    auto bytes = readFile(path);
    if (!bytes) return std::unexpected(bytes.error());
    try {
        return fo4::material::read_material_file(*bytes);
    } catch (const fo4::parse_error& ex) {
        return std::unexpected(Error{"MaterialParseFailed", ex.what()});
    }
}

Result<nlohmann::json> MaterialService::inspect(const std::filesystem::path& path) const {
    auto file = load(path);
    if (!file) return std::unexpected(file.error());
    return nlohmann::json{
        {"fileName", path.filename().string()},
        {"format", file->format == fo4::material::MaterialFormat::bgem ? "BGEM" : "BGSM"},
        {"version", file->version()},
        {"fields", fieldRows(*file)},
        {"trailingBytes", file->trailing.size()}
    };
}

Result<std::string> MaterialService::setFields(
    const std::filesystem::path& path,
    const std::map<std::string, std::string, std::less<>>& fields,
    const std::filesystem::path& output) const {
    if (fields.empty()) return std::unexpected(Error{"MaterialNoChanges", "No field changes to apply"});
    auto file = load(path);
    if (!file) return std::unexpected(file.error());

    std::vector<std::string> applied;
    try {
        for (const auto& [name, value] : fields) {
            if (!applyField(*file, name, value)) {
                return std::unexpected(Error{"MaterialUnknownField", "Unknown material field '" + name + "'. A conditional field such as EmittanceColor is listed only when its switch (EmitEnabled) is already true in the saved file. Setting the switch in the same call does not add it, so set the switch first and the field in a later call."});
            }
            applied.push_back(name);
        }
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"MaterialFieldParseFailed", ex.what()});
    }

    const auto destination = output.empty() ? path : output;
    try {
        const auto bytes = fo4::material::write_material_file(*file);
        fo4::io::write_file_atomic(destination, bytes);
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"MaterialWriteFailed", ex.what()});
    }

    std::ostringstream message;
    message << "Set " << applied.size() << " field(s) (";
    for (std::size_t i = 0; i < applied.size(); ++i) {
        if (i != 0) message << ", ";
        message << applied[i];
    }
    message << ") in '" << path.filename().string() << "', wrote to '" << destination.string() << "'.";
    return message.str();
}

}
