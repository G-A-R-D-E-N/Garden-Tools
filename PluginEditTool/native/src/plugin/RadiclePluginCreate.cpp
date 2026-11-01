#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {
namespace {

std::string createStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::string createLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string normalizedPluginFilename(std::string fileName) {
    const auto lower = createLower(fileName);
    if (!lower.ends_with(".esp") && !lower.ends_with(".esm") &&
        !lower.ends_with(".esl")) {
        fileName += ".esp";
    }
    return fileName;
}

bool pathWithin(const std::filesystem::path& root,
                const std::filesystem::path& path) {
    auto rootIt = root.begin();
    auto pathIt = path.begin();
    for (; rootIt != root.end(); ++rootIt, ++pathIt) {
        if (pathIt == path.end() || *rootIt != *pathIt) return false;
    }
    return true;
}

bool protectedPluginName(std::string_view name) {
    const auto lower = createLower(name);
    if (lower == "fallout4.esm" ||
        lower == "dlcrobot.esm" ||
        lower == "dlccoast.esm" ||
        lower == "dlcworkshop01.esm" ||
        lower == "dlcworkshop02.esm" ||
        lower == "dlcworkshop03.esm" ||
        lower == "dlcnukaworld.esm" ||
        lower == "dlcultrahighresolution.esm") {
        return true;
    }
    return lower.starts_with("cc") &&
           (lower.ends_with(".esm") || lower.ends_with(".esp") ||
            lower.ends_with(".esl"));
}

bool reservedWindowsStem(std::string_view stem) {
    const auto lower = createLower(stem);
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul") {
        return true;
    }
    if (lower.size() == 4 &&
        (lower.starts_with("com") || lower.starts_with("lpt")) &&
        lower[3] >= '1' && lower[3] <= '9') {
        return true;
    }
    return false;
}

std::optional<Error> validatePluginFilename(std::string_view name) {
    if (name.empty()) return Error{"PluginNameMissing", "Plugin name is empty"};
    if (name == "." || name == "..") {
        return Error{"InvalidPluginName", "Plugin name cannot be '.' or '..'"};
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return Error{"InvalidPluginName", "Plugin filename cannot start/end with a space or end with a dot"};
    }
    for (const unsigned char ch : name) {
        if (ch < 0x20 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
            ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
            return Error{"InvalidPluginName", "Plugin filename contains a Windows-invalid character"};
        }
    }
    const auto dot = name.find('.');
    const auto stem = dot == std::string_view::npos ? name : name.substr(0, dot);
    if (reservedWindowsStem(stem)) {
        return Error{"InvalidPluginName", "Plugin filename uses a reserved Windows device name"};
    }
    return std::nullopt;
}

fo4::Plugin makeEmptyPlugin(std::string_view fileName,
                            const std::filesystem::path& path) {
    fo4::Plugin plugin;
    plugin.path = path;
    plugin.key = fo4::PluginKey::from_filename(fileName);

    const auto lower = createLower(fileName);
    std::uint32_t flagBits = 0;
    if (lower.ends_with(".esm")) flagBits |= fo4::RecordFlags::master;
    if (lower.ends_with(".esl")) {
        flagBits |= fo4::RecordFlags::master;
        flagBits |= fo4::RecordFlags::small_file;
    }
    const fo4::RecordFlags flags{flagBits};

    plugin.header.version = 1.0f;
    plugin.header.record_count = 0;
    plugin.header.next_form_id = 0x800u;
    plugin.header.tes4_flags = flags;
    plugin.header.form_version = 131;

    plugin.tes4.header.signature = fo4::sig_tes4;
    plugin.tes4.header.flags = flags;
    plugin.tes4.header.form_id = fo4::RawFormId{0};
    plugin.tes4.header.form_version = 131;

    fo4::Subrecord hedr;
    hedr.signature = fo4::sig_hedr;
    hedr.data.resize(12);
    const float version = 1.0f;
    const std::uint32_t recordCount = 0;
    const std::uint32_t nextFormId = 0x800u;
    std::memcpy(hedr.data.data(), &version, sizeof(version));
    std::memcpy(hedr.data.data() + 4, &recordCount, sizeof(recordCount));
    std::memcpy(hedr.data.data() + 8, &nextFormId, sizeof(nextFormId));
    plugin.tes4.subrecords.push_back(std::move(hedr));
    return plugin;
}

bool emptyCreatedPlugin(const fo4::Plugin& plugin) {
    return plugin.groups.empty() && plugin.header.masters.empty() &&
           plugin.header.next_form_id == 0x800u;
}

}  // namespace

