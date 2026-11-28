#include "ragdoll_capsule.hpp"

#include "fo4/hkx/layout.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace fo4::hkx::detail {
namespace {

using V3 = std::array<double, 3>;

constexpr std::string_view kCapsuleClass = "hknpCapsuleShape";
constexpr std::string_view kMassClass = "hknpShapeMassProperties";
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kVertexCount = 8;
constexpr std::size_t kPlaneCount = 8;
constexpr std::size_t kRealPlanes = 6;
constexpr std::size_t kFaceCount = 6;
constexpr std::size_t kIndexCount = 24;
constexpr std::size_t kRowStride = 16;
constexpr std::size_t kPlaneOffsetInRow = 12;
constexpr double kPadDivisor = 99.0;
constexpr double kMinLength = 1.0e-3;
constexpr double kMinRadius = 1.0e-3;
constexpr double kMaxExtent = 1.0e6;
constexpr double kDirectionTolerance = 1.0e-5;
constexpr double kFrameTolerance = 1.0e-4;
constexpr double kNormalTolerance = 2.0e-4;
constexpr double kPointTolerance = 2.0e-6;
constexpr double kVolumeTolerance = 1.0e-6;
constexpr double kLaneSlack = 1.5;
constexpr double kLaneSeparation = 2.0 * kLaneSlack + 1.0;
constexpr int kLaneLimit = 32767;
constexpr int kCentreWordBase = 14208;
constexpr int kInertiaWordBase = 14080;
constexpr int kExponentStep = 128;
constexpr int kExponentRange = 100;
constexpr std::size_t kCentreAt = 0;
constexpr std::size_t kInertiaAt = 8;
constexpr std::size_t kMassAt = 24;
constexpr std::size_t kVolumeAt = 28;
constexpr std::size_t kMassBlockBytes = 32;

V3 sub(const V3& x, const V3& y) { return {x[0] - y[0], x[1] - y[1], x[2] - y[2]}; }
V3 add(const V3& x, const V3& y) { return {x[0] + y[0], x[1] + y[1], x[2] + y[2]}; }
V3 scaled(const V3& x, double s) { return {x[0] * s, x[1] * s, x[2] * s}; }
double dot(const V3& x, const V3& y) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; }
V3 cross(const V3& x, const V3& y) {
  return {x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
}
double length_of(const V3& x) { return std::sqrt(dot(x, x)); }
V3 widen(const HkxVec3& v) { return {v[0], v[1], v[2]}; }
HkxVec3 narrow(const V3& v) { return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])}; }

double round_away(double x) { return x < 0.0 ? -std::floor(-x + 0.5) : std::floor(x + 0.5); }

