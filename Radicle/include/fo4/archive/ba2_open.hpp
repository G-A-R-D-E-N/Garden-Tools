#pragma once

#include <filesystem>
#include <variant>

#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_texture.hpp"

namespace fo4::archive {

using AnyBa2Archive = std::variant<Ba2GeneralArchive, Ba2TextureArchive>;

AnyBa2Archive open_ba2(const std::filesystem::path& path);

}
