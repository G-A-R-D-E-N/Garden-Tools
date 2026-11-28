#pragma once

#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/spline_write.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

struct HkxAppend {
  std::vector<std::byte> bytes;
  // Invariant: object_index is the appended object's position in the file's object order.
  std::size_t object_index{0};
};

// Requires a reader-accepted packfile, a class the layout table knows, and instance_bytes of exactly its size.
HkxStatus append_object(std::span<const std::byte> source, std::string_view class_name, std::uint32_t signature,
                        std::span<const std::byte> instance_bytes, HkxAppend& out);

// Invariant: verify needs one more object with unchanged old offsets and bytes; empty skips the byte check.
HkxStatus verify_append(std::span<const std::byte> source, std::span<const std::byte> written,
                        std::span<const std::byte> expected_instance = {});

// Requires frame_count * track_count finite samples; appends a clip with transforms, empty tails and no binding.
HkxStatus append_interleaved_animation(std::span<const std::byte> source, const HkxInterleavedAnimation& animation,
                                       HkxAppend& out);

// Requires a clip whose derived block timing is finite and positive; writes empty tails and no binding.
// Constraint: the reader accepts only the six rotation quantisations it decodes, so this writer uses format 5.
HkxStatus append_spline_animation(std::span<const std::byte> source, const HkxInterleavedAnimation& animation,
                                  const HkxSplineOptions& options, HkxAppend& out);

// Constraint: the packfile must be one read from `source` and edited in memory; it is laid out again with the source's headers.
HkxStatus rewrite_packfile(std::span<const std::byte> source, const HkxPackfile& packfile,
                           std::vector<std::byte>& out);

struct HkxRemoval {
  // Invariant: ascending indices into the object list as read, the requested objects plus any the cascade took.
  std::vector<std::size_t> removed;
  // Invariant: empty on success, else one sentence naming why nothing was removed.
  std::string refusal;
};

// Requires a packfile as read and not grown since: its object list must still match the data section's virtual fixups.
// Constraint: removal does not move anything, so the dead bytes are zeroed and their fixups dropped and the file does not shrink.
// Constraint: an object is refused while anything outside the dead bytes points at it; with cascade, an object whose referrers all died dies too.
// Invariant: on any refusal the packfile is unchanged; on success its object list is stale, so write it with rewrite_packfile and read the result.
HkxStatus remove_objects(HkxPackfile& packfile, std::span<const std::size_t> object_indices, bool cascade,
                         HkxRemoval& out);

enum class HkxRepackVerdict : std::uint8_t {
  Equal = 0,
  ReadFailed,
  Refused,
  RewriteFailed,
  RereadFailed,
  Differs,
};

struct HkxRepackReport {
  HkxRepackVerdict verdict{HkxRepackVerdict::ReadFailed};
  HkxStatus status{HkxStatus::Ok};
  // Invariant: informational only; the contract is model equality, and rewrite_packfile re-lays the fixup tables.
  bool bytes_identical{false};
  std::size_t source_size{0};
  std::size_t rewritten_size{0};
  // Invariant: empty unless the verdict is Differs, then one sentence naming the first differing part.
  std::string difference;
};

// Constraint: `b` is the reread of a rewrite of `a`, so b's section data may only extend a's with 0xFF filler; empty means equal.
std::string first_packfile_difference(const HkxPackfile& a, const HkxPackfile& b);

// Constraint: Equal means read, rewrite_packfile, reread gives the same packfile model; Refused is a writer-unsupported layout.
HkxRepackReport repack_check(std::span<const std::byte> bytes);

}  // namespace fo4::hkx
