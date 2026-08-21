#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::material {

using Color3 = std::array<float, 3>;
using FieldValue = std::variant<bool, std::uint8_t, std::uint32_t, float, std::string, Color3>;

struct Header {
    std::uint32_t signature{0};
    std::uint32_t version{0};
    bool tileU{false};
    bool tileV{false};
    float uOffset{0.0F};
    float vOffset{0.0F};
    float uScale{1.0F};
    float vScale{1.0F};
    float alpha{1.0F};
    std::uint8_t alphaBlendMode0{0};
    std::uint32_t alphaBlendMode1{0};
    std::uint32_t alphaBlendMode2{0};
    std::uint8_t alphaTestRef{0};
    bool alphaTest{false};
    bool zBufferWrite{false};
    bool zBufferTest{false};
    bool ssr{false};
    bool wetSsr{false};
    bool decal{false};
    bool twoSided{false};
    bool decalNoFade{false};
    bool nonOccluder{false};
    bool refraction{false};
    bool refractionFalloff{false};
    float refractionPower{0.0F};
    bool envMapping{false};
    float envMappingMaskScale{0.0F};
    bool depthBias{false};
    bool grayscaleToPaletteColor{false};
    std::uint8_t maskWrites{0};
};

struct Field {
    std::string name;
    FieldValue value;
};

struct Document {
    enum class Format { Bgsm, Bgem };

    Format format{Format::Bgsm};
    Header header;
    std::vector<Field> fields;
    std::vector<std::uint8_t> trailingBytes;
};

class MaterialService {
public:
    Result<Document> load(const std::filesystem::path& path) const;
    Result<nlohmann::json> inspect(const std::filesystem::path& path) const;
    Result<std::string> setFields(const std::filesystem::path& path,
                                  const std::map<std::string, std::string, std::less<>>& fields,
                                  const std::filesystem::path& output = {}) const;

private:
    Result<std::vector<std::uint8_t>> encode(const Document& document) const;
};

}
