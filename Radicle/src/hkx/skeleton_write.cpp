#include "fo4/hkx/skeleton_write.hpp"

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using detail::Elements;
using detail::elements_at;
using detail::holds_layout;
using detail::local_target;
using detail::value_at;

constexpr std::size_t kQsTransformSize = 48;
constexpr std::size_t kParentIndexSize = 2;
constexpr std::size_t kArrayHeaderBytes = 16;
constexpr std::size_t kCountAt = 8;
constexpr std::size_t kCapacityAt = 12;
constexpr std::uint32_t kCapacityFlags = 0xC0000000u;
// Constraint: these lane offsets are the ones skeleton.cpp:decode_skeleton reads, read back by the pose tests.
constexpr std::size_t kQsRotationOffset = 16;
constexpr std::size_t kQsScaleOffset = 32;
constexpr std::size_t kFloatSize = 4;

struct Range {
  std::size_t start{0};
  std::size_t length{0};
};

struct Run {
  std::size_t header{0};
  std::size_t first{0};
  std::size_t count{0};
  std::size_t stride{0};

  std::size_t length() const { return count * stride; }
  bool holds(std::size_t at) const { return at >= first && at < first + length(); }
};

struct Rig {
  Run bones;
  Run parents;
  Run pose;
  std::size_t bone_name{0};
  std::optional<std::size_t> bone_lock;
};

HkxStatus refuse(std::string& why, std::string text) {
  why = std::move(text);
  return HkxStatus::UnsupportedLayout;
}

bool same_bits(const void* a, const void* b, std::size_t size) { return std::memcmp(a, b, size) == 0; }

bool same_transform(const HkxQsTransform& a, const HkxQsTransform& b) {
  return same_bits(a.translation.data(), b.translation.data(), sizeof(float) * 3) &&
         same_bits(a.rotation.data(), b.rotation.data(), sizeof(float) * 4) &&
         same_bits(a.scale.data(), b.scale.data(), sizeof(float) * 3);
}

// Constraint: floats compare by bits, so a NaN reference float that was read still reads back equal.
bool same_rig(const HkxSkeleton& a, const HkxSkeleton& b) {
  if (a.name != b.name || a.bone_names != b.bone_names || a.parent_indices != b.parent_indices ||
      a.lock_translation != b.lock_translation || a.reference_pose.size() != b.reference_pose.size() ||
      a.reference_floats.size() != b.reference_floats.size() || a.float_slots.size() != b.float_slots.size() ||
      a.local_frames.size() != b.local_frames.size() || a.partitions.size() != b.partitions.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.reference_pose.size(); ++i) {
    if (!same_transform(a.reference_pose[i], b.reference_pose[i])) return false;
  }
  if (!a.reference_floats.empty() &&
      !same_bits(a.reference_floats.data(), b.reference_floats.data(), a.reference_floats.size() * sizeof(float))) {
    return false;
  }
  for (std::size_t i = 0; i < a.float_slots.size(); ++i) {
    if (a.float_slots[i].name != b.float_slots[i].name || a.float_slots[i].named != b.float_slots[i].named) return false;
  }
  for (std::size_t i = 0; i < a.local_frames.size(); ++i) {
    const auto& x = a.local_frames[i];
    const auto& y = b.local_frames[i];
    if (x.bone_index != y.bone_index || x.has_frame != y.has_frame || x.frame_offset != y.frame_offset) return false;
  }
  for (std::size_t i = 0; i < a.partitions.size(); ++i) {
    const auto& x = a.partitions[i];
    const auto& y = b.partitions[i];
    if (x.name != y.name || x.named != y.named || x.start_bone != y.start_bone || x.num_bones != y.num_bones) {
      return false;
    }
  }
  return true;
}

HkxStatus locate_object(const HkxPackfile& packfile, const HkxSkeleton& chosen, std::size_t& object, std::string& why) {
  std::size_t found = packfile.objects.size();
  for (std::size_t i = 0; i < packfile.objects.size(); ++i) {
    if (packfile.objects[i].class_name != "hkaSkeleton") continue;
    HkxSkeleton rig;
    if (read_skeleton_object(packfile, packfile.objects[i], rig) != HkxStatus::Ok) continue;
    if (rig.name != chosen.name || rig.bone_names != chosen.bone_names || rig.parent_indices != chosen.parent_indices) {
      continue;
    }
    if (found != packfile.objects.size()) {
      return refuse(why, "the file holds two identical skeletons, so the one to edit is ambiguous");
    }
    found = i;
  }
  if (found == packfile.objects.size()) return refuse(why, "the skeleton that was read is not an object of the file");
  object = found;
  return HkxStatus::Ok;
}

