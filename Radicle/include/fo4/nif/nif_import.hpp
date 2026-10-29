#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct ImportOptions {
  std::string material;
  std::string tex_diffuse;
  std::string tex_normal;
  bool box_collision{false};
  bool from_blender{false};
};

std::vector<std::byte> import_obj(std::string_view obj_text, const ImportOptions& options);

}
