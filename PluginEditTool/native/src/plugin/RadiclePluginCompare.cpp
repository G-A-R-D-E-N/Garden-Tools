#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/plugin_diff.hpp>
#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin {
namespace {

std::string compareStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::filesystem::path compareUtf8Path(std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char ch : value) utf8.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(utf8);
}

bool isPluginExtension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext == ".esp" || ext == ".esm" || ext == ".esl";
}

std::string_view changeName(fo4::RecordChange change) {
    switch (change) {
        case fo4::RecordChange::added: return "added";
        case fo4::RecordChange::removed: return "removed";
        case fo4::RecordChange::changed: return "changed";
    }
    return "changed";
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::comparePluginTo(const nlohmann::json& arguments) const {
    const auto name = compareStringArgument(arguments, 0);
    const auto otherText = compareStringArgument(arguments, 1);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (otherText.empty()) return std::unexpected(Error{"PluginPathMissing", "Comparison file path is empty"});
    const auto otherPath = compareUtf8Path(otherText);
    if (!isPluginExtension(otherPath)) {
        return std::unexpected(Error{"InvalidPluginExtension", "Comparison file must be .esp, .esm or .esl"});
    }

    std::scoped_lock lock(mutex_);
    const auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }

    try {
        std::ifstream in(otherPath, std::ios::binary);
        if (!in) return std::unexpected(Error{"PluginReadFailed", "Cannot open comparison file: " + otherText});
        std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const auto other = fo4::read_plugin(std::as_bytes(std::span(raw)),
                                            std::string(loaded->plugin.key.filename()));
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& diff : fo4::diff_plugins(other, loaded->plugin)) {
            nlohmann::json subrecords = nlohmann::json::array();
            for (const auto signature : diff.changed_subrecords) subrecords.push_back(signature.to_string());
            rows.push_back({
                {"FormKey", diff.key.to_string()},
                {"Signature", diff.signature.to_string()},
                {"Change", changeName(diff.change)},
                {"HeaderChanged", diff.header_changed},
                {"Subrecords", std::move(subrecords)}
            });
        }
        return nlohmann::json(rows.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginReadFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