HkxStatus find_runs(const HkxPackfile& packfile, const HkxObjectView& object, Rig& rig, std::string& why) {
  const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
  const HkxClassLayout* bone = find_layout("hkaBone");
  if (layout == nullptr || bone == nullptr || !holds_layout(object, *layout)) return HkxStatus::UnsupportedLayout;
  const auto name = find_member_offset(*bone, "name");
  const auto parents = find_member_offset(*layout, "parentIndices");
  const auto bones = find_member_offset(*layout, "bones");
  const auto pose = find_member_offset(*layout, "referencePose");
  if (!name || !parents || !bones || !pose || bone->size == 0) return HkxStatus::UnsupportedLayout;
  rig.bone_name = static_cast<std::size_t>(*name);
  if (const auto lock = find_member_offset(*bone, "lockTranslation"); lock && static_cast<std::size_t>(*lock) < bone->size) {
    rig.bone_lock = static_cast<std::size_t>(*lock);
  }
  if (packfile.pointer_size != 8) return refuse(why, "only 8-byte pointer packfiles are supported");

  const HkxSection& data = packfile.sections[packfile.data_section];
  const auto fill = [&](std::size_t member, std::size_t stride, Run& run) {
    run.header = object.offset + member;
    run.stride = stride;
    if (run.header > data.data.size() || data.data.size() - run.header < kArrayHeaderBytes) {
      return HkxStatus::FixupOutOfRange;
    }
    Elements elements;
    if (const auto status = elements_at(data, run.header, packfile.pointer_size, stride, elements);
        status != HkxStatus::Ok) {
      return status;
    }
    run.first = elements.first;
    run.count = elements.count;
    return HkxStatus::Ok;
  };
  if (const auto status = fill(static_cast<std::size_t>(*bones), bone->size, rig.bones); status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = fill(static_cast<std::size_t>(*parents), kParentIndexSize, rig.parents);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = fill(static_cast<std::size_t>(*pose), kQsTransformSize, rig.pose); status != HkxStatus::Ok) {
    return status;
  }
  if (rig.bones.count == 0 || rig.parents.count != rig.bones.count || rig.pose.count != rig.bones.count) {
    return refuse(why, "the bone, parent index and reference pose arrays do not have the same length");
  }
  return HkxStatus::Ok;
}

// Requires the same standalone rule as BGS: bone indices are used by every class that is not one of these three.
std::string removal_refusal(const HkxPackfile& packfile, const HkxSkeleton& rig) {
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name != "hkRootLevelContainer" && object.class_name != "hkaAnimationContainer" &&
        object.class_name != "hkaSkeleton") {
      return "removing a bone would shift bone indices that " + std::string(object.class_name) +
             " may use, so only a skeleton-only file can lose bones";
    }
  }
  if (!rig.partitions.empty()) return "this skeleton has partitions, whose bone ranges a removal would have to rewrite";
  if (!rig.local_frames.empty()) return "this skeleton has local frames, whose bone indices a removal would have to rewrite";
  return {};
}

// Constraint: an array is only moved in place when nothing else points into it or reads bytes inside it.
std::string storage_refusal(const HkxPackfile& packfile, const Rig& rig) {
  const std::array<const Run*, 3> runs{&rig.bones, &rig.parents, &rig.pose};
  const auto in_any = [&](std::size_t at) {
    return std::any_of(runs.begin(), runs.end(), [at](const Run* run) { return run->holds(at); });
  };
  const auto is_header = [&](std::size_t at) {
    return at == rig.bones.header || at == rig.parents.header || at == rig.pose.header;
  };
  for (std::size_t i = 0; i < runs.size(); ++i) {
    for (std::size_t j = i + 1; j < runs.size(); ++j) {
      if (runs[i]->first < runs[j]->first + runs[j]->length() && runs[j]->first < runs[i]->first + runs[i]->length()) {
        return "the bone arrays share storage";
      }
    }
  }
  for (const HkxObjectView& object : packfile.objects) {
    if (in_any(object.offset)) return "an object starts inside a bone array";
  }
  const HkxSection& data = packfile.sections[packfile.data_section];
  for (const HkxLocalFixup& fixup : data.local_fixups) {
    if (is_header(fixup.source)) continue;
    if (in_any(fixup.source)) {
      const bool name_pointer =
          rig.bones.holds(fixup.source) && (fixup.source - rig.bones.first) % rig.bones.stride == rig.bone_name;
      if (!name_pointer) return "a pointer sits inside a bone array";
    }
    if (in_any(fixup.destination)) return "another pointer shares the storage of a bone array";
  }
  for (std::size_t s = 0; s < packfile.sections.size(); ++s) {
    for (const HkxRemoteFixup& fixup : packfile.sections[s].global_fixups) {
      if (s == packfile.data_section && in_any(fixup.source)) return "a pointer sits inside a bone array";
      if (fixup.section == packfile.data_section && in_any(fixup.destination)) {
        return "another object points into a bone array";
      }
    }
  }
  return {};
}

