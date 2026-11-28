#pragma once

#include "fo4/nif/customization_remap.hpp"
#include "write_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace fo4::nif::remap_detail {

// Invariant: an encoded record is k_remap_record_bytes: four half weights then four u8 bone indices.
void append_record(write_detail::Bytes& out, const RemapRecord& record);
RemapRecord record_at(const write_detail::Bytes& bytes, std::size_t at);

// Requires a CustomizationRemapData block of the model; replaces its payload with one record per entry.
void store_records(write_detail::Model& model, std::int32_t block, std::span<const RemapRecord> records);

// Requires an extra-data block the shape lists; the copy is appended and the shape's reference is pointed at it.
std::int32_t clone_extra(write_detail::Model& model, std::uint32_t shape, std::int32_t block);

// Invariant: returns the reason an op cannot carry the shape's remap data, and a shape with none yields empty records.
std::optional<std::string> load_for_op(const NifFile& nif, std::uint32_t shape, CustomizationRemap& remap);

}  // namespace fo4::nif::remap_detail
