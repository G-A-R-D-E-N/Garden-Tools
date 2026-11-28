#include "RadiclePluginQuestInternal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fo4/localization/strings.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin::questinternal {

std::vector<std::byte> readLocalizedFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("could not open localized string file");
    const auto end = stream.tellg();
    if (end < 0) throw std::runtime_error("could not determine localized string file size");
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream) throw std::runtime_error("could not read localized string file");
    }
    return bytes;
}

std::optional<Error> encodeLocalizedText(std::string_view language,
                                          std::string_view text,
                                          std::vector<std::byte>& bytes) {
    bytes.clear();
    bytes.reserve(text.size());
    const bool english = language == "en";
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::size_t width = 0;
        std::uint32_t codePoint = 0;
        if (first <= 0x7f) {
            width = 1;
            codePoint = first;
        } else if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
            codePoint = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3;
            codePoint = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4;
            codePoint = first & 0x07;
        } else {
            return Error{
                "LocalizedLogEncodingUnsupported",
                "localized CNAM text is not valid UTF-8"
            };
        }
        if (width > text.size() - index) {
            return Error{
                "LocalizedLogEncodingUnsupported",
                "localized CNAM text is not valid UTF-8"
            };
        }
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xc0) != 0x80) {
                return Error{
                    "LocalizedLogEncodingUnsupported",
                    "localized CNAM text is not valid UTF-8"
                };
            }
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if ((width == 2 && codePoint < 0x80) ||
            (width == 3 && codePoint < 0x800) ||
            (width == 4 && codePoint < 0x10000) ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff) || codePoint > 0x10ffff) {
            return Error{
                "LocalizedLogEncodingUnsupported",
                "localized CNAM text is not valid UTF-8"
            };
        }

        if (!english) {
            bytes.insert(bytes.end(),
                         reinterpret_cast<const std::byte*>(text.data() + index),
                         reinterpret_cast<const std::byte*>(text.data() + index + width));
        } else if (codePoint <= 0x7f || (codePoint >= 0xa0 && codePoint <= 0xff)) {
            bytes.push_back(std::byte{static_cast<unsigned char>(codePoint)});
        } else {
            std::uint8_t encoded = 0;
            switch (codePoint) {
            case 0x20ac: encoded = 0x80; break;
            case 0x201a: encoded = 0x82; break;
            case 0x192: encoded = 0x83; break;
            case 0x201e: encoded = 0x84; break;
            case 0x2026: encoded = 0x85; break;
            case 0x2020: encoded = 0x86; break;
            case 0x2021: encoded = 0x87; break;
            case 0x2c6: encoded = 0x88; break;
            case 0x2030: encoded = 0x89; break;
            case 0x160: encoded = 0x8a; break;
            case 0x2039: encoded = 0x8b; break;
            case 0x152: encoded = 0x8c; break;
            case 0x17d: encoded = 0x8e; break;
            case 0x2018: encoded = 0x91; break;
            case 0x2019: encoded = 0x92; break;
            case 0x201c: encoded = 0x93; break;
            case 0x201d: encoded = 0x94; break;
            case 0x2022: encoded = 0x95; break;
            case 0x2013: encoded = 0x96; break;
            case 0x2014: encoded = 0x97; break;
            case 0x2dc: encoded = 0x98; break;
            case 0x2122: encoded = 0x99; break;
            case 0x161: encoded = 0x9a; break;
            case 0x203a: encoded = 0x9b; break;
            case 0x153: encoded = 0x9c; break;
            case 0x17e: encoded = 0x9e; break;
            case 0x178: encoded = 0x9f; break;
            default:
                return Error{
                    "LocalizedLogEncodingUnsupported",
                    "localized English CNAM text is not representable in CP1252"
                };
            }
            bytes.push_back(std::byte{encoded});
        }
        index += width;
    }
    return std::nullopt;
}

