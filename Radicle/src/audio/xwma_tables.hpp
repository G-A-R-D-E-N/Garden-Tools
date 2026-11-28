#pragma once

#include <cstdint>

namespace fo4::audio::xwma_tables {

extern const std::uint32_t coef0_huffcodes[];
extern const std::uint8_t coef0_huffbits[];
extern const std::uint16_t levels0[];
extern const std::uint32_t coef1_huffcodes[];
extern const std::uint8_t coef1_huffbits[];
extern const std::uint16_t levels1[];
extern const std::uint32_t coef2_huffcodes[];
extern const std::uint8_t coef2_huffbits[];
extern const std::uint16_t levels2[];
extern const std::uint32_t coef3_huffcodes[];
extern const std::uint8_t coef3_huffbits[];
extern const std::uint16_t levels3[];
extern const std::uint32_t coef4_huffcodes[];
extern const std::uint8_t coef4_huffbits[];
extern const std::uint16_t levels4[];
extern const std::uint32_t coef5_huffcodes[];
extern const std::uint8_t coef5_huffbits[];
extern const std::uint16_t levels5[];
extern const std::uint8_t exponent_band_22050[3][25];
extern const std::uint8_t exponent_band_32000[3][25];
extern const std::uint8_t exponent_band_44100[3][25];
extern const std::uint8_t hgain_hufftab[37][2];
extern const std::uint32_t scalefactor_code[121];
extern const std::uint8_t scalefactor_bits[121];
extern const std::uint16_t critical_freqs[25];

inline constexpr int coef_count[6] = {666, 555, 1336, 1072, 476, 435};

}  // namespace fo4::audio::xwma_tables
