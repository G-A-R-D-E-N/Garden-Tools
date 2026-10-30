#include "fo4/nif/describe.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/vertex_desc.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

std::string texture_path_fault(const std::string& path) {
  if (path.size() >= 2 && path[1] == ':') return "is a drive-letter path";
  if (!path.empty() && (path.front() == '/' || path.front() == '\\')) return "is an absolute path";
  return {};
}

template <typename Probe>
VerifyCheck shape_check(std::string name, const std::vector<Shape>& shapes, Probe probe) {
  for (const auto& shape : shapes) {
    auto fault = probe(shape);
    if (!fault.empty()) {
      return {std::move(name), "shape '" + shape.name + "' (block " + std::to_string(shape.block) + ") " + fault};
    }
  }
  return {std::move(name), {}};
}

std::string parent_fault(const NifFile& nif) {
  std::string fault;
  for (const auto& node : nif.nodes()) {
    for (const auto child : node.children) {
      if (!fault.empty() || nif.parent(child) == static_cast<std::int32_t>(node.block)) continue;
      fault = "block " + std::to_string(child) + " is listed under node " + std::to_string(node.block) +
              " but its parent is " + std::to_string(nif.parent(child));
    }
  }
  return fault;
}

std::string editor_marker_fault(const NifFile& nif) {
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type != "BSXFlags") continue;
    try {
      if (!is_editor_marker(read_bsx_flags(nif, static_cast<std::uint32_t>(block)))) continue;
    } catch (const parse_error& error) {
      return "block " + std::to_string(block) + ": " + error.parse_message();
    }
    return "block " + std::to_string(block) + " sets the BSX editor-marker bit";
  }
  return {};
}

// Invariant: the stored offsets are the cumulative attribute widths, so a descriptor whose offsets and sizes disagree is reported.
// Invariant: the size nibble is compared with vertex_bytes_for, the reader's stride.
std::string descriptor_fault(const NifFile& nif) {
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (!is_shape_block_type(nif.blocks()[block].type)) continue;
    VertexDesc desc;
    try {
      desc = read_vertex_desc(nif, static_cast<std::uint32_t>(block));
    } catch (const parse_error&) {
      continue;
    }
    std::uint32_t expected = desc.attribute_bytes(VertexAttribute::Position);
    for (std::size_t index = 1; index < k_vertex_attribute_count; ++index) {
      const auto attribute = static_cast<VertexAttribute>(index);
      if (!desc.has(attribute)) continue;
      const auto width = desc.attribute_bytes(attribute);
      if (width == 0) continue;
      const auto offset = desc.attribute_offset(attribute);
      if (offset != expected) {
        return "block " + std::to_string(block) + " stores " + vertex_attribute_name(attribute) + " at " +
               std::to_string(offset) + " but its attributes put it at " + std::to_string(expected);
      }
      expected += width;
    }
    const auto filled = vertex_bytes_for(desc.flags());
    if (filled != desc.size_bytes()) {
      return "block " + std::to_string(block) + " declares a " + std::to_string(desc.size_bytes()) +
             "-byte vertex but its flags fill " + std::to_string(filled) + " bytes";
    }
  }
  return {};
}

std::vector<Shape> read_shapes_for_verify(const NifFile& nif, std::string& fault) {
  std::vector<Shape> shapes;
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (!is_shape_block_type(nif.blocks()[block].type)) continue;
    try {
      shapes.push_back(read_shape(nif, static_cast<std::uint32_t>(block)));
    } catch (const parse_error& error) {
      if (fault.empty()) {
        fault = "block " + std::to_string(block) + ": " + error.parse_message();
      }
    }
  }
  return shapes;
}

}  // namespace

VerifyReport verify_nif(const NifFile& nif) {
  VerifyReport report;
  report.checks.push_back({"header parses", {}});
  report.checks.push_back({"each block is listed under one parent only", parent_fault(nif)});
  report.checks.push_back({"no editor markers", editor_marker_fault(nif)});

  std::string fault;
  const auto shapes = read_shapes_for_verify(nif, fault);
  report.checks.push_back({"shapes parse", fault});
  report.checks.push_back({"vertex descriptor matches the data", descriptor_fault(nif)});

  report.checks.push_back(shape_check("shapes have vertices and triangles", shapes, [](const Shape& shape) -> std::string {
    if (shape.verts.empty() || shape.tris.empty()) return "has no vertices or triangles";
    return {};
  }));
  report.checks.push_back(shape_check("normals match the vertex count", shapes, [](const Shape& shape) -> std::string {
    if (!shape.normals.empty() && shape.normals.size() != shape.verts.size()) {
      return "has normals that do not match its vertex count";
    }
    return {};
  }));
  report.checks.push_back(shape_check("UVs match the vertex count", shapes, [](const Shape& shape) -> std::string {
    if (!shape.uvs.empty() && shape.uvs.size() != shape.verts.size() / 3 * 2) {
      return "has UVs that do not match its vertex count";
    }
    return {};
  }));
  report.checks.push_back(shape_check("shader and skin reads are clean", shapes, [](const Shape& shape) -> std::string {
    if (!shape.warning.empty()) return "reported: " + shape.warning;
    return {};
  }));
  report.checks.push_back(shape_check("texture paths are relative", shapes, [](const Shape& shape) -> std::string {
    for (const auto& path : shape.textures) {
      if (auto fault = texture_path_fault(path); !fault.empty()) return "texture '" + path + "' " + fault;
    }
    return {};
  }));
  return report;
}

}  // namespace fo4::nif
