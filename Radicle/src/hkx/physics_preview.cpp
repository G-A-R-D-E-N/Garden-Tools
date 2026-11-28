#include "fo4/hkx/physics_preview.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace fo4::hkx {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kBeta = 0.2;
constexpr double kContactMargin = 0.02;
constexpr double kGroundedDepth = -1.0e-4;
constexpr double kFrameTolerance = 1.0e-3;
constexpr int kVelocityIterations = 16;
constexpr int kPositionPasses = 4;
constexpr double kInfinity = std::numeric_limits<double>::infinity();

struct V3 {
  double x{0.0};
  double y{0.0};
  double z{0.0};
};

V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator-(const V3& a) { return {-a.x, -a.y, -a.z}; }
V3 operator*(const V3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
V3& operator+=(V3& a, const V3& b) {
  a = a + b;
  return a;
}
double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(const V3& a) { return std::sqrt(dot(a, a)); }
V3 unit(const V3& a) {
  const double len = length(a);
  return len < 1.0e-12 ? V3{} : a * (1.0 / len);
}
bool finite(const V3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
V3 to_v3(const HkxVec3& a) { return {a[0], a[1], a[2]}; }
HkxVec3 to_array(const V3& a) {
  return {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z)};
}

struct Q {
  double x{0.0};
  double y{0.0};
  double z{0.0};
  double w{1.0};
};

Q mul(const Q& a, const Q& b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Q conj(const Q& q) { return {-q.x, -q.y, -q.z, q.w}; }
double norm(const Q& q) { return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); }
Q unit(const Q& q) {
  const double len = norm(q);
  return {q.x / len, q.y / len, q.z / len, q.w / len};
}
V3 rotate(const Q& q, const V3& v) {
  const V3 u{q.x, q.y, q.z};
  const V3 t = cross(u, v) * 2.0;
  return v + t * q.w + cross(u, t);
}
Q to_q(const HkxQuat& q) { return {q[0], q[1], q[2], q[3]}; }
HkxQuat to_array(const Q& q) {
  return {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)};
}

struct Frame {
  std::array<V3, 3> col{};
  V3 t{};
};

struct Pose {
  V3 origin{};
  Q rot{};
};

struct Body {
  std::int32_t id{0};
  double mass{0.0};
  double inv_mass{0.0};
  V3 inv_inertia{};
  V3 com_local{};
  V3 a_local{};
  V3 b_local{};
  double radius{0.0};
  V3 origin{};
  Q rot{};
  V3 v{};
  V3 w{};
  std::array<V3, 3> axis{};
  bool grounded{false};

  void refresh() {
    axis[0] = rotate(rot, V3{1.0, 0.0, 0.0});
    axis[1] = rotate(rot, V3{0.0, 1.0, 0.0});
    axis[2] = rotate(rot, V3{0.0, 0.0, 1.0});
  }
  V3 com() const { return origin + rotate(rot, com_local); }
  V3 point(const V3& local) const { return origin + rotate(rot, local); }
  V3 inv_i(const V3& x) const {
    return axis[0] * (inv_inertia.x * dot(axis[0], x)) + axis[1] * (inv_inertia.y * dot(axis[1], x)) +
           axis[2] * (inv_inertia.z * dot(axis[2], x));
  }
};

struct Joint {
  std::int32_t id{0};
  HkxConstraintKind kind{HkxConstraintKind::Unknown};
  std::size_t a{0};
  std::size_t b{0};
  Frame fa{};
  Frame fb{};
  int twist_axis{0};
  int twist_ref{1};
  int cone_axis{0};
  int cone_ref{0};
  int limit_axis{0};
  std::optional<double> twist_min;
  std::optional<double> twist_max;
  std::optional<double> cone_max;
  std::optional<double> hinge_min;
  std::optional<double> hinge_max;
  bool overridden{false};
};

struct Jac {
  V3 la{};
  V3 aa{};
  V3 lb{};
  V3 ab{};
};

struct Row {
  std::size_t a{0};
  std::size_t b{0};
  Jac j{};
  double bias{0.0};
  double lo{-kInfinity};
  double hi{kInfinity};
  double acc{0.0};
};

struct Contact {
  std::size_t a{0};
  std::size_t b{0};
  V3 n{};
  double depth{0.0};
  V3 pa{};
  V3 pb{};
};

Jac linear_jac(const V3& n, const V3& ra, const V3& rb) { return {-n, -cross(ra, n), n, cross(rb, n)}; }
Jac angular_jac(const V3& n) { return {V3{}, -n, V3{}, n}; }

double signed_angle(const V3& axis, const V3& u, const V3& v, bool& valid) {
  const V3 up = u - axis * dot(u, axis);
  const V3 vp = v - axis * dot(v, axis);
  valid = length(up) > 1.0e-9 && length(vp) > 1.0e-9;
  if (!valid) return 0.0;
  return std::atan2(dot(axis, cross(up, vp)), dot(up, vp));
}

