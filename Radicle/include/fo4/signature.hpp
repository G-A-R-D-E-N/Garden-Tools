#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace fo4 {

class Signature {
public:
  constexpr Signature() noexcept = default;

  constexpr Signature(char a, char b, char c, char d) noexcept
      : bytes_{static_cast<char>(a), static_cast<char>(b), static_cast<char>(c),
               static_cast<char>(d)} {}

  static constexpr Signature from_bytes(std::uint32_t packed) noexcept {
    return Signature{
        static_cast<char>(packed & 0xFFu),
        static_cast<char>((packed >> 8) & 0xFFu),
        static_cast<char>((packed >> 16) & 0xFFu),
        static_cast<char>((packed >> 24) & 0xFFu),
    };
  }

  static constexpr Signature from_string(std::string_view text) {
    Signature s;
    if (text.size() >= 1) s.bytes_[0] = text[0];
    if (text.size() >= 2) s.bytes_[1] = text[1];
    if (text.size() >= 3) s.bytes_[2] = text[2];
    if (text.size() >= 4) s.bytes_[3] = text[3];
    return s;
  }

  constexpr std::uint32_t packed() const noexcept {
    return static_cast<std::uint8_t>(bytes_[0]) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes_[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes_[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes_[3])) << 24);
  }

  constexpr std::string_view view() const noexcept {
    return std::string_view{bytes_.data(), 4};
  }

  std::string to_string() const { return std::string{view()}; }

  constexpr const std::array<char, 4>& bytes() const noexcept { return bytes_; }

  friend constexpr bool operator==(const Signature&, const Signature&) = default;
  friend constexpr auto operator<=>(const Signature&, const Signature&) = default;

private:
  std::array<char, 4> bytes_{};
};

inline constexpr Signature sig_tes4 = Signature::from_string("TES4");
inline constexpr Signature sig_grup = Signature::from_string("GRUP");
inline constexpr Signature sig_xxxx = Signature::from_string("XXXX");
inline constexpr Signature sig_hedr = Signature::from_string("HEDR");
inline constexpr Signature sig_cnam = Signature::from_string("CNAM");
inline constexpr Signature sig_snam = Signature::from_string("SNAM");
inline constexpr Signature sig_mast = Signature::from_string("MAST");
inline constexpr Signature sig_data = Signature::from_string("DATA");
inline constexpr Signature sig_onam = Signature::from_string("ONAM");
inline constexpr Signature sig_intv = Signature::from_string("INTV");
inline constexpr Signature sig_incc = Signature::from_string("INCC");
inline constexpr Signature sig_edid = Signature::from_string("EDID");
inline constexpr Signature sig_name = Signature::from_string("NAME");
inline constexpr Signature sig_kwda = Signature::from_string("KWDA");
inline constexpr Signature sig_ctda = Signature::from_string("CTDA");
inline constexpr Signature sig_xlcn = Signature::from_string("XLCN");
inline constexpr Signature sig_xown = Signature::from_string("XOWN");
inline constexpr Signature sig_xezn = Signature::from_string("XEZN");
inline constexpr Signature sig_ptrn = Signature::from_string("PTRN");
inline constexpr Signature sig_vmad = Signature::from_string("VMAD");
inline constexpr Signature sig_qust = Signature::from_string("QUST");
inline constexpr Signature sig_perk = Signature::from_string("PERK");
inline constexpr Signature sig_pack = Signature::from_string("PACK");
inline constexpr Signature sig_scen = Signature::from_string("SCEN");
inline constexpr Signature sig_info = Signature::from_string("INFO");
inline constexpr Signature sig_term = Signature::from_string("TERM");
inline constexpr Signature sig_cnto = Signature::from_string("CNTO");
inline constexpr Signature sig_lvlo = Signature::from_string("LVLO");
inline constexpr Signature sig_xtel = Signature::from_string("XTEL");
inline constexpr Signature sig_xloc = Signature::from_string("XLOC");
inline constexpr Signature sig_xlkr = Signature::from_string("XLKR");
inline constexpr Signature sig_xesp = Signature::from_string("XESP");
inline constexpr Signature sig_coed = Signature::from_string("COED");
inline constexpr Signature sig_xndp = Signature::from_string("XNDP");

}  // namespace fo4
