#include "fo4/previs/bake_oc.hpp"

#include "fo4/nif/nif.hpp"

#include <fstream>

namespace fo4::previs {
namespace {

BakeOcStatus write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return BakeOcStatus::io_error;
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return out ? BakeOcStatus::ok : BakeOcStatus::io_error;
}

}  // namespace

bool oc_nif_has_shared_combined_geom(std::span<const std::byte> nif_bytes) {
  if (nif_bytes.empty()) return false;
  try {
    const auto nif = fo4::nif::read_nif(nif_bytes);
    for (const auto& block : nif.blocks()) {
      if (block.type == "BSPackedCombinedSharedGeomDataExtra") return true;
    }
  } catch (...) {
    return false;
  }
  return false;
}

BakeOcStatus bake_oc_single_source_bytes(std::span<const std::byte> source,
                                         const std::filesystem::path& output_oc_nif) {
  if (source.empty()) return BakeOcStatus::invalid_input;
  if (!oc_nif_has_shared_combined_geom(source)) return BakeOcStatus::unsupported;
  if (output_oc_nif.empty()) return BakeOcStatus::invalid_input;
  std::error_code ec;
  std::filesystem::create_directories(output_oc_nif.parent_path(), ec);
  if (ec) return BakeOcStatus::io_error;
  return write_bytes(output_oc_nif, source);
}

BakeOcStatus bake_oc_single_source(const std::filesystem::path& source_nif,
                                   const std::filesystem::path& output_oc_nif) {
  if (source_nif.empty() || output_oc_nif.empty()) return BakeOcStatus::invalid_input;
  std::ifstream in(source_nif, std::ios::binary);
  if (!in) return BakeOcStatus::io_error;
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if (size <= 0) return BakeOcStatus::invalid_input;
  in.seekg(0, std::ios::beg);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!in) return BakeOcStatus::io_error;
  return bake_oc_single_source_bytes(bytes, output_oc_nif);
}

}  // namespace fo4::previs