void closest_segments(const V3& p1, const V3& q1, const V3& p2, const V3& q2, V3& c1, V3& c2) {
  constexpr double eps = 1.0e-12;
  const V3 d1 = q1 - p1;
  const V3 d2 = q2 - p2;
  const V3 r = p1 - p2;
  const double a = dot(d1, d1);
  const double e = dot(d2, d2);
  const double f = dot(d2, r);
  double s = 0.0;
  double t = 0.0;
  if (a <= eps && e <= eps) {
    c1 = p1;
    c2 = p2;
    return;
  }
  if (a <= eps) {
    t = std::clamp(f / e, 0.0, 1.0);
  } else {
    const double c = dot(d1, r);
    if (e <= eps) {
      s = std::clamp(-c / a, 0.0, 1.0);
    } else {
      const double b = dot(d1, d2);
      const double denom = a * e - b * b;
      s = denom > eps ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
      t = (b * s + f) / e;
      if (t < 0.0) {
        t = 0.0;
        s = std::clamp(-c / a, 0.0, 1.0);
      } else if (t > 1.0) {
        t = 1.0;
        s = std::clamp((b - c) / a, 0.0, 1.0);
      }
    }
  }
  c1 = p1 + d1 * s;
  c2 = p2 + d2 * t;
}

bool parse_frame(const std::optional<std::vector<float>>& source, Frame& out) {
  if (!source || source->size() != 16) return false;
  for (float value : *source) {
    if (!std::isfinite(value)) return false;
  }
  const std::vector<float>& f = *source;
  for (std::size_t i = 0; i < 3; ++i) out.col[i] = {f[4 * i], f[4 * i + 1], f[4 * i + 2]};
  out.t = {f[12], f[13], f[14]};
  for (std::size_t i = 0; i < 3; ++i) {
    if (std::fabs(length(out.col[i]) - 1.0) > kFrameTolerance) return false;
    if (std::fabs(dot(out.col[i], out.col[(i + 1) % 3])) > kFrameTolerance) return false;
  }
  return dot(out.col[0], cross(out.col[1], out.col[2])) > 0.0;
}

bool axis_ok(int axis) { return axis >= 0 && axis <= 2; }

bool angle_ok(const std::optional<float>& value, double lo, double hi) {
  return value && std::isfinite(*value) && *value >= lo && *value <= hi;
}

V3 clamp_length(const V3& value, double limit) {
  const double len = length(value);
  return len > limit ? value * (limit / len) : value;
}

}  // namespace

const char* preview_status_name(HkxPreviewStatus status) {
  switch (status) {
    case HkxPreviewStatus::Ok: return "Ok";
    case HkxPreviewStatus::NoBodies: return "NoBodies";
    case HkxPreviewStatus::TooManyBodies: return "TooManyBodies";
    case HkxPreviewStatus::TooManyConstraints: return "TooManyConstraints";
    case HkxPreviewStatus::UnsupportedShape: return "UnsupportedShape";
    case HkxPreviewStatus::BadMass: return "BadMass";
    case HkxPreviewStatus::BadPose: return "BadPose";
    case HkxPreviewStatus::BadFrame: return "BadFrame";
    case HkxPreviewStatus::BadConstraint: return "BadConstraint";
    case HkxPreviewStatus::MissingLimits: return "MissingLimits";
    case HkxPreviewStatus::BadOption: return "BadOption";
    case HkxPreviewStatus::BadTarget: return "BadTarget";
    case HkxPreviewStatus::PoseUnavailable: return "PoseUnavailable";
    case HkxPreviewStatus::Diverged: return "Diverged";
  }
  return "Unknown";
}

struct HkxPhysicsPreview::Impl {
  HkxRagdollModel model;
  HkxPreviewOptions options;
  std::vector<Body> bodies;
  std::vector<Pose> base;
  std::vector<Pose> start;
  std::vector<Joint> joints;
  std::vector<char> jointed;
  std::unordered_map<std::int32_t, std::size_t> index_of;
  std::vector<Row> rows;
  std::vector<Contact> contacts;
  std::size_t count{0};
  std::int64_t steps{0};
  bool open{false};

  void close() {
    model = {};
    bodies.clear();
    base.clear();
    start.clear();
    joints.clear();
    jointed.clear();
    index_of.clear();
    rows.clear();
    contacts.clear();
    count = 0;
    steps = 0;
    open = false;
  }

  void reset() {
    for (std::size_t i = 0; i < count; ++i) {
      Body& body = bodies[i];
      body.origin = start[i].origin;
      body.rot = start[i].rot;
      body.v = {};
      body.w = {};
      body.grounded = false;
      body.refresh();
    }
    steps = 0;
  }

  struct Angles {
    std::optional<double> twist;
    std::optional<double> cone;
    std::optional<double> hinge;
  };

