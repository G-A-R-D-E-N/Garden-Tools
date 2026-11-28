#include <fo4/f4se/cpp_scanner.hpp>

#include <cctype>

namespace fo4::f4se {

namespace {

std::string trimCopy(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

}

std::string CppScanner::blankComments(std::string_view source) {
    if (source.empty()) return std::string();

    std::string result;
    result.reserve(source.size());
    std::size_t i = 0;

    while (i < source.size()) {
        const char c = source[i];

        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') {
                result.push_back(' ');
                i++;
            }
            continue;
        }

        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            result.append("  ");
            i += 2;
            while (i < source.size() && !(source[i] == '*' && i + 1 < source.size() &&
                                          source[i + 1] == '/')) {
                result.push_back(source[i] == '\n' ? '\n' : ' ');
                i++;
            }
            if (i < source.size()) {
                result.append("  ");
                i += 2;
            }
            continue;
        }

        if (c == '"' || c == '\'') {
            const char quote = c;
            result.push_back(c);
            i++;
            while (i < source.size()) {
                if (source[i] == '\\' && i + 1 < source.size()) {
                    result.push_back(source[i]);
                    result.push_back(source[i + 1]);
                    i += 2;
                    continue;
                }
                result.push_back(source[i]);
                if (source[i] == quote) {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }

        result.push_back(c);
        i++;
    }

    return result;
}

int CppScanner::findMatchingAngle(std::string_view text, int openIndex) {
    if (openIndex < 0 || openIndex >= static_cast<int>(text.size()) ||
        text[static_cast<std::size_t>(openIndex)] != '<') {
        return -1;
    }

    int depth = 0;
    for (int i = openIndex; i < static_cast<int>(text.size()); i++) {
        switch (text[static_cast<std::size_t>(i)]) {
            case '<':
                depth++;
                break;
            case '>':
                depth--;
                if (depth == 0) return i;
                break;
            case ';':
            case '{':
            case '}':
                return -1;
        }
    }
    return -1;
}

std::vector<std::string> CppScanner::splitTemplateArguments(std::string_view arguments) {
    std::vector<std::string> parts;
    if (arguments.empty()) return parts;

    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < arguments.size(); i++) {
        switch (arguments[i]) {
            case '<':
                depth++;
                break;
            case '>':
                depth--;
                break;
            case ',':
                if (depth == 0) {
                    parts.push_back(trimCopy(arguments.substr(start, i - start)));
                    start = i + 1;
                }
                break;
        }
    }

    const auto last = trimCopy(arguments.substr(start));
    if (!last.empty()) parts.push_back(last);
    return parts;
}

int CppScanner::lineAt(std::string_view text, int offset) {
    int line = 1;
    const auto limit = static_cast<std::size_t>(offset < 0 ? 0 : offset);
    for (std::size_t i = 0; i < limit && i < text.size(); i++) {
        if (text[i] == '\n') line++;
    }
    return line;
}

}
