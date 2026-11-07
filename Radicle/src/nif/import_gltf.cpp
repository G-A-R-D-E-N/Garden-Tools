#include "fo4/nif/nif_import.hpp"

#include "fo4/nif/nif.hpp"
#include "gltf_read.hpp"
#include "import_gltf_build.hpp"
#include "import_gltf_scene.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace fo4::nif {

std::vector<std::byte> import_gltf(std::span<const std::byte> bytes, const ImportOptions& options,
                                   const GltfUriLoader& load_uri) {
  try {
    const auto doc = gltf_detail::open_document(bytes, load_uri);
    std::optional<NifFile> skeleton;
    import_detail::BoneNames bones;
    if (!options.skeleton.empty()) {
      skeleton.emplace(read_nif(options.skeleton));
      for (const auto& node : skeleton->nodes()) bones.insert(node.name);
    }
    const auto scene = import_detail::read_scene(doc, options, skeleton ? &bones : nullptr);
    return import_detail::build_gltf_nif(scene, skeleton ? &*skeleton : nullptr, options.box_collision);
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error(std::string("glTF JSON is not valid: ") + error.what());
  }
}

}