bool confined(const std::vector<std::byte>& before, const std::vector<std::byte>& after,
              const std::vector<Range>& allowed) {
  if (after.size() < before.size()) return false;
  std::vector<char> open(after.size(), 0);
  for (const Range& range : allowed) {
    for (std::size_t i = 0; i < range.length && range.start + i < open.size(); ++i) open[range.start + i] = 1;
  }
  for (std::size_t i = 0; i < after.size(); ++i) {
    if (i < before.size() && before[i] == after[i]) continue;
    if (open[i] == 0) return false;
  }
  return true;
}

bool same_locals(const std::vector<HkxLocalFixup>& a, const std::vector<HkxLocalFixup>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const HkxLocalFixup& x, const HkxLocalFixup& y) {
    return x.source == y.source && x.destination == y.destination;
  });
}

// Constraint: rewrite_packfile sorts these tables by source, so table order is not compared.
bool same_remotes(std::vector<HkxRemoteFixup> a, std::vector<HkxRemoteFixup> b) {
  const auto by_source = [](const HkxRemoteFixup& x, const HkxRemoteFixup& y) {
    return x.source != y.source ? x.source < y.source : x.destination < y.destination;
  };
  std::sort(a.begin(), a.end(), by_source);
  std::sort(b.begin(), b.end(), by_source);
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const HkxRemoteFixup& x, const HkxRemoteFixup& y) {
    return x.source == y.source && x.section == y.section && x.destination == y.destination;
  });
}

// Constraint: rewrite_packfile pads each section to 16 bytes with 0xFF, so a written section may only gain that filler.
bool same_with_filler(const std::vector<std::byte>& expected, const std::vector<std::byte>& got) {
  if (got.size() < expected.size() || !std::equal(expected.begin(), expected.end(), got.begin())) return false;
  return std::all_of(got.begin() + static_cast<std::ptrdiff_t>(expected.size()), got.end(),
                     [](std::byte value) { return value == std::byte{0xFF}; });
}

void put_u32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
  std::memcpy(bytes.data() + at, &value, sizeof value);
}