  V3 axis_a(const Joint& j, int i) const { return rotate(bodies[j.a].rot, j.fa.col[static_cast<std::size_t>(i)]); }
  V3 axis_b(const Joint& j, int i) const { return rotate(bodies[j.b].rot, j.fb.col[static_cast<std::size_t>(i)]); }

  Angles measure(const Joint& j) const {
    Angles out;
    bool valid = false;
    if (j.kind == HkxConstraintKind::Ragdoll) {
      const double twist = signed_angle(axis_a(j, j.twist_axis), axis_a(j, j.twist_ref), axis_b(j, j.twist_ref), valid);
      if (valid) out.twist = twist;
      out.cone = std::acos(std::clamp(dot(axis_a(j, j.cone_axis), axis_b(j, j.cone_ref)), -1.0, 1.0));
    } else if (j.kind == HkxConstraintKind::LimitedHinge || j.kind == HkxConstraintKind::Hinge) {
      const int next = (j.limit_axis + 1) % 3;
      const double hinge = signed_angle(axis_a(j, j.limit_axis), axis_a(j, next), axis_b(j, next), valid);
      if (valid) out.hinge = hinge;
    }
    return out;
  }

  static void push_limit(std::vector<Row>& out, std::size_t a, std::size_t b, const V3& n, double excess) {
    Row row;
    row.a = a;
    row.b = b;
    row.j = angular_jac(n);
    row.bias = (excess > 0.0 ? kBeta : 1.0) * excess / kPreviewStep;
    row.lo = -kInfinity;
    row.hi = 0.0;
    out.push_back(row);
  }

  static void push_lock(std::vector<Row>& out, std::size_t a, std::size_t b, const V3& n, double error) {
    Row row;
    row.a = a;
    row.b = b;
    row.j = angular_jac(n);
    row.bias = kBeta * error / kPreviewStep;
    out.push_back(row);
  }

  void joint_rows(const Joint& j) {
    const Body& A = bodies[j.a];
    const Body& B = bodies[j.b];
    const V3 pa = A.point(j.fa.t);
    const V3 pb = B.point(j.fb.t);
    const V3 ra = pa - A.com();
    const V3 rb = pb - B.com();
    const V3 err = pb - pa;
    const std::array<V3, 3> world{V3{1.0, 0.0, 0.0}, V3{0.0, 1.0, 0.0}, V3{0.0, 0.0, 1.0}};
    for (const V3& e : world) {
      Row row;
      row.a = j.a;
      row.b = j.b;
      row.j = linear_jac(e, ra, rb);
      row.bias = kBeta * dot(err, e) / kPreviewStep;
      rows.push_back(row);
    }

    if (j.kind == HkxConstraintKind::Fixed) {
      V3 e{};
      for (int i = 0; i < 3; ++i) e += cross(axis_a(j, i), axis_b(j, i)) * 0.5;
      for (const V3& n : world) push_lock(rows, j.a, j.b, n, dot(e, n));
    } else if (j.kind == HkxConstraintKind::Hinge || j.kind == HkxConstraintKind::LimitedHinge) {
      const V3 hinge_a = axis_a(j, j.limit_axis);
      const V3 hinge_b = axis_b(j, j.limit_axis);
      const V3 c = cross(hinge_a, hinge_b);
      for (int i = 1; i <= 2; ++i) {
        const V3 p = axis_a(j, (j.limit_axis + i) % 3);
        push_lock(rows, j.a, j.b, p, dot(c, p));
      }
      const Angles angles = measure(j);
      if (j.kind == HkxConstraintKind::LimitedHinge && angles.hinge && j.hinge_min && j.hinge_max) {
        push_limit(rows, j.a, j.b, hinge_a, *angles.hinge - *j.hinge_max);
        push_limit(rows, j.a, j.b, -hinge_a, *j.hinge_min - *angles.hinge);
      }
    } else if (j.kind == HkxConstraintKind::Ragdoll) {
      const Angles angles = measure(j);
      if (angles.twist && j.twist_min && j.twist_max) {
        const V3 twist = axis_a(j, j.twist_axis);
        push_limit(rows, j.a, j.b, twist, *angles.twist - *j.twist_max);
        push_limit(rows, j.a, j.b, -twist, *j.twist_min - *angles.twist);
      }
      const V3 swing = unit(cross(axis_b(j, j.cone_ref), axis_a(j, j.cone_axis)));
      if (angles.cone && j.cone_max && length(swing) > 0.0) push_limit(rows, j.a, j.b, -swing, *angles.cone - *j.cone_max);
    }
  }

