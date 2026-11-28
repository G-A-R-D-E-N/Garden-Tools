#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct OsdOffset {
  std::uint16_t vertex{0};
  std::array<float, 3> delta{0.0f, 0.0f, 0.0f};
};

// Invariant: name is the data name as stored in the file; vertex indices are unique but not sorted.
struct OsdDataSet {
  std::string name;
  std::vector<OsdOffset> offsets;
};

// Constraint: only on-disk magic 00 'D' 'S' 'O' with version 1 is read; anything else is a parse_error.
// Invariant: the file holds flat data names, no shape names; shapes come from the .osp that refers to it.
struct OsdFile {
  std::uint32_t version{0};
  std::vector<OsdDataSet> sets;

  const OsdDataSet* find(std::string_view name) const;
};

// Constraint: a short read, duplicate name, duplicate vertex, non-finite float or trailing bytes is a parse_error.
OsdFile parse_osd(std::span<const std::byte> bytes);

// Invariant: file and name_in_file are the two halves of the element text around its last path separator.
// Invariant: a .bsd reference keeps its whole text in file, leaves name_in_file empty and is never read.
struct OspData {
  std::string name;
  std::string target;
  bool local{false};
  std::string file;
  std::string name_in_file;
  bool legacy_bsd{false};
};

// Invariant: the flags are carried as written; this layer does not interpret zap, uv, invert, small or big.
struct OspSlider {
  std::string name;
  bool invert{false};
  bool hidden{false};
  bool zap{false};
  bool uv{false};
  float small_value{0.0f};
  float big_value{0.0f};
  std::vector<OspData> data;
};

struct OspShape {
  std::string name;
  std::string target;
  std::string data_folder;
};

struct OspSet {
  std::string name;
  std::string data_folder;
  std::string source_file;
  std::string output_path;
  std::string output_file;
  std::vector<OspShape> shapes;
  std::vector<OspSlider> sliders;

  const OspSlider* find_slider(std::string_view name) const;
  const OspShape* find_shape(std::string_view name) const;
};

// Constraint: only root version 1 with SliderSet elements is read; other versions are a parse_error.
struct OspFile {
  std::uint32_t version{0};
  std::vector<OspSet> sets;

  const OspSet* find_set(std::string_view name) const;
};

OspFile parse_osp(std::string_view text);

// Invariant: shape name maps to its target, the slider's data entry with that target is chosen, and its file matches by last path component ignoring ASCII case.
// Invariant: nullptr means the slider, shape, entry or data name is absent; a .bsd entry never matches.
const OsdDataSet* find_osd_diff(const OsdFile& osd, std::string_view osd_file_name, const OspSet& set,
                                std::string_view slider_name, std::string_view shape_name);

// Requires positions to be 3 floats per vertex and a finite weight.
// Invariant: adds delta * weight to each listed vertex; any refusal returns the reason and leaves positions untouched.
std::optional<std::string> apply_osd_diff(std::span<float> positions, const OsdDataSet& diff, float weight);

// Requires a tri-shape block that is not BSDynamicTriShape with full-precision positions; anything else is an invalid_argument.
// Invariant: the diff is added to the stored shape positions, a refusal of apply_osd_diff is an invalid_argument, and the sphere and stored normals follow.
// Invariant: moved counts vertices whose position changed; zero returns the input bytes unchanged.
std::vector<std::byte> apply_osd_to_shape(const NifFile& nif, std::uint32_t block, const OsdDataSet& diff, float weight,
                                          std::size_t* moved = nullptr);

}  // namespace fo4::nif
