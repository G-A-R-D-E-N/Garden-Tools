#include <fo4/graph/graph_name_table.hpp>

#include <cctype>
#include <climits>
#include <stdexcept>

#include <fo4/papyrus/token.hpp>

namespace fo4::graph {
namespace {

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}

GraphNameTable::GraphNameTable(papyrus::ScriptIndex* index, const papyrus::Script* owner) {
    for (const auto& [word, kind] : papyrus::Keywords::all()) taken_.insert(word);

    if (index == nullptr || owner == nullptr) return;

    const auto chain = index->baseChain(owner);
    for (const auto* script : chain) {
        for (const auto& function : script->functions) taken_.insert(lower(function.name));
        for (const auto& property : script->properties) taken_.insert(lower(property.name));
        for (const auto& variable : script->variables) taken_.insert(lower(variable.name));
    }
}

void GraphNameTable::reserve(std::string_view name) {
    if (name.empty()) return;
    bool whitespaceOnly = true;
    for (const char c : name) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            whitespaceOnly = false;
            break;
        }
    }
    if (!whitespaceOnly) taken_.insert(lower(name));
}

bool GraphNameTable::isReserved(std::string_view name) const {
    return !name.empty() && taken_.contains(lower(name));
}

std::string GraphNameTable::allocate(std::string_view hint) {
    const auto stem = clean(hint);

    if (taken_.insert(lower(stem)).second) return stem;

    for (int suffix = 2; suffix < INT_MAX; ++suffix) {
        const auto candidate = stem + std::to_string(suffix);
        if (taken_.insert(lower(candidate)).second) return candidate;
    }

    throw std::runtime_error("Ran out of names, which should not be reachable.");
}

std::string GraphNameTable::clean(std::string_view hint) {
    if (hint.empty()) return "value";
    bool whitespaceOnly = true;
    for (const char c : hint) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            whitespaceOnly = false;
            break;
        }
    }
    if (whitespaceOnly) return "value";

    std::string kept;
    for (const char c : hint) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_') kept.push_back(c);
    }
    if (kept.empty()) return "value";
    if (std::isdigit(static_cast<unsigned char>(kept[0]))) kept = "_" + kept;
    return kept;
}

}