bool finite3(const HkxVec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

CapsuleRefusal unsupported(std::string reason) { return CapsuleRefusal{false, std::move(reason)}; }

std::optional<std::size_t> offset_of(std::string_view class_name, std::string_view member) {
  const HkxClassLayout* layout = find_layout(class_name);
  if (!layout) return std::nullopt;
  const auto offset = find_member_offset(*layout, member);
  if (!offset) return std::nullopt;
  return static_cast<std::size_t>(*offset);
}

std::optional<std::size_t> object_at(const HkxPackfile& pack, std::size_t offset) {
  const auto found = std::lower_bound(pack.objects.begin(), pack.objects.end(), offset,
                                      [](const HkxObjectView& object, std::size_t value) { return object.offset < value; });
  if (found == pack.objects.end() || found->offset != offset) return std::nullopt;
  return static_cast<std::size_t>(found - pack.objects.begin());
}

// Requires the payload of a hkRelArray to lie inside the object's own bytes and the stored count to equal the proven one.
std::optional<std::size_t> rel_rows(const HkxObjectView& object, std::size_t field, std::size_t stride, std::size_t expected) {
  if (field > object.data.size() || object.data.size() - field < 4) return std::nullopt;
  const std::size_t count = value_at<std::uint16_t>(object.data, field);
  const std::size_t at = field + value_at<std::uint16_t>(object.data, field + 2);
  if (count != expected || at > object.data.size() || expected * stride > object.data.size() - at) return std::nullopt;
  return at;
}

std::size_t pointers_to(const HkxSection& section, std::size_t target) {
  std::size_t count = 0;
  for (const HkxLocalFixup& fixup : section.local_fixups) {
    if (fixup.destination == target) ++count;
  }
  for (const HkxRemoteFixup& fixup : section.global_fixups) {
    if (fixup.destination == target) ++count;
  }
  return count;
}

struct Stored {
  std::size_t a_at{0};
  std::size_t b_at{0};
  std::size_t radius_at{0};
  std::array<std::size_t, kVertexCount> vertex_at{};
  std::array<std::size_t, kRealPlanes> plane_at{};
  std::size_t mass_at{0};
  bool shared{false};
  V3 a{};
  V3 b{};
  double radius{0.0};
  std::array<V3, kVertexCount> vertices{};
  std::array<V3, kRealPlanes> normals{};
  std::array<double, kRealPlanes> plane_w{};
  std::array<std::int16_t, 4> centre_words{};
  std::array<std::int16_t, 4> inertia_words{};
  float mass{0.0f};
  float volume{0.0f};
};

std::optional<CapsuleRefusal> read_stored(const HkxPackfile& pack, std::size_t index, Stored& out) {
  if (index >= pack.objects.size() || pack.objects[index].class_name != kCapsuleClass) {
    return unsupported("shape " + std::to_string(index) + " is not a capsule");
  }
  if (pack.pointer_size != 8 || pack.data_section >= pack.sections.size()) {
    return unsupported("the packfile layout is not the proven 8-byte layout");
  }
  const HkxSection& section = pack.sections[pack.data_section];
  const HkxObjectView& object = pack.objects[index];

  const HkxClassLayout* capsule = find_layout(kCapsuleClass);
  const HkxClassLayout* face = find_layout("hknpConvexPolytopeShapeFace");
  const HkxClassLayout* properties = find_layout("hkRefCountedProperties");
  const HkxClassLayout* entry = find_layout("hkRefCountedPropertiesEntry");
  const HkxClassLayout* mass_layout = find_layout(kMassClass);
  if (!capsule || !face || !properties || !entry || !mass_layout) {
    return unsupported("the capsule, polytope or mass property layouts are missing from the proven tables");
  }
  const auto a_off = find_member_offset(*capsule, "a");
  const auto b_off = find_member_offset(*capsule, "b");
  const auto entries_off = find_member_offset(*properties, "entries");
  const auto object_off = find_member_offset(*entry, "object");
  const auto compressed_off = find_member_offset(*mass_layout, "compressedMassProperties");
  const auto radius_off = offset_of("hknpShape", "convexRadius");
  const auto properties_off = offset_of("hknpShape", "properties");
  const auto vertices_off = offset_of("hknpConvexShape", "vertices");
  const auto planes_off = offset_of("hknpConvexPolytopeShape", "planes");
  const auto faces_off = offset_of("hknpConvexPolytopeShape", "faces");
  const auto indices_off = offset_of("hknpConvexPolytopeShape", "indices");
  if (!a_off || !b_off || !entries_off || !object_off || !compressed_off || !radius_off || !properties_off || !vertices_off ||
      !planes_off || !faces_off || !indices_off) {
    return unsupported("a capsule or mass property member is missing from the proven layout tables");
  }
  if (!holds_layout(object, *capsule)) return unsupported("the capsule object is shorter than its layout");

  const auto vertex_rows = rel_rows(object, *vertices_off, kRowStride, kVertexCount);
  const auto plane_rows = rel_rows(object, *planes_off, kRowStride, kPlaneCount);
  const auto face_rows = rel_rows(object, *faces_off, face->size, kFaceCount);
  const auto index_rows = rel_rows(object, *indices_off, 1, kIndexCount);
  if (!vertex_rows || !plane_rows || !face_rows || !index_rows) {
    return unsupported("the polytope is not 8 vertices, 8 planes, 6 faces and 24 indices, the only form proven on vanilla data");
  }

  out.a_at = object.offset + *a_off;
  out.b_at = object.offset + *b_off;
  out.radius_at = object.offset + *radius_off;
  for (std::size_t lane = 0; lane < 3; ++lane) {
    out.a[lane] = value_at<float>(object.data, *a_off + lane * 4);
    out.b[lane] = value_at<float>(object.data, *b_off + lane * 4);
  }
  out.radius = value_at<float>(object.data, *radius_off);
  for (std::size_t i = 0; i < kVertexCount; ++i) {
    const std::size_t row = *vertex_rows + i * kRowStride;
    out.vertex_at[i] = object.offset + row;
    for (std::size_t lane = 0; lane < 3; ++lane) out.vertices[i][lane] = value_at<float>(object.data, row + lane * 4);
  }
  for (std::size_t i = 0; i < kRealPlanes; ++i) {
    const std::size_t row = *plane_rows + i * kRowStride;
    out.plane_at[i] = object.offset + row;
    for (std::size_t lane = 0; lane < 3; ++lane) out.normals[i][lane] = value_at<float>(object.data, row + lane * 4);
    out.plane_w[i] = value_at<float>(object.data, row + kPlaneOffsetInRow);
  }

  const auto properties_target = pointer_target(pack, section, object.offset + *properties_off);
  if (!properties_target) return unsupported("the capsule has no mass properties object");
  Elements entries;
  if (elements_at(section, *properties_target + *entries_off, pack.pointer_size, entry->size, entries) != HkxStatus::Ok ||
      entries.count != 1) {
    return unsupported("the capsule's mass property list does not hold exactly one entry");
  }
  const auto mass_target = pointer_target(pack, section, entries.first + *object_off);
  if (!mass_target) return unsupported("the capsule's mass property entry has no object");
  const auto mass_index = object_at(pack, *mass_target);
  if (!mass_index || pack.objects[*mass_index].class_name != kMassClass ||
      !holds_layout(pack.objects[*mass_index], *mass_layout)) {
    return unsupported("the capsule's mass property entry is not an hknpShapeMassProperties object");
  }
  const HkxObjectView& mass = pack.objects[*mass_index];
  if (*compressed_off > mass.data.size() || mass.data.size() - *compressed_off < kMassBlockBytes) {
    return unsupported("the mass properties object is shorter than the compressed block");
  }
  out.mass_at = mass.offset + *compressed_off;
  const std::span<const std::byte> block = mass.data.subspan(*compressed_off, kMassBlockBytes);
  for (std::size_t i = 0; i < 4; ++i) {
    out.centre_words[i] = value_at<std::int16_t>(block, kCentreAt + i * 2);
    out.inertia_words[i] = value_at<std::int16_t>(block, kInertiaAt + i * 2);
  }
  out.mass = value_at<float>(block, kMassAt);
  out.volume = value_at<float>(block, kVolumeAt);
  out.shared = pointers_to(section, *properties_target) > 1 || pointers_to(section, *mass_target) > 1;
  return std::nullopt;
}

struct Geometry {
  double length{0.0};
  double half{0.0};
  double volume{0.0};
  V3 axis{};
  V3 centre{};
  std::array<V3, kVertexCount> vertices{};
  std::array<double, kRealPlanes> plane_w{};
};

// Requires a positive length; the frame e1, e2 is the one stored with the capsule, e0 follows the endpoints.
Geometry geometry_for(const V3& a, const V3& b, double radius, const V3& e1, const V3& e2,
                      const std::array<V3, kRealPlanes>& normals) {
  Geometry g;
  const V3 ab = sub(b, a);
  g.length = length_of(ab);
  g.axis = scaled(ab, 1.0 / g.length);
  g.centre = scaled(add(a, b), 0.5);
  const double pad = radius / kPadDivisor;
  g.half = g.length * 0.5 + pad;
  const V3 e0 = scaled(g.axis, -1.0);
  for (std::size_t i = 0; i < kVertexCount; ++i) {
    V3 point = g.centre;
    point = add(point, scaled(e0, (i & 1U) != 0 ? g.half : -g.half));
    point = add(point, scaled(e1, (i & 2U) != 0 ? pad : -pad));
    point = add(point, scaled(e2, (i & 4U) != 0 ? pad : -pad));
    g.vertices[i] = point;
  }
  const std::array<double, kRealPlanes> extent{pad, pad, pad, pad, g.half, g.half};
  for (std::size_t i = 0; i < kRealPlanes; ++i) g.plane_w[i] = -(dot(normals[i], g.centre) + extent[i]);
  g.volume = kPi * radius * radius * g.length + 4.0 / 3.0 * kPi * radius * radius * radius;
  return g;
}

// Constraint: inertia of a solid capsule of the given mass is cylinder plus two hemispheres, about the axis and about a transverse axis.
std::array<double, 2> capsule_inertia(double mass, double volume, double radius, double length) {
  const double density = mass / volume;
  const double cylinder = density * kPi * radius * radius * length;
  const double spheres = density * 4.0 / 3.0 * kPi * radius * radius * radius;
  const double axial = cylinder * radius * radius / 2.0 + spheres * 2.0 / 5.0 * radius * radius;
  const double transverse = cylinder * (length * length / 12.0 + radius * radius / 4.0) +
                            spheres * (2.0 / 5.0 * radius * radius + length * length / 4.0 + 3.0 * radius * length / 8.0);
  return {axial, transverse};
}

std::optional<int> exponent_of(std::int16_t word, int base) {
  const int delta = base - static_cast<int>(word);
  if (delta % kExponentStep != 0) return std::nullopt;
  const int exponent = delta / kExponentStep;
  if (exponent < -kExponentRange || exponent > kExponentRange) return std::nullopt;
  return exponent;
}

// Constraint: the largest exponent whose rounded lanes all fit in an int16 is chosen; nullopt for an all-zero input or an exponent out of range.
std::optional<int> best_exponent(const V3& values) {
  const double biggest = std::max({std::fabs(values[0]), std::fabs(values[1]), std::fabs(values[2])});
  if (!(biggest > 0.0) || !std::isfinite(biggest)) return std::nullopt;
  int exponent = static_cast<int>(std::floor(std::log2(kLaneLimit + 0.5) - std::log2(biggest)));
  const auto fits = [&](int e) {
    for (const double value : values) {
      if (std::fabs(round_away(std::ldexp(value, e))) > kLaneLimit) return false;
    }
    return true;
  };
  while (exponent > -kExponentRange && !fits(exponent)) --exponent;
  while (exponent < kExponentRange && fits(exponent + 1)) ++exponent;
  if (!fits(exponent) || exponent < -kExponentRange || exponent > kExponentRange) return std::nullopt;
  return exponent;
}

struct Decoded {
  int centre_exponent{0};
  int inertia_exponent{0};
  std::size_t axial{0};
  std::array<double, 3> lanes{};
  V3 e1{};
  V3 e2{};
};

CapsuleRefusal mismatch(const std::string& what, double value) {
  return unsupported(what + " differs from the capsule model by " + std::to_string(value) +
                     ", so the stored polytope or mass data is not the proven form");
}

std::optional<CapsuleRefusal> verify_stored(const Stored& s, Decoded& out) {
  const V3 ab = sub(s.b, s.a);
  const double length = length_of(ab);
  if (!(length >= kMinLength) || !(s.radius >= kMinRadius) || !std::isfinite(length) || !std::isfinite(s.radius)) {
    return unsupported("the capsule endpoints coincide or its radius is not positive");
  }
  const V3 axis = scaled(ab, 1.0 / length);
  const double pad = s.radius / kPadDivisor;
  const V3 e1 = scaled(sub(s.vertices[2], s.vertices[0]), 0.5 / pad);
  const V3 e2 = scaled(sub(s.vertices[4], s.vertices[0]), 0.5 / pad);
  const V3 e0 = scaled(axis, -1.0);
  const std::array<double, 6> frame_error{std::fabs(length_of(e1) - 1.0), std::fabs(length_of(e2) - 1.0),
                                          std::fabs(dot(e1, axis)),       std::fabs(dot(e2, axis)),
                                          std::fabs(dot(e1, e2)),         length_of(sub(e2, cross(axis, e1)))};
  for (const double error : frame_error) {
    if (!(error <= kFrameTolerance)) return mismatch("the stored vertex frame", error);
  }
  const std::array<V3, kRealPlanes> wanted{e1, scaled(e2, -1.0), e2, scaled(e1, -1.0), e0, scaled(e0, -1.0)};
  for (std::size_t i = 0; i < kRealPlanes; ++i) {
    const double error = length_of(sub(s.normals[i], wanted[i]));
    if (!(error <= kNormalTolerance)) return mismatch("plane " + std::to_string(i) + " normal", error);
  }

  const Geometry g = geometry_for(s.a, s.b, s.radius, e1, e2, s.normals);
  const double scale = std::max({1.0, g.half, length_of(g.centre)});
  for (std::size_t i = 0; i < kVertexCount; ++i) {
    const double error = length_of(sub(s.vertices[i], g.vertices[i])) / scale;
    if (!(error <= kPointTolerance)) return mismatch("vertex " + std::to_string(i), error);
  }
  for (std::size_t i = 0; i < kRealPlanes; ++i) {
    const double error = std::fabs(s.plane_w[i] - g.plane_w[i]) / scale;
    if (!(error <= kPointTolerance)) return mismatch("plane " + std::to_string(i) + " offset", error);
  }
  if (!(s.mass > 0.0f) || !std::isfinite(s.mass) || !std::isfinite(s.volume)) {
    return unsupported("the stored mass is not a positive finite number");
  }
  const double volume_error = std::fabs(static_cast<double>(s.volume) - g.volume) / g.volume;
  if (!(volume_error <= kVolumeTolerance)) return mismatch("the stored volume", volume_error);

  const auto centre_exponent = exponent_of(s.centre_words[3], kCentreWordBase);
  const auto inertia_exponent = exponent_of(s.inertia_words[3], kInertiaWordBase);
  if (!centre_exponent || !inertia_exponent) return unsupported("a compressed mass exponent word is not of the proven form");
  for (std::size_t lane = 0; lane < 3; ++lane) {
    const double error = std::fabs(std::ldexp(static_cast<double>(s.centre_words[lane]), -*centre_exponent) - g.centre[lane]);
    if (!(error <= kLaneSlack * std::ldexp(1.0, -*centre_exponent))) return mismatch("the stored centre of mass", error);
  }
  const auto inertia = capsule_inertia(s.mass, g.volume, s.radius, g.length);
  std::size_t matches = 0;
  std::size_t axial = 0;
  for (std::size_t candidate = 0; candidate < 3; ++candidate) {
    bool all = true;
    for (std::size_t lane = 0; lane < 3; ++lane) {
      const double expected = std::ldexp(3.0 * (lane == candidate ? inertia[0] : inertia[1]), *inertia_exponent);
      if (!(std::fabs(static_cast<double>(s.inertia_words[lane]) - expected) <= kLaneSlack)) all = false;
    }
    if (all) {
      ++matches;
      axial = candidate;
    }
  }
  if (matches != 1) {
    return unsupported(matches == 0 ? "the stored inertia lanes do not match a solid capsule of the stored mass"
                                    : "the stored inertia lanes do not identify one axial lane");
  }
  out.centre_exponent = *centre_exponent;
  out.inertia_exponent = *inertia_exponent;
  out.axial = axial;
  for (std::size_t lane = 0; lane < 3; ++lane) {
    out.lanes[lane] = std::ldexp(static_cast<double>(s.inertia_words[lane]), -*inertia_exponent);
  }
  out.e1 = e1;
  out.e2 = e2;
  return std::nullopt;
}

std::optional<CapsuleRefusal> load_verified(const HkxPackfile& pack, std::size_t object, Stored& stored, Decoded& decoded) {
  if (const auto problem = read_stored(pack, object, stored)) return problem;
  if (const auto problem = verify_stored(stored, decoded)) return problem;
  if (stored.shared) {
    return CapsuleRefusal{true, "the capsule's mass properties are used by another object, so it was not changed"};
  }
  return std::nullopt;
}

ByteWrite floats_write(std::size_t at, const float* values, std::size_t count) {
  ByteWrite write;
  write.at = at;
  write.bytes.resize(count * sizeof(float));
  std::memcpy(write.bytes.data(), values, write.bytes.size());
  return write;
}

ByteWrite words_write(std::size_t at, const std::array<std::int16_t, 4>& words) {
  ByteWrite write;
  write.at = at;
  write.bytes.resize(words.size() * sizeof(std::int16_t));
  std::memcpy(write.bytes.data(), words.data(), write.bytes.size());
  return write;
}

}  // namespace

