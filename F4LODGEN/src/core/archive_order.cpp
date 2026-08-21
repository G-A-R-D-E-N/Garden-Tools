#include "f4lod/archive_order.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace f4lod {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1U);
    if (value.size() >= 2U &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1U, value.size() - 2U);
    }
    return value;
}

std::string windows_basename(std::string value) {
    value = trim(std::move(value));
    const auto slash = value.find_last_of("/\\");
    if (slash != std::string::npos) value.erase(0U, slash + 1U);
    return value;
}

struct IniArchiveValues {
    bool valid{true};
    std::string error;
    std::unordered_map<std::string, std::string> archive_values;
};

IniArchiveValues read_archive_ini(const std::filesystem::path& path) {
    IniArchiveValues result;
    if (path.empty()) return result;

    std::ifstream input(path);
    if (!input) {
        result.valid = false;
        result.error = "Could not open INI file: " + path.string();
        return result;
    }

    std::string section;
    std::string line;
    bool first_line = true;
    while (std::getline(input, line)) {
        if (first_line) {
            first_line = false;
            if (line.size() >= 3U &&
                static_cast<unsigned char>(line[0]) == 0xEFU &&
                static_cast<unsigned char>(line[1]) == 0xBBU &&
                static_cast<unsigned char>(line[2]) == 0xBFU) {
                line.erase(0U, 3U);
            }
        }

        line = trim(std::move(line));
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower_ascii(trim(line.substr(1U, line.size() - 2U)));
            continue;
        }
        if (section != "archive") continue;

        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        auto key = lower_ascii(trim(line.substr(0U, equals)));
        auto value = trim(line.substr(equals + 1U));
        result.archive_values[std::move(key)] = std::move(value);
    }
    return result;
}

std::vector<std::string> split_archive_list(const std::string& value) {
    std::vector<std::string> result;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto comma = value.find(',', begin);
        const auto end = comma == std::string::npos ? value.size() : comma;
        auto item = trim(value.substr(begin, end - begin));
        if (!item.empty()) result.push_back(std::move(item));
        if (comma == std::string::npos) break;
        begin = comma + 1U;
    }
    return result;
}

std::vector<std::filesystem::path> enumerate_top_level_ba2s(
    const std::filesystem::path& data_root,
    std::string& error) {
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    if (!std::filesystem::is_directory(data_root, ec) || ec) {
        error = "Data directory does not exist or is not readable.";
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(
             data_root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            error = "Could not enumerate Fallout 4 Data directory while ordering BA2 archives: " + ec.message();
            return {};
        }
        if (!entry.is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        if (lower_ascii(entry.path().extension().string()) == ".ba2") result.push_back(entry.path());
    }

    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lower_ascii(lhs.filename().string()) < lower_ascii(rhs.filename().string());
    });
    return result;
}

std::optional<std::filesystem::path> find_ba2(
    const std::vector<std::filesystem::path>& available,
    const std::string& requested) {
    const auto requested_name = lower_ascii(windows_basename(requested));
    for (const auto& path : available) {
        if (lower_ascii(path.filename().string()) == requested_name) return path;
    }
    return std::nullopt;
}

bool append_archive(
    ArchiveOrderResult& result,
    const std::filesystem::path& path,
    ArchiveOrderSource source,
    std::string owner,
    std::unordered_set<std::string>& seen) {
    const auto key = lower_ascii(path.filename().string());
    if (!seen.insert(key).second) return true;

    auto archive = inspect_ba2_index(path);
    if (!archive.valid) {
        result.error = "Could not inspect selected BA2 archive " + path.string() + ": " + archive.error;
        return false;
    }

    result.entries_low_to_high.push_back(ArchiveOrderEntry{
        path,
        source,
        std::move(owner),
        archive.type,
    });
    if (archive.type == Ba2ArchiveType::general) ++result.general_archive_count;
    else if (archive.type == Ba2ArchiveType::texture) ++result.texture_archive_count;
    result.archives_low_to_high.push_back(std::move(archive));
    return true;
}

} // namespace

ArchiveOrderResult build_fallout4_archive_order(const ArchiveOrderOptions& options) {
    ArchiveOrderResult result;
    std::string enumerate_error;
    const auto available = enumerate_top_level_ba2s(options.data_root, enumerate_error);
    if (!enumerate_error.empty()) {
        result.error = std::move(enumerate_error);
        return result;
    }

    const auto primary = read_archive_ini(options.primary_ini);
    if (!primary.valid) {
        result.error = primary.error;
        return result;
    }
    const auto custom = read_archive_ini(options.custom_ini);
    if (!custom.valid) {
        result.error = custom.error;
        return result;
    }

    std::unordered_set<std::string> seen;
    constexpr std::array<std::string_view, 4> ini_keys{
        "sresourceindexfilelist",
        "sresourcestartuparchivelist",
        "sresourcearchivelist",
        "sresourcearchivelist2",
    };

    for (const auto key_view : ini_keys) {
        const std::string key{key_view};
        const auto custom_value = custom.archive_values.find(key);
        const auto primary_value = primary.archive_values.find(key);
        const std::string* value = nullptr;
        std::string owner;
        if (custom_value != custom.archive_values.end()) {
            value = &custom_value->second;
            owner = options.custom_ini.string();
        } else if (primary_value != primary.archive_values.end()) {
            value = &primary_value->second;
            owner = options.primary_ini.string();
        }
        if (!value) continue;

        for (const auto& requested : split_archive_list(*value)) {
            const auto found = find_ba2(available, requested);
            if (!found.has_value()) {
                result.warnings.push_back(
                    "INI archive is not present in Data: " + requested + " (" + owner + ")");
                continue;
            }
            if (!append_archive(result, *found, ArchiveOrderSource::ini, owner, seen)) return result;
        }
    }

    for (const auto& plugin : options.enabled_plugins_low_to_high) {
        const auto stem = lower_ascii(std::filesystem::path(plugin).stem().string());
        if (stem.empty()) continue;

        std::vector<std::filesystem::path> matches;
        for (const auto& archive_path : available) {
            const auto archive_stem = lower_ascii(archive_path.stem().string());
            if (archive_stem.starts_with(stem)) matches.push_back(archive_path);
        }
        std::sort(matches.begin(), matches.end(), [](const auto& lhs, const auto& rhs) {
            return lower_ascii(lhs.filename().string()) < lower_ascii(rhs.filename().string());
        });

        for (const auto& archive_path : matches) {
            if (!append_archive(result, archive_path, ArchiveOrderSource::plugin, plugin, seen)) return result;
        }
    }

    result.valid = true;
    return result;
}

const char* archive_order_source_name(ArchiveOrderSource source) noexcept {
    switch (source) {
    case ArchiveOrderSource::ini: return "ini";
    case ArchiveOrderSource::plugin: return "plugin";
    }
    return "plugin";
}

} // namespace f4lod
