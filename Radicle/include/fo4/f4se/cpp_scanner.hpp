#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fo4::f4se {

class CppScanner {
public:
    static std::string blankComments(std::string_view source);

    static int findMatchingAngle(std::string_view text, int openIndex);

    static std::vector<std::string> splitTemplateArguments(std::string_view arguments);

    static int lineAt(std::string_view text, int offset);
};

}
