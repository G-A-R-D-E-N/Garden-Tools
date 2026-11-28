#pragma once

#include <fo4/plugin/container.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4/signature.hpp>

#include <cstddef>
#include <exception>
#include <optional>
#include <span>

namespace fo4ide::plugin::fragmentread {

// Constraint: the extras span begins after the leading VMAD script list, or is empty when the record holds no readable VMAD tail.
inline std::optional<std::span<const std::byte>> vmad_extras(const fo4::Record& record) {
    const auto vmad = fo4::Signature::from_string("VMAD");
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != vmad) continue;
        const std::span<const std::byte> data(subrecord.data);
        fo4::VmadScripts scripts;
        try {
            scripts = fo4::read_vmad_scripts(data);
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (scripts.unsupported) return std::nullopt;
        if (scripts.trailing_bytes >= data.size()) return std::span<const std::byte>{};
        return data.subspan(data.size() - scripts.trailing_bytes);
    }
    return std::nullopt;
}

inline std::optional<fo4::InfoVmadExtras> read_info_fragments(const fo4::Record& record) {
    const auto extras = vmad_extras(record);
    if (!extras || extras->empty()) return std::nullopt;
    const auto vmad = fo4::Signature::from_string("VMAD");
    const fo4::Subrecord* vmad_subrecord = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == vmad) {
            vmad_subrecord = &subrecord;
            break;
        }
    }
    if (vmad_subrecord == nullptr) return std::nullopt;
    fo4::VmadScripts scripts;
    try {
        scripts = fo4::read_vmad_scripts(std::span<const std::byte>(vmad_subrecord->data));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    auto info = fo4::read_info_vmad_extras(*extras, scripts.object_format);
    if (info.unsupported) return std::nullopt;
    return info;
}

inline std::optional<fo4::SceneVmadExtras> read_scene_fragments(const fo4::Record& record) {
    const auto extras = vmad_extras(record);
    if (!extras || extras->empty()) return std::nullopt;
    const auto vmad = fo4::Signature::from_string("VMAD");
    const fo4::Subrecord* vmad_subrecord = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == vmad) {
            vmad_subrecord = &subrecord;
            break;
        }
    }
    if (vmad_subrecord == nullptr) return std::nullopt;
    fo4::VmadScripts scripts;
    try {
        scripts = fo4::read_vmad_scripts(std::span<const std::byte>(vmad_subrecord->data));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    auto scene = fo4::read_scene_vmad_extras(*extras, scripts.object_format);
    if (scene.unsupported) return std::nullopt;
    return scene;
}

}  // namespace fo4ide::plugin::fragmentread
