#include <fo4/papyrus/script_names.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fo4::papyrus {

std::string extractScriptName(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream tokens(line);
        std::string keyword;
        tokens >> keyword;
        if (keyword.empty() || keyword.front() == ';') continue;
        std::transform(keyword.begin(), keyword.end(), keyword.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (keyword != "scriptname") return "";
        std::string name;
        tokens >> name;
        return name;
    }
    return "";
}

}  // namespace fo4::papyrus
