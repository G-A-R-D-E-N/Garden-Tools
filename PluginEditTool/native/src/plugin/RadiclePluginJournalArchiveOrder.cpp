#include "RadiclePluginJournalSession.hpp"
#include "RadiclePluginBa2Source.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin::detail {
namespace {
using Named = std::pair<std::string, std::string>;

std::vector<Named> archiveNames(const Ba2ReadRoot& root) {
    std::vector<Named> names;
    for (auto& name : root.list({}, 4096)) {
        auto folded = fo4::text::fold(name);
        if (folded.ends_with(".ba2")) names.emplace_back(std::move(folded), std::move(name));
    }
    std::sort(names.begin(), names.end());
    for (std::size_t i = 1; i < names.size(); ++i)
        if (names[i - 1].first == names[i].first) throw std::runtime_error("ambiguous archive filenames");
    return names;
}

std::vector<JournalArchiveReference> select(const Ba2ReadRoot& root, const std::vector<Named>& names,
                                            const std::vector<std::string>& plugins) {
    std::vector<JournalArchiveReference> result;
    std::set<std::string> added;
    for (const auto& plugin : plugins) {
        auto stem = fo4::text::fold(plugin);
        if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
        for (int pass = 0; pass != 2; ++pass) {
            for (const auto& [folded, original] : names) {
                const auto base = std::string_view(folded).substr(0, folded.size() - 4);
                const auto separator = base.rfind(" - ");
                const bool match = pass == 0 ? folded == stem + ".ba2" :
                    separator != std::string_view::npos && separator + 3 < base.size() && base.substr(0, separator) == stem;
                if (match && added.insert(folded).second) result.push_back({root.path(), {}, original});
            }
        }
    }
    return result;
}
}

std::vector<JournalArchiveReference> journalArchiveOrder(
    const std::shared_ptr<const Ba2ReadRoot>& dataRoot, const std::shared_ptr<const Ba2ReadRoot>& modRoot,
    const std::vector<std::string>& plugins, std::string_view ownPlugin) {
    if (!modRoot) throw std::runtime_error("missing mod archive read root");
    auto data = dataRoot ? select(*dataRoot, archiveNames(*dataRoot), plugins) : std::vector<JournalArchiveReference>{};
    if (dataRoot && dataRoot->sameRoot(*modRoot)) {
        if (data.size() > kJournalMaxArchiveProviders) throw std::runtime_error("too many archive providers");
        return data;
    }
    auto mod = select(*modRoot, archiveNames(*modRoot), {std::string(ownPlugin)});
    std::erase_if(data, [&](const auto& item) {
        return std::any_of(mod.begin(), mod.end(), [&](const auto& other) {
            return fo4::text::equals_folded(item.filename, other.filename);
        });
    });
    data.insert(data.end(), mod.begin(), mod.end());
    if (data.size() > kJournalMaxArchiveProviders) throw std::runtime_error("too many archive providers");
    return data;
}
}
