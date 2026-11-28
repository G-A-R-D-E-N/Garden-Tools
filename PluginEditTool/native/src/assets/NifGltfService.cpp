#include <fo4ide/assets/NifGltfService.hpp>

#include <fo4/error.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/nif/gltf_export.hpp>
#include <fo4/nif/nif.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fo4ide::assets {
namespace {

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::filesystem::path fromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

bool isLink(const std::filesystem::path& path) {
#ifdef _WIN32
    WIN32_FIND_DATAW data{};
    const auto find = FindFirstFileW(path.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;
    FindClose(find);
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
           (data.dwReserved0 == IO_REPARSE_TAG_SYMLINK || data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
#else
    std::error_code ec;
    return std::filesystem::is_symlink(path, ec);
#endif
}

std::optional<std::filesystem::path> linkedComponent(const std::filesystem::path& directory,
                                                     const std::filesystem::path& relative) {
    auto current = directory;
    for (const auto& part : relative) {
        current /= part;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(current, ec);
        if (status.type() == std::filesystem::file_type::not_found) return std::nullopt;
        if (ec || isLink(current)) return current;
    }
    return std::nullopt;
}

struct PublishItem {
    std::filesystem::path target;
    const std::vector<std::byte>* bytes;
};

void writeIfAbsent(const std::filesystem::path& target, std::span<const std::byte> bytes) {
    auto staged = fo4::io::StagedFile::create(target);
    staged.write(bytes);
    staged.finish();
    staged.verify(bytes.size());
    if (!std::move(staged).commit_if_absent()) {
        throw std::runtime_error("Refusing to replace existing path: " + target.string());
    }
}

// Invariant: items are published in order, so textures must come before the .glb that references them.
void publishAll(const std::vector<PublishItem>& items, bool overwrite) {
    struct Done {
        std::filesystem::path target;
        std::filesystem::path backup;
    };
    std::vector<Done> done;
    std::vector<std::filesystem::path> madeDirectories;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    try {
        for (const auto& item : items) {
            std::error_code ec;
            const auto parent = item.target.parent_path();
            for (auto directory = parent; !directory.empty() && !std::filesystem::exists(directory, ec);
                 directory = directory.parent_path()) {
                madeDirectories.push_back(directory);
            }
            if (!parent.empty()) std::filesystem::create_directories(parent);
            std::filesystem::path backup;
            if (std::filesystem::exists(item.target, ec)) {
                if (!overwrite || !std::filesystem::is_regular_file(item.target, ec)) {
                    throw std::runtime_error("Refusing to replace existing path: " + item.target.string());
                }
                backup = item.target;
                backup += ".fo4ide-prev-" + std::to_string(stamp) + "-" + std::to_string(done.size());
                std::filesystem::rename(item.target, backup, ec);
                if (ec) throw std::runtime_error("Could not move " + item.target.string() + " aside: " + ec.message());
            }
            if (overwrite) {
                done.push_back({item.target, backup});
                fo4::io::write_file_atomic(item.target, std::as_bytes(std::span(*item.bytes)));
            } else {
                writeIfAbsent(item.target, std::as_bytes(std::span(*item.bytes)));
                done.push_back({item.target, backup});
            }
        }
    } catch (...) {
        std::error_code ec;
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            std::filesystem::remove(it->target, ec);
            if (!it->backup.empty()) std::filesystem::rename(it->backup, it->target, ec);
        }
        const auto depth = [](const std::filesystem::path& directory) {
            return std::distance(directory.begin(), directory.end());
        };
        std::ranges::sort(madeDirectories, std::greater{}, depth);
        for (const auto& directory : madeDirectories) std::filesystem::remove(directory, ec);
        throw;
    }
    std::error_code ec;
    for (const auto& entry : done) {
        if (!entry.backup.empty()) std::filesystem::remove(entry.backup, ec);
    }
}

}

std::optional<fo4::nif::GltfTextureMode> parseGltfTextureMode(std::string_view name) {
    if (name.empty() || name == "png") return fo4::nif::GltfTextureMode::png;
    if (name == "copy") return fo4::nif::GltfTextureMode::copy;
    if (name == "uri") return fo4::nif::GltfTextureMode::uri;
    if (name == "png") return fo4::nif::GltfTextureMode::png;
    if (name == "embed") return fo4::nif::GltfTextureMode::embed;
    return std::nullopt;
}

Result<std::string> exportNifGltf(const std::filesystem::path& nifPath,
                                  const std::filesystem::path& outputPath,
                                  bool overwrite,
                                  const NifGltfOptions& options) {
    if (nifPath.empty()) return std::unexpected(Error{"InvalidPath", "NIF path is empty"});
    if (!std::filesystem::is_regular_file(nifPath)) {
        return std::unexpected(Error{"FileNotFound", "NIF not found: " + nifPath.string()});
    }
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output glTF path is empty"});
    std::error_code same;
    if (std::filesystem::equivalent(nifPath, outputPath, same)) {
        return std::unexpected(Error{"InvalidPath", "Output glTF path is the NIF itself; choose another file"});
    }
    const auto existsPath = [](const std::filesystem::path& path) {
        std::error_code taken;
        return std::filesystem::exists(path, taken);
    };

    fo4::nif::GltfExport exported;
    try {
        const auto raw = readFile(nifPath);
        fo4::nif::GltfOptions gltf;
        gltf.materials = options.materials;
        gltf.textures = options.textures;
        gltf.tangents = options.tangents;
        gltf.vertex_colours = options.vertexColours;
        gltf.assets = options.assets;
        exported = fo4::nif::export_glb(fo4::nif::read_nif(std::as_bytes(std::span(raw))), gltf);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifExportFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifExportFailed", error.what()});
    }

    const auto directory = outputPath.has_parent_path() ? outputPath.parent_path() : std::filesystem::path(".");
    std::vector<std::filesystem::path> targets;
    for (const auto& file : exported.files) targets.push_back(directory / fromUtf8(file.path));
    if (!overwrite) {
        std::vector<std::filesystem::path> conflicts;
        if (existsPath(outputPath)) conflicts.push_back(outputPath);
        for (const auto& target : targets) {
            if (existsPath(target)) conflicts.push_back(target);
        }
        if (!conflicts.empty()) {
            std::string message = "Refusing to replace existing files without overwrite:";
            for (const auto& path : conflicts) message += "\n" + path.string();
            return std::unexpected(Error{"OutputExists", message});
        }
    }
    std::vector<std::filesystem::path> relatives{outputPath.filename()};
    for (const auto& file : exported.files) relatives.push_back(fromUtf8(file.path));
    for (const auto& relative : relatives) {
        if (const auto linked = linkedComponent(directory, relative)) {
            return std::unexpected(Error{"InvalidPath", "Refusing to write through a linked path: " + linked->string()});
        }
    }
    std::vector<PublishItem> items;
    items.reserve(targets.size() + 1);
    for (std::size_t i = 0; i < targets.size(); ++i) items.push_back({targets[i], &exported.files[i].bytes});
    items.push_back({outputPath, &exported.glb});
    try {
        publishAll(items, overwrite);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifExportFailed", error.what()});
    }

    std::string text = "RESULT: gltf export OK\nSaved: " + outputPath.string() +
                       "\nBytes: " + std::to_string(exported.glb.size());
    if (!targets.empty()) text += "\nTextures: " + std::to_string(targets.size());
    for (const auto& warning : exported.warnings) text += "\nWarning: " + warning;
    return text;
}

}
