#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::nif {

// Constraint: source and target are offsets from the start of the owning object, and target may equal the object length.
struct HknpLocalFixup {
  std::uint32_t source{0};
  std::uint32_t target{0};
};

// Constraint: source is an offset from the start of the owning object, and object indexes HknpPackfile::objects.
struct HknpGlobalFixup {
  std::uint32_t source{0};
  std::uint32_t object{0};
};

struct HknpPackObject {
  std::string class_name;
  // Invariant: bytes keep any trailing 16-byte padding the file stored, so a rebuild writes them back unchanged.
  std::vector<std::byte> bytes;
  // Ordering: fixups stay in the order the file stored them, which is the order a rebuild writes them.
  std::vector<HknpLocalFixup> local;
  std::vector<HknpGlobalFixup> global;
};

struct HknpClassName {
  std::uint32_t hash{0};
  std::string name;
};

struct HknpPackfile {
  // Ordering: classes keep the file's __classnames__ order, and every object's class must appear here.
  std::vector<HknpClassName> classes;
  // Invariant: objects stay in __data__ order, and objects[0] is the root that the file header names.
  std::vector<HknpPackObject> objects;
  // Constraint: the span of the global fixup table as stored, including reserved 0xFF slots; 0 means the minimum.
  std::uint32_t global_fixup_bytes{0};
  // Constraint: the header is 0x40 bytes plus this tail, and the file stores the tail length as the u16 at 0x3E (0 for physics, 0x10 for cloth).
  std::vector<std::byte> header_tail;
};

// Requires the whole buffer to be one packfile: any truncation, trailing byte or out-of-range table raises fo4::parse_error.
HknpPackfile read_hknp_packfile(std::span<const std::byte> bytes);

// Constraint: for an unedited packfile the output is byte-identical to the input; the real-data test asserts it on vanilla.
std::vector<std::byte> write_hknp_packfile(const HknpPackfile& pack);

}  // namespace fo4::nif
