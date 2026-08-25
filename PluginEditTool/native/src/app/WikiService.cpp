#include <fo4ide/app/WikiService.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fo4ide::app {
namespace wiki {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string trimEnd(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

bool isWhitespace(std::string_view value) {
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return std::isspace(c); });
}

bool iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

std::string readFileText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read '" + path.string() + "'.");
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// Simple '*' wildcard match (C# Directory.GetFiles pattern subset).
bool wildcardMatch(std::string_view name, std::string_view pattern) {
    if (pattern == "*") return true;
    if (pattern.find('*') == std::string_view::npos) return name == pattern;
    const auto star = pattern.find('*');
    if (star == 0) {
        return name.size() >= pattern.size() - 1 &&
               name.substr(name.size() - (pattern.size() - 1)) == pattern.substr(1);
    }
    return name.size() >= pattern.size() - 1 &&
           name.substr(0, star) == pattern.substr(0, star) &&
           name.substr(name.size() - (pattern.size() - star - 1)) ==
               pattern.substr(star + 1);
}

std::vector<std::filesystem::path> enumerateFiles(const std::filesystem::path& root,
                                                  std::string pattern) {
    std::vector<std::filesystem::path> matches;
    std::vector<std::filesystem::path> stack{root};
    std::error_code ec;
    while (!stack.empty()) {
        const auto dir = stack.back();
        stack.pop_back();
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code statusEc;
            const auto& entry = *it;
            if (entry.is_symlink(statusEc)) continue;
            if (entry.is_directory(statusEc)) {
                stack.push_back(entry.path());
            } else if (entry.is_regular_file(statusEc) &&
                       wildcardMatch(entry.path().filename().string(), pattern)) {
                matches.push_back(entry.path());
            }
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

std::optional<std::filesystem::path> findFile(const std::filesystem::path& root,
                                              const std::string& fileName) {
    const auto matches = enumerateFiles(root, fileName);
    if (matches.empty()) return std::nullopt;
    return matches.front();
}

std::string replaceAll(std::string value, std::string_view from, std::string_view to) {
    if (from.empty()) return value;
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

// Port of System.Net.WebUtility.HtmlDecode for the entities the mirror pages
// actually use (the five XML entities, nbsp, and numeric references).
std::string htmlDecode(std::string_view html) {
    std::string out;
    out.reserve(html.size());
    for (std::size_t i = 0; i < html.size();) {
        if (html[i] != '&') {
            out.push_back(html[i++]);
            continue;
        }
        const auto semi = html.find(';', i);
        if (semi == std::string_view::npos || semi - i > 12) {
            out.push_back(html[i++]);
            continue;
        }
        const auto entity = html.substr(i, semi - i + 1);
        std::string decoded;
        if (entity == "&amp;") decoded = "&";
        else if (entity == "&lt;") decoded = "<";
        else if (entity == "&gt;") decoded = ">";
        else if (entity == "&quot;") decoded = "\"";
        else if (entity == "&apos;") decoded = "'";
        else if (entity == "&nbsp;") decoded = " ";
        else if (entity.size() > 3 && entity[1] == '#') {
            const auto body = entity.substr(2, entity.size() - 3);
            try {
                const auto isHex = !body.empty() && (body[0] == 'x' || body[0] == 'X');
                const auto number = static_cast<unsigned long long>(std::stoul(
                    std::string(isHex ? body.substr(1) : body), nullptr, isHex ? 16 : 10));
                if (number <= 0x7F) decoded.assign(1, static_cast<char>(number));
            } catch (...) {
            }
        }
        if (!decoded.empty()) {
            out += decoded;
            i = semi + 1;
        } else {
            out.push_back(html[i++]);
        }
    }
    return out;
}

std::string stripTags(std::string html) {
    html = std::regex_replace(html, std::regex(R"(<[\s\S]*?>)"), " ");
    html = htmlDecode(html);
    html = std::regex_replace(html, std::regex(R"(\s+)"), " ");
    return trim(std::move(html));
}

std::optional<std::string> extractTag(const std::string& html, const std::string& tag) {
    std::smatch match;
    const auto pattern = "<" + tag + R"([^>]*>([\s\S]*?)</)" + tag + ">";
    if (!std::regex_search(html, match, std::regex(pattern))) return std::nullopt;
    return stripTags(match[1].str());
}

// The C# uses a lookahead ((?=<h2|<div id="catlinks")); a consuming
// non-greedy alternation captures the same section body.
std::string extractSection(const std::string& html, const std::string& sectionId) {
    std::smatch match;
    const auto pattern = R"(<span[^>]*\bid=")" + sectionId +
                         R"("[^>]*>[\s\S]*?</h2>([\s\S]*?)(?:<h2|<div id="catlinks"))";
    if (!std::regex_search(html, match, std::regex(pattern))) return "";
    return match[1].str();
}

std::vector<std::string> splitOwners(const std::filesystem::path& path) {
    // The C# takes the last "_-_" segment of the file stem.
    const auto stem = path.stem().string();
    const std::string separator = "_-_";
    const auto pos = stem.rfind(separator);
    if (pos == std::string::npos) return {stem};
    return {stem.substr(pos + separator.size())};
}

}  // namespace

