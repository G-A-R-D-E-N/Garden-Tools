#include "write_refs.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif::write_detail {
namespace {

class Walk {
 public:
  explicit Walk(const Bytes& bytes) : cursor_(bytes) {}

  void skip(std::size_t count) { cursor_.read_bytes(count); }
  std::uint8_t u8() { return cursor_.read_u8(); }
  std::uint16_t u16() { return cursor_.read_u16(); }
  std::uint32_t u32() { return cursor_.read_u32(); }

  void ref() {
    refs_.push_back(cursor_.position());
    cursor_.read_i32();
  }
  void refs(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) ref();
  }
  void ref_list() { refs(u32()); }

  void object_net() {
    skip(4);
    ref_list();
    ref();
  }
  void av_object() {
    object_net();
    skip(56);
    ref();
  }
  void node() {
    av_object();
    ref_list();
  }
  void time_controller() {
    ref();
    skip(18);
    ref();
  }
  void single_controller() {
    time_controller();
    ref();
  }
  void modifier() {
    skip(8);
    ref();
    skip(1);
  }
  void emitter() {
    modifier();
    skip(56);
  }
  void collider() {
    skip(6);
    refs(4);
  }

  std::vector<std::size_t> finish(std::string_view type) {
    if (!cursor_.at_end()) {
      throw parse_error(0, "block type " + std::string(type) + " does not match its reference layout");
    }
    return std::move(refs_);
  }

 private:
  binary::Cursor cursor_;
  std::vector<std::size_t> refs_;
};

void geometry_data(Walk& walk) {
  walk.skip(4);
  const std::size_t vertices = walk.u16();
  walk.skip(4);
  if (walk.u8() != 0) walk.skip(12 * vertices);
  const auto flags = walk.u16();
  walk.skip(4);
  const bool normals = walk.u8() != 0;
  if (normals) walk.skip(12 * vertices);
  if (normals && (flags & 0x1000U) != 0) walk.skip(24 * vertices);
  walk.skip(16);
  if (walk.u8() != 0) walk.skip(16 * vertices);
  walk.skip(8 * vertices * (flags & 1U));
  walk.ref();
}

struct Layout {
  std::string_view type;
  void (*walk)(Walk&);
};

