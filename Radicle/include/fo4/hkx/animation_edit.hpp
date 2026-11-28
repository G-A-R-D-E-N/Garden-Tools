#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/spline_write.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::hkx {

enum class HkxEditStatus : std::uint8_t {
  Ok = 0,
  BadPackfile,
  UnsupportedClip,
  UnsupportedMotion,
  BindingMismatch,
  BadRange,
  BadValue,
  NotRebuildable,
  BudgetExceeded,
  WriteFailed,
};

// Invariant: this is the one spelling of a status, so a consumer's refusal text and the log cannot disagree.
const char* hkx_edit_status_name(HkxEditStatus status);

// Invariant: an edit keeps the track set, so the binding's track maps stay valid for the edited clip.
struct HkxClipEdit {
  HkxAnimation animation;
  HkxExtractedMotion motion;
};

struct HkxEditReport {
  std::size_t annotations_dropped{0};
  bool resampled{false};
  float position_error{0.0f};
  float rotation_error{0.0f};
  float scale_error{0.0f};
};

struct HkxRetimeBudget {
  float position{std::numeric_limits<float>::infinity()};
  float rotation{std::numeric_limits<float>::infinity()};
  float scale{std::numeric_limits<float>::infinity()};
};

struct HkxKey {
  std::optional<std::array<float, 3>> translation;
  std::optional<std::array<float, 4>> rotation;
  std::optional<std::array<float, 3>> scale;
};

// Requires one decodable clip, no float tracks, absent or Default root motion and a binding at the clip; else refused.
HkxEditStatus open_clip_edit(std::span<const std::byte> source, HkxClipEdit& out);

// Constraint: every edit works on a copy and swaps it in on success; trim requires first <= last and two frames kept.
HkxEditStatus trim_clip(HkxClipEdit& clip, std::size_t first_frame, std::size_t last_frame,
                        HkxEditReport* report = nullptr);

// Requires a scale in [0.01, 100]; keep_frame_rate resamples, otherwise the same frames play at another rate.
HkxEditStatus retime_clip(HkxClipEdit& clip, float scale, bool keep_frame_rate, const HkxRetimeBudget& budget = {},
                          HkxEditReport* report = nullptr);

// Requires 1 <= blend_frames <= frame_count - 1; the last frames ease into frame 0 and root motion is left alone.
HkxEditStatus loop_clip(HkxClipEdit& clip, std::size_t blend_frames, HkxEditReport* report = nullptr);

// Requires at least one field set; a rotation must be finite and not all zero, and is stored normalised.
HkxEditStatus set_key(HkxClipEdit& clip, std::size_t track, std::size_t frame, const HkxKey& key);

HkxEditStatus copy_pose(HkxClipEdit& clip, std::size_t from_frame, std::size_t to_frame);

// Constraint: frame-structure edits must refuse with NotRebuildable while Default root motion has samples.
HkxEditStatus insert_frames(HkxClipEdit& clip, std::size_t at, std::size_t count);
HkxEditStatus delete_frames(HkxClipEdit& clip, std::size_t first, std::size_t count,
                            HkxEditReport* report = nullptr);
// Requires the moved block to start at `to` in the result; annotation times stay where they are.
HkxEditStatus move_frames(HkxClipEdit& clip, std::size_t first, std::size_t count, std::size_t to);

// Requires a finite time in [0, duration]; an unknown track name creates the track.
HkxEditStatus add_annotation(HkxClipEdit& clip, std::string_view track_name, float time, std::string_view text);
HkxEditStatus edit_annotation(HkxClipEdit& clip, std::size_t track, std::size_t index, float time,
                              std::string_view text);
HkxEditStatus remove_annotation(HkxClipEdit& clip, std::size_t track, std::size_t index);

struct HkxSaveOptions {
  bool spline{false};
  HkxSplineOptions spline_options{};
  float max_position_error{0.02f};
  float max_rotation_error{0.01f};
  float max_scale_error{0.01f};
};

// Requires `source` to be the file the edit was opened from; out stays empty on any refusal.
HkxEditStatus save_clip_edit(std::span<const std::byte> source, const HkxClipEdit& edited,
                             const HkxSaveOptions& options, std::vector<std::byte>& out,
                             HkxEditReport* report = nullptr);

}  // namespace fo4::hkx
