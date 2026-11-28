#include "fo4/nif/outfit_studio.hpp"

#include "fo4/avm2/xml.hpp"
#include "fo4/error.hpp"
#include "shape_ops_internal.hpp"

#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fo4::nif {
namespace {

using fo4::avm2::XmlNode;

constexpr std::size_t k_osd_header_bytes = 12;
constexpr std::size_t k_osd_record_bytes = 14;
constexpr std::size_t k_osd_min_set_bytes = 3;
constexpr std::uint32_t k_osd_version = 1;
constexpr std::uint32_t k_osd_magic = 0x4F534400u;
constexpr std::string_view k_bom = "\xEF\xBB\xBF";

class OsdReader {
public:
  explicit OsdReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  std::size_t offset() const { return offset_; }
  std::size_t remaining() const { return bytes_.size() - offset_; }

  void need(std::uint64_t count) const {
    if (count > remaining()) throw parse_error(offset_, "osd file ends inside a block");
  }

  std::uint8_t u8() {
    need(1);
    return static_cast<std::uint8_t>(bytes_[offset_++]);
  }

  std::uint16_t u16() {
    need(2);
    const auto value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(at(0)) | (static_cast<std::uint16_t>(at(1)) << 8));
    offset_ += 2;
    return value;
  }

  std::uint32_t u32() {
    need(4);
    const auto value = static_cast<std::uint32_t>(at(0)) | (static_cast<std::uint32_t>(at(1)) << 8) |
                       (static_cast<std::uint32_t>(at(2)) << 16) | (static_cast<std::uint32_t>(at(3)) << 24);
    offset_ += 4;
    return value;
  }

  float f32() { return std::bit_cast<float>(u32()); }

  std::string text(std::size_t count) {
    need(count);
    std::string out(reinterpret_cast<const char*>(bytes_.data() + offset_), count);
    offset_ += count;
    return out;
  }

private:
  std::uint8_t at(std::size_t extra) const { return static_cast<std::uint8_t>(bytes_[offset_ + extra]); }

  std::span<const std::byte> bytes_;
  std::size_t offset_{0};
};

