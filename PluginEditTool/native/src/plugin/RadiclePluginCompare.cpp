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

nlohmann::json subrecordRows(const fo4::Record* record) {
    if (record == nullptr) return nullptr;
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& sub : record->subrecords) {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string hex;
        const auto shown = std::min<std::size_t>(sub.data.size(), 64);
        for (std::size_t i = 0; i < shown; ++i) {
            const auto byte = std::to_integer<unsigned>(sub.data[i]);
            if (i != 0) hex += ' ';
            hex += digits[byte >> 4];
            hex += digits[byte & 0xF];
        }
        if (sub.data.size() > shown) hex += " ...";
        std::string text;
        const bool terminated = !sub.data.empty() && sub.data.back() == std::byte{0};
        const bool printable = terminated && std::all_of(sub.data.begin(), sub.data.end() - 1, [](std::byte b) {
            const auto c = std::to_integer<unsigned char>(b);
            return c >= 0x20 && c < 0x7F;
        });
        if (printable) text.assign(reinterpret_cast<const char*>(sub.data.data()), sub.data.size() - 1);
        rows.push_back({{"Signature", sub.signature.to_string()}, {"Size", sub.data.size()},
                        {"Hex", std::move(hex)}, {"Text", std::move(text)}});
    }
    return rows;
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

    OpLock lock(*this, OpLock::Mode::Read);
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
        const auto formKeyText = compareStringArgument(arguments, 2);
        if (!formKeyText.empty()) {
            const auto key = fo4::FormKey::parse(formKeyText);
            const auto* before = fo4::find_record(other, key);
            const auto* after = fo4::find_record(loaded->plugin, key);
            const auto at = [](const fo4::Record* record, std::size_t i) {
                return record != nullptr && i < record->subrecords.size() ? &record->subrecords[i] : nullptr;
            };
            const auto count = std::max(before ? before->subrecords.size() : 0, after ? after->subrecords.size() : 0);
            nlohmann::json differs = nlohmann::json::array();
            for (std::size_t i = 0; i < count; ++i) {
                const auto* a = at(before, i);
                const auto* b = at(after, i);
                differs.push_back(a == nullptr || b == nullptr || a->signature != b->signature || a->data != b->data);
            }
            return nlohmann::json(nlohmann::json{
                {"FormKey", key.to_string()},
                {"Before", subrecordRows(before)},
                {"After", subrecordRows(after)},
                {"Differs", std::move(differs)}
            }.dump());
        }
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& diff : fo4::diff_plugins(other, loaded->plugin)) {
            nlohmann::json subrecords = nlohmann::json::array();
            for (const auto signature : diff.changed_subrecords) subrecords.push_back(signature.to_string());
            rows.push_back({
                {"FormKey", diff.key.to_string()},
                {"Signature", diff.signature.to_string()},
                {"Change", changeName(diff.change)},
                {"HeaderChanged", diff.header_changed},
                {"PlacementChanged", diff.placement_changed},
                {"Subrecords", std::move(subrecords)}
            });
        }
        return nlohmann::json(rows.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginReadFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