// Invariant: bones, parents and pose are rewritten as one gather, so the three arrays always keep one entry per bone.
HkxStatus rewrite_rig(HkxPackfile& work, const Rig& rig, const HkxSkeleton& was, const HkxSkeleton& edited,
                      const std::vector<std::size_t>& origin, std::vector<Range>& allowed, std::string& why) {
  HkxSection& data = work.sections[work.data_section];
  const std::size_t had = was.bone_names.size();
  const std::size_t now = edited.bone_names.size();
  const std::size_t grown_from = data.data.size();
  const std::size_t stride = rig.bones.stride;
  if (origin.size() != now || edited.parent_indices.size() != now || edited.reference_pose.size() != now ||
      edited.lock_translation.size() != now || was.reference_pose.size() != had ||
      was.lock_translation.size() != had) {
    return HkxStatus::BadSection;
  }

  std::vector<std::size_t> old_name(had);
  for (std::size_t i = 0; i < had; ++i) {
    const auto target = local_target(data, rig.bones.first + i * stride + rig.bone_name);
    if (!target) return HkxStatus::FixupOutOfRange;
    old_name[i] = *target;
  }

  std::vector<std::byte> bone_rows(now * stride);
  std::vector<std::byte> parent_rows(now * kParentIndexSize);
  std::vector<std::byte> pose_rows(now * kQsTransformSize);
  std::vector<HkxLocalFixup> names;
  for (std::size_t j = 0; j < now; ++j) {
    const std::size_t from = origin[j];
    if (from >= had) return HkxStatus::BadSection;
    std::copy_n(data.data.begin() + static_cast<std::ptrdiff_t>(rig.bones.first + from * stride), stride,
                bone_rows.begin() + static_cast<std::ptrdiff_t>(j * stride));
    std::copy_n(data.data.begin() + static_cast<std::ptrdiff_t>(rig.pose.first + from * kQsTransformSize),
                kQsTransformSize, pose_rows.begin() + static_cast<std::ptrdiff_t>(j * kQsTransformSize));
    std::memcpy(parent_rows.data() + j * kParentIndexSize, &edited.parent_indices[j], kParentIndexSize);
    if (!same_transform(edited.reference_pose[j], was.reference_pose[from])) {
      std::byte* row = pose_rows.data() + j * kQsTransformSize;
      const HkxQsTransform& pose = edited.reference_pose[j];
      for (std::size_t k = 0; k < 3; ++k) {
        std::memcpy(row + k * kFloatSize, &pose.translation[k], kFloatSize);
        std::memcpy(row + kQsScaleOffset + k * kFloatSize, &pose.scale[k], kFloatSize);
      }
      for (std::size_t k = 0; k < 4; ++k) std::memcpy(row + kQsRotationOffset + k * kFloatSize, &pose.rotation[k], kFloatSize);
    }
    if (edited.lock_translation[j] != was.lock_translation[from]) {
      if (!rig.bone_lock) {
        why = "this skeleton layout has no translation lock byte to change";
        return HkxStatus::UnsupportedLayout;
      }
      bone_rows[j * stride + *rig.bone_lock] = std::byte{static_cast<unsigned char>(edited.lock_translation[j] ? 1 : 0)};
    }
    std::size_t text = old_name[from];
    if (edited.bone_names[j] != was.bone_names[from]) {
      text = data.data.size();
      for (const char ch : edited.bone_names[j]) data.data.push_back(static_cast<std::byte>(ch));
      data.data.push_back(std::byte{0});
    }
    names.push_back({static_cast<std::uint32_t>(rig.bones.first + j * stride + rig.bone_name),
                     static_cast<std::uint32_t>(text)});
  }

  const auto put = [&](const Run& run, const std::vector<std::byte>& rows) {
    std::copy(rows.begin(), rows.end(), data.data.begin() + static_cast<std::ptrdiff_t>(run.first));
    std::fill(data.data.begin() + static_cast<std::ptrdiff_t>(run.first + rows.size()),
              data.data.begin() + static_cast<std::ptrdiff_t>(run.first + run.length()), std::byte{0});
    allowed.push_back({run.first, run.length()});
    if (now == had) return;
    const std::uint32_t flags = value_at<std::uint32_t>(data.data, run.header + kCapacityAt) & kCapacityFlags;
    put_u32(data.data, run.header + kCountAt, static_cast<std::uint32_t>(now));
    put_u32(data.data, run.header + kCapacityAt, flags | static_cast<std::uint32_t>(now));
    allowed.push_back({run.header + kCountAt, kArrayHeaderBytes - kCountAt});
  };
  put(rig.bones, bone_rows);
  put(rig.parents, parent_rows);
  put(rig.pose, pose_rows);
  allowed.push_back({grown_from, data.data.size() - grown_from});

  const std::size_t bones_end = rig.bones.first + rig.bones.length();
  auto& locals = data.local_fixups;
  locals.erase(std::remove_if(locals.begin(), locals.end(),
                              [&](const HkxLocalFixup& fixup) {
                                return fixup.source >= rig.bones.first && fixup.source < bones_end;
                              }),
               locals.end());
  locals.insert(locals.end(), names.begin(), names.end());
  std::sort(locals.begin(), locals.end(),
            [](const HkxLocalFixup& a, const HkxLocalFixup& b) { return a.source < b.source; });
  return HkxStatus::Ok;
}

HkxStatus verify_written(const HkxPackfile& original, const HkxPackfile& work, std::span<const std::byte> produced,
                         std::size_t object, const HkxSkeleton& edited, std::string& why) {
  HkxPackfile back;
  if (read_packfile(produced, back) != HkxStatus::Ok) return refuse(why, "the written file could not be read back");
  if (back.sections.size() != original.sections.size() || back.user_tag != original.user_tag ||
      back.file_version != original.file_version || back.data_section != original.data_section ||
      back.class_names_section != original.class_names_section) {
    return refuse(why, "the written file changed its container layout");
  }
  if (back.objects.size() != original.objects.size()) return refuse(why, "the written file changed its object count");
  for (std::size_t i = 0; i < original.objects.size(); ++i) {
    if (back.objects[i].offset != original.objects[i].offset ||
        back.objects[i].signature != original.objects[i].signature ||
        back.objects[i].class_name != original.objects[i].class_name) {
      return refuse(why, "the written file moved or renamed an object");
    }
  }
  for (std::size_t i = 0; i < original.sections.size(); ++i) {
    const HkxSection& was = i == original.data_section ? work.sections[i] : original.sections[i];
    const HkxSection& now = back.sections[i];
    if (now.tag != was.tag || !same_with_filler(was.data, now.data) || !same_locals(was.local_fixups, now.local_fixups) ||
        !same_remotes(original.sections[i].global_fixups, now.global_fixups) ||
        !same_remotes(original.sections[i].virtual_fixups, now.virtual_fixups)) {
      return refuse(why, "the written file changed section " + original.sections[i].tag + " beyond the edited skeleton");
    }
  }
  HkxSkeleton reread;
  if (read_skeleton_object(back, back.objects[object], reread) != HkxStatus::Ok || !same_rig(reread, edited)) {
    return refuse(why, "the written skeleton did not read back as the edited one");
  }
  HkxSkeleton picked;
  if (read_skeleton(back, picked) != HkxStatus::Ok || !same_rig(picked, edited)) {
    return refuse(why, "the edited skeleton would no longer be the one the file reads as its rig");
  }
  return HkxStatus::Ok;
}

}  // namespace