const Layout k_layouts[] = {
    {"NiCamera", [](Walk& w) { w.av_object(); w.skip(47); w.ref(); w.skip(8); }},
    {"NiPointLight", [](Walk& w) { w.av_object(); w.skip(52); }},
    {"BSBlastNode", [](Walk& w) { w.node(); w.skip(3); }},
    {"BSDamageStage", [](Walk& w) { w.node(); w.skip(3); }},
    {"BSTreeNode", [](Walk& w) { w.node(); w.ref_list(); w.ref_list(); }},
    {"BSMasterParticleSystem", [](Walk& w) { w.node(); w.skip(2); w.ref_list(); }},
    {"BSProceduralLightningController",
     [](Walk& w) {
       w.time_controller();
       w.refs(9);
       w.skip(29);
       w.ref();
     }},
    {"BSMultiBound", [](Walk& w) { w.ref(); }},
    {"BSWaterShaderProperty", [](Walk& w) { w.object_net(); w.skip(28); }},
    {"BSSkyShaderProperty", [](Walk& w) { w.object_net(); w.skip(24); w.skip(w.u32()); w.skip(4); }},
    {"NiParticleSystem",
     [](Walk& w) {
       w.av_object();
       w.skip(16);
       w.refs(3);
       w.skip(16);
       w.ref();
       w.skip(1);
       w.ref_list();
     }},
    {"NiPSysData",
     [](Walk& w) {
       geometry_data(w);
       w.skip(8);
       w.skip(16 * std::size_t{w.u32()} + 18);
       w.skip(1);
     }},
    {"NiPSysAgeDeathModifier", [](Walk& w) { w.modifier(); w.skip(1); w.ref(); }},
    {"NiPSysBoundUpdateModifier", [](Walk& w) { w.modifier(); w.skip(2); }},
    {"NiPSysPositionModifier", [](Walk& w) { w.modifier(); }},
    {"NiPSysSpawnModifier", [](Walk& w) { w.modifier(); w.skip(26); }},
    {"NiPSysGravityModifier", [](Walk& w) { w.modifier(); w.ref(); w.skip(33); }},
    {"NiPSysDragModifier", [](Walk& w) { w.modifier(); w.ref(); w.skip(24); }},
    {"NiPSysRotationModifier", [](Walk& w) { w.modifier(); w.skip(30); }},
    {"BSPSysSimpleColorModifier", [](Walk& w) { w.modifier(); w.skip(72); }},
    {"BSPSysScaleModifier", [](Walk& w) { w.modifier(); w.skip(4 * std::size_t{w.u32()}); }},
    {"BSPSysLODModifier", [](Walk& w) { w.modifier(); w.skip(16); }},
    {"BSPSysSubTexModifier", [](Walk& w) { w.modifier(); w.skip(28); }},
    {"BSWindModifier", [](Walk& w) { w.modifier(); w.skip(4); }},
    {"BSPSysInheritVelocityModifier", [](Walk& w) { w.modifier(); w.ref(); w.skip(12); }},
    {"BSPSysRecycleBoundModifier", [](Walk& w) { w.modifier(); w.skip(24); w.ref(); }},
    {"BSParentVelocityModifier", [](Walk& w) { w.modifier(); w.skip(4); }},
    {"NiPSysBombModifier", [](Walk& w) { w.modifier(); w.ref(); w.skip(28); }},
    {"NiPSysColliderManager", [](Walk& w) { w.modifier(); w.ref(); }},
    {"NiPSysBoxEmitter", [](Walk& w) { w.emitter(); w.ref(); w.skip(12); }},
    {"NiPSysSphereEmitter", [](Walk& w) { w.emitter(); w.ref(); w.skip(4); }},
    {"NiPSysCylinderEmitter", [](Walk& w) { w.emitter(); w.ref(); w.skip(8); }},
    {"NiPSysMeshEmitter", [](Walk& w) { w.emitter(); w.ref_list(); w.skip(20); }},
    {"NiPSysPlanarCollider", [](Walk& w) { w.collider(); w.skip(32); }},
    {"NiPSysSphericalCollider", [](Walk& w) { w.collider(); w.skip(4); }},
    {"NiPSysEmitterCtlr", [](Walk& w) { w.single_controller(); w.skip(4); w.ref(); }},
    {"BSPSysMultiTargetEmitterCtlr",
     [](Walk& w) {
       w.single_controller();
       w.skip(4);
       w.ref();
       w.skip(2);
       w.ref();
     }},
    {"NiPSysEmitterSpeedCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
    {"NiPSysEmitterInitialRadiusCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
    {"NiPSysEmitterLifeSpanCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
    {"NiPSysGravityStrengthCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
    {"NiPSysEmitterDeclinationCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
    {"NiPSysEmitterDeclinationVarCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
    {"NiPSysEmitterPlanarAngleCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
    {"NiPSysInitialRotSpeedCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
    {"NiPSysInitialRotSpeedVarCtlr", [](Walk& w) { w.single_controller(); w.skip(4); }},
};

}  // namespace

std::optional<std::vector<std::size_t>> particle_ref_offsets(const Model& model, std::string_view type,
                                                             const Bytes& bytes) {
  for (const auto& layout : k_layouts) {
    if (layout.type != type) continue;
    require_fo4_version(model, type);
    Walk walk(bytes);
    layout.walk(walk);
    return walk.finish(type);
  }
  return std::nullopt;
}

bool has_particle_child_list(std::string_view type) {
  return type == "BSBlastNode" || type == "BSDamageStage" || type == "BSTreeNode" || type == "BSMasterParticleSystem";
}

}  // namespace fo4::nif::write_detail
