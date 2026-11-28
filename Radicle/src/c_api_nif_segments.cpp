#include "fo4/c_api_nif.h"

#include "c_api_internal.hpp"
#include "c_api_nif_internal.hpp"
#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using namespace radicle_c_detail;

constexpr const char* k_segment_type = "BSSubIndexTriShape";
constexpr CopyMessages k_cut_offsets{"out_values and out_count are required", "failed to allocate segment cut offsets"};
constexpr CopyMessages k_ssf_file{"out_bytes and out_size are required", "failed to allocate a segment ssf path"};

// Invariant: a non-segment block is NOT_FOUND and a block past the file is INVALID_ARGUMENT, both before the reader runs.
template <typename Body>
radicle_status with_table(const radicle_nif* nif, uint32_t block, const char* what, Body body) {
  clear_error();
  if (!nif) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif is required");
  if (block >= nif->file.blocks().size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "block index is out of range");
  if (nif->file.blocks()[block].type != k_segment_type) {
    return fail(RADICLE_STATUS_NOT_FOUND, "block is not a BSSubIndexTriShape");
  }
  return guard_exceptions(what, [&] { return body(fo4::nif::read_segments(nif->file, block)); });
}

radicle_status invalid(const char* message) { return fail(RADICLE_STATUS_INVALID_ARGUMENT, message); }

}  // namespace

extern "C" {

radicle_status radicle_nif_read_segments(const radicle_nif* nif, uint32_t block, radicle_nif_segment_table* out_table) {
  if (!out_table) return invalid("out_table is required");
  *out_table = {};
  return with_table(nif, block, "unknown exception while reading a segment table", [&](const auto& table) {
    radicle_nif_segment_table out{};
    out.primitives = table.primitives;
    out.total_segments = table.total_segments;
    out.segment_count = table.segments.size();
    out.segment_start_count = table.segment_starts.size();
    out.user_data_count = table.user_data.size();
    *out_table = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_read_segment(const radicle_nif* nif,
                                        uint32_t block,
                                        size_t segment,
                                        radicle_nif_segment* out_segment) {
  if (!out_segment) return invalid("out_segment is required");
  *out_segment = {};
  return with_table(nif, block, "unknown exception while reading a segment", [&](const auto& table) {
    if (segment >= table.segments.size()) return invalid("segment index is out of range");
    const auto& source = table.segments[segment];
    radicle_nif_segment out{};
    out.start_index = source.start_index;
    out.primitives = source.primitives;
    out.parent_array_index = source.parent_array_index;
    out.sub_segment_count = source.sub_segments.size();
    *out_segment = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_read_sub_segment(const radicle_nif* nif,
                                            uint32_t block,
                                            size_t segment,
                                            size_t sub_segment,
                                            radicle_nif_sub_segment* out_sub_segment) {
  if (!out_sub_segment) return invalid("out_sub_segment is required");
  *out_sub_segment = {};
  return with_table(nif, block, "unknown exception while reading a sub-segment", [&](const auto& table) {
    if (segment >= table.segments.size()) return invalid("segment index is out of range");
    const auto& subs = table.segments[segment].sub_segments;
    if (sub_segment >= subs.size()) return invalid("sub-segment index is out of range");
    radicle_nif_sub_segment out{};
    out.start_index = subs[sub_segment].start_index;
    out.primitives = subs[sub_segment].primitives;
    out.parent_array_index = subs[sub_segment].parent_array_index;
    out.unused = subs[sub_segment].unused;
    *out_sub_segment = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_segment_start(const radicle_nif* nif, uint32_t block, size_t index, uint32_t* out_start) {
  if (!out_start) return invalid("out_start is required");
  *out_start = 0;
  return with_table(nif, block, "unknown exception while reading a segment start", [&](const auto& table) {
    if (index >= table.segment_starts.size()) return invalid("segment start index is out of range");
    *out_start = table.segment_starts[index];
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_read_segment_user_data(const radicle_nif* nif,
                                                  uint32_t block,
                                                  size_t index,
                                                  radicle_nif_segment_user_data* out_user_data) {
  if (!out_user_data) return invalid("out_user_data is required");
  *out_user_data = {};
  return with_table(nif, block, "unknown exception while reading segment user data", [&](const auto& table) {
    if (index >= table.user_data.size()) return invalid("segment user data index is out of range");
    const auto& source = table.user_data[index];
    radicle_nif_segment_user_data out{};
    out.user_index = source.user_index;
    out.bone_id = source.bone_id;
    out.cut_offset_count = source.cut_offsets.size();
    *out_user_data = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_segment_cut_offsets(const radicle_nif* nif,
                                               uint32_t block,
                                               size_t index,
                                               float** out_values,
                                               size_t* out_count) {
  if (!out_values || !out_count) return invalid("out_values and out_count are required");
  *out_values = nullptr;
  *out_count = 0;
  return with_table(nif, block, "unknown exception while reading segment cut offsets", [&](const auto& table) {
    if (index >= table.user_data.size()) return invalid("segment user data index is out of range");
    return copy_out(table.user_data[index].cut_offsets, out_values, out_count, k_cut_offsets);
  });
}

radicle_status radicle_nif_segment_ssf_file(const radicle_nif* nif,
                                            uint32_t block,
                                            uint8_t** out_bytes,
                                            size_t* out_size) {
  if (!out_bytes || !out_size) return invalid("out_bytes and out_size are required");
  *out_bytes = nullptr;
  *out_size = 0;
  return with_table(nif, block, "unknown exception while reading a segment ssf file", [&](const auto& table) {
    return copy_out(table.ssf_file, out_bytes, out_size, k_ssf_file);
  });
}

}