Result<void> RadiclePluginBackend::setTrustedNewPluginRoot(
    const std::filesystem::path& root) {
    OpLock lock(*this, OpLock::Mode::Lifecycle);
    if (root.empty()) {
        trustedNewPluginRoot_.clear();
        return {};
    }

    std::error_code createEc;
    std::filesystem::create_directories(root, createEc);
    if (createEc) {
        return std::unexpected(Error{
            "NewPluginRootInvalid",
            "Could not create MO2 overwrite directory: " + root.string()
        });
    }
    std::error_code rootEc;
    const auto canonical = std::filesystem::weakly_canonical(root, rootEc);
    if (rootEc || canonical.empty() || !std::filesystem::is_directory(canonical)) {
        return std::unexpected(Error{
            "NewPluginRootInvalid",
            "Could not resolve MO2 overwrite directory: " + root.string()
        });
    }

    if (!dataDirectory_.empty()) {
        std::error_code dataEc;
        const auto data = std::filesystem::weakly_canonical(dataDirectory_, dataEc);
        if (dataEc || data.empty()) {
            return std::unexpected(Error{
                "NewPluginRootInvalid",
                "Could not resolve the protected game Data directory"
            });
        }
        if (pathWithin(data, canonical)) {
            return std::unexpected(Error{
                "NewPluginRootProtected",
                "Refusing to use the game Data directory as the new-plugin output root"
            });
        }
    }

    trustedNewPluginRoot_ = canonical;
    return {};
}

