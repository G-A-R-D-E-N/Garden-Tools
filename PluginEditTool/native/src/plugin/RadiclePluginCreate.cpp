#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/io/path_text.hpp>
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

std::string backupTimestamp() {
    const auto now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local);
    return buffer;
}

}  // namespace

Result<void> RadiclePluginBackend::setTrustedNewPluginRoot(
    const std::filesystem::path& root) {
    OpLock lock(*this, OpLock::Mode::Lifecycle);
    if (root.empty()) {
        trustedNewPluginRoot_.clear();
        return {};
    }
    if (!root.is_absolute()) {
        return std::unexpected(Error{
            "NewPluginRootInvalid",
            "New plugin folder must be an absolute path: " + root.string()
        });
    }

    if (!dataDirectory_.empty()) {
        std::error_code dataEc;
        const auto data = std::filesystem::weakly_canonical(dataDirectory_, dataEc);
        std::error_code requestedEc;
        const auto requested = std::filesystem::weakly_canonical(root, requestedEc);
        if (dataEc || data.empty() || requestedEc || requested.empty()) {
            return std::unexpected(Error{
                "NewPluginRootInvalid",
                "Could not resolve the protected game Data directory"
            });
        }
        if (pathWithin(data, requested)) {
            return std::unexpected(Error{
                "NewPluginRootProtected",
                "Refusing to use the game Data directory as the new-plugin output root"
            });
        }
    }

    std::error_code createEc;
    std::filesystem::create_directories(root, createEc);
    if (createEc) {
        return std::unexpected(Error{
            "NewPluginRootInvalid",
            "Could not create new plugin folder: " + root.string()
        });
    }
    std::error_code rootEc;
    const auto canonical = std::filesystem::weakly_canonical(root, rootEc);
    if (rootEc || canonical.empty() || !std::filesystem::is_directory(canonical)) {
        return std::unexpected(Error{
            "NewPluginRootInvalid",
            "Could not resolve new plugin folder: " + root.string()
        });
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
            "No trusted new plugin folder is configured. Load an MO2 profile before creating a plugin."
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
                "New plugin target escaped the trusted new plugin folder"
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
            .source = "New plugins",
            .loadedTime = loadedTime,
            .trusted = true,
            .plugin = std::move(verified),
            .dirty = false,
            .stagedLocalization = std::nullopt,
        });
        dropBorrowedEnvironmentLocked();
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
    const auto& createdPath = (*created)->path;
    return nlohmann::json(
        "Created new plugin '" + (*created)->name + "' at " + fo4::io::utf8(createdPath) + ". "
        "Add records or overrides, then save_plugin to persist further changes. "
        "If this folder is a mod in MO2's mods folder, press F5 in MO2, enable the mod '" +
        fo4::io::utf8(createdPath.parent_path().filename()) +
        "' in the left pane and place the plugin in the right pane; a folder outside MO2's mods folder must be added to MO2 by hand.");
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
    if (it->source != "New plugins" || trustedNewPluginRoot_.empty()) {
        return std::unexpected(Error{
            "CreatedPluginRollbackRefused",
            "Refusing to discard a plugin that was not created in the trusted new plugin folder"
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
            "Auto-created plugin no longer resolves inside the trusted new plugin folder"
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
    dropBorrowedEnvironmentLocked();
    try {
        markGraphDirtyLocked();
    } catch (const std::exception& error) {
        plugins_.insert(plugins_.begin() + static_cast<std::ptrdiff_t>(index), std::move(removed));
        dropBorrowedEnvironmentLocked();
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
        dropBorrowedEnvironmentLocked();
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

Result<nlohmann::json> RadiclePluginBackend::backupPlugin(const nlohmann::json& arguments) {
    const auto name = createStringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {name});
    const auto* loaded = findPlugin(name);
    if (!loaded) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    if (protectedPluginName(loaded->name)) {
        return std::unexpected(Error{
            "PluginProtected",
            "Refusing to back up protected game plugin: " + loaded->name
        });
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to back up protected or game-Data plugin: " + loaded->name
        });
    }
    std::error_code statEc;
    if (!std::filesystem::is_regular_file(loaded->path, statEc)) {
        return std::unexpected(Error{
            "PluginFileMissing",
            "Plugin file is missing on disk: " + fo4::io::utf8(loaded->path)
        });
    }

    const auto source = loaded->path;
    const auto stamp = backupTimestamp();
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto suffix = attempt == 0 ? std::string{} : "_" + std::to_string(attempt);
        auto backup = source;
        backup += "." + stamp + suffix + ".bak";
        std::error_code existsEc;
        if (std::filesystem::exists(std::filesystem::symlink_status(backup, existsEc))) continue;
        std::error_code copyEc;
        std::filesystem::copy_file(source, backup, std::filesystem::copy_options::none, copyEc);
        if (!copyEc) {
            return nlohmann::json{{"ok", true}, {"plugin", loaded->name}, {"backup", fo4::io::utf8(backup)}}.dump();
        }
        if (copyEc == std::errc::file_exists) continue;
        // Constraint: a partial backup copy must be removed.
        std::error_code cleanupEc;
        std::filesystem::remove(backup, cleanupEc);
        return std::unexpected(Error{
            "BackupFailed",
            "Could not back up " + fo4::io::utf8(source) + ": " + copyEc.message()
        });
    }
    return std::unexpected(Error{"BackupFailed", "No free backup name next to " + fo4::io::utf8(source)});
}

}  // namespace fo4ide::plugin