HkxSkeletonEditReport apply_skeleton_edits(const HkxSkeleton& skeleton, std::span<const HkxSkeletonEdit> edits,
                                           HkxSkeleton& out, std::vector<std::size_t>* origin) {
  HkxSkeletonEditReport report;
  HkxSkeleton work = skeleton;
  std::vector<std::size_t> kept(skeleton.bone_names.size());
  std::iota(kept.begin(), kept.end(), std::size_t{0});
  for (std::size_t i = 0; i < edits.size(); ++i) {
    HkxSkeleton next;
    const auto step = apply_skeleton_edit(work, edits[i], next);
    if (!step.changed) {
      report.refusal = "edit " + std::to_string(i + 1) + ": " + step.refusal;
      return report;
    }
    if (edits[i].kind == HkxSkeletonEditKind::Remove) kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(edits[i].bone));
    work = std::move(next);
  }
  out = std::move(work);
  if (origin != nullptr) *origin = std::move(kept);
  report.changed = !edits.empty();
  return report;
}

HkxStatus write_skeleton(std::span<const std::byte> source, std::span<const HkxSkeletonEdit> edits,
                         HkxSkeletonWrite& out) {
  out = HkxSkeletonWrite{};
  std::string& why = out.refusal;

  HkxPackfile original;
  if (const auto status = read_packfile(source, original); status != HkxStatus::Ok) return status;
  HkxSkeleton was;
  if (const auto status = read_skeleton(original, was); status != HkxStatus::Ok) return status;

  HkxSkeleton edited;
  std::vector<std::size_t> origin;
  if (const auto report = apply_skeleton_edits(was, edits, edited, &origin); !report.refusal.empty()) {
    return refuse(why, report.refusal);
  }
  if (edited.bone_names.empty()) return refuse(why, "a skeleton must keep at least one bone");
  if (same_rig(was, edited)) return HkxStatus::Ok;

  std::size_t object = 0;
  if (const auto status = locate_object(original, was, object, why); status != HkxStatus::Ok) return status;
  Rig rig;
  if (const auto status = find_runs(original, original.objects[object], rig, why); status != HkxStatus::Ok) {
    return status;
  }
  if (edited.bone_names.size() < was.bone_names.size()) {
    if (auto text = removal_refusal(original, was); !text.empty()) return refuse(why, std::move(text));
  }
  if (auto text = storage_refusal(original, rig); !text.empty()) return refuse(why, std::move(text));

  HkxPackfile work;
  if (const auto status = read_packfile(source, work); status != HkxStatus::Ok) return status;
  HkxSection& data = work.sections[work.data_section];
  const std::vector<std::byte> data_before = data.data;
  const std::vector<HkxLocalFixup> locals_before = data.local_fixups;
  std::vector<Range> allowed;
  if (const auto status = rewrite_rig(work, rig, was, edited, origin, allowed, why); status != HkxStatus::Ok) {
    if (why.empty()) why = "the skeleton arrays could not be rewritten";
    return status;
  }
  if (!confined(data_before, data.data, allowed)) {
    return refuse(why, "a skeleton edit reached bytes outside the arrays it owns");
  }

  std::vector<std::byte> produced;
  if (data.data.size() == data_before.size() && same_locals(locals_before, data.local_fixups)) {
    produced.assign(source.begin(), source.end());
    for (std::size_t i = 0; i < data.data.size(); ++i) {
      if (data.data[i] != data_before[i]) produced[data.file_offset + i] = data.data[i];
    }
  } else if (const auto status = rewrite_packfile(source, work, produced); status != HkxStatus::Ok) {
    return refuse(why, "the packfile could not be laid out again with the edited skeleton");
  }

  if (const auto status = verify_written(original, work, produced, object, edited, why); status != HkxStatus::Ok) {
    return status;
  }
  out.bytes = std::move(produced);
  out.changed = true;
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