  void collect_contacts() {
    contacts.clear();
    for (std::size_t i = 0; i < count; ++i) bodies[i].grounded = false;
    const std::size_t world = count;
    if (options.ground) {
      const double ground_z = options.ground_z;
      for (std::size_t i = 0; i < count; ++i) {
        Body& body = bodies[i];
        for (const V3& local : {body.a_local, body.b_local}) {
          const V3 end = body.point(local);
          const double depth = ground_z + body.radius - end.z;
          if (depth <= -kContactMargin) continue;
          Contact contact;
          contact.a = world;
          contact.b = i;
          contact.n = V3{0.0, 0.0, -1.0};
          contact.depth = depth;
          contact.pb = end - V3{0.0, 0.0, body.radius};
          contacts.push_back(contact);
          if (depth > kGroundedDepth) body.grounded = true;
        }
      }
    }
    if (!options.body_collision) return;
    for (std::size_t i = 0; i < count; ++i) {
      for (std::size_t k = i + 1; k < count; ++k) {
        if (jointed[i * count + k] != 0) continue;
        const Body& A = bodies[i];
        const Body& B = bodies[k];
        V3 ca;
        V3 cb;
        closest_segments(A.point(A.a_local), A.point(A.b_local), B.point(B.a_local), B.point(B.b_local), ca, cb);
        const V3 diff = ca - cb;
        const double dist = length(diff);
        const double depth = A.radius + B.radius - dist;
        if (depth <= -kContactMargin) continue;
        Contact contact;
        contact.a = i;
        contact.b = k;
        contact.n = dist < 1.0e-9 ? V3{0.0, 0.0, 1.0} : diff * (1.0 / dist);
        contact.depth = depth;
        contact.pa = ca - contact.n * A.radius;
        contact.pb = cb + contact.n * B.radius;
        contacts.push_back(contact);
      }
    }
  }

  void solve(Row& row) {
    Body& A = bodies[row.a];
    Body& B = bodies[row.b];
    const Jac& j = row.j;
    const double cdot = dot(j.la, A.v) + dot(j.aa, A.w) + dot(j.lb, B.v) + dot(j.ab, B.w);
    const double k = dot(j.la, j.la) * A.inv_mass + dot(j.aa, A.inv_i(j.aa)) + dot(j.lb, j.lb) * B.inv_mass +
                     dot(j.ab, B.inv_i(j.ab));
    if (k < 1.0e-18) return;
    const double lambda = -(cdot + row.bias) / k;
    const double next = std::clamp(row.acc + lambda, row.lo, row.hi);
    const double applied = next - row.acc;
    row.acc = next;
    A.v += j.la * (A.inv_mass * applied);
    A.w += A.inv_i(j.aa) * applied;
    B.v += j.lb * (B.inv_mass * applied);
    B.w += B.inv_i(j.ab) * applied;
  }

  void integrate() {
    for (std::size_t i = 0; i < count; ++i) {
      Body& body = bodies[i];
      const V3 center = body.com() + body.v * kPreviewStep;
      const Q spin{body.w.x, body.w.y, body.w.z, 0.0};
      const Q delta = mul(spin, body.rot);
      body.rot = unit(Q{body.rot.x + delta.x * 0.5 * kPreviewStep, body.rot.y + delta.y * 0.5 * kPreviewStep,
                        body.rot.z + delta.z * 0.5 * kPreviewStep, body.rot.w + delta.w * 0.5 * kPreviewStep});
      body.origin = center - rotate(body.rot, body.com_local);
      body.refresh();
    }
  }

  void correct_positions() {
    for (int pass = 0; pass < kPositionPasses; ++pass) {
      collect_contacts();
      for (const Contact& contact : contacts) {
        if (contact.depth <= 0.0) continue;
        Body& A = bodies[contact.a];
        Body& B = bodies[contact.b];
        const double total = A.inv_mass + B.inv_mass;
        if (total <= 0.0) continue;
        A.origin += contact.n * (contact.depth * A.inv_mass / total);
        B.origin += contact.n * (-contact.depth * B.inv_mass / total);
      }
    }
  }

  void step_once() {
    for (std::size_t i = 0; i < count; ++i) bodies[i].v.z -= static_cast<double>(options.gravity) * kPreviewStep;
    rows.clear();
    for (const Joint& joint : joints) joint_rows(joint);
    collect_contacts();
    for (const Contact& contact : contacts) {
      const Body& A = bodies[contact.a];
      const Body& B = bodies[contact.b];
      Row row;
      row.a = contact.a;
      row.b = contact.b;
      row.j = linear_jac(contact.n, contact.pa - A.com(), contact.pb - B.com());
      row.bias = contact.depth < 0.0 ? contact.depth / kPreviewStep : 0.0;
      row.lo = -kInfinity;
      row.hi = 0.0;
      rows.push_back(row);
    }
    for (int iteration = 0; iteration < kVelocityIterations; ++iteration) {
      for (Row& row : rows) solve(row);
    }
    integrate();
    correct_positions();
    ++steps;
  }

  bool state_finite() const {
    for (std::size_t i = 0; i < count; ++i) {
      const Body& body = bodies[i];
      if (!finite(body.origin) || !finite(body.v) || !finite(body.w) || !std::isfinite(body.rot.w)) return false;
    }
    return true;
  }
};

