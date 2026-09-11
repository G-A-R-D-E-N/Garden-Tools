#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <fstream>
#include <stdexcept>
#include <utility>

#include <fo4/io/atomic_write.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {

namespace {

std::vector<std::byte> readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("failed to read " + path.string());
    const auto size = stream.tellg();
    if (size < 0) throw std::runtime_error("failed to size " + path.string());
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream) throw std::runtime_error("failed to read " + path.string());
    }
    return bytes;
}

}

void RadiclePluginBackend::stagePluginMutationLocked(LoadedPlugin& loaded,
                                                     fo4::Plugin mutated) {
    if (!loaded.dirty) loaded.originalPlugin = loaded.plugin;
    loaded.plugin = std::move(mutated);
    loaded.dirty = true;
    ++mutationEpoch_;
    rebuildGraphLocked();
}

void RadiclePluginBackend::stageLocalizationMutationLocked(
    LoadedPlugin& loaded, StagedLocalization staged) {
    if (!loaded.dirty) loaded.originalPlugin = loaded.plugin;
    loaded.stagedLocalization = std::move(staged);
    loaded.dirty = true;
    ++mutationEpoch_;
}

Result<void> RadiclePluginBackend::persistPluginLocked(
    LoadedPlugin& loaded, const std::filesystem::path& output) {
    try {
        if (loaded.stagedLocalization.has_value()) {
            if (output != loaded.path) {
                return std::unexpected(Error{
                    "SavePathNotAllowed",
                    "Refusing Save As while localization sidecars are pending; "
                    "save in place or discard first: " + loaded.name
                });
            }
            const auto& staged = *loaded.stagedLocalization;
            if (readFileBytes(staged.stringsPath) != staged.originalStringsBytes ||
                readFileBytes(staged.dlstringsPath) != staged.originalDlstringsBytes ||
                readFileBytes(staged.ilstringsPath) != staged.originalIlstringsBytes) {
                return std::unexpected(Error{
                    "LocalizedSidecarChangedOnDisk",
                    "localized sibling table changed after it was staged; "
                    "discard or reload before saving"
                });
            }
            const auto pluginBytes = fo4::write_plugin(loaded.plugin);
            fo4::io::write_file_pair_dependency_first(
                staged.dlstringsPath,
                staged.dlstringsBytes,
                output,
                pluginBytes);
        } else {
            fo4::write_plugin_file(loaded.plugin, output);
        }
        loaded.path = output;
        std::error_code ec;
        loaded.loadedTime = std::filesystem::last_write_time(output, ec);
        loaded.originalPlugin.reset();
        loaded.stagedLocalization.reset();
        loaded.dirty = false;
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginWriteFailed", error.what()});
    }
}

}