std::string lookupFunction(std::string_view wikiRoot, std::string_view script,
                           std::string_view function) {
    if (isWhitespace(wikiRoot) || !std::filesystem::is_directory(std::filesystem::path(wikiRoot))) {
        return std::string(kNotConfiguredMessage);
    }
    const auto functionTrim = trim(std::string(function));
    const auto scriptTrim = trim(std::string(script));
    if (functionTrim.empty()) return "Provide a function name, e.g. 'GetBaseObject'.";

    std::optional<std::filesystem::path> page;
    if (!scriptTrim.empty()) {
        page = findFile(std::filesystem::path(wikiRoot), functionTrim + "_-_" + scriptTrim + ".html");
    }
    if (!page) {
        const auto matches = enumerateFiles(std::filesystem::path(wikiRoot),
                                            functionTrim + "_-_*.html");
        if (matches.empty()) {
            return "No CK wiki page found for function '" + functionTrim + "'" +
                   (scriptTrim.empty() ? "" : " on script '" + scriptTrim + "'") +
                   ". Names are case-sensitive to the wiki's own spelling; call papyrus_script_info "
                   "on the owning script first to see its exact function list.";
        }
        if (matches.size() > 1) {
            std::ostringstream owners;
            for (std::size_t i = 0; i < matches.size(); ++i) {
                const auto parts = splitOwners(matches[i]);
                if (i) owners << ", ";
                owners << (parts.empty() ? std::string{} : parts.front());
            }
            return "'" + functionTrim + "' is defined on multiple scripts: " + owners.str() +
                   ". Pass 'script' to disambiguate.";
        }
        page = matches.front();
    }

    const auto html = readFileText(*page);
    const auto title = extractTag(html, "h1")
                           .value_or(replaceAll(page->stem().string(), "_-_", " - "));
    std::smatch memberOf;
    const bool hasMemberOf =
        std::regex_search(html, memberOf, std::regex(R"(<b>Member of:</b>\s*<a[^>]*>([^<]+)</a>)"));
    const auto syntax = stripTags(extractSection(html, "Syntax"));
    const auto parameters = stripTags(extractSection(html, "Parameters"));
    const auto returnValue = stripTags(extractSection(html, "Return_Value"));
    const auto caveat = stripTags(extractSection(html, "Caveat"));

    std::ostringstream sb;
    sb << title << "\n";
    if (hasMemberOf) sb << "Member of: " << memberOf[1].str() << "\n";
    if (!syntax.empty()) sb << "Syntax: " << syntax << "\n";
    if (!parameters.empty()) sb << "Parameters: " << parameters << "\n";
    if (!returnValue.empty()) sb << "Return Value: " << returnValue << "\n";
    if (!caveat.empty()) sb << "Caveat: " << caveat << "\n";
    return trimEnd(sb.str());
}

std::string lookupScriptInfo(std::string_view wikiRoot, std::string_view script) {
    if (isWhitespace(wikiRoot) || !std::filesystem::is_directory(std::filesystem::path(wikiRoot))) {
        return std::string(kNotConfiguredMessage);
    }
    auto scriptTrim = trim(std::string(script));
    scriptTrim = std::regex_replace(scriptTrim, std::regex(R"([_ ]?Script$)", std::regex::icase), "");
    if (scriptTrim.empty()) {
        return "Provide a script name, e.g. 'ActiveMagicEffect' or 'ObjectReference'.";
    }

    const auto page = findFile(std::filesystem::path(wikiRoot), scriptTrim + "_Script.html");
    if (!page) {
        return "No CK wiki page found for script '" + scriptTrim +
               "'. Check the exact name (case-sensitive) -- the wiki mirror's own page list is "
               "under fallout4\\*_Script.html.";
    }

    const auto html = readFileText(*page);
    std::smatch extends;
    const bool hasExtends =
        std::regex_search(html, extends, std::regex(R"(<b>Extends:</b>\s*<a[^>]*>([^<]+)</a>)"));
    const auto definition = stripTags(extractSection(html, "Definition"));
    const auto globalFns = stripTags(extractSection(html, "Global_Functions"));
    const auto memberFns = stripTags(extractSection(html, "Member_Functions"));
    const auto events = stripTags(extractSection(html, "Events"));

    std::ostringstream sb;
    sb << scriptTrim << " Script\n";
    if (hasExtends) sb << "Extends: " << extends[1].str() << "\n";
    if (!definition.empty()) sb << "Definition: " << definition << "\n";
    if (!globalFns.empty() && !iequals(globalFns, "None")) {
        sb << "Global Functions: " << globalFns << "\n";
    }
    if (!memberFns.empty() && !iequals(memberFns, "None")) {
        sb << "Member Functions: " << memberFns << "\n";
    }
    if (!events.empty() && !iequals(events, "None")) {
        sb << "Events: " << events << "\n";
    }
    return trimEnd(sb.str());
}

}  // namespace wiki
}  // namespace fo4ide::app
