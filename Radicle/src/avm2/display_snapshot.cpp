#include <fo4/avm2/display_snapshot.hpp>

#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/text.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fo4::avm2 {

namespace {

constexpr double kFixedScale = 10000.0;
constexpr double kFixedLimit = 9.0e18;

std::string fixed(double value) {
  const double scaled = value * kFixedScale;
  if (std::isnan(scaled)) {
    return "nan";
  }
  if (!(std::fabs(scaled) < kFixedLimit)) {
    return scaled < 0.0 ? "-inf" : "inf";
  }
  return std::to_string(std::llround(scaled));
}

void append_node(const DisplayObject& object, std::size_t level, std::size_t depth,
                 const SnapshotMatrix& parent, std::string& out) {
  const SnapshotMatrix matrix = compose_matrix(parent, local_matrix(object));
  const Class* klass = object.display_class();
  const std::size_t child_count =
      object.is_container() ? static_cast<const DisplayObjectContainer&>(object).children().size()
                            : 0;
  out.append(level * 2, ' ');
  out += "class=" + (klass == nullptr ? std::string{} : klass->name);
  out += " depth=" + std::to_string(depth);
  out += " id=" + std::to_string(object.character_id());
  out += " bounds=" + fixed(object.origin_x()) + " " + fixed(object.origin_y()) + " " +
         fixed(object.origin_x() + object.width()) + " " +
         fixed(object.origin_y() + object.height());
  out += " matrix=" + fixed(matrix.a) + " " + fixed(matrix.b) + " " + fixed(matrix.c) + " " +
         fixed(matrix.d) + " " + fixed(matrix.tx) + " " + fixed(matrix.ty);
  out += object.visible() ? " visible=1" : " visible=0";
  out += " alpha=" + fixed(object.alpha());
  out += " children=" + std::to_string(child_count);
  if (object.is_text_field()) {
    const std::string& text = static_cast<const TextFieldObject&>(object).text();
    out += " text=" + std::to_string(text.size()) + ":" + text;
  }
  out += '\n';
  if (!object.is_container()) {
    return;
  }
  const auto& container = static_cast<const DisplayObjectContainer&>(object);
  for (std::size_t index = 0; index < container.children().size(); ++index) {
    const auto& child = container.children()[index];
    if (child != nullptr) {
      append_node(*child, level + 1, index, matrix, out);
    }
  }
}

}  // namespace

std::string display_snapshot_text(const DisplayObject& root) {
  std::string out;
  append_node(root, 0, 0, SnapshotMatrix{}, out);
  return out;
}

std::uint64_t fnv1a64(std::string_view bytes) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char byte : bytes) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::uint64_t display_snapshot_hash(const DisplayObject& root) {
  return fnv1a64(display_snapshot_text(root));
}

}  // namespace fo4::avm2