std::optional<CapsuleRefusal> check_capsule(const HkxPackfile& pack, std::size_t object) {
  Stored stored;
  Decoded decoded;
  return load_verified(pack, object, stored, decoded);
}

std::optional<CapsuleRefusal> derive_capsule(const HkxPackfile& pack, std::size_t object, const std::optional<HkxVec3>& a,
                                             const std::optional<HkxVec3>& b, const std::optional<float>& radius,
                                             CapsuleChange& out) {
  Stored stored;
  Decoded old;
  if (const auto problem = load_verified(pack, object, stored, old)) return problem;

  const HkxVec3 old_a = narrow(stored.a);
  const HkxVec3 old_b = narrow(stored.b);
  const auto old_radius = static_cast<float>(stored.radius);
  const HkxVec3 new_a = a.value_or(old_a);
  const HkxVec3 new_b = b.value_or(old_b);
  const float new_radius = radius.value_or(old_radius);
  if (!finite3(new_a) || !finite3(new_b) || !std::isfinite(new_radius)) {
    return unsupported("capsule endpoints and radius must be finite numbers");
  }
  for (std::size_t lane = 0; lane < 3; ++lane) {
    if (std::fabs(new_a[lane]) > kMaxExtent || std::fabs(new_b[lane]) > kMaxExtent) {
      return unsupported("capsule endpoints must lie within " + std::to_string(kMaxExtent) + " units of the origin");
    }
  }
  if (!(new_radius >= kMinRadius) || new_radius > kMaxExtent) {
    return unsupported("capsule radius must be between " + std::to_string(kMinRadius) + " and " + std::to_string(kMaxExtent));
  }
  const V3 new_axis_raw = sub(widen(new_b), widen(new_a));
  const double new_length = length_of(new_axis_raw);
  if (!(new_length >= kMinLength)) {
    return unsupported("capsule length must be at least " + std::to_string(kMinLength));
  }

  out = CapsuleChange{};
  out.a = new_a;
  out.b = new_b;
  out.radius = new_radius;
  const bool moved = new_a != old_a || new_b != old_b;
  if (!moved && new_radius == old_radius) {
    for (const V3& vertex : stored.vertices) out.vertices.push_back(narrow(vertex));
    return std::nullopt;
  }

  const V3 old_axis = scaled(sub(stored.b, stored.a), 1.0 / length_of(sub(stored.b, stored.a)));
  const V3 new_axis = scaled(new_axis_raw, 1.0 / new_length);
  const double turned = length_of(sub(new_axis, old_axis));
  if (!(turned <= kDirectionTolerance)) {
    return unsupported("the new endpoints turn the capsule axis by " + std::to_string(turned) +
                       "; only length and radius changes along the stored axis are supported because the packed principal axis frame is not proven");
  }

  const Geometry g = geometry_for(widen(new_a), widen(new_b), new_radius, old.e1, old.e2, stored.normals);
  const auto inertia = capsule_inertia(stored.mass, g.volume, new_radius, g.length);
  V3 lanes{};
  for (std::size_t lane = 0; lane < 3; ++lane) lanes[lane] = 3.0 * (lane == old.axial ? inertia[0] : inertia[1]);
  const auto inertia_exponent = best_exponent(lanes);
  if (!inertia_exponent) return unsupported("the new inertia cannot be packed into the compressed form");
  std::array<std::int16_t, 4> inertia_words{};
  std::array<double, 3> new_lanes{};
  for (std::size_t lane = 0; lane < 3; ++lane) {
    const double rounded = round_away(std::ldexp(lanes[lane], *inertia_exponent));
    if (!(std::fabs(rounded) >= 1.0)) return unsupported("an inertia lane of the new capsule would round to zero");
    inertia_words[lane] = static_cast<std::int16_t>(rounded);
    new_lanes[lane] = rounded;
  }
  if (!(std::fabs(new_lanes[old.axial] - new_lanes[(old.axial + 1) % 3]) >= kLaneSeparation)) {
    return unsupported("the new axial and transverse inertia are too close to tell the capsule axis apart");
  }
  inertia_words[3] = static_cast<std::int16_t>(kInertiaWordBase - kExponentStep * *inertia_exponent);

  if (moved) {
    const auto centre_exponent = best_exponent(g.centre);
    if (!centre_exponent) {
      return unsupported("a capsule centred on the origin is not editable: the centre exponent rule is not proven for a zero centre");
    }
    std::array<std::int16_t, 4> centre_words{};
    for (std::size_t lane = 0; lane < 3; ++lane) {
      centre_words[lane] = static_cast<std::int16_t>(round_away(std::ldexp(g.centre[lane], *centre_exponent)));
    }
    centre_words[3] = static_cast<std::int16_t>(kCentreWordBase - kExponentStep * *centre_exponent);
    out.writes.push_back(words_write(stored.mass_at + kCentreAt, centre_words));
  }
  out.writes.push_back(words_write(stored.mass_at + kInertiaAt, inertia_words));
  const auto volume = static_cast<float>(g.volume);
  out.writes.push_back(floats_write(stored.mass_at + kVolumeAt, &volume, 1));
  if (new_a != old_a) out.writes.push_back(floats_write(stored.a_at, new_a.data(), 3));
  if (new_b != old_b) out.writes.push_back(floats_write(stored.b_at, new_b.data(), 3));
  if (new_radius != old_radius) out.writes.push_back(floats_write(stored.radius_at, &new_radius, 1));
  for (std::size_t i = 0; i < kVertexCount; ++i) {
    const HkxVec3 vertex = narrow(g.vertices[i]);
    out.vertices.push_back(vertex);
    out.writes.push_back(floats_write(stored.vertex_at[i], vertex.data(), 3));
  }
  for (std::size_t i = 0; i < kRealPlanes; ++i) {
    const auto w = static_cast<float>(g.plane_w[i]);
    out.writes.push_back(floats_write(stored.plane_at[i] + kPlaneOffsetInRow, &w, 1));
  }
  for (std::size_t lane = 0; lane < 3; ++lane) {
    out.inertia_ratio[lane] = old.lanes[lane] / std::ldexp(new_lanes[lane], -*inertia_exponent);
  }
  return std::nullopt;
}

}  // namespace fo4::hkx::detail
