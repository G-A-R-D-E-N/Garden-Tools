#include <fo4ide/plugin/RadicleLightJson.hpp>

#include <cstdint>

namespace fo4ide::plugin {
namespace {

nlohmann::json byteColourJson(const fo4::env::ByteColor& colour) {
    return nlohmann::json::array({colour.red, colour.green, colour.blue});
}

nlohmann::json unitColourJson(const fo4::env::ByteColor& colour) {
    constexpr float kMax = 255.0F;
    return {{"r", static_cast<float>(colour.red) / kMax},
            {"g", static_cast<float>(colour.green) / kMax},
            {"b", static_cast<float>(colour.blue) / kMax}};
}

nlohmann::json optionalFloat(const std::optional<float>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json optionalColour(const std::optional<fo4::env::ByteColor>& colour) {
    return colour ? byteColourJson(*colour) : nlohmann::json(nullptr);
}

bool hasBit(std::uint32_t flags, std::uint32_t bit) { return (flags & bit) != 0U; }

}  // namespace

nlohmann::json lightJson(const fo4::env::LightRecordData& record) {
    if (!record.data) return nullptr;
    const auto& data = *record.data;
    const std::uint32_t flags = data.flags;
    const bool spot = hasBit(flags, kLightFlagShadowSpotlight) || hasBit(flags, kLightFlagNonShadowSpotlight);
    const bool shadowFlag = hasBit(flags, kLightFlagShadowSpotlight) ||
                            hasBit(flags, kLightFlagShadowHemisphere) ||
                            hasBit(flags, kLightFlagShadowOmnidirectional);

    nlohmann::json json{
        {"radius", data.radius},
        {"color", unitColourJson(data.color)},
        {"fade", optionalFloat(record.fade)},
        {"flags", flags},
        {"fov", data.fov},
        {"falloffExponent", data.falloff_exponent},
        {"nearClip", data.near_clip},
        {"constant", data.constant},
        {"scalar", data.scalar},
        {"exponent", optionalFloat(data.exponent)},
        {"flickerPeriod", data.flicker_period},
        {"flickerIntensity", data.flicker_intensity_amplitude},
        {"flickerMovement", data.flicker_movement_amplitude},
        {"isSpot", spot},
        {"castsShadow", shadowFlag},
        {"ambientOnly", hasBit(flags, kLightFlagAmbientOnly)},
        {"offByDefault", hasBit(flags, kLightFlagOffByDefault)},
        {"flicker", hasBit(flags, kLightFlagFlicker)},
        {"pulse", hasBit(flags, kLightFlagPulse)},
        {"nonSpecular", hasBit(flags, kLightFlagNonSpecular)},
    };
    return json;
}

nlohmann::json cellLightingJson(const std::optional<fo4::env::CellLighting>& own,
                                const std::optional<fo4::env::CellLighting>& templateLighting) {
    if (!own && !templateLighting) return nullptr;

    const std::uint32_t inherits = own && own->inherits ? *own->inherits : 0U;
    const auto inherited = [&](std::uint32_t bit) {
        return !own || (templateLighting && hasBit(inherits, bit));
    };
    const auto pick = [&](std::uint32_t bit) -> const fo4::env::CellLighting& {
        return inherited(bit) ? *templateLighting : *own;
    };
    const fo4::env::CellLighting& directional = own ? *own : *templateLighting;

    const auto& ambient = pick(kInheritAmbient);
    const auto& fogColours = pick(kInheritFogColours);
    const auto& fogNear = pick(kInheritFogNear);
    const auto& fogFar = pick(kInheritFogFar);

    nlohmann::json json{
        {"ambient", byteColourJson(ambient.ambient)},
        {"directional", byteColourJson(directional.directional)},
        {"directionalRotationXY", directional.directional_rotation_xy},
        {"directionalRotationZ", directional.directional_rotation_z},
        {"directionalFade", directional.directional_fade},
        {"fogNearColor", byteColourJson(fogColours.fog_near_color)},
        {"fogFarColor", optionalColour(fogColours.fog_far_color)},
        {"fogNear", fogNear.fog_near},
        {"fogFar", fogFar.fog_far},
        {"fogClipDistance", directional.fog_clip_distance},
        {"fogPower", directional.fog_power},
        {"inherits", inherits},
        {"hasTemplate", templateLighting.has_value()},
        {"ownLighting", own.has_value()},
        {"directionalSource", own ? "cell" : "template"},
    };
    return json;
}

}  // namespace fo4ide::plugin
