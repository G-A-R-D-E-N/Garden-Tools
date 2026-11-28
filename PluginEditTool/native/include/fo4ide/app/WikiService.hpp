#pragma once

#include <string>
#include <string_view>

namespace fo4ide::app {

// Port of PapyrusWikiService: offline Creation Kit Wiki HTML mirror lookups.
// The C# ExtractSection uses a .NET lookahead; here it is rewritten as a
// non-greedy consuming alternation, which std::regex supports. Returns plain
// text exactly as the C# formats it. Throws std::runtime_error on unreadable
// files (the C# lets the exception reach PapyrusInterop, which returns an
// "Error: ..." string).
namespace wiki {

constexpr std::string_view kNotConfiguredMessage =
    "No CK wiki mirror configured. Launch the MCP server with '--ck-wiki <folder>' pointing at an "
    "offline Creation Kit Wiki HTML mirror (or set CkWikiPath in Settings for the in-app AI panel).";

std::string lookupFunction(std::string_view wikiRoot, std::string_view script,
                           std::string_view function);

std::string lookupScriptInfo(std::string_view wikiRoot, std::string_view script);

}  // namespace wiki

}  // namespace fo4ide::app