Result<RadiclePluginBackend::LoadedPlugin*> RadiclePluginBackend::createPluginLocked(
    std::string fileName) {
    fileName = normalizedPluginFilename(std::move(fileName));
    if (const auto error = validatePluginFilename(fileName)) return std::unexpected(*error);
    if (protectedPluginName(fileName)) {
        return std::unexpected(Error{
            "PluginProtected",
            "Refusing to create or replace protected game plugin: " + fileName
        });
    }

    for (auto& loaded : plugins_) {
        if (createLower(loaded.name) == createLower(fileName)) {
            return std::unexpected(Error{
                "PluginAlreadyLoaded",
                "'" + fileName + "' is already loaded; use that plugin instead"
            });
        }
    }
    if (trustedNewPluginRoot_.empty()) {
        return std::unexpected(Error{
            "NewPluginRootUnavailable",
            "No trusted MO2 overwrite directory is configured. Load an MO2 profile before creating a plugin."
        });
    }

    std::filesystem::path target;
    bool createdOnDisk = false;
    try {
        const auto requested = trustedNewPluginRoot_ / std::filesystem::path(fileName);
        std::error_code targetEc;
        target = std::filesystem::weakly_canonical(requested, targetEc);
        if (targetEc || target.empty() || !pathWithin(trustedNewPluginRoot_, target) ||
            target.parent_path() != trustedNewPluginRoot_) {
            return std::unexpected(Error{
                "NewPluginPathNotAllowed",
                "New plugin target escaped the trusted MO2 overwrite directory"
            });
        }
        if (std::filesystem::exists(target)) {
            return std::unexpected(Error{
                "PluginAlreadyExists",
                "A file already exists at the new plugin target: " + target.string()
            });
        }

        auto fresh = makeEmptyPlugin(fileName, target);
        fo4::write_plugin_file(fresh, target);
        createdOnDisk = true;

        auto verified = fo4::read_plugin_file(target);
        if (createLower(verified.key.filename()) != createLower(fileName)) {
            std::error_code cleanupEc;
            std::filesystem::remove(target, cleanupEc);
            createdOnDisk = false;
            return std::unexpected(Error{
                "CreatedPluginIdentityMismatch",
                "New plugin read-back identity did not match the requested filename"
            });
        }
        std::error_code timeEc;
        const auto loadedTime = std::filesystem::last_write_time(target, timeEc);
        if (timeEc) {
            std::error_code cleanupEc;
            std::filesystem::remove(target, cleanupEc);
            createdOnDisk = false;
            return std::unexpected(Error{
                "CreatedPluginTimestampFailed",
                "Could not read the new plugin timestamp after creation"
            });
        }

        plugins_.push_back({
            .name = fileName,
            .path = target,
            .source = "MO2 overwrite",
            .loadedTime = loadedTime,
            .trusted = true,
            .plugin = std::move(verified),
            .dirty = false,
            .stagedLocalization = std::nullopt,
        });
        try {
            markGraphDirtyLocked();
        } catch (...) {
            plugins_.pop_back();
            throw;
        }
        return &plugins_.back();
    } catch (const std::exception& error) {
        if (createdOnDisk && !target.empty()) {
            std::error_code cleanupEc;
            std::filesystem::remove(target, cleanupEc);
        }
        return std::unexpected(Error{"CreatePluginFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::createPlugin(
    const nlohmann::json& arguments) {
    auto fileName = createStringArgument(arguments, 0);
    if (fileName.empty()) {
        return std::unexpected(Error{
            "PluginNameMissing",
            "Provide a plugin name, for example 'ConflictPatch.esp'"
        });
    }

    OpLock lock(*this, OpLock::Mode::Lifecycle);
    auto created = createPluginLocked(std::move(fileName));
    if (!created) return std::unexpected(created.error());
    return nlohmann::json(
        "Created new plugin '" + (*created)->name + "' in MO2 overwrite. "
        "Add records or overrides, then save_plugin to persist further changes.");
}

Result<void> RadiclePluginBackend::discardCreatedPlugin(std::string_view name) {
    OpLock lock(*this, OpLock::Mode::Lifecycle);
    const auto normalized = normalizedPluginFilename(std::string(name));
    auto it = std::find_if(plugins_.begin(), plugins_.end(), [&](const LoadedPlugin& loaded) {
        return createLower(loaded.name) == createLower(normalized);
    });
    if (it == plugins_.end()) {
        return std::unexpected(Error{
            "CreatedPluginNotLoaded",
            "Auto-created plugin is no longer loaded: " + normalized
        });
    }
    if (it->source != "MO2 overwrite" || trustedNewPluginRoot_.empty()) {
        return std::unexpected(Error{
            "CreatedPluginRollbackRefused",
            "Refusing to discard a plugin that was not created in the trusted MO2 overwrite root"
        });
    }
    if (!fileUnchangedSinceLoad(*it)) {
        return std::unexpected(Error{
            "CreatedPluginChangedOnDisk",
            "Refusing to discard an auto-created plugin that changed on disk: " + it->path.string()
        });
    }
    if (!emptyCreatedPlugin(it->plugin)) {
        return std::unexpected(Error{
            "CreatedPluginNotEmpty",
            "Refusing to discard an auto-created plugin after it gained records or masters"
        });
    }

    std::error_code pathEc;
    const auto canonicalPath = std::filesystem::weakly_canonical(it->path, pathEc);
    if (pathEc || canonicalPath.empty() ||
        canonicalPath.parent_path() != trustedNewPluginRoot_ ||
        !pathWithin(trustedNewPluginRoot_, canonicalPath)) {
        return std::unexpected(Error{
            "CreatedPluginRollbackPathInvalid",
            "Auto-created plugin no longer resolves inside the trusted MO2 overwrite root"
        });
    }

    try {
        const auto disk = fo4::read_plugin_file(canonicalPath);
        if (!emptyCreatedPlugin(disk)) {
            return std::unexpected(Error{
                "CreatedPluginNotEmpty",
                "Refusing to discard an auto-created plugin whose on-disk contents are no longer empty"
            });
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{
            "CreatedPluginRollbackReadFailed",
            "Could not verify auto-created plugin before rollback: " + std::string(error.what())
        });
    }

    const auto index = static_cast<std::size_t>(std::distance(plugins_.begin(), it));
    auto removed = std::move(*it);
    plugins_.erase(plugins_.begin() + static_cast<std::ptrdiff_t>(index));
    try {
        markGraphDirtyLocked();
    } catch (const std::exception& error) {
        plugins_.insert(plugins_.begin() + static_cast<std::ptrdiff_t>(index), std::move(removed));
        try {
            markGraphDirtyLocked();
        } catch (...) {
        }
        return std::unexpected(Error{"CreatedPluginRollbackGraphFailed", error.what()});
    }

    std::error_code removeEc;
    const bool removedFile = std::filesystem::remove(canonicalPath, removeEc);
    if (removeEc || !removedFile) {
        plugins_.insert(plugins_.begin() + static_cast<std::ptrdiff_t>(index), std::move(removed));
        try {
            markGraphDirtyLocked();
        } catch (...) {
        }
        return std::unexpected(Error{
            "CreatedPluginRollbackDeleteFailed",
            "Could not delete empty auto-created plugin: " + canonicalPath.string()
        });
    }
    return {};
}

}  // namespace fo4ide::plugin
