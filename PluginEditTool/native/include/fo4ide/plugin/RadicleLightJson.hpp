#pragma once

#include <cstdint>
#include <optional>

#include <nlohmann/json.hpp>

#include <fo4/environment/base_records.hpp>
#include <fo4/environment/world.hpp>

namespace fo4ide::plugin {

inline constexpr std::uint32_t kLightFlagFlicker = 0x0000'0008U;
inline constexpr std::uint32_t kLightFlagOffByDefault = 0x0000'0020U;
inline constexpr std::uint32_t kLightFlagPulse = 0x0000'0080U;
inline constexpr std::uint32_t kLightFlagShadowSpotlight = 0x0000'0400U;
inline constexpr std::uint32_t kLightFlagShadowHemisphere = 0x0000'0800U;
inline constexpr std::uint32_t kLightFlagShadowOmnidirectional = 0x0000'1000U;
inline constexpr std::uint32_t kLightFlagNonShadowSpotlight = 0x0000'4000U;
inline constexpr std::uint32_t kLightFlagNonSpecular = 0x0000'8000U;
inline constexpr std::uint32_t kLightFlagAmbientOnly = 0x0010'0000U;

inline constexpr std::uint32_t kInheritAmbient = 0x01U;
inline constexpr std::uint32_t kInheritFogColours = 0x04U;
inline constexpr std::uint32_t kInheritFogNear = 0x08U;
inline constexpr std::uint32_t kInheritFogFar = 0x10U;

nlohmann::json lightJson(const fo4::env::LightRecordData& record);

nlohmann::json cellLightingJson(const std::optional<fo4::env::CellLighting>& own,
                                const std::optional<fo4::env::CellLighting>& templateLighting);

}  // namespace fo4ide::plugin
