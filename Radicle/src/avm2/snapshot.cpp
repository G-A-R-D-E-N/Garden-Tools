#include <fo4/avm2/snapshot.hpp>

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/text.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

constexpr double kPi = 3.14159265358979323846;

SnapshotMatrix local_matrix(const DisplayObject& object) {
  const double radians = object.rotation() * kPi / 180.0;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  SnapshotMatrix matrix;
  matrix.a = cosine * object.scale_x();
  matrix.b = sine * object.scale_x();
  matrix.c = -sine * object.scale_y();
  matrix.d = cosine * object.scale_y();
  matrix.tx = object.x();
  matrix.ty = object.y();
  return matrix;
}

SnapshotMatrix compose(const SnapshotMatrix& parent, const SnapshotMatrix& child) {
  SnapshotMatrix result;
  result.a = parent.a * child.a + parent.c * child.b;
  result.b = parent.b * child.a + parent.d * child.b;
  result.c = parent.a * child.c + parent.c * child.d;
  result.d = parent.b * child.c + parent.d * child.d;
  result.tx = parent.a * child.tx + parent.c * child.ty + parent.tx;
  result.ty = parent.b * child.tx + parent.d * child.ty + parent.ty;
  return result;
}

SnapshotEntryKind entry_kind(const DisplayObject& object) {
  if (object.is_text_field()) {
    return SnapshotEntryKind::Text;
  }
  const Class* klass = object.display_class();
  if (klass != nullptr) {
    if (klass->name.find("Bitmap") != std::string::npos) {
      return SnapshotEntryKind::Bitmap;
    }
    if (klass->name.find("Shape") != std::string::npos) {
      return SnapshotEntryKind::Shape;
    }
  }
  return SnapshotEntryKind::DisplayObject;
}

SnapshotEntry make_entry(const DisplayObject& object, const std::string& path,
                         const SnapshotMatrix& matrix) {
  SnapshotEntry entry;
  entry.kind = entry_kind(object);
  entry.character_id = object.character_id();
  entry.clip_depth = object.clip_depth();
  entry.blend_mode = object.blend_mode();
  entry.visible = object.visible();
  entry.alpha = object.alpha();
  entry.path = path;
  entry.name = object.name();
  entry.matrix = matrix;
  entry.color.alpha_multiplier = object.alpha();
  if (object.is_text_field()) {
    const auto& text_field = static_cast<const TextFieldObject&>(object);
    for (const TextRun& run : text_field.runs()) {
      SnapshotTextRun snapshot_run;
      snapshot_run.font_id = run.font_id;
      snapshot_run.font_size = run.font_size;
      snapshot_run.x = run.x;
      snapshot_run.y = run.y;
      snapshot_run.color = run.color;
      for (const GlyphPlacement& glyph : run.glyphs) {
        SnapshotGlyph snapshot_glyph;
        snapshot_glyph.font_id = run.font_id;
        snapshot_glyph.glyph_id = glyph.glyph_id;
        snapshot_glyph.x = glyph.x;
        snapshot_glyph.advance = glyph.advance;
        snapshot_run.glyphs.push_back(snapshot_glyph);
      }
      entry.runs.push_back(std::move(snapshot_run));
    }
  }
  return entry;
}

void walk(const DisplayObject& object, const std::string& path, const SnapshotMatrix& parent,
          bool root, std::vector<SnapshotEntry>& entries) {
  const SnapshotMatrix matrix = root ? local_matrix(object) : compose(parent, local_matrix(object));
  entries.push_back(make_entry(object, path, matrix));
  if (!object.is_container()) {
    return;
  }
  const auto& container = static_cast<const DisplayObjectContainer&>(object);
  for (std::size_t index = 0; index < container.children().size(); ++index) {
    const auto& child = container.children()[index];
    if (child == nullptr) {
      continue;
    }
    walk(*child, path + "/" + std::to_string(index), matrix, false, entries);
  }
}

class Hasher {
 public:
  void bytes(const void* data, std::size_t size) {
    const auto* raw = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
      hash_ ^= raw[index];
      hash_ *= 1099511628211ULL;
    }
  }
  void u8(std::uint8_t value) { bytes(&value, sizeof(value)); }
  void u16(std::uint16_t value) { bytes(&value, sizeof(value)); }
  void u32(std::uint32_t value) { bytes(&value, sizeof(value)); }
  void i32(std::int32_t value) { bytes(&value, sizeof(value)); }
  void f64(double value) { bytes(&value, sizeof(value)); }
  void text(const std::string& value) {
    u32(static_cast<std::uint32_t>(value.size()));
    bytes(value.data(), value.size());
  }
  std::uint64_t value() const noexcept { return hash_; }

 private:
  std::uint64_t hash_{1469598103934665603ULL};
};

}  // namespace

FrameSnapshot build_frame_snapshot(const DisplayObject& root) {
  FrameSnapshot snapshot;
  walk(root, std::string{}, SnapshotMatrix{}, true, snapshot.entries);
  return snapshot;
}

std::uint64_t snapshot_hash(const FrameSnapshot& snapshot) {
  Hasher hasher;
  hasher.u32(static_cast<std::uint32_t>(snapshot.entries.size()));
  for (const SnapshotEntry& entry : snapshot.entries) {
    hasher.u8(static_cast<std::uint8_t>(entry.kind));
    hasher.u32(entry.character_id);
    hasher.i32(entry.clip_depth);
    hasher.u8(entry.blend_mode);
    hasher.u8(entry.visible ? 1 : 0);
    hasher.f64(entry.alpha);
    hasher.text(entry.path);
    hasher.text(entry.name);
    hasher.f64(entry.matrix.a);
    hasher.f64(entry.matrix.b);
    hasher.f64(entry.matrix.c);
    hasher.f64(entry.matrix.d);
    hasher.f64(entry.matrix.tx);
    hasher.f64(entry.matrix.ty);
    hasher.f64(entry.color.red_multiplier);
    hasher.f64(entry.color.green_multiplier);
    hasher.f64(entry.color.blue_multiplier);
    hasher.f64(entry.color.alpha_multiplier);
    hasher.i32(entry.color.red_offset);
    hasher.i32(entry.color.green_offset);
    hasher.i32(entry.color.blue_offset);
    hasher.i32(entry.color.alpha_offset);
    hasher.u32(static_cast<std::uint32_t>(entry.runs.size()));
    for (const SnapshotTextRun& run : entry.runs) {
      hasher.u16(run.font_id);
      hasher.f64(run.font_size);
      hasher.f64(run.x);
      hasher.f64(run.y);
      hasher.u32(run.color);
      hasher.u32(static_cast<std::uint32_t>(run.glyphs.size()));
      for (const SnapshotGlyph& glyph : run.glyphs) {
        hasher.u16(glyph.font_id);
        hasher.u16(glyph.glyph_id);
        hasher.f64(glyph.x);
        hasher.f64(glyph.advance);
      }
    }
  }
  hasher.u32(static_cast<std::uint32_t>(snapshot.sounds.size()));
  for (const SnapshotSoundEvent& sound : snapshot.sounds) {
    hasher.u8(static_cast<std::uint8_t>(sound.kind));
    hasher.u16(sound.sound_id);
    hasher.f64(sound.volume);
    hasher.u32(sound.loops);
  }
  return hasher.value();
}

void SoundEventQueue::append_to(FrameSnapshot& snapshot) {
  snapshot.sounds.insert(snapshot.sounds.end(), events_.begin(), events_.end());
  events_.clear();
}

}  // namespace fo4::avm2