bool ascii_equal(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    auto a = left[i];
    auto b = right[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

std::size_t last_separator(std::string_view text) { return text.find_last_of("/\\"); }

std::string_view last_component(std::string_view path) {
  const auto at = last_separator(path);
  return at == std::string_view::npos ? path : path.substr(at + 1);
}

[[noreturn]] void osp_fail(const std::string& message) { throw parse_error(0, "osp: " + message); }

const std::string* attribute(const XmlNode& node, std::string_view name) {
  for (const auto& attr : node.attributes) {
    if (attr->name == name) return &attr->text;
  }
  return nullptr;
}

std::string required_attribute(const XmlNode& node, std::string_view name) {
  const auto* value = attribute(node, name);
  if (value == nullptr || value->empty()) osp_fail(node.name + " needs a non-empty " + std::string(name) + " attribute");
  return *value;
}

bool flag_attribute(const XmlNode& node, std::string_view name) {
  const auto* value = attribute(node, name);
  if (value == nullptr) return false;
  if (ascii_equal(*value, "true")) return true;
  if (ascii_equal(*value, "false")) return false;
  osp_fail(node.name + " attribute " + std::string(name) + " is neither true nor false");
}

float float_attribute(const XmlNode& node, std::string_view name) {
  const auto* value = attribute(node, name);
  if (value == nullptr) return 0.0f;
  errno = 0;
  char* end = nullptr;
  const double parsed = std::strtod(value->c_str(), &end);
  if (value->empty() || end != value->c_str() + value->size() || errno != 0 || !std::isfinite(parsed) ||
      std::fabs(parsed) > static_cast<double>(std::numeric_limits<float>::max())) {
    osp_fail(node.name + " attribute " + std::string(name) + " is not a finite number");
  }
  return static_cast<float>(parsed);
}

std::string element_text(const XmlNode& node) {
  std::string out;
  for (const auto& child : node.children) {
    if (child->kind == XmlNode::Kind::Text) out += child->text;
  }
  return out;
}

const XmlNode* first_child(const XmlNode& node, std::string_view name) {
  for (const auto& child : node.children) {
    if (child->kind == XmlNode::Kind::Element && child->name == name) return child.get();
  }
  return nullptr;
}

std::string child_text(const XmlNode& node, std::string_view name) {
  const auto* child = first_child(node, name);
  return child == nullptr ? std::string{} : element_text(*child);
}

OspData read_data(const XmlNode& node, const std::string& slider_name) {
  OspData data;
  data.target = required_attribute(node, "target");
  const auto* name = attribute(node, "name");
  data.name = name != nullptr ? *name : slider_name;
  data.local = flag_attribute(node, "local");
  const auto text = element_text(node);
  if (text.empty()) osp_fail("slider " + slider_name + " has a Data element with no file text");
  if (text.size() > 4 && ascii_equal(std::string_view(text).substr(text.size() - 4), ".bsd")) {
    data.legacy_bsd = true;
    data.file = text;
    return data;
  }
  const auto at = last_separator(text);
  if (at == std::string::npos) {
    osp_fail("slider " + slider_name + " Data text is neither <file>.osd\\<name> nor <name>.bsd");
  }
  data.file = text.substr(0, at);
  data.name_in_file = text.substr(at + 1);
  if (data.name_in_file.empty() || data.file.size() <= 4 ||
      !ascii_equal(std::string_view(data.file).substr(data.file.size() - 4), ".osd")) {
    osp_fail("slider " + slider_name + " Data text is neither <file>.osd\\<name> nor <name>.bsd");
  }
  return data;
}

OspSlider read_slider(const XmlNode& node) {
  OspSlider slider;
  slider.name = required_attribute(node, "name");
  slider.invert = flag_attribute(node, "invert");
  slider.hidden = flag_attribute(node, "hidden");
  slider.zap = flag_attribute(node, "zap");
  slider.uv = flag_attribute(node, "uv");
  slider.small_value = float_attribute(node, "small");
  slider.big_value = float_attribute(node, "big");
  for (const auto& child : node.children) {
    if (child->kind == XmlNode::Kind::Element && child->name == "Data") {
      slider.data.push_back(read_data(*child, slider.name));
    }
  }
  return slider;
}

OspSet read_set(const XmlNode& node) {
  OspSet set;
  set.name = required_attribute(node, "name");
  set.data_folder = child_text(node, "DataFolder");
  set.source_file = child_text(node, "SourceFile");
  set.output_path = child_text(node, "OutputPath");
  set.output_file = child_text(node, "OutputFile");
  for (const auto& child : node.children) {
    if (child->kind != XmlNode::Kind::Element) continue;
    if (child->name == "Shape") {
      OspShape shape;
      shape.name = element_text(*child);
      if (shape.name.empty()) osp_fail("set " + set.name + " has a Shape element with no name");
      shape.target = required_attribute(*child, "target");
      if (const auto* folder = attribute(*child, "DataFolder")) shape.data_folder = *folder;
      if (set.find_shape(shape.name) != nullptr) osp_fail("set " + set.name + " repeats shape " + shape.name);
      set.shapes.push_back(std::move(shape));
    } else if (child->name == "Slider") {
      auto slider = read_slider(*child);
      if (set.find_slider(slider.name) != nullptr) osp_fail("set " + set.name + " repeats slider " + slider.name);
      set.sliders.push_back(std::move(slider));
    }
  }
  return set;
}

}  // namespace

const OsdDataSet* OsdFile::find(std::string_view name) const {
  for (const auto& set : sets) {
    if (set.name == name) return &set;
  }
  return nullptr;
}

const OspSlider* OspSet::find_slider(std::string_view slider_name) const {
  for (const auto& slider : sliders) {
    if (slider.name == slider_name) return &slider;
  }
  return nullptr;
}

const OspShape* OspSet::find_shape(std::string_view shape_name) const {
  for (const auto& shape : shapes) {
    if (shape.name == shape_name) return &shape;
  }
  return nullptr;
}

const OspSet* OspFile::find_set(std::string_view set_name) const {
  for (const auto& set : sets) {
    if (set.name == set_name) return &set;
  }
  return nullptr;
}

OsdFile parse_osd(std::span<const std::byte> bytes) {
  OsdReader in(bytes);
  if (bytes.size() < k_osd_header_bytes) throw parse_error(0, "osd file is shorter than its header");
  if (in.u32() != k_osd_magic) throw parse_error(0, "not an osd file (magic 00 44 53 4F expected)");
  OsdFile file;
  file.version = in.u32();
  if (file.version != k_osd_version) throw parse_error(4, "only osd version 1 is read");
  const auto set_count = in.u32();
  if (set_count > in.remaining() / k_osd_min_set_bytes) throw parse_error(8, "osd set count is larger than the file");
  std::unordered_set<std::string> names;
  std::vector<std::uint32_t> stamp(std::size_t{1} << 16, 0);
  for (std::uint32_t index = 0; index < set_count; ++index) {
    OsdDataSet set;
    const auto name_at = in.offset();
    set.name = in.text(in.u8());
    if (!names.insert(set.name).second) throw parse_error(name_at, "osd file repeats data name " + set.name);
    const auto count = in.u16();
    in.need(static_cast<std::uint64_t>(count) * k_osd_record_bytes);
    set.offsets.reserve(count);
    for (std::uint16_t record = 0; record < count; ++record) {
      const auto record_at = in.offset();
      OsdOffset offset;
      offset.vertex = in.u16();
      for (auto& axis : offset.delta) {
        axis = in.f32();
        if (!std::isfinite(axis)) throw parse_error(record_at, "osd offset is not finite in " + set.name);
      }
      if (stamp[offset.vertex] == index + 1) throw parse_error(record_at, "osd data " + set.name + " repeats a vertex");
      stamp[offset.vertex] = index + 1;
      set.offsets.push_back(offset);
    }
    file.sets.push_back(std::move(set));
  }
  if (in.remaining() != 0) throw parse_error(in.offset(), "osd file has bytes after its last data set");
  return file;
}

OspFile parse_osp(std::string_view text) {
  if (text.substr(0, k_bom.size()) == k_bom) text.remove_prefix(k_bom.size());
  std::vector<std::shared_ptr<XmlNode>> roots;
  std::string message;
  if (!fo4::avm2::parse_xml(text, false, roots, message)) osp_fail(message);
  if (roots.size() != 1 || roots[0]->kind != XmlNode::Kind::Element) osp_fail("no root element");
  const XmlNode& root = *roots[0];
  if (root.name != "SliderSetInfo") osp_fail("the root element is " + root.name + ", not SliderSetInfo");
  const auto* version = attribute(root, "version");
  if (version == nullptr || *version != "1") {
    osp_fail("only SliderSetInfo version 1 is read, found " + (version == nullptr ? std::string("none") : *version));
  }
  OspFile file;
  file.version = 1;
  for (const auto& child : root.children) {
    if (child->kind != XmlNode::Kind::Element || child->name != "SliderSet") continue;
    auto set = read_set(*child);
    if (file.find_set(set.name) != nullptr) osp_fail("the file repeats set " + set.name);
    file.sets.push_back(std::move(set));
  }
  if (file.sets.empty()) osp_fail("the file holds no SliderSet");
  return file;
}

const OsdDataSet* find_osd_diff(const OsdFile& osd, std::string_view osd_file_name, const OspSet& set,
                                std::string_view slider_name, std::string_view shape_name) {
  const auto* slider = set.find_slider(slider_name);
  const auto* shape = set.find_shape(shape_name);
  if (slider == nullptr || shape == nullptr) return nullptr;
  const auto wanted = last_component(osd_file_name);
  for (const auto& data : slider->data) {
    if (data.legacy_bsd || data.target != shape->target) continue;
    if (!ascii_equal(last_component(data.file), wanted)) continue;
    if (const auto* diff = osd.find(data.name_in_file)) return diff;
  }
  return nullptr;
}

std::optional<std::string> apply_osd_diff(std::span<float> positions, const OsdDataSet& diff, float weight) {
  if (positions.size() % 3 != 0) return "positions are not 3 floats per vertex";
  if (!std::isfinite(weight)) return "the weight is not finite";
  const std::size_t vertices = positions.size() / 3;
  for (const auto& offset : diff.offsets) {
    if (offset.vertex >= vertices) {
      return "vertex " + std::to_string(offset.vertex) + " is past the " + std::to_string(vertices) + " positions";
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const float scaled = offset.delta[axis] * weight;
      if (!std::isfinite(scaled)) return "a weighted offset is not finite";
      if (!std::isfinite(positions[std::size_t{offset.vertex} * 3 + axis] + scaled)) {
        return "a position moved by the weight is not finite";
      }
    }
  }
  for (const auto& offset : diff.offsets) {
    for (std::size_t axis = 0; axis < 3; ++axis) positions[std::size_t{offset.vertex} * 3 + axis] += offset.delta[axis] * weight;
  }
  return std::nullopt;
}

std::vector<std::byte> apply_osd_to_shape(const NifFile& nif, std::uint32_t block, const OsdDataSet& diff, float weight,
                                          std::size_t* moved) {
  if (moved != nullptr) *moved = 0;
  if (block >= nif.blocks().size() || !is_shape_block_type(nif.blocks()[block].type)) {
    throw std::invalid_argument("block " + std::to_string(block) + " is not a tri-shape");
  }
  const auto shape = read_shape(nif, block);
  if (shape.verts.empty()) throw std::invalid_argument("the shape has no vertex data");
  if (nif.blocks()[block].type == "BSDynamicTriShape") {
    throw std::invalid_argument("BSDynamicTriShape keeps dynamic vertices that a slider write would leave stale");
  }
  const auto layout = shape_detail::read_layout(nif, block);
  if (!layout.full) throw std::invalid_argument("half-precision positions have no exact write-back");
  auto positions = shape.verts;
  if (const auto refusal = apply_osd_diff(positions, diff, weight)) throw std::invalid_argument(*refusal);
  std::size_t changed = 0;
  for (std::size_t v = 0; v < positions.size() / 3; ++v) {
    if (positions[3 * v] != shape.verts[3 * v] || positions[3 * v + 1] != shape.verts[3 * v + 1] ||
        positions[3 * v + 2] != shape.verts[3 * v + 2]) {
      ++changed;
    }
  }
  if (moved != nullptr) *moved = changed;
  if (changed == 0) return nif.bytes();
  return shape_detail::rewrite_positions(nif, block, layout, shape_detail::to_vec3(positions));
}

}  // namespace fo4::nif