HkxPhysicsPreview::HkxPhysicsPreview() : impl_(std::make_unique<Impl>()) {}
HkxPhysicsPreview::~HkxPhysicsPreview() = default;

bool HkxPhysicsPreview::is_open() const { return impl_->open; }

HkxPreviewStatus HkxPhysicsPreview::open(const HkxRagdollModel& model, const HkxRagdollMeasures& measures,
                                         const HkxPreviewOptions& options, std::string* message) {
  Impl& s = *impl_;
  s.close();
  std::string scratch;
  std::string& text = message ? *message : scratch;
  const auto fail = [&](HkxPreviewStatus status, std::string reason) {
    text = std::move(reason);
    s.close();
    return status;
  };

  if (!std::isfinite(options.gravity) || options.gravity < 0.0f || options.gravity > 1000.0f ||
      !std::isfinite(options.ground_z)) {
    return fail(HkxPreviewStatus::BadOption, "gravity must be between 0 and 1000 and the ground height must be finite");
  }
  if (model.bodies.empty()) return fail(HkxPreviewStatus::NoBodies, "the ragdoll has no bodies");
  if (model.bodies.size() > static_cast<std::size_t>(kPreviewMaxBodies)) {
    return fail(HkxPreviewStatus::TooManyBodies, "a preview holds at most " + std::to_string(kPreviewMaxBodies) + " bodies");
  }
  if (model.constraints.size() > static_cast<std::size_t>(kPreviewMaxConstraints)) {
    return fail(HkxPreviewStatus::TooManyConstraints,
                "a preview holds at most " + std::to_string(kPreviewMaxConstraints) + " constraints");
  }

  std::unordered_map<std::int32_t, const HkxRagdollBodyMeasure*> motions;
  for (const HkxRagdollBodyMeasure& measure : measures.bodies) motions.emplace(measure.body, &measure);
  std::unordered_map<std::int32_t, const HkxRagdollCapsuleMeasure*> capsules;
  for (const HkxRagdollCapsuleMeasure& measure : measures.capsules) capsules.emplace(measure.shape, &measure);

  s.options = options;
  for (const HkxRigidBody& source : model.bodies) {
    const std::string where = "body " + std::to_string(source.id);
    if (!s.index_of.emplace(source.id, s.bodies.size()).second) {
      return fail(HkxPreviewStatus::BadTarget, where + " appears twice");
    }
    const HkxPhysicsShape* shape = nullptr;
    for (const HkxPhysicsShape& candidate : model.shapes) {
      if (candidate.id == source.shape_id) shape = &candidate;
    }
    const auto capsule = capsules.find(source.shape_id);
    if (shape == nullptr || shape->kind != HkxPhysicsShapeKind::Capsule || capsule == capsules.end()) {
      return fail(HkxPreviewStatus::UnsupportedShape, where + " is not a capsule; the preview simulates capsules only");
    }
    const HkxRagdollCapsuleMeasure& geometry = *capsule->second;
    const V3 a = to_v3(geometry.a);
    const V3 b = to_v3(geometry.b);
    if (!finite(a) || !finite(b) || !std::isfinite(geometry.radius) || geometry.radius <= 0.0f) {
      return fail(HkxPreviewStatus::UnsupportedShape, where + " has a capsule with no finite endpoints or radius");
    }
    const auto motion = motions.find(source.id);
    if (motion == motions.end()) return fail(HkxPreviewStatus::BadMass, where + " has no motion row with a mass");
    const HkxRagdollBodyMeasure& measure = *motion->second;
    const float mass = measure.mass();
    const V3 inertia = to_v3(measure.inverse_inertia);
    if (!std::isfinite(mass) || mass < kPreviewMinMass || mass > kPreviewMaxMass || !finite(inertia) ||
        inertia.x <= 0.0 || inertia.y <= 0.0 || inertia.z <= 0.0) {
      return fail(HkxPreviewStatus::BadMass,
                  where + " needs a mass between 0.0001 and 100000 and a positive inverse inertia");
    }
    const V3 position = to_v3(source.position);
    const Q rotation = to_q(source.rotation);
    if (!finite(position) || !std::isfinite(norm(rotation)) || norm(rotation) < 1.0e-6) {
      return fail(HkxPreviewStatus::BadPose, where + " has a non-finite position or a degenerate rotation");
    }

    Body body;
    body.id = source.id;
    body.mass = mass;
    body.inv_mass = 1.0 / mass;
    body.inv_inertia = inertia;
    body.a_local = a;
    body.b_local = b;
    body.radius = geometry.radius;
    body.origin = position;
    body.rot = unit(rotation);
    const V3 center = to_v3(measure.center_of_mass);
    if (!finite(center)) return fail(HkxPreviewStatus::BadPose, where + " has a non-finite center of mass");
    if (center.x != 0.0 || center.y != 0.0 || center.z != 0.0) body.com_local = rotate(conj(body.rot), center - position);
    body.refresh();
    s.bodies.push_back(body);
    s.base.push_back({body.origin, body.rot});
  }
  s.count = s.bodies.size();
  s.jointed.assign(s.count * s.count, 0);

  for (const HkxPhysicsConstraint& source : model.constraints) {
    const std::string where = "constraint " + std::to_string(source.id);
    const auto a = s.index_of.find(source.body_a);
    const auto b = s.index_of.find(source.body_b);
    if (a == s.index_of.end() || b == s.index_of.end()) return fail(HkxPreviewStatus::BadTarget, where + " names a missing body");
    if (a->second == b->second) return fail(HkxPreviewStatus::BadConstraint, where + " joins a body to itself");
    if (source.kind == HkxConstraintKind::Unknown) {
      return fail(HkxPreviewStatus::BadConstraint, where + " has a constraint kind the preview does not simulate");
    }
    Joint joint;
    joint.id = source.id;
    joint.kind = source.kind;
    joint.a = a->second;
    joint.b = b->second;
    if (!parse_frame(source.frame_a, joint.fa) || !parse_frame(source.frame_b, joint.fb)) {
      return fail(HkxPreviewStatus::BadFrame, where + " needs two finite, orthonormal, right-handed frames of sixteen floats");
    }
    if (!axis_ok(source.twist_axis) || !axis_ok(source.twist_ref_axis) || !axis_ok(source.cone_twist_axis) ||
        !axis_ok(source.cone_ref_axis) || !axis_ok(source.limit_axis) || source.twist_axis == source.twist_ref_axis) {
      return fail(HkxPreviewStatus::BadConstraint, where + " names a frame axis outside 0 to 2");
    }
    joint.twist_axis = source.twist_axis;
    joint.twist_ref = source.twist_ref_axis;
    joint.cone_axis = source.cone_twist_axis;
    joint.cone_ref = source.cone_ref_axis;
    joint.limit_axis = source.limit_axis;
    if (source.kind == HkxConstraintKind::Ragdoll) {
      if (!source.twist_min_angle || !source.twist_max_angle || !source.cone_max_angle) {
        return fail(HkxPreviewStatus::MissingLimits, where + " is a ragdoll constraint without twist and cone limits");
      }
      if (!angle_ok(source.twist_min_angle, -kPi, kPi) || !angle_ok(source.twist_max_angle, -kPi, kPi) ||
          *source.twist_min_angle > *source.twist_max_angle || !angle_ok(source.cone_max_angle, 0.0, kPi)) {
        return fail(HkxPreviewStatus::BadConstraint, where + " has twist limits outside -pi..pi, unordered, or a cone outside 0..pi");
      }
      joint.twist_min = *source.twist_min_angle;
      joint.twist_max = *source.twist_max_angle;
      joint.cone_max = *source.cone_max_angle;
    } else if (source.kind == HkxConstraintKind::LimitedHinge) {
      const std::optional<float> lo = source.min_angle;
      const std::optional<float> hi = source.max_angle;
      if (!angle_ok(lo, -kPi, kPi) || !angle_ok(hi, -kPi, kPi) || *lo > *hi) {
        return fail(HkxPreviewStatus::BadConstraint, where + " has hinge limits outside -pi..pi or unordered");
      }
      joint.hinge_min = *lo;
      joint.hinge_max = *hi;
    }
    s.jointed[joint.a * s.count + joint.b] = 1;
    s.jointed[joint.b * s.count + joint.a] = 1;
    s.joints.push_back(joint);
  }

  Body world;
  s.bodies.push_back(world);
  s.model = model;
  s.start = s.base;
  s.open = true;
  s.reset();
  text.clear();
  return HkxPreviewStatus::Ok;
}