std::optional<Error> loadLocalizedQuestTables(const nlohmann::json& arguments,
                                              const std::filesystem::path& pluginPath,
                                              const std::filesystem::path& directory,
                                              LocalizedQuestTables& result,
                                              bool allowAbsent) {
    if (!arguments.is_array() || arguments.size() <= 3 || !arguments[3].is_object()) {
        return Error{
            "LocalizedLogTextUnsupported",
            "localized QUST CNAM authoring requires language and stringsDirectory"
        };
    }
    const auto& context = arguments[3];
    if (!context.contains("language") || !context["language"].is_string() ||
        !context.contains("stringsDirectory") || !context["stringsDirectory"].is_string()) {
        return Error{
            "InvalidLocalizedQuestContext",
            "localized QUST context requires string language and stringsDirectory"
        };
    }
    const auto language = fo4::text::fold(context["language"].get<std::string>());
    static constexpr std::array<std::string_view, 11> supportedLanguages{
        "en", "de", "it", "es", "esmx", "fr", "pl", "ptbr", "cn", "ru", "ja"};
    if (std::find(supportedLanguages.begin(), supportedLanguages.end(), language) ==
            supportedLanguages.end()) {
        return Error{
            "InvalidLocalizedQuestContext",
            "localized QUST context has an unsupported language token: " + language
        };
    }
    result.language = language;

    const auto stem = fo4::io::utf8(pluginPath.stem()) + "_" + language;
    const auto load = [&](fo4::localization::StringTableKind kind,
                          std::string_view extension,
                          fo4::localization::LocalizedStringTable& table)
        -> std::optional<Error> {
        const auto path = directory / fo4::io::from_utf8(stem + std::string(extension));
        if (kind == fo4::localization::StringTableKind::strings) result.stringsPath = path;
        else if (kind == fo4::localization::StringTableKind::dlstrings) result.dlstringsPath = path;
        else result.ilstringsPath = path;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path, ec);
        if (allowAbsent && (ec == std::errc::no_such_file_or_directory ||
                            (!ec && status.type() == std::filesystem::file_type::not_found))) {
            table = {kind, {}};
            return std::nullopt;
        }
        if (ec || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(status)) {
            return Error{
                "LocalizedLogTableUnavailable",
                "localized sibling table is unavailable: " + path.string()
            };
        }
        try {
            const auto originalBytes = readLocalizedFileBytes(path);
            table = fo4::localization::read_string_table(originalBytes, kind);
#ifdef FO4IDE_TEST_HOOKS
            if (kind == fo4::localization::StringTableKind::ilstrings &&
                localizedQuestAfterTableParseHook != nullptr) {
                localizedQuestAfterTableParseHook();
            }
#endif
            if (kind == fo4::localization::StringTableKind::strings) {
                result.stringsExist = true;
                result.originalStringsBytes = originalBytes;
            } else if (kind == fo4::localization::StringTableKind::dlstrings) {
                result.dlstringsExist = true;
                result.originalDlstringsBytes = originalBytes;
            } else {
                result.ilstringsExist = true;
                result.originalIlstringsBytes = originalBytes;
            }
        } catch (const std::exception& error) {
            return Error{
                "LocalizedLogTableInvalid",
                "could not read or snapshot localized sibling table " + path.string() + ": " +
                    error.what()
            };
        }
        return std::nullopt;
    };

    if (const auto error = load(fo4::localization::StringTableKind::strings,
                               ".STRINGS", result.strings)) {
        return error;
    }
    if (const auto error = load(fo4::localization::StringTableKind::dlstrings,
                               ".DLSTRINGS", result.dlstrings)) {
        return error;
    }
    if (const auto error = load(fo4::localization::StringTableKind::ilstrings,
                               ".ILSTRINGS", result.ilstrings)) {
        return error;
    }

    std::uint32_t maximum = 0;
    for (const auto* table : {&result.strings, &result.dlstrings, &result.ilstrings}) {
        for (const auto& entry : table->entries) maximum = std::max(maximum, entry.id);
    }
    if (maximum == std::numeric_limits<std::uint32_t>::max()) {
        return Error{
            "LocalizedLogTableFull",
            "localized string-table ID space is exhausted"
        };
    }
    result.nextId = maximum + 1;
    return std::nullopt;
}

}
