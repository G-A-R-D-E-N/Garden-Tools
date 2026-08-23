#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::f4se {

// Port of F4SERegionMerge: preserves hand-written function bodies across
// regeneration, keyed by the FO4IDE_BODY region markers.
class RegionMerge {
public:
    static constexpr const char* kBeginPrefix = "#pragma region FO4IDE_BODY ";
    static constexpr const char* kEndPrefix = "#pragma endregion FO4IDE_BODY ";

    // "SIGNATURE CHANGED" banner text (matches the C# constant; the test
    // matches a trimmed fragment of it).
    static const std::string& signatureChangedBanner();

    struct Region {
        std::string name;
        std::string body;
        std::string signatureLine;
    };

    static std::string begin(std::string_view name);
    static std::string end(std::string_view name);

    // Recovers the named regions (body + the signature line above the begin
    // marker) from a previous file.
    static std::map<std::string, Region> read(std::string_view existing);

    // Splices preserved bodies into freshly generated text, adding the
    // signature-changed banner when a function's signature moved on.
    static std::string merge(std::string_view generated, std::string_view existing);

    // Names preserved in `existing` that the new text no longer declares.
    static std::vector<std::string> orphaned(std::string_view generated,
                                             std::string_view existing);

private:
    static std::string signatureLineBefore(const std::string& text, int markerStart);
};

}  // namespace fo4ide::f4se
