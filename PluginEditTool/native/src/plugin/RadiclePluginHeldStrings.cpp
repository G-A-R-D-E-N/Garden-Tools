#include "RadiclePluginHeldStrings.hpp"
#include "RadiclePluginBa2Source.hpp"
#include "RadiclePluginJournalManifest.hpp"

#include <fo4/io/path_text.hpp>
#include <fo4/text/encoding.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fo4ide::plugin::detail {
namespace {
using Name = std::tuple<std::string, std::string, fo4::env::StringSource>;

std::optional<std::vector<Name>> names(const Ba2ReadRoot& root, const std::filesystem::path& relative,
                                       std::string_view plugin) {
    auto entries = root.listOptional(relative, 4096);
    if (!entries) return std::nullopt;
    auto stem = fo4::text::fold(plugin);
    if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
    if (stem.empty()) throw std::runtime_error("plugin filename has no stem");
    const auto prefix = stem + '_';
    std::set<std::string> seen;
    std::vector<Name> result;
    for (const auto& filename : *entries) {
        if (!fo4::text::is_valid_utf8(filename)) throw std::runtime_error("invalid source filename");
        const auto folded = fo4::text::fold(filename);
        if (!folded.starts_with(prefix)) continue;
        const auto dot = folded.rfind('.');
        if (dot == std::string::npos || dot <= prefix.size()) continue;
        const auto extension = std::string_view(folded).substr(dot);
        fo4::env::StringSource source;
        if (extension == ".strings") source = fo4::env::StringSource::normal;
        else if (extension == ".dlstrings") source = fo4::env::StringSource::dl;
        else if (extension == ".ilstrings") source = fo4::env::StringSource::il;
        else continue;
        auto language = folded.substr(prefix.size(), dot - prefix.size());
        if (!std::all_of(language.begin(), language.end(), [](unsigned char ch) { return std::isalnum(ch) != 0; })) continue;
        if (!seen.insert(folded).second || result.size() == 128) throw std::runtime_error("ambiguous or oversized string inventory");
        result.emplace_back(filename, std::move(language), source);
    }
    return result;
}

fo4::localization::StringTableKind kind(fo4::env::StringSource source) {
    if (source == fo4::env::StringSource::dl) return fo4::localization::StringTableKind::dlstrings;
    if (source == fo4::env::StringSource::il) return fo4::localization::StringTableKind::ilstrings;
    return fo4::localization::StringTableKind::strings;
}
}

fo4::localization::LocalizedStringTable parseHeldStringTable(std::span<const std::byte> bytes,
    fo4::localization::StringTableKind tableKind, std::size_t& remaining, std::size_t& remainingRows) {
    if (bytes.size() < 8) throw std::runtime_error("invalid string table header");
    std::uint32_t rows = 0;
    for (std::size_t i = 0; i < 4; ++i) rows |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[i])) << (8 * i);
    if (rows > remainingRows) throw std::runtime_error("string row budget exceeded");
    const auto rowCost = static_cast<std::size_t>(rows) * (sizeof(fo4::localization::LocalizedStringEntry) + 72);
    if (rowCost > remaining) throw std::runtime_error("string working budget exceeded");
    remaining -= rowCost;
    const auto maxRecords = std::min<std::size_t>(1000000, bytes.size());
    const auto recordCost = maxRecords * 3 * sizeof(std::size_t);
    if (recordCost > remaining) throw std::runtime_error("string working budget exceeded");
    auto table = fo4::localization::read_string_table_bounded(bytes, tableKind, {remainingRows, maxRecords, remaining - recordCost});
    remainingRows -= table.entries.size();
    for (const auto& entry : table.entries) remaining -= entry.text.size();
    return table;
}

HeldStringDirectory HeldStringDirectory::capture(std::shared_ptr<const Ba2ReadRoot> root,
                                                  std::filesystem::path relative, std::string pluginFilename,
                                                  std::size_t maxWorkingBytes) {
    if (!root) throw std::runtime_error("string read root unavailable");
    HeldStringDirectory result;
    result.root_ = std::move(root); result.relative_ = std::move(relative); result.plugin_ = std::move(pluginFilename);
    const auto listed = names(*result.root_, result.relative_, result.plugin_);
    result.exists_ = listed.has_value();
    constexpr std::size_t overhead = 8 * 1024 * 1024;
    auto remaining = std::min<std::size_t>(maxWorkingBytes, kJournalMaxFileBytes);
    const auto budget = remaining;
    if (remaining < overhead) throw std::runtime_error("string working budget exceeded");
    remaining -= overhead;
    std::size_t remainingRows = 1000000;
    if (listed) for (const auto& [filename, language, source] : *listed) {
        auto lease = result.root_->open(result.relative_ / fo4::io::from_utf8(filename), remaining);
        std::vector<std::byte> bytes(static_cast<std::size_t>(lease->size()));
        lease->read(0, bytes);
        remaining -= bytes.size();
        auto table = parseHeldStringTable(bytes, kind(source), remaining, remainingRows);
        result.tables_.push_back({filename, language, source, std::move(bytes), std::move(table), std::move(lease)});
    }
    result.retainedBytes_ = budget - remaining;
    result.verify();
    return result;
}

void HeldStringDirectory::verify() const {
    const auto current = names(*root_, relative_, plugin_);
    if (current.has_value() != exists_ || (current && current->size() != tables_.size()))
        throw std::runtime_error("string inventory changed");
    if (!current) return;
    for (std::size_t i = 0; i < tables_.size(); ++i) {
        const auto& table = tables_[i];
        if (std::get<0>((*current)[i]) != table.filename || table.lease->size() != table.bytes.size())
            throw std::runtime_error("string inventory changed");
        std::vector<std::byte> buffer(std::min<std::size_t>(table.bytes.size(), 65536));
        for (std::size_t offset = 0; offset < table.bytes.size();) {
            const auto count = std::min(buffer.size(), table.bytes.size() - offset);
            table.lease->read(offset, std::span(buffer).first(count));
            if (!std::equal(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count), table.bytes.begin() + static_cast<std::ptrdiff_t>(offset)))
                throw std::runtime_error("string source changed");
            offset += count;
        }
    }
}

std::filesystem::path HeldStringDirectory::directory() const { return root_->path() / relative_; }
const std::filesystem::path& HeldStringDirectory::rootPath() const noexcept { return root_->path(); }
}
