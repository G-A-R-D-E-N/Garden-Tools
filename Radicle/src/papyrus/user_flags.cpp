#include <fo4/papyrus/user_flags.hpp>

#include "codegen_detail.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {

using codegen_detail::lower;
using codegen_detail::iequals;
using fo4::pex::PexUserFlag;

void UserFlagTable::declare(std::string name, std::uint8_t bit) {
    masks_[lower(name)] = 1u << bit;
    const auto found = std::find_if(flags_.begin(), flags_.end(),
                                    [bit](const PexUserFlag& f) { return f.index == bit; });
    if (found == flags_.end()) {
        PexUserFlag flag;
        flag.name = lower(name);
        flag.index = bit;
        flags_.push_back(std::move(flag));
    }
}

void UserFlagTable::declareComposite(std::string name, std::uint32_t mask) {
    masks_[lower(name)] = mask;
}

void UserFlagTable::sort() {
    std::stable_sort(flags_.begin(), flags_.end(),
                     [](const PexUserFlag& a, const PexUserFlag& b) { return a.index < b.index; });
}

std::uint32_t UserFlagTable::maskFor(std::string_view flagName) const {
    const auto it = masks_.find(lower(flagName));
    return it == masks_.end() ? 0u : it->second;
}

bool UserFlagTable::knows(std::string_view flagName) const {
    return masks_.contains(lower(flagName));
}

std::uint32_t UserFlagTable::maskFor(const std::vector<std::string>& written) const {
    std::uint32_t mask = 0;
    for (const auto& name : written) mask |= maskFor(name);
    return mask;
}

UserFlagTable UserFlagTable::fallout4Default() {
    UserFlagTable table;
    table.declare("Hidden", 0);
    table.declare("Conditional", 1);
    table.declare("Default", 2);
    table.declare("CollapsedOnRef", 3);
    table.declare("CollapsedOnBase", 4);
    table.declare("Mandatory", 5);
    table.declareComposite("Collapsed", (1u << 3) | (1u << 4));
    table.sort();
    return table;
}

namespace {

std::string stripComments(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            if (i < text.size()) out.push_back('\n');
            continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            ++i;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

std::vector<std::string> splitWhitespace(std::string_view line) {
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i > start) parts.emplace_back(line.substr(start, i - start));
    }
    return parts;
}

bool parseByte(std::string_view text, std::uint8_t& out) {
    try {
        const auto value = std::stoul(std::string(text));
        if (value > 31) return false;
        out = static_cast<std::uint8_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

UserFlagTable UserFlagTable::parse(std::string_view text) {
    UserFlagTable table;
    struct Composite {
        std::string name;
        std::vector<std::string> children;
    };
    std::vector<Composite> composites;

    std::istringstream stream(stripComments(text));
    std::string raw;
    while (std::getline(stream, raw)) {
        auto line = raw;
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t\r") + 1);
        if (line.empty()) continue;

        const auto brace = line.find('{');
        if (brace != std::string::npos) {
            line = line.substr(0, brace);
            line.erase(line.find_last_not_of(" \t") + 1);
        }
        if (line.empty()) continue;

        const auto parts = splitWhitespace(line);
        if (parts.size() < 3 || !iequals(parts[0], "Flag")) continue;

        std::uint8_t bit = 0;
        if (parseByte(parts[2], bit)) {
            table.declare(parts[1], bit);
            continue;
        }

        Composite composite;
        composite.name = parts[1];
        for (std::size_t i = 2; i < parts.size(); ++i) {
            if (parts[i] != "&") composite.children.push_back(parts[i]);
        }
        if (!composite.children.empty()) composites.push_back(std::move(composite));
    }

    for (const auto& composite : composites) {
        std::uint32_t mask = 0;
        for (const auto& child : composite.children) mask |= table.maskFor(child);
        if (mask != 0) table.declareComposite(composite.name, mask);
    }

    table.sort();
    return table;
}

UserFlagTable UserFlagTable::fromFileOrDefault(const std::filesystem::path& path) {
    if (path.empty()) return fallout4Default();
    std::ifstream stream(path);
    if (!stream) return fallout4Default();
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const auto table = parse(buffer.str());
    return table.flags_.empty() ? fallout4Default() : table;
}

std::optional<std::filesystem::path> UserFlagTable::findFlagFile(
    const std::vector<std::filesystem::path>& roots) {
    for (const auto& root : roots) {
        if (root.empty()) continue;
        const auto direct = root / "Institute_Papyrus_Flags.flg";
        if (std::filesystem::exists(direct)) return direct;
    }
    return std::nullopt;
}

}  // namespace fo4::papyrus
