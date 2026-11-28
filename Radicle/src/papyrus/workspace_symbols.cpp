#include <fo4/papyrus/analysis_service.hpp>

#include <fo4/papyrus/symbols.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

namespace {

struct Candidate {
    int rank{0};
    Symbol symbol;
};

std::string lowerText(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool isWordStart(std::string_view name, std::size_t i) {
    if (i == 0) return true;
    const auto previous = name[i - 1];
    if (previous == '_' || previous == ':') return true;
    return std::isupper(static_cast<unsigned char>(name[i])) != 0 &&
           std::isupper(static_cast<unsigned char>(previous)) == 0;
}

int matchRank(std::string_view name, std::string_view query) {
    const auto lowered = lowerText(name);
    const auto wanted = lowerText(query);
    if (lowered == wanted) return 0;
    if (lowered.compare(0, wanted.size(), wanted) == 0) return 1;

    std::size_t next = 0;
    for (std::size_t i = 0; i < lowered.size() && next < wanted.size(); ++i) {
        if (isWordStart(name, i) && lowered[i] == wanted[next]) ++next;
    }
    if (next == wanted.size()) return 2;

    std::size_t matched = 0;
    for (const char ch : lowered) {
        if (matched < wanted.size() && ch == wanted[matched]) ++matched;
    }
    return matched == wanted.size() ? 3 : -1;
}

}  // namespace

std::string AnalysisService::workspaceSymbolsJson(const std::string& query, int limit,
                                                  const std::string* imports,
                                                  const std::vector<std::filesystem::path>& baseImports) {
    auto index = buildIndex(std::string(), imports, baseImports);

    std::vector<Candidate> candidates;
    for (const auto& name : index.scriptNames()) {
        const Script* script = index.resolve(name);
        if (!script) continue;
        for (auto& symbol : Symbols::documentSymbols(*script)) {
            if (symbol.kind == SymbolKind::Import) continue;
            const int rank = matchRank(symbol.name, query);
            if (rank >= 0) candidates.push_back({rank, std::move(symbol)});
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         if (a.rank != b.rank) return a.rank < b.rank;
                         return lowerText(a.symbol.name) < lowerText(b.symbol.name);
                     });

    const auto cap = static_cast<std::size_t>(
        limit <= 0 ? k_defaultWorkspaceLimit : std::min(limit, k_maxWorkspaceLimit));
    nlohmann::json results = nlohmann::json::array();
    for (std::size_t i = 0; i < candidates.size() && i < cap; ++i) {
        const auto& symbol = candidates[i].symbol;
        results.push_back({
            {"name", symbol.name},
            {"kind", std::string(symbolKindName(symbol.kind))},
            {"signature", symbol.signature},
            {"container",
             symbol.container ? nlohmann::json(*symbol.container) : nlohmann::json(nullptr)},
            {"file", symbol.file ? nlohmann::json(*symbol.file) : nlohmann::json(nullptr)},
            {"line", symbol.nameSpan.line},
            {"column", symbol.nameSpan.column},
            {"start", symbol.nameSpan.start},
            {"length", symbol.nameSpan.length},
        });
    }

    nlohmann::json payload;
    payload["truncated"] = candidates.size() > cap;
    payload["results"] = std::move(results);
    return payload.dump();
}

}  // namespace fo4::papyrus
