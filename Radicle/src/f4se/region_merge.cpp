#include <fo4/f4se/region_merge.hpp>

#include <algorithm>
#include <optional>

namespace fo4::f4se {

namespace {

struct Marker {
    int index{0};
    int length{0};
    bool isBegin{false};
    std::string name;
};

bool isSpaceTab(char c) { return c == ' ' || c == '\t'; }

std::size_t skipSpaceTab(std::string_view line, std::size_t at) {
    while (at < line.size() && isSpaceTab(line[at])) at++;
    return at;
}

bool tryMarker(std::string_view line, bool& isBegin, std::string& name) {
    if (line.size() >= 7 && line.substr(0, 7) == "#pragma") {
        auto at = skipSpaceTab(line, 8);
        std::string_view kind;
        if (line.substr(at, 6) == "region") {
            kind = "region";
        } else if (line.substr(at, 9) == "endregion") {
            kind = "endregion";
        } else {
            return false;
        }
        at += kind.size();
        at = skipSpaceTab(line, at);
        if (line.substr(at, 11) != "FO4IDE_BODY") return false;
        at = skipSpaceTab(line, at + 11);
        isBegin = kind == "region";
        name = std::string(line.substr(at));
        while (!name.empty() && (isSpaceTab(name.back()) || name.back() == '\r')) {
            name.pop_back();
        }
        return true;
    }

    if (line.size() >= 2 && line[0] == '/' && line[1] == '/') {
        auto at = skipSpaceTab(line, 2);
        std::string_view kind;
        if (line.substr(at, 3) == ">>>") {
            kind = ">>>";
        } else if (line.substr(at, 3) == "<<<") {
            kind = "<<<";
        } else {
            return false;
        }
        at += 3;
        at = skipSpaceTab(line, at);
        if (line.substr(at, 5) != "body:") return false;
        at = skipSpaceTab(line, at + 5);
        isBegin = kind == ">>>";
        name = std::string(line.substr(at));
        while (!name.empty() && (isSpaceTab(name.back()) || name.back() == '\r')) {
            name.pop_back();
        }
        return true;
    }

    return false;
}

std::vector<Marker> findMarkers(const std::string& text) {
    std::vector<Marker> markers;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lineStart = i;
        const auto lineEnd = text.find('\n', i);
        const auto end = lineEnd == std::string::npos ? text.size() : lineEnd;
        auto line = std::string_view(text).substr(lineStart, end - lineStart);
        while (!line.empty() && isSpaceTab(line.front())) line.remove_prefix(1);

        bool isBegin = false;
        std::string name;
        if (tryMarker(line, isBegin, name)) {
            markers.push_back(
                Marker{static_cast<int>(lineStart), static_cast<int>(end - lineStart),
                       isBegin, std::move(name)});
        }
        if (lineEnd == std::string::npos) break;
        i = lineEnd + 1;
    }
    return markers;
}

std::string replaceCrLf(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        out.push_back(text[i]);
    }
    return out;
}

std::string trimNewlines(std::string_view text) {
    while (!text.empty() && text.front() == '\n') text.remove_prefix(1);
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return std::string(text);
}

std::string trimWhitespace(std::string_view text) {
    while (!text.empty() && isSpaceTab(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpaceTab(text.back())) text.remove_suffix(1);
    return std::string(text);
}

}

const std::string& RegionMerge::signatureChangedBanner() {
    static const std::string banner =
        "\t#pragma message(\"FO4IDE: preserved body signature changed; review it.\")";
    return banner;
}

std::string RegionMerge::begin(std::string_view name) {
    return std::string(kBeginPrefix) + std::string(name);
}

std::string RegionMerge::end(std::string_view name) {
    return std::string(kEndPrefix) + std::string(name);
}

std::map<std::string, RegionMerge::Region> RegionMerge::read(std::string_view existing) {
    std::map<std::string, Region> regions;
    if (existing.empty()) return regions;

    const auto normalized = replaceCrLf(existing);
    std::optional<std::string> openName;
    int openEnd = -1;
    int openStart = -1;

    for (const auto& match : findMarkers(normalized)) {
        if (match.isBegin) {
            openName = match.name;
            openStart = match.index;
            openEnd = match.index + match.length;
            continue;
        }

        if (!openName || *openName != match.name) continue;

        const auto body = trimNewlines(
            std::string_view(normalized).substr(static_cast<std::size_t>(openEnd),
                                                static_cast<std::size_t>(match.index - openEnd)));
        regions[match.name] =
            Region{match.name, body, signatureLineBefore(normalized, openStart)};
        openName.reset();
    }

    return regions;
}

std::string RegionMerge::merge(std::string_view generated, std::string_view existing) {
    const auto preserved = read(existing);
    if (preserved.empty()) return replaceCrLf(generated);

    const auto normalized = replaceCrLf(generated);
    std::string result;
    result.reserve(normalized.size());

    int cursor = 0;
    std::optional<std::string> openName;
    int openEnd = -1;
    int openStart = -1;

    for (const auto& match : findMarkers(normalized)) {
        if (match.isBegin) {
            openName = match.name;
            openStart = match.index;
            openEnd = match.index + match.length;
            continue;
        }

        const auto name = match.name;
        if (!openName || *openName != name) continue;
        openName.reset();

        const auto found = preserved.find(name);
        if (found == preserved.end()) continue;
        const auto& region = found->second;

        result.append(normalized, static_cast<std::size_t>(cursor),
                      static_cast<std::size_t>(openEnd - cursor));
        result.push_back('\n');

        const auto signature = signatureLineBefore(normalized, openStart);
        if (signature != region.signatureLine && !region.signatureLine.empty()) {
            result.append(signatureChangedBanner());
            result.push_back('\n');
        }

        result.append(region.body);
        result.push_back('\n');
        cursor = match.index;
    }

    result.append(normalized, static_cast<std::size_t>(cursor),
                  normalized.size() - static_cast<std::size_t>(cursor));
    return result;
}

std::vector<std::string> RegionMerge::orphaned(std::string_view generated,
                                               std::string_view existing) {
    const auto oldNames = read(existing);
    const auto newNames = read(generated);
    std::vector<std::string> orphans;
    for (const auto& [name, region] : oldNames) {
        (void)region;
        if (newNames.find(name) == newNames.end()) orphans.push_back(name);
    }
    return orphans;
}

std::string RegionMerge::signatureLineBefore(const std::string& text, int markerStart) {
    int end = static_cast<int>(text.rfind('\n', std::max(0, markerStart - 1)));

    while (end > 0) {
        const auto start = static_cast<int>(text.rfind('\n', end - 1)) + 1;
        const auto line = trimWhitespace(
            std::string_view(text).substr(static_cast<std::size_t>(start),
                                          static_cast<std::size_t>(end - start)));
        if (!line.empty() && line != "{" && line != "}") return line;
        if (start == 0) return "";
        end = start - 1;
    }

    return "";
}

}
