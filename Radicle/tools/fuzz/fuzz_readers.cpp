#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_texture.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/pex/pex.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

void run(std::uint8_t selector, std::span<const std::byte> bytes) {
  switch (selector % 7) {
    case 0: {
      auto plugin = fo4::read_plugin(bytes);
      fo4::visit_records(plugin, [](fo4::Record& record) { static_cast<void>(record.subrecords.size()); });
      break;
    }
    case 1:
      for (const auto& entry : fo4::archive::read_ba2_general(bytes).entries) {
        static_cast<void>(fo4::archive::extract_ba2_general_entry(entry));
      }
      break;
    case 2:
      for (const auto& entry : fo4::archive::read_ba2_texture(bytes).entries) {
        static_cast<void>(fo4::archive::extract_ba2_texture_entry(entry));
      }
      break;
    case 3:
      static_cast<void>(fo4::nif::read_shapes(fo4::nif::read_nif(bytes)));
      break;
    case 4: {
      const std::vector<std::uint8_t> raw(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                          reinterpret_cast<const std::uint8_t*>(bytes.data()) + bytes.size());
      static_cast<void>(fo4::pex::PexFile::fromBytes(raw));
      break;
    }
    default:
      for (const auto kind : {fo4::localization::StringTableKind::strings, fo4::localization::StringTableKind::dlstrings,
                              fo4::localization::StringTableKind::ilstrings}) {
        static_cast<void>(fo4::localization::read_string_table(bytes, kind));
      }
      break;
  }
}

}  // namespace

// Constraint: first input byte selects the reader; a documented parse failure is not a finding, anything else is.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) return 0;
  try {
    run(data[0], std::as_bytes(std::span(data + 1, size - 1)));
  } catch (const std::runtime_error&) {
  }
  return 0;
}
