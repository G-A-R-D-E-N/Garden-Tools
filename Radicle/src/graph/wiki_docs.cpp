#include <fo4/graph/wiki_docs.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string_view>
#include <vector>

namespace fo4::graph {
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

std::string lower(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string ciKey(std::string_view value) { return lower(value); }

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

std::string urlDecode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = value.substr(i + 1, 2);
            const auto isHexDigit = [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                       (c >= 'A' && c <= 'F');
            };
            if (isHexDigit(hex[0]) && isHexDigit(hex[1])) {
                out.push_back(static_cast<char>(std::stoul(std::string(hex), nullptr, 16)));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

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

std::vector<std::filesystem::path> enumerateHtml(const std::filesystem::path& root) {
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
                       wildcardMatch(entry.path().filename().string(), "*.html")) {
                matches.push_back(entry.path());
            }
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

std::optional<std::string> readAllText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::string clean(const std::string& html) {
    std::string text = std::regex_replace(html, std::regex(R"(<[^>]+>)"), " ");
    text = htmlDecode(text);
    std::replace(text.begin(), text.end(), static_cast<char>(0xA0), ' ');
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

std::optional<std::string> firstParagraph(const std::string& html) {
    std::smatch match;
    if (!std::regex_search(html, match, std::regex(R"(<p>(.*?)</p>)", std::regex::icase | std::regex::ECMAScript))) {
        return std::nullopt;
    }
    const auto text = trim(clean(match[1].str()));
    return text.empty() ? std::nullopt : std::optional<std::string>(text);
}

}

CkWikiDocProvider::CkWikiDocProvider(std::optional<std::filesystem::path> wikiRoot) {
    if (wikiRoot && std::filesystem::is_directory(*wikiRoot)) {
        root_ = std::move(wikiRoot);
        buildIndexes();
    }
}

void CkWikiDocProvider::buildIndexes() {
    if (!root_) return;
    for (const auto& path : enumerateHtml(*root_)) {
        const auto name = urlDecode(path.stem().string());
        const std::string separator = "_-_";
        const auto separatorPos = name.find(separator);
        if (separatorPos != std::string::npos && separatorPos > 0) {
            const auto function = name.substr(0, separatorPos);
            const auto script = name.substr(separatorPos + separator.size());
            byFunction_[ciKey(function + "|" + script)] = path;
        }
        const std::string scriptSuffix = "_Script";
        if (name.size() > scriptSuffix.size() &&
            lower(name.substr(name.size() - scriptSuffix.size())) == lower(scriptSuffix)) {
            byScript_[ciKey(name.substr(0, name.size() - scriptSuffix.size()))] = path;
        }
    }
}

const WikiFunctionDoc* CkWikiDocProvider::function(std::string_view scriptName,
                                                   std::string_view functionName) {
    if (!root_) return nullptr;
    const auto key = ciKey(std::string(functionName) + "|" + std::string(scriptName));
    const auto cached = functions_.find(key);
    if (cached != functions_.end()) {
        return cached->second ? &*cached->second : nullptr;
    }

    const auto page = byFunction_.find(key);
    if (page == byFunction_.end()) return nullptr;

    auto doc = readFunction(scriptName, functionName);
    const auto inserted = functions_.emplace(key, std::move(doc));
    const auto& stored = inserted.first->second;
    return stored ? &*stored : nullptr;
}

const WikiScriptDoc* CkWikiDocProvider::script(std::string_view scriptName) {
    if (!root_) return nullptr;
    const auto key = ciKey(scriptName);
    const auto cached = scripts_.find(key);
    if (cached != scripts_.end()) return &cached->second;

    const auto page = byScript_.find(key);
    if (page == byScript_.end()) return nullptr;

    const auto text = readAllText(page->second);
    if (!text) {
        recordFailure();
        return nullptr;
    }
    const auto summary = firstParagraph(*text);
    const auto [it, _] = scripts_.emplace(
        key, WikiScriptDoc{summary ? *summary : std::optional<std::string>{}});
    return &it->second;
}

WikiDocStats CkWikiDocProvider::stats() const {
    if (!root_) return WikiDocStats::empty();
    return WikiDocStats{static_cast<int>(byFunction_.size()), parsed_, failed_};
}

std::optional<WikiFunctionDoc> CkWikiDocProvider::readFunction(std::string_view scriptName,
                                                               std::string_view functionName) {
    const auto key = ciKey(std::string(functionName) + "|" + std::string(scriptName));
    const auto page = byFunction_.find(key);
    if (page == byFunction_.end()) return std::nullopt;

    const auto text = readAllText(page->second);
    if (!text) {
        recordFailure();
        return std::nullopt;
    }
    const auto& html = *text;

    WikiFunctionDoc doc;
    std::optional<std::string> syntax;
    std::optional<std::string> returnValue;

    const std::regex sectionPattern(
        R"re(<span[^>]*id="(Syntax|Parameters|Return_Value)"[^>]*>.*?</h2>(.*?)(?:<h2|<div id="catlinks))re",
        std::regex::icase | std::regex::ECMAScript);
    const std::regex liPattern(R"(<li>(.*?)</li>)", std::regex::icase | std::regex::ECMAScript);
    const std::regex defaultPattern(R"(Default:\s*([^<]+))", std::regex::icase);

    for (std::sregex_iterator it(html.begin(), html.end(), sectionPattern), end;
         it != end; ++it) {
        const auto& match = *it;
        const auto sectionId = lower(match[1].str());
        const auto body = match[2].str();
        if (sectionId == "syntax") {
            const auto cleaned = clean(body);
            const auto newline = cleaned.find('\n');
            const auto line = newline == std::string::npos ? cleaned : cleaned.substr(0, newline);
            syntax = trim(line);
        } else if (sectionId == "return_value") {
            returnValue = trim(clean(body));
        } else if (sectionId == "parameters") {
            for (std::sregex_iterator lit(body.begin(), body.end(), liPattern), lend;
                 lit != lend; ++lit) {
                const auto raw = (*lit)[1].str();
                const auto cleaned = trim(clean(raw));
                if (cleaned.empty()) continue;

                const auto colon = cleaned.find(':');
                const auto name = trim(colon != std::string::npos
                                           ? cleaned.substr(0, colon)
                                           : cleaned);
                if (name.empty() || name.find(' ') != std::string::npos) continue;

                const auto described = colon != std::string::npos
                                           ? std::optional<std::string>(trim(cleaned.substr(colon + 1)))
                                           : std::nullopt;
                std::optional<std::string> defaulted;
                std::smatch defaultMatch;
                if (std::regex_search(raw, defaultMatch, defaultPattern)) {
                    defaulted = trim(defaultMatch[1].str());
                }
                doc.parameters[ciKey(name)] = WikiParameterDoc{described, defaulted};
            }
        }
    }

    ++parsed_;
    doc.summary = firstParagraph(html);
    doc.syntaxLine = syntax;
    doc.returnValue = returnValue;
    return doc;
}

void CkWikiDocProvider::recordFailure() { ++failed_; }

}
