#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::f4se {

class RegionMerge {
public:
    static constexpr const char* kBeginPrefix = "#pragma region FO4IDE_BODY ";
    static constexpr const char* kEndPrefix = "#pragma endregion FO4IDE_BODY ";

    static const std::string& signatureChangedBanner();

    struct Region {
        std::string name;
        std::string body;
        std::string signatureLine;
    };

    static std::string begin(std::string_view name);
    static std::string end(std::string_view name);

    static std::map<std::string, Region> read(std::string_view existing);

    static std::string merge(std::string_view generated, std::string_view existing);

    static std::vector<std::string> orphaned(std::string_view generated,
                                             std::string_view existing);

private:
    static std::string signatureLineBefore(const std::string& text, int markerStart);
};

}