void HkxPhysicsPreview::reset() {
  if (impl_->open) impl_->reset();
}

HkxPreviewStatus HkxPhysicsPreview::step(std::int32_t count, std::string* message) {
  Impl& s = *impl_;
  if (!s.open) return HkxPreviewStatus::BadTarget;
  if (count < 0 || count > kPreviewMaxStepsPerCall) {
    if (message) *message = "steps must be between 0 and " + std::to_string(kPreviewMaxStepsPerCall);
    return HkxPreviewStatus::BadOption;
  }
  for (std::int32_t i = 0; i < count; ++i) {
    s.step_once();
    if (!s.state_finite()) {
      s.reset();
      if (message) *message = "the simulation produced a non-finite state and was reset to the start pose";
      return HkxPreviewStatus::Diverged;
    }
  }
  return HkxPreviewStatus::Ok;
}

HkxPreviewStatus HkxPhysicsPreview::apply_impulse(std::int32_t body, const HkxVec3& linear,
                                                  const std::optional<HkxVec3>& angular) {
  Impl& s = *impl_;
  if (!s.open) return HkxPreviewStatus::BadTarget;
  const auto at = s.index_of.find(body);
  if (at == s.index_of.end()) return HkxPreviewStatus::BadTarget;
  const V3 push = to_v3(linear);
  const V3 twist = angular ? to_v3(*angular) : V3{};
  if (!finite(push) || !finite(twist)) return HkxPreviewStatus::BadOption;
  Body& target = s.bodies[at->second];
  target.v += clamp_length(push, kPreviewMaxImpulse) * target.inv_mass;
  target.w += target.inv_i(clamp_length(twist, kPreviewMaxImpulse));
  return HkxPreviewStatus::Ok;
}

