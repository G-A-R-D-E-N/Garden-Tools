#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/io/atomic_write.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
namespace {

std::string seqStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::filesystem::path seqUtf8Path(std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char ch : value) utf8.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(utf8);
}

bool pathWithin(const std::filesystem::path& root,
                const std::filesystem::path& path) {
    const auto rootEnd = std::mismatch(root.begin(), root.end(), path.begin(), path.end()).first;
    return rootEnd == root.end();
}

Result<bool> questStartGameEnabled(const fo4::Record& record) {
    const auto sigDnam = fo4::Signature::from_string("DNAM");
    const fo4::Subrecord* data = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != sigDnam) continue;
        if (data != nullptr) {
            return std::unexpected(Error{
                "QuestDataMalformed",
                "QUST contains duplicate DNAM general-data subrecords: " +
                    record.form_key.to_string()
            });
        }
        data = &subrecord;
    }
    if (data == nullptr) return false;
    if (data->data.size() != 12) {
        return std::unexpected(Error{
            "QuestDataMalformed",
            "QUST DNAM must be exactly 12 bytes: " + record.form_key.to_string()
        });
    }
    std::uint16_t flags = 0;
    std::memcpy(&flags, data->data.data(), sizeof(flags));
    return (flags & 0x0001u) != 0u;
}

std::string seqEditorId(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid || subrecord.data.empty()) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::createSeqFile(
    const nlohmann::json& arguments) const {
    const auto pluginName = seqStringArgument(arguments, 0);
    const auto requestedOutput = seqStringArgument(arguments, 1);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "CreateSeqFile requires a plugin name"});
    }

    std::scoped_lock lock(mutex_);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
    if (loaded->plugin.header.masters.size() > 255) {
        return std::unexpected(Error{
            "TooManyMasters",
            "Cannot encode SEQ IDs for a plugin with more than 255 masters"
        });
    }

    const auto sigQuest = fo4::Signature::from_string("QUST");
    std::size_t currentIndex = plugins_.size();
    for (std::size_t index = 0; index < plugins_.size(); ++index) {
        if (&plugins_[index] == loaded) {
            currentIndex = index;
            break;
        }
    }
    if (currentIndex == plugins_.size()) {
        return std::unexpected(Error{"PluginStateInvalid", "Loaded plugin index could not be resolved"});
    }

    std::vector<std::uint32_t> ids;
    std::vector<std::string> included;
    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (record->header.signature != sigQuest) continue;
        auto enabled = questStartGameEnabled(*record);
        if (!enabled) return std::unexpected(enabled.error());
        if (!*enabled) continue;

        bool previouslyEnabled = false;
        for (std::size_t index = currentIndex; index-- > 0;) {
            const auto* previous = fo4::find_record(plugins_[index].plugin, record->form_key);
            if (previous == nullptr) continue;
            if (previous->header.signature == sigQuest) {
                auto previousEnabled = questStartGameEnabled(*previous);
                if (!previousEnabled) return std::unexpected(previousEnabled.error());
                previouslyEnabled = *previousEnabled;
            }
            break;
        }
        if (previouslyEnabled) continue;

        const auto raw =
            (static_cast<std::uint32_t>(loaded->plugin.header.masters.size()) << 24u) |
            (record->form_key.local().value() & 0x00FFFFFFu);
        ids.push_back(raw);
        const auto editor = seqEditorId(*record);
        included.push_back(
            (editor.empty() ? std::string("(no EditorID)") : editor) +
            " [" + record->form_key.to_string() + "]");
    }

    if (ids.empty()) {
        return nlohmann::json(
            "'" + pluginName +
            "' has no newly start-game-enabled quests, so it does not need a SEQ file.");
    }

    try {
        std::error_code rootEc;
        const auto pluginRoot = std::filesystem::weakly_canonical(
            loaded->path.parent_path(), rootEc);
        if (rootEc || pluginRoot.empty()) {
            return std::unexpected(Error{
                "SeqOutputPathInvalid",
                "Could not resolve the loaded plugin directory"
            });
        }

        std::filesystem::path outputDir;
        if (requestedOutput.empty()) {
            outputDir = pluginRoot / "Seq";
        } else {
            auto requested = seqUtf8Path(requestedOutput);
            if (requested.is_relative()) requested = pluginRoot / requested;
            std::error_code outputEc;
            outputDir = std::filesystem::weakly_canonical(requested, outputEc);
            if (outputEc || outputDir.empty() || !pathWithin(pluginRoot, outputDir)) {
                return std::unexpected(Error{
                    "SeqOutputPathNotAllowed",
                    "SEQ output must stay inside the loaded plugin's directory"
                });
            }
        }
        std::filesystem::create_directories(outputDir);
        std::error_code canonicalEc;
        outputDir = std::filesystem::weakly_canonical(outputDir, canonicalEc);
        if (canonicalEc || !pathWithin(pluginRoot, outputDir)) {
            return std::unexpected(Error{
                "SeqOutputPathNotAllowed",
                "SEQ output directory escaped the loaded plugin's directory"
            });
        }

        auto fileName = loaded->path.filename();
        fileName.replace_extension(".seq");
        const auto outputPath = outputDir / fileName;

        std::vector<std::byte> bytes;
        bytes.resize(ids.size() * sizeof(std::uint32_t));
        for (std::size_t index = 0; index < ids.size(); ++index) {
            std::memcpy(bytes.data() + index * sizeof(std::uint32_t),
                        &ids[index], sizeof(std::uint32_t));
        }
        fo4::io::write_file_atomic(outputPath, bytes);

        std::string report =
            "Wrote " + outputPath.string() + " with " +
            std::to_string(ids.size()) + " start-game-enabled quest(s):";
        const auto shown = std::min<std::size_t>(included.size(), 50);
        for (std::size_t index = 0; index < shown; ++index) {
            report += "\n  " + included[index];
        }
        if (included.size() > shown) {
            report += "\n  ... and " + std::to_string(included.size() - shown) + " more";
        }
        report += "\nDeploy it as Data/Seq/" + fileName.string() + " alongside the plugin.";
        return nlohmann::json(std::move(report));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SeqWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
