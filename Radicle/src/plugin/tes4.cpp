#include "fo4/plugin/reader.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/text/encoding.hpp"

#include <cstring>
#include <span>

namespace fo4 {
namespace {

std::string read_zstring(std::span<const std::byte> data) {
  if (data.empty()) {
    return {};
  }
  const auto* chars = reinterpret_cast<const char*>(data.data());
  std::size_t n = data.size();
  if (chars[n - 1] == '\0') {
    --n;
  }
  return std::string{chars, n};
}

std::uint64_t read_u64_le(std::span<const std::byte> data) {
  if (data.size() < 8) {
    return 0;
  }
  std::uint64_t value = 0;
  std::memcpy(&value, data.data(), 8);
  return value;
}

std::uint32_t read_u32_le(std::span<const std::byte> data) {
  if (data.size() < 4) {
    return 0;
  }
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), 4);
  return value;
}

MasterReference named_master(const std::string& name) {
  MasterReference master;
  master.key = PluginKey::from_filename(text::windows1252_to_utf8(name));
  master.kind = master.key.looks_light() ? PluginKind::light : PluginKind::full;
  return master;
}

}  // namespace

PluginHeader parse_tes4_header(const Record& tes4) {
  PluginHeader header;
  header.tes4_flags = tes4.header.flags;
  header.form_version = tes4.header.form_version;

  std::optional<std::string> pending_master;
  for (const auto& sub : tes4.subrecords) {
    if (sub.signature == sig_hedr) {
      if (sub.data.size() >= 12) {
        binary::Cursor cursor{sub.data};
        header.version = cursor.read_f32();
        header.record_count = cursor.read_u32();
        header.next_form_id = cursor.read_u32();
      }
    } else if (sub.signature == sig_cnam) {
      header.author = read_zstring(sub.data);
    } else if (sub.signature == sig_snam) {
      header.description = read_zstring(sub.data);
    } else if (sub.signature == sig_mast) {
      if (pending_master) header.masters.push_back(named_master(*pending_master));
      pending_master = read_zstring(sub.data);
    } else if (sub.signature == sig_data) {
      MasterReference master;
      if (pending_master) {
        master = named_master(*pending_master);
        pending_master.reset();
      }
      master.data_bytes.assign(sub.data.begin(), sub.data.end());
      master.data_size = read_u64_le(sub.data);
      header.masters.push_back(std::move(master));
    } else if (sub.signature == sig_onam) {
      const std::span<const std::byte> onam = sub.data;
      for (std::size_t i = 0; i + 4 <= onam.size(); i += 4) {
        header.overrides.push_back(RawFormId{read_u32_le(onam.subspan(i, 4))});
      }
    } else if (sub.signature == sig_intv) {
      header.intv = read_u32_le(sub.data);
    } else if (sub.signature == sig_incc) {
      header.incc = read_u32_le(sub.data);
    }
  }

  if (pending_master) header.masters.push_back(named_master(*pending_master));
  return header;
}

}  // namespace fo4
