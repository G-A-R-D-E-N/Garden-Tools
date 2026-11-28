#pragma once

#include "fo4/hkx/layout.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hkx {

enum class SampleLink : std::uint8_t { Linked, NoPointer, Truncated };

struct MotionSpec {
  std::string frame_class{"hkaDefaultAnimatedReferenceFrame"};
  std::array<float, 3> up{0.0f, 0.0f, 1.0f};
  std::array<float, 3> forward{0.0f, 1.0f, 0.0f};
  float duration{2.0f};
  std::vector<std::array<float, 4>> samples;
  std::uint8_t frame_type{0};
  // Invariant: NoPointer drops the sample fixup and Truncated counts four samples too many.
  SampleLink sample_link{SampleLink::Linked};
  bool second_clip{false};
  // Constraint: false leaves the frame object in the file with no fixup from extractedMotion.
  bool link_frame{true};
};

// Constraint: read_extracted_motion selects the clip first, so the clip must be a supported class.
inline std::vector<std::byte> motion_packfile(const std::optional<MotionSpec>& spec) {
  std::vector<std::byte> names;
  const auto clip_name = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                        "hkaInterleavedUncompressedAnimation");
  std::vector<std::byte> data(fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size, std::byte{0});
  std::vector<std::uint32_t> local;
  std::vector<std::uint32_t> virtuals{0, 0, clip_name};

  if (spec) {
    const auto frame_name = add_class_name(names, signature_of(spec->frame_class.c_str()), spec->frame_class);
    const std::size_t frame_at = data.size();
    data.resize(frame_at + fo4::hkx::find_layout(spec->frame_class.c_str())->size, std::byte{0});
    if (spec->link_frame) link(local, member_at("hkaAnimation", "extractedMotion"), frame_at);
    virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(frame_at), 0u, frame_name});
    data[frame_at + member_at("hkaAnimatedReferenceFrame", "frameType")] = std::byte{spec->frame_type};

    if (spec->frame_class == "hkaDefaultAnimatedReferenceFrame") {
      const char* default_frame = "hkaDefaultAnimatedReferenceFrame";
      for (std::size_t axis = 0; axis < 3; ++axis) {
        patch_float(data, frame_at + member_at(default_frame, "up") + 4 * axis, spec->up[axis]);
        patch_float(data, frame_at + member_at(default_frame, "forward") + 4 * axis, spec->forward[axis]);
      }
      patch_float(data, frame_at + member_at(default_frame, "duration"), spec->duration);

      const std::size_t samples_at = data.size();
      for (const auto& sample : spec->samples) {
        for (const float value : sample) put_float(data, value);
      }
      const std::size_t samples_field = frame_at + member_at(default_frame, "referenceFrameSamples");
      if (spec->sample_link != SampleLink::NoPointer) link(local, samples_field, samples_at);
      set_count(data, samples_field, spec->samples.size() + (spec->sample_link == SampleLink::Truncated ? 4u : 0u));
    }
    if (spec->second_clip) {
      const std::size_t second_at = data.size();
      data.resize(second_at + fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size, std::byte{0});
      virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(second_at), 0u, clip_name});
    }
  }

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, std::move(virtuals)},
  });
}

}  // namespace fo4::test::hkx
