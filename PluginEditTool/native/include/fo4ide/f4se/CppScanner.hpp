#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::f4se {

// Port of F4SECppScanner: comment blanking and template-argument scanning for
// the registration extractor.
class CppScanner {
public:
    // Replaces comments with spaces (block comments keep their newlines) so
    // every offset and line number in the input survives.
    static std::string blankComments(std::string_view source);

    // Index of the '>' matching the '<' at openIndex, or -1 when the angle
    // list is never closed (a ; { } in between refuses too).
    static int findMatchingAngle(std::string_view text, int openIndex);

    // Splits a template argument list on top-level commas (depth-aware).
    static std::vector<std::string> splitTemplateArguments(std::string_view arguments);

    // 1-based line number of offset (counts '\n' before it).
    static int lineAt(std::string_view text, int offset);
};

}  // namespace fo4ide::f4se