HkxPreviewStatus HkxPhysicsPreview::override_limits(const HkxRagdollConstraintEdit& limits, std::string* message) {
  Impl& s = *impl_;
  std::string scratch;
  std::string& text = message ? *message : scratch;
  if (!s.open) return HkxPreviewStatus::BadTarget;
  Joint* joint = nullptr;
  for (Joint& candidate : s.joints) {
    if (candidate.id == limits.constraint) joint = &candidate;
  }
  if (joint == nullptr) {
    text = "no constraint " + std::to_string(limits.constraint);
    return HkxPreviewStatus::BadTarget;
  }
  if (limits.body_a || limits.body_b || limits.frame_a || limits.frame_b) {
    text = "a preview override changes limits only, not bodies or frames";
    return HkxPreviewStatus::BadOption;
  }
  const bool twist = limits.twist_min_angle || limits.twist_max_angle || limits.cone_max_angle;
  const bool hinge = limits.min_angle || limits.max_angle;
  if (!twist && !hinge) {
    text = "the override names no limit";
    return HkxPreviewStatus::BadOption;
  }
  if ((twist && joint->kind != HkxConstraintKind::Ragdoll) || (hinge && joint->kind != HkxConstraintKind::LimitedHinge)) {
    text = "the constraint has no such limit field";
    return HkxPreviewStatus::BadConstraint;
  }
  std::optional<double> twist_min = joint->twist_min;
  std::optional<double> twist_max = joint->twist_max;
  std::optional<double> cone_max = joint->cone_max;
  std::optional<double> hinge_min = joint->hinge_min;
  std::optional<double> hinge_max = joint->hinge_max;
  if (limits.twist_min_angle) twist_min = *limits.twist_min_angle;
  if (limits.twist_max_angle) twist_max = *limits.twist_max_angle;
  if (limits.cone_max_angle) cone_max = *limits.cone_max_angle;
  if (limits.min_angle) hinge_min = *limits.min_angle;
  if (limits.max_angle) hinge_max = *limits.max_angle;
  const auto in_range = [](const std::optional<double>& value, double lo, double hi) {
    return !value || (std::isfinite(*value) && *value >= lo && *value <= hi);
  };
  const bool ordered = (!twist_min || !twist_max || *twist_min <= *twist_max) && (!hinge_min || !hinge_max || *hinge_min <= *hinge_max);
  if (!in_range(twist_min, -kPi, kPi) || !in_range(twist_max, -kPi, kPi) || !in_range(cone_max, 0.0, kPi) ||
      !in_range(hinge_min, -kPi, kPi) || !in_range(hinge_max, -kPi, kPi) || !ordered) {
    text = "limits must be finite, ordered, within -pi..pi (cone 0..pi)";
    return HkxPreviewStatus::BadConstraint;
  }
  joint->twist_min = twist_min;
  joint->twist_max = twist_max;
  joint->cone_max = cone_max;
  joint->hinge_min = hinge_min;
  joint->hinge_max = hinge_max;
  joint->overridden = true;
  return HkxPreviewStatus::Ok;
}

void HkxPhysicsPreview::clear_overrides() {
  Impl& s = *impl_;
  if (!s.open) return;
  for (Joint& joint : s.joints) {
    for (const HkxPhysicsConstraint& source : s.model.constraints) {
      if (source.id != joint.id) continue;
      if (joint.kind == HkxConstraintKind::Ragdoll) {
        joint.twist_min = *source.twist_min_angle;
        joint.twist_max = *source.twist_max_angle;
        joint.cone_max = *source.cone_max_angle;
      } else if (joint.kind == HkxConstraintKind::LimitedHinge) {
        joint.hinge_min = source.min_angle;
        joint.hinge_max = source.max_angle;
      }
      joint.overridden = false;
    }
  }
}

