#include <fo4ide/plugin/RadiclePluginStringLanguages.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include <fo4/archive/ba2_reader.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {
namespace {

std::string fold(std::string_view value) {
    return fo4::text::fold(value);
}

bool token(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0;
    });
}

bool matchingTableName(std::string_view filename, std::string_view pluginStem, std::string& language) {
    const auto name = fold(filename);
    const auto prefix = fold(pluginStem) + '_';
    if (!name.starts_with(prefix)) return false;
    const auto dot = name.rfind('.');
    if (dot == std::string::npos ||
        (name.substr(dot) != ".strings" && name.substr(dot) != ".dlstrings" && name.substr(dot) != ".ilstrings")) {
        return false;
    }
    const auto candidate = std::string_view(name).substr(prefix.size(), dot - prefix.size());
    if (!token(candidate)) return false;
    language.assign(candidate);
    return true;
}

Error unavailable(std::string message) {
    return {"LocalizedSourcesUnavailable", std::move(message)};
}

bool sameArchiveIndex(const fo4::archive::Ba2Reader& left, const fo4::archive::Ba2Reader& right) {
    if (left.kind() != right.kind() || left.version() != right.version() ||
        left.has_name_table() != right.has_name_table() || left.entries().size() != right.entries().size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.entries().size(); ++i) {
        const auto& a = left.entries()[i];
        const auto& b = right.entries()[i];
        if (a.path != b.path || a.name_hash != b.name_hash || a.extension_hash != b.extension_hash ||
            a.directory_hash != b.directory_hash || a.unknown != b.unknown ||
            a.texture.height != b.texture.height || a.texture.width != b.texture.width ||
            a.texture.mip_count != b.texture.mip_count || a.texture.dxgi_format != b.texture.dxgi_format ||
            a.texture.flags != b.texture.flags || a.texture.tile_mode != b.texture.tile_mode ||
            a.chunks.size() != b.chunks.size()) {
            return false;
        }
        for (std::size_t chunk = 0; chunk < a.chunks.size(); ++chunk) {
            const auto& x = a.chunks[chunk];
            const auto& y = b.chunks[chunk];
            if (x.offset != y.offset || x.packed_size != y.packed_size || x.unpacked_size != y.unpacked_size ||
                x.mip_first != y.mip_first || x.mip_last != y.mip_last) {
                return false;
            }
        }
    }
    return true;
}

}

Result<std::set<std::string>> discoverPluginStringLanguages(
    std::string_view pluginFilename,
    std::span<const std::filesystem::path> stringsDirectories,
    const fo4::archive::AssetResolver* assets,
    std::span<const std::string_view> stagedLanguages) {
    try {
        std::string pluginStem(pluginFilename);
        const auto dot = pluginStem.rfind('.');
        if (dot != std::string::npos) pluginStem.resize(dot);
        if (pluginStem.empty()) return std::unexpected(unavailable("plugin filename is empty"));

        std::set<std::string> languages;
        for (const auto language : stagedLanguages) {
            if (!token(language)) return std::unexpected(unavailable("staged language token is invalid"));
            languages.insert(fold(language));
        }

        for (const auto& directory : stringsDirectories) {
            std::error_code ec;
            const auto status = std::filesystem::status(directory, ec);
            if (ec == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found) continue;
            if (ec) return std::unexpected(unavailable("could not inspect a strings directory"));
            if (!std::filesystem::is_directory(status)) return std::unexpected(unavailable("a strings source is not a directory"));
            std::filesystem::directory_iterator it(directory, ec), end;
            if (ec) return std::unexpected(unavailable("could not enumerate a strings directory"));
            std::set<std::string> tableNames;
            for (; it != end; it.increment(ec)) {
                if (ec) return std::unexpected(unavailable("could not enumerate a strings directory"));
                std::error_code entryError;
                if (!it->is_regular_file(entryError)) {
                    if (entryError) return std::unexpected(unavailable("could not inspect a strings entry"));
                    continue;
                }
                std::string language;
                if (matchingTableName(fo4::io::utf8(it->path().filename()), pluginStem, language)) {
                    if (!tableNames.insert(fold(fo4::io::utf8(it->path().filename()))).second) {
                        return std::unexpected(unavailable("a strings directory has case-folded duplicate tables"));
                    }
                    languages.insert(std::move(language));
                }
            }
            if (ec) return std::unexpected(unavailable("could not enumerate a strings directory"));
        }

        if (assets != nullptr) {
            for (const auto& archive : assets->archives()) {
                const auto reopened = fo4::archive::Ba2Reader::open(archive.path());
                if (!reopened.has_name_table()) return std::unexpected(unavailable("an archive has no entry names"));
                if (!sameArchiveIndex(archive, reopened)) {
                    return std::unexpected(unavailable("an archive changed after it was resolved"));
                }
                for (const auto& entry : archive.entries()) {
                    const auto normalized = fo4::archive::normalize_asset_path(entry.path);
                    const auto path = fold(normalized);
                    constexpr std::string_view prefix = "strings/";
                    if (!path.starts_with(prefix)) continue;
                    std::string language;
                    if (matchingTableName(std::string_view(path).substr(prefix.size()), pluginStem, language)) {
                        const auto extension = path.rfind('.');
                        const auto requested = fo4::archive::normalize_asset_path(
                            std::string(prefix) + pluginStem + "_" + language + path.substr(extension));
                        if (requested != normalized) return std::unexpected(unavailable("archive language path cannot be resolved consistently"));
                        languages.insert(std::move(language));
                    }
                }
            }
        }
        return languages;
    } catch (const std::exception&) {
        return std::unexpected(unavailable("could not enumerate localized sources"));
    }
}

}
