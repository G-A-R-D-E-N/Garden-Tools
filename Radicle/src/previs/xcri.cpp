#include "fo4/previs/xcri.hpp"

#include "fo4/error.hpp"

#include <cstring>
#include <string>

namespace fo4::previs {
namespace {

std::uint32_t u32le(std::span<const std::byte> data, std::size_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, data.data() + at, 4);
  return v;
}

void append_le(std::vector<std::byte>& out, std::uint32_t v) {
  out.push_back(static_cast<std::byte>(v & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 16) & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 24) & 0xFFu));
}

}  // namespace

XcriRaw parse_xcri_bytes(std::span<const std::byte> data) {
  if (data.size() < 8) throw parse_error(0, "XCRI is shorter than its counts");
  const auto meshes = std::uint64_t{u32le(data, 0)};
  const auto words = std::uint64_t{u32le(data, 4)};
  if ((words % 2u) != 0u) throw parse_error(4, "XCRI reference count must be even");
  if (std::uint64_t{data.size() - 8} != 4u * meshes + 4u * words) {
    throw parse_error(0, "XCRI size does not match its counts");
  }
  XcriRaw out;
  out.mesh_ids.reserve(static_cast<std::size_t>(meshes));
  for (std::uint64_t i = 0; i < meshes; ++i) {
    out.mesh_ids.push_back(u32le(data, 8 + static_cast<std::size_t>(4 * i)));
  }
  const std::size_t first = 8 + static_cast<std::size_t>(4 * meshes);
  out.refs.reserve(static_cast<std::size_t>(words / 2));
  for (std::uint64_t i = 0; i < words / 2; ++i) {
    const std::size_t at = first + static_cast<std::size_t>(8 * i);
    out.refs.emplace_back(u32le(data, at), u32le(data, at + 4));
  }
  return out;
}

std::vector<std::byte> write_xcri_bytes(const XcriRaw& raw) {
  if (raw.mesh_ids.size() > 0xFFFFFFFFu || raw.refs.size() > 0x7FFFFFFFu) {
    throw parse_error(0, "XCRI table count exceeds u32");
  }
  std::vector<std::byte> out;
  out.reserve(8 + 4 * raw.mesh_ids.size() + 8 * raw.refs.size());
  append_le(out, static_cast<std::uint32_t>(raw.mesh_ids.size()));
  append_le(out, static_cast<std::uint32_t>(raw.refs.size() * 2));
  for (const auto id : raw.mesh_ids) append_le(out, id);
  for (const auto& [local, mesh] : raw.refs) {
    append_le(out, local);
    append_le(out, mesh);
  }
  return out;
}

std::optional<CombinedIndex> decode_combined_index(const env::Environment& env,
                                                   const env::RecordRef& cell_ref) {
  const auto* record = cell_ref.record;
  if (record == nullptr) return std::nullopt;
  const Subrecord* found = nullptr;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == Signature::from_string("XCRI")) {
      if (found != nullptr) throw env::WorldDecodeError("CELL XCRI must appear at most once");
      found = &sub;
    }
  }
  if (found == nullptr) return std::nullopt;
  XcriRaw raw;
  try {
    raw = parse_xcri_bytes(found->data);
  } catch (const parse_error& e) {
    throw env::WorldDecodeError(cell_ref.record->form_key.to_string() + " XCRI is malformed: " +
                                 e.parse_message());
  }
  CombinedIndex out;
  out.mesh_ids = raw.mesh_ids;
  out.refs.reserve(raw.refs.size());
  for (const auto& [local, mesh] : raw.refs) {
    const auto key = env.resolve_link(cell_ref, RawFormId{local});
    out.refs.push_back(CombinedReference{mesh, key});
  }
  return out;
}

PackedDate decode_packed_date(std::uint16_t value) noexcept {
  return PackedDate{static_cast<unsigned>(2000 + (value >> 9)), static_cast<unsigned>((value >> 5) & 15u),
                    static_cast<unsigned>(value & 31u)};
}

bool is_precalc_stale(std::uint16_t cell_stamp, std::uint16_t child_date) noexcept {
  if (cell_stamp == 0 || child_date == 0) return false;
  return child_date > cell_stamp;
}

}  // namespace fo4::previs