HkxPreviewStatus HkxPhysicsPreview::set_pose(std::span<const HkxBoneFrame> animation_world, std::string* message) {
  Impl& s = *impl_;
  std::string scratch;
  std::string& text = message ? *message : scratch;
  if (!s.open) return HkxPreviewStatus::BadTarget;
  std::vector<HkxBoneFrame> frames;
  if (!map_pose(s.model, animation_world, frames)) {
    const HkxSkeleton* expected = s.model.animation_skeleton ? &*s.model.animation_skeleton : s.model.skeleton ? &*s.model.skeleton : nullptr;
    if (expected && expected->reference_pose.size() != animation_world.size()) {
      text = "the pose has " + std::to_string(animation_world.size()) + " bones but the ragdoll animation skeleton has " +
             std::to_string(expected->reference_pose.size());
      return HkxPreviewStatus::PoseUnavailable;
    }
    text = "the animation pose cannot be mapped onto the physics skeleton";
    return HkxPreviewStatus::PoseUnavailable;
  }
  const std::vector<HkxBoneFrame> reference = world_reference_frames(*s.model.skeleton);
  std::vector<Pose> placed = s.base;
  for (const HkxRagdollBoneBinding& binding : s.model.bone_bindings) {
    const auto at = s.index_of.find(binding.body_id);
    const auto bone = static_cast<std::size_t>(binding.bone_index);
    if (at == s.index_of.end() || binding.bone_index < 0 || bone >= frames.size() || bone >= reference.size()) {
      text = "bone " + std::to_string(binding.bone_index) + " is bound to a body or bone the preview cannot place";
      return HkxPreviewStatus::PoseUnavailable;
    }
    const Q now = to_q(frames[bone].rotation);
    const Q then = to_q(reference[bone].rotation);
    if (norm(now) < 1.0e-6 || norm(then) < 1.0e-6) {
      text = "bone " + std::to_string(binding.bone_index) + " has a degenerate rotation";
      return HkxPreviewStatus::BadPose;
    }
    const Q delta = unit(mul(unit(now), conj(unit(then))));
    const V3 shift = to_v3(frames[bone].position) - rotate(delta, to_v3(reference[bone].position));
    Pose& pose = placed[at->second];
    pose.origin = rotate(delta, s.base[at->second].origin) + shift;
    pose.rot = unit(mul(delta, s.base[at->second].rot));
    if (!finite(pose.origin) || !std::isfinite(pose.rot.w)) {
      text = "the animation pose holds a non-finite value";
      return HkxPreviewStatus::BadPose;
    }
  }
  s.start = std::move(placed);
  s.reset();
  return HkxPreviewStatus::Ok;
}

std::int64_t HkxPhysicsPreview::steps() const { return impl_->steps; }

std::vector<HkxPreviewBodyState> HkxPhysicsPreview::bodies() const {
  const Impl& s = *impl_;
  std::vector<HkxPreviewBodyState> out;
  for (std::size_t i = 0; i < s.count; ++i) {
    const Body& body = s.bodies[i];
    HkxPreviewBodyState state;
    state.body = body.id;
    state.mass = static_cast<float>(body.mass);
    state.position = to_array(body.origin);
    state.rotation = to_array(body.rot);
    state.center_of_mass = to_array(body.com());
    state.linear_velocity = to_array(body.v);
    state.angular_velocity = to_array(body.w);
    state.grounded = body.grounded;
    out.push_back(state);
  }
  return out;
}

std::vector<HkxPreviewConstraintState> HkxPhysicsPreview::constraints() const {
  const Impl& s = *impl_;
  std::vector<HkxPreviewConstraintState> out;
  const auto narrow = [](const std::optional<double>& value) {
    return value ? std::optional<float>(static_cast<float>(*value)) : std::optional<float>();
  };
  for (const Joint& joint : s.joints) {
    HkxPreviewConstraintState state;
    state.constraint = joint.id;
    state.kind = joint.kind;
    state.overridden = joint.overridden;
    const Body& A = s.bodies[joint.a];
    const Body& B = s.bodies[joint.b];
    state.anchor_error = static_cast<float>(length(B.point(joint.fb.t) - A.point(joint.fa.t)));
    const Impl::Angles angles = s.measure(joint);
    state.twist_angle = narrow(angles.twist);
    state.cone_angle = narrow(angles.cone);
    state.hinge_angle = narrow(angles.hinge);
    state.twist_min = narrow(joint.twist_min);
    state.twist_max = narrow(joint.twist_max);
    state.cone_max = narrow(joint.cone_max);
    state.hinge_min = narrow(joint.hinge_min);
    state.hinge_max = narrow(joint.hinge_max);
    double worst = 0.0;
    if (angles.twist && joint.twist_max) worst = std::max(worst, *angles.twist - *joint.twist_max);
    if (angles.twist && joint.twist_min) worst = std::max(worst, *joint.twist_min - *angles.twist);
    if (angles.cone && joint.cone_max) worst = std::max(worst, *angles.cone - *joint.cone_max);
    if (joint.kind == HkxConstraintKind::LimitedHinge && angles.hinge && joint.hinge_max) {
      worst = std::max(worst, *angles.hinge - *joint.hinge_max);
    }
    if (joint.kind == HkxConstraintKind::LimitedHinge && angles.hinge && joint.hinge_min) {
      worst = std::max(worst, *joint.hinge_min - *angles.hinge);
    }
    state.violation = static_cast<float>(worst);
    out.push_back(state);
  }
  return out;
}

}  // namespace fo4::hkx
