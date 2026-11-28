// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fo4/audio/xwma_decoder.hpp"

#include "fo4/audio/sound_container.hpp"
#include "fo4/error.hpp"
#include "xwma_dsp.hpp"
#include "xwma_tables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

namespace fo4::audio {
namespace {

namespace tbl = xwma_tables;
using namespace xwma_dsp;

constexpr int k_block_min_bits = 7;
constexpr int k_block_max_bits = 11;
constexpr int k_block_max_size = 1 << k_block_max_bits;
constexpr int k_block_nb_sizes = k_block_max_bits - k_block_min_bits + 1;
constexpr int k_high_band_max = 16;
constexpr int k_noise_tab_size = 8192;
constexpr int k_max_superframe = 32768;
constexpr int k_max_channels = 2;
constexpr int k_flags2 = 31;
constexpr int k_exp_pow_count = 156;

[[noreturn]] void fail(std::size_t offset, const char* message) { throw parse_error(offset, message); }

struct SharedTables {
  std::array<Vlc, 6> coef;
  std::array<std::vector<std::uint16_t>, 6> run;
  std::array<std::vector<float>, 6> level;
  Vlc exp;
  Vlc hgain;
  std::array<float, k_noise_tab_size> noise{};
  std::array<std::vector<float>, k_block_max_bits + 1> window;

  SharedTables() {
    const std::uint32_t* codes[6] = {tbl::coef0_huffcodes, tbl::coef1_huffcodes, tbl::coef2_huffcodes,
                                     tbl::coef3_huffcodes, tbl::coef4_huffcodes, tbl::coef5_huffcodes};
    const std::uint8_t* bits[6] = {tbl::coef0_huffbits, tbl::coef1_huffbits, tbl::coef2_huffbits,
                                   tbl::coef3_huffbits, tbl::coef4_huffbits, tbl::coef5_huffbits};
    const std::uint16_t* levels[6] = {tbl::levels0, tbl::levels1, tbl::levels2,
                                      tbl::levels3, tbl::levels4, tbl::levels5};
    for (int t = 0; t < 6; ++t) {
      const int n = tbl::coef_count[t];
      for (int i = 0; i < n; ++i) {
        if (bits[t][i] > 0) {
          coef[static_cast<std::size_t>(t)].add(codes[t][i], bits[t][i], i);
        }
      }
      auto& run_table = run[static_cast<std::size_t>(t)];
      auto& level_table = level[static_cast<std::size_t>(t)];
      run_table.assign(static_cast<std::size_t>(n), 0);
      level_table.assign(static_cast<std::size_t>(n), 0.0f);
      int i = 2;
      int level_value = 1;
      int k = 0;
      while (i < n) {
        const int l = levels[t][k++];
        for (int j = 0; j < l && i < n; ++j, ++i) {
          run_table[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(j);
          level_table[static_cast<std::size_t>(i)] = static_cast<float>(level_value);
        }
        ++level_value;
      }
    }
    for (int i = 0; i < 121; ++i) {
      exp.add(tbl::scalefactor_code[i], tbl::scalefactor_bits[i], i);
    }
    std::uint64_t code = 0;
    for (int i = 0; i < 37; ++i) {
      const int len = tbl::hgain_hufftab[i][1];
      hgain.add(static_cast<std::uint32_t>(code >> (32 - len)), len, tbl::hgain_hufftab[i][0]);
      code += std::uint64_t{1} << (32 - len);
    }
    const float norm = static_cast<float>((1.0 / static_cast<double>(static_cast<float>(1LL << 31))) *
                                          std::sqrt(3.0) * static_cast<double>(0.02f));
    std::uint32_t seed = 1;
    for (int i = 0; i < k_noise_tab_size; ++i) {
      seed = seed * 314159u + 1u;
      noise[static_cast<std::size_t>(i)] = static_cast<float>(static_cast<std::int32_t>(seed)) * norm;
    }
    for (int bits_n = k_block_min_bits; bits_n <= k_block_max_bits; ++bits_n) {
      const int n = 1 << bits_n;
      auto& w = window[static_cast<std::size_t>(bits_n)];
      w.resize(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i) {
        w[static_cast<std::size_t>(i)] = static_cast<float>(std::sin((i + 0.5) / (2.0 * n) * std::numbers::pi));
      }
    }
  }
};

const SharedTables& shared() {
  static const SharedTables tables;
  return tables;
}

int frame_len_bits_for(std::uint32_t sample_rate) {
  if (sample_rate <= 16000) {
    return 9;
  }
  if (sample_rate <= 22050) {
    return 10;
  }
  return 11;
}

using Planar = std::array<std::vector<float>, k_max_channels>;

class WmaV2Decoder {
 public:
  WmaV2Decoder(const XwmaFormat& format, std::size_t error_offset)
      : channels_(format.channels), sample_rate_(static_cast<int>(format.sample_rate)), tables_(shared()) {
    std::int64_t bit_rate = static_cast<std::int64_t>(format.average_bytes_per_second) * 8;
    if (channels_ == 1) {
      if (sample_rate_ == 22050 && (bit_rate == 48000 || bit_rate == 192000)) {
        bit_rate = 20000;
      } else if (sample_rate_ == 32000 && (bit_rate == 48000 || bit_rate == 192000)) {
        bit_rate = 20000;
      } else if (sample_rate_ == 44100 && (bit_rate == 96000 || bit_rate == 192000)) {
        bit_rate = 48000;
      }
    } else if (channels_ == 2) {
      if (sample_rate_ == 22050 && (bit_rate == 48000 || bit_rate == 192000)) {
        bit_rate = 32000;
      } else if (sample_rate_ == 32000 && bit_rate == 192000) {
        bit_rate = 48000;
      }
    }
    if (format.sample_rate > 50000 || channels_ > k_max_channels || bit_rate <= 0) {
      fail(error_offset, "xWMA stream parameters are outside what the WMA v2 decoder supports");
    }
    frame_len_bits_ = frame_len_bits_for(format.sample_rate);
    next_block_len_bits_ = prev_block_len_bits_ = block_len_bits_ = frame_len_bits_;
    frame_len_ = 1 << frame_len_bits_;

    int nb = ((k_flags2 >> 3) & 3) + 1;
    if (bit_rate / channels_ >= 32000) {
      nb += 2;
    }
    nb = std::min(nb, frame_len_bits_ - k_block_min_bits);
    nb_block_sizes_ = nb + 1;

    use_noise_coding_ = true;
    float high_freq = static_cast<float>(sample_rate_ * 0.5);
    int sample_rate1 = sample_rate_;
    if (sample_rate1 >= 44100) {
      sample_rate1 = 44100;
    } else if (sample_rate1 >= 22050) {
      sample_rate1 = 22050;
    } else if (sample_rate1 >= 16000) {
      sample_rate1 = 16000;
    } else if (sample_rate1 >= 11025) {
      sample_rate1 = 11025;
    } else if (sample_rate1 >= 8000) {
      sample_rate1 = 8000;
    }

    const float bps = static_cast<float>(bit_rate) / static_cast<float>(channels_ * sample_rate_);
    const double offset_bytes = static_cast<double>(bps) * frame_len_ / 8.0 + 0.5;
    if (!(offset_bytes < 1.0e6)) {
      fail(error_offset, "xWMA byte rate is too large for its sample rate");
    }
    byte_offset_bits_ = log2_floor(static_cast<unsigned>(static_cast<int>(offset_bytes))) + 2;
    if (byte_offset_bits_ + 3 > 25) {
      fail(error_offset, "xWMA byte offset field is too wide");
    }

    float bps1 = bps;
    if (channels_ == 2) {
      bps1 = static_cast<float>(bps * 1.6);
    }
    if (sample_rate1 == 44100) {
      if (bps1 >= 0.61) {
        use_noise_coding_ = false;
      } else {
        high_freq = static_cast<float>(high_freq * 0.4);
      }
    } else if (sample_rate1 == 22050) {
      if (bps1 >= 1.16) {
        use_noise_coding_ = false;
      } else if (bps1 >= 0.72) {
        high_freq = static_cast<float>(high_freq * 0.7);
      } else {
        high_freq = static_cast<float>(high_freq * 0.6);
      }
    } else if (sample_rate1 == 16000) {
      high_freq = static_cast<float>(high_freq * (bps > 0.5 ? 0.5 : 0.3));
    } else if (sample_rate1 == 11025) {
      high_freq = static_cast<float>(high_freq * 0.7);
    } else if (sample_rate1 == 8000) {
      if (bps <= 0.625) {
        high_freq = static_cast<float>(high_freq * 0.5);
      } else if (bps > 0.75) {
        use_noise_coding_ = false;
      } else {
        high_freq = static_cast<float>(high_freq * 0.65);
      }
    } else {
      if (bps >= 0.8) {
        high_freq = static_cast<float>(high_freq * 0.75);
      } else if (bps >= 0.6) {
        high_freq = static_cast<float>(high_freq * 0.6);
      } else {
        high_freq = static_cast<float>(high_freq * 0.5);
      }
    }

    for (int k = 0; k < nb_block_sizes_; ++k) {
      const auto ks = static_cast<std::size_t>(k);
      const int block_len = frame_len_ >> k;
      const std::uint8_t* table = nullptr;
      int a = frame_len_bits_ - k_block_min_bits - k;
      if (a < 3) {
        if (sample_rate_ >= 44100) {
          table = tbl::exponent_band_44100[a];
        } else if (sample_rate_ >= 32000) {
          table = tbl::exponent_band_32000[a];
        } else if (sample_rate_ >= 22050) {
          table = tbl::exponent_band_22050[a];
        }
      }
      if (table != nullptr) {
        const int n = *table++;
        for (int i = 0; i < n; ++i) {
          exponent_bands_[ks][static_cast<std::size_t>(i)] = table[i];
        }
        exponent_sizes_[ks] = n;
      } else {
        int j = 0;
        int lpos = 0;
        for (int i = 0; i < 25; ++i) {
          a = tbl::critical_freqs[i];
          const int b = sample_rate_;
          int pos = ((block_len * 2 * a) + (b << 1)) / (4 * b);
          pos <<= 2;
          if (pos > block_len) {
            pos = block_len;
          }
          if (pos > lpos) {
            exponent_bands_[ks][static_cast<std::size_t>(j++)] = static_cast<std::uint16_t>(pos - lpos);
          }
          if (pos >= block_len) {
            break;
          }
          lpos = pos;
        }
        exponent_sizes_[ks] = j;
      }

      coefs_end_[ks] = (frame_len_ - ((frame_len_ * 9) / 100)) >> k;
      const float scaled = (static_cast<float>(block_len * 2) * high_freq) / static_cast<float>(sample_rate_);
      high_band_start_[ks] = static_cast<int>(static_cast<double>(scaled) + 0.5);
      int j = 0;
      int pos = 0;
      for (int i = 0; i < exponent_sizes_[ks]; ++i) {
        int start = pos;
        pos += exponent_bands_[ks][static_cast<std::size_t>(i)];
        int end = pos;
        start = std::max(start, high_band_start_[ks]);
        end = std::min(end, coefs_end_[ks]);
        if (end > start && j < k_high_band_max) {
          exponent_high_bands_[ks][static_cast<std::size_t>(j++)] = end - start;
        }
      }
      exponent_high_sizes_[ks] = j;
    }

    for (std::size_t i = 0; i < k_max_channels; ++i) {
      max_exponent_[i] = 1.0f;
    }

    coef_vlc_table_ = 2;
    if (sample_rate_ >= 32000) {
      if (bps1 < 0.72) {
        coef_vlc_table_ = 0;
      } else if (bps1 < 1.16) {
        coef_vlc_table_ = 1;
      }
    }

    for (int k = 0; k < nb_block_sizes_; ++k) {
      imdct_.emplace_back(frame_len_ >> k);
    }
    last_superframe_.assign(k_max_superframe + 64, 0);
  }

  int frame_len() const { return frame_len_; }

  bool decode_packet(const std::uint8_t* buf, int buf_size, Planar& out) {
    Planar frames;
    if (!decode_superframe(buf, buf_size, frames)) {
      last_superframe_len_ = 0;
      return false;
    }
    for (std::size_t ch = 0; ch < static_cast<std::size_t>(channels_); ++ch) {
      out[ch].insert(out[ch].end(), frames[ch].begin(), frames[ch].end());
    }
    return true;
  }

  void drain(Planar& out) {
    for (std::size_t ch = 0; ch < static_cast<std::size_t>(channels_); ++ch) {
      out[ch].insert(out[ch].end(), frame_out_[ch].begin(), frame_out_[ch].begin() + frame_len_);
    }
  }

 private:
  bool decode_superframe(const std::uint8_t* buf, int buf_size, Planar& frames) {
    BitReader gb(buf, static_cast<std::int64_t>(buf_size) * 8);
    gb.skip(4);
    int nb_frames = static_cast<int>(gb.get(4)) - (last_superframe_len_ <= 0 ? 1 : 0);
    if (nb_frames <= 0) {
      if (nb_frames < 0 || gb.left() <= 8) {
        return false;
      }
      if (last_superframe_len_ + buf_size - 1 > k_max_superframe) {
        return false;
      }
      std::uint8_t* q = last_superframe_.data() + last_superframe_len_;
      for (int len = buf_size - 1; len > 0; --len) {
        *q++ = static_cast<std::uint8_t>(gb.get(8));
      }
      std::memset(q, 0, 64);
      last_superframe_len_ += 8 * buf_size - 8;
      return true;
    }

    const int bit_offset = static_cast<int>(gb.get(byte_offset_bits_ + 3));
    if (bit_offset > gb.left()) {
      return false;
    }

    if (last_superframe_len_ > 0) {
      if (last_superframe_len_ + ((bit_offset + 7) >> 3) > k_max_superframe) {
        return false;
      }
      std::uint8_t* q = last_superframe_.data() + last_superframe_len_;
      int len = bit_offset;
      while (len > 7) {
        *q++ = static_cast<std::uint8_t>(gb.get(8));
        len -= 8;
      }
      if (len > 0) {
        *q++ = static_cast<std::uint8_t>(gb.get(len) << (8 - len));
      }
      std::memset(q, 0, 64);
      BitReader last(last_superframe_.data(), static_cast<std::int64_t>(last_superframe_len_) * 8 + bit_offset);
      if (last_bitoffset_ > 0) {
        last.skip(last_bitoffset_);
      }
      if (!decode_frame(last, frames)) {
        return false;
      }
      --nb_frames;
    }

    int pos = bit_offset + 4 + 4 + byte_offset_bits_ + 3;
    if (pos >= k_max_superframe * 8 || pos > buf_size * 8) {
      return false;
    }
    BitReader body(buf + (pos >> 3), static_cast<std::int64_t>(buf_size - (pos >> 3)) * 8);
    if ((pos & 7) > 0) {
      body.skip(pos & 7);
    }

    reset_block_lengths_ = true;
    for (int i = 0; i < nb_frames; ++i) {
      if (!decode_frame(body, frames)) {
        return false;
      }
    }

    pos = static_cast<int>(body.position()) + ((bit_offset + 4 + 4 + byte_offset_bits_ + 3) & ~7);
    last_bitoffset_ = pos & 7;
    pos >>= 3;
    const int len = buf_size - pos;
    if (len > k_max_superframe || len < 0) {
      return false;
    }
    last_superframe_len_ = len;
    std::memcpy(last_superframe_.data(), buf + pos, static_cast<std::size_t>(len));
    return true;
  }

  bool decode_frame(BitReader& gb, Planar& frames) {
    block_pos_ = 0;
    for (;;) {
      const int ret = decode_block(gb);
      if (ret < 0) {
        return false;
      }
      if (ret > 0) {
        break;
      }
    }
    if (gb.left() < 0) {
      return false;
    }
    for (std::size_t ch = 0; ch < static_cast<std::size_t>(channels_); ++ch) {
      auto& fo = frame_out_[ch];
      frames[ch].insert(frames[ch].end(), fo.begin(), fo.begin() + frame_len_);
      std::memmove(fo.data(), fo.data() + frame_len_, static_cast<std::size_t>(frame_len_) * sizeof(float));
    }
    return true;
  }

  void window(float* out) {
    const float* in = output_.data();
    const auto& windows = tables_.window;
    if (block_len_bits_ <= prev_block_len_bits_) {
      const auto& w = windows[static_cast<std::size_t>(block_len_bits_)];
      for (int i = 0; i < block_len_; ++i) {
        out[i] = in[i] * w[static_cast<std::size_t>(i)] + out[i];
      }
    } else {
      const int len = 1 << prev_block_len_bits_;
      const int n = (block_len_ - len) / 2;
      const auto& w = windows[static_cast<std::size_t>(prev_block_len_bits_)];
      for (int i = 0; i < len; ++i) {
        out[n + i] = in[n + i] * w[static_cast<std::size_t>(i)] + out[n + i];
      }
      std::memcpy(out + n + len, in + n + len, static_cast<std::size_t>(n) * sizeof(float));
    }

    out += block_len_;
    in += block_len_;

    if (block_len_bits_ <= next_block_len_bits_) {
      const auto& w = windows[static_cast<std::size_t>(block_len_bits_)];
      for (int i = 0; i < block_len_; ++i) {
        out[i] = in[i] * w[static_cast<std::size_t>(block_len_ - 1 - i)];
      }
    } else {
      const int len = 1 << next_block_len_bits_;
      const int n = (block_len_ - len) / 2;
      const auto& w = windows[static_cast<std::size_t>(next_block_len_bits_)];
      std::memcpy(out, in, static_cast<std::size_t>(n) * sizeof(float));
      for (int i = 0; i < len; ++i) {
        out[n + i] = in[n + i] * w[static_cast<std::size_t>(len - 1 - i)];
      }
      std::memset(out + n + len, 0, static_cast<std::size_t>(n) * sizeof(float));
    }
  }

  bool decode_exp_vlc(BitReader& gb, std::size_t ch) {
    const auto size_index = static_cast<std::size_t>(frame_len_bits_ - block_len_bits_);
    const auto& ptr = exponent_bands_[size_index];
    const int bands = exponent_sizes_[size_index];
    float* q = exponents_[ch].data();
    float* const q_end = q + block_len_;
    float max_scale = 0;
    int last_exp = 36;
    int band = 0;
    while (q < q_end) {
      const int code = tables_.exp.decode(gb);
      if (code < 0 || band >= bands) {
        return false;
      }
      last_exp += code - 60;
      if (static_cast<unsigned>(last_exp + 60) >= static_cast<unsigned>(k_exp_pow_count)) {
        return false;
      }
      const float v = static_cast<float>(std::pow(10.0, last_exp / 16.0));
      max_scale = std::max(max_scale, v);
      int n = ptr[static_cast<std::size_t>(band++)];
      while (n-- > 0 && q < q_end) {
        *q++ = v;
      }
    }
    max_exponent_[ch] = max_scale;
    return true;
  }

  int decode_block(BitReader& gb) {
    const auto channels = static_cast<std::size_t>(channels_);
    std::array<int, k_max_channels> nb_coefs{};

    const int n_bits = log2_floor(static_cast<unsigned>(nb_block_sizes_ - 1)) + 1;
    if (reset_block_lengths_) {
      reset_block_lengths_ = false;
      int v = static_cast<int>(gb.get(n_bits));
      if (v >= nb_block_sizes_) {
        return -1;
      }
      prev_block_len_bits_ = frame_len_bits_ - v;
      v = static_cast<int>(gb.get(n_bits));
      if (v >= nb_block_sizes_) {
        return -1;
      }
      block_len_bits_ = frame_len_bits_ - v;
    } else {
      prev_block_len_bits_ = block_len_bits_;
      block_len_bits_ = next_block_len_bits_;
    }
    {
      const int v = static_cast<int>(gb.get(n_bits));
      if (v >= nb_block_sizes_) {
        return -1;
      }
      next_block_len_bits_ = frame_len_bits_ - v;
    }

    if (frame_len_bits_ - block_len_bits_ >= nb_block_sizes_) {
      return -1;
    }
    block_len_ = 1 << block_len_bits_;
    if (block_pos_ + block_len_ > frame_len_) {
      return -1;
    }

    if (channels == 2) {
      ms_stereo_ = gb.get1() != 0;
    }
    bool any_coded = false;
    for (std::size_t ch = 0; ch < channels; ++ch) {
      channel_coded_[ch] = gb.get1() != 0;
      any_coded = any_coded || channel_coded_[ch];
    }

    const int bsize = frame_len_bits_ - block_len_bits_;
    const auto bs = static_cast<std::size_t>(bsize);

    if (any_coded) {
      int total_gain = 1;
      for (;;) {
        if (gb.left() < 7) {
          return -1;
        }
        const int a = static_cast<int>(gb.get(7));
        total_gain += a;
        if (a != 127) {
          break;
        }
      }

      const int coef_nb_bits =
          total_gain < 15 ? 13 : total_gain < 32 ? 12 : total_gain < 40 ? 11 : total_gain < 45 ? 10 : 9;

      for (std::size_t ch = 0; ch < channels; ++ch) {
        nb_coefs[ch] = coefs_end_[bs];
      }

      if (use_noise_coding_) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
          if (channel_coded_[ch]) {
            const auto hn = static_cast<std::size_t>(exponent_high_sizes_[bs]);
            for (std::size_t i = 0; i < hn; ++i) {
              const bool a = gb.get1() != 0;
              high_band_coded_[ch][i] = a;
              if (a) {
                nb_coefs[ch] -= exponent_high_bands_[bs][i];
              }
            }
          }
        }
        for (std::size_t ch = 0; ch < channels; ++ch) {
          if (channel_coded_[ch]) {
            const auto hn = static_cast<std::size_t>(exponent_high_sizes_[bs]);
            bool have_value = false;
            int val = 0;
            for (std::size_t i = 0; i < hn; ++i) {
              if (high_band_coded_[ch][i]) {
                if (!have_value) {
                  val = static_cast<int>(gb.get(7)) - 19;
                  have_value = true;
                } else {
                  const int code = tables_.hgain.decode(gb);
                  if (code < 0) {
                    return -1;
                  }
                  val += code - 18;
                }
                high_band_values_[ch][i] = val;
              }
            }
          }
        }
      }

      if (block_len_bits_ == frame_len_bits_ || gb.get1() != 0) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
          if (channel_coded_[ch]) {
            if (!decode_exp_vlc(gb, ch)) {
              return -1;
            }
            exponents_bsize_[ch] = bsize;
            exponents_initialized_[ch] = true;
          }
        }
      }

      for (std::size_t ch = 0; ch < channels; ++ch) {
        if (channel_coded_[ch] && !exponents_initialized_[ch]) {
          return -1;
        }
      }

      for (std::size_t ch = 0; ch < channels; ++ch) {
        if (channel_coded_[ch]) {
          const int tindex = (ch == 1 && ms_stereo_) ? 1 : 0;
          float* ptr = coefs1_[ch].data();
          std::memset(ptr, 0, static_cast<std::size_t>(block_len_) * sizeof(float));
          if (!run_level_decode(gb, coef_vlc_table_ * 2 + tindex, ptr, nb_coefs[ch], coef_nb_bits)) {
            return -1;
          }
        }
      }

      const int n4 = block_len_ / 2;
      const float mdct_norm = static_cast<float>(1.0 / static_cast<float>(n4));
      const auto& noise = tables_.noise;

      for (std::size_t ch = 0; ch < channels; ++ch) {
        if (!channel_coded_[ch]) {
          continue;
        }
        const float* coefs1 = coefs1_[ch].data();
        const float* exponents = exponents_[ch].data();
        const int esize = exponents_bsize_[ch];
        float mult = static_cast<float>(std::pow(10.0, total_gain * 0.05) / max_exponent_[ch]);
        mult *= mdct_norm;
        float* coefs = coefs_[ch].data();
        if (use_noise_coding_) {
          float mult1;
          const auto n1 = static_cast<std::size_t>(exponent_high_sizes_[bs]);

          int eidx = high_band_start_[bs] << bsize >> esize;
          std::size_t last_high_band = 0;
          std::array<float, k_high_band_max> exp_power{};
          for (std::size_t j = 0; j < n1; ++j) {
            const int nn = exponent_high_bands_[bs][j];
            if (high_band_coded_[ch][j]) {
              float e2 = 0;
              for (int i = 0; i < nn; ++i) {
                const float v = exponents[eidx + (i << bsize >> esize)];
                e2 += v * v;
              }
              exp_power[j] = e2 / static_cast<float>(nn);
              last_high_band = j;
            }
            eidx += nn << bsize >> esize;
          }

          eidx = 0;
          for (std::size_t seg = 0; seg <= n1; ++seg) {
            const bool main_segment = seg == 0;
            const std::size_t j = seg - 1;
            const int nn = main_segment ? high_band_start_[bs] : exponent_high_bands_[bs][j];
            if (!main_segment && high_band_coded_[ch][j]) {
              mult1 = std::sqrt(exp_power[j] / exp_power[last_high_band]);
              mult1 = static_cast<float>(mult1 * std::pow(10.0, high_band_values_[ch][j] * 0.05));
              mult1 = mult1 / (max_exponent_[ch] * 0.02f);
              mult1 *= mdct_norm;
              for (int i = 0; i < nn; ++i) {
                const float ns = noise[static_cast<std::size_t>(noise_index_)];
                noise_index_ = (noise_index_ + 1) & (k_noise_tab_size - 1);
                *coefs++ = ns * exponents[eidx + (i << bsize >> esize)] * mult1;
              }
              eidx += nn << bsize >> esize;
            } else {
              for (int i = 0; i < nn; ++i) {
                const float ns = noise[static_cast<std::size_t>(noise_index_)];
                noise_index_ = (noise_index_ + 1) & (k_noise_tab_size - 1);
                *coefs++ = ((*coefs1++) + ns) * exponents[eidx + (i << bsize >> esize)] * mult;
              }
              eidx += nn << bsize >> esize;
            }
          }

          const int tail = block_len_ - coefs_end_[bs];
          mult1 = mult * exponents[eidx + ((-(1 << bsize)) >> esize)];
          for (int i = 0; i < tail; ++i) {
            *coefs++ = noise[static_cast<std::size_t>(noise_index_)] * mult1;
            noise_index_ = (noise_index_ + 1) & (k_noise_tab_size - 1);
          }
        } else {
          const int nn = nb_coefs[ch];
          for (int i = 0; i < nn; ++i) {
            *coefs++ = coefs1[i] * exponents[i << bsize >> esize] * mult;
          }
          const int tail = block_len_ - coefs_end_[bs];
          for (int i = 0; i < tail; ++i) {
            *coefs++ = 0.0f;
          }
        }
      }

      if (ms_stereo_ && channel_coded_[1]) {
        if (!channel_coded_[0]) {
          std::fill_n(coefs_[0].begin(), block_len_, 0.0f);
          channel_coded_[0] = true;
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(block_len_); ++i) {
          const float a = coefs_[0][i];
          const float b = coefs_[1][i];
          coefs_[0][i] = a + b;
          coefs_[1][i] = a - b;
        }
      }
    }

    for (std::size_t ch = 0; ch < channels; ++ch) {
      const int n4 = block_len_ / 2;
      if (channel_coded_[ch]) {
        imdct_[bs].run(coefs_[ch].data(), output_.data());
      } else if (!(ms_stereo_ && ch == 1)) {
        std::fill(output_.begin(), output_.end(), 0.0f);
      }
      const int index = (frame_len_ / 2) + block_pos_ - n4;
      window(frame_out_[ch].data() + index);
    }

    block_pos_ += block_len_;
    return block_pos_ >= frame_len_ ? 1 : 0;
  }

  bool run_level_decode(BitReader& gb, int table, float* ptr, int num_coefs, int coef_nb_bits) {
    const auto t = static_cast<std::size_t>(table);
    const Vlc& vlc = tables_.coef[t];
    const auto& level_table = tables_.level[t];
    const auto& run_table = tables_.run[t];
    const auto coef_mask = static_cast<unsigned>(block_len_ - 1);
    int offset = 0;
    for (; offset < num_coefs; ++offset) {
      const int code = vlc.decode(gb);
      if (code < 0) {
        return false;
      }
      if (code > 1) {
        offset += run_table[static_cast<std::size_t>(code)];
        const bool negative = gb.get1() == 0;
        const float level = level_table[static_cast<std::size_t>(code)];
        ptr[static_cast<unsigned>(offset) & coef_mask] = negative ? -level : level;
      } else if (code == 1) {
        break;
      } else {
        const int level = static_cast<int>(gb.get(coef_nb_bits));
        offset += static_cast<int>(gb.get(frame_len_bits_));
        const bool negative = gb.get1() == 0;
        ptr[static_cast<unsigned>(offset) & coef_mask] = static_cast<float>(negative ? -level : level);
      }
    }
    return offset <= num_coefs;
  }

  int channels_;
  int sample_rate_;
  const SharedTables& tables_;
  int frame_len_bits_{0};
  int frame_len_{0};
  int nb_block_sizes_{0};
  int byte_offset_bits_{0};
  bool use_noise_coding_{false};
  int coef_vlc_table_{2};
  std::array<int, k_block_nb_sizes> exponent_sizes_{};
  std::array<std::array<std::uint16_t, 25>, k_block_nb_sizes> exponent_bands_{};
  std::array<int, k_block_nb_sizes> high_band_start_{};
  std::array<int, k_block_nb_sizes> coefs_end_{};
  std::array<int, k_block_nb_sizes> exponent_high_sizes_{};
  std::array<std::array<int, k_high_band_max>, k_block_nb_sizes> exponent_high_bands_{};
  std::array<std::array<int, k_high_band_max>, k_max_channels> high_band_coded_{};
  std::array<std::array<int, k_high_band_max>, k_max_channels> high_band_values_{};
  bool reset_block_lengths_{true};
  int block_len_bits_{0};
  int next_block_len_bits_{0};
  int prev_block_len_bits_{0};
  int block_len_{0};
  int block_pos_{0};
  bool ms_stereo_{false};
  std::array<bool, k_max_channels> channel_coded_{};
  std::array<int, k_max_channels> exponents_bsize_{};
  std::array<bool, k_max_channels> exponents_initialized_{};
  std::array<std::array<float, k_block_max_size>, k_max_channels> exponents_{};
  std::array<float, k_max_channels> max_exponent_{};
  std::array<std::array<float, k_block_max_size>, k_max_channels> coefs1_{};
  std::array<std::array<float, k_block_max_size>, k_max_channels> coefs_{};
  std::array<float, k_block_max_size * 2> output_{};
  std::array<std::array<float, k_block_max_size * 2>, k_max_channels> frame_out_{};
  std::vector<Imdct> imdct_;
  std::vector<std::uint8_t> last_superframe_;
  int last_bitoffset_{0};
  int last_superframe_len_{0};
  int noise_index_{0};
};

std::int16_t to_s16(float v) {
  if (!std::isfinite(v)) {
    return 0;
  }
  const float r = std::nearbyint(v);
  return static_cast<std::int16_t>(std::clamp(r, -32768.0f, 32767.0f));
}

}  // namespace

XwmaPcm decode_xwma(std::span<const std::byte> bytes) {
  const SoundContainer container = parse_sound_container(bytes);
  const XwmaFormat& format = container.format;
  if (format.channels > k_max_channels) {
    fail(container.riff_offset, "xWMA with more than two channels is not supported");
  }

  WmaV2Decoder decoder(format, container.riff_offset);

  XwmaPcm pcm;
  pcm.sample_rate = format.sample_rate;
  pcm.channels = format.channels;

  const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data()) + container.data_offset;
  const std::size_t align = format.block_align;
  Planar planar;
  std::vector<std::uint8_t> padded;
  for (std::size_t offset = 0; offset < container.data_size; offset += align) {
    const std::size_t take = std::min(align, container.data_size - offset);
    const std::uint8_t* packet = data + offset;
    if (take < align) {
      padded.assign(align, 0);
      std::memcpy(padded.data(), packet, take);
      packet = padded.data();
    }
    if (!decoder.decode_packet(packet, static_cast<int>(align), planar)) {
      ++pcm.bad_packets;
    }
  }
  decoder.drain(planar);

  const std::size_t skip = static_cast<std::size_t>(decoder.frame_len()) * 2;
  const std::size_t total = planar[0].size();
  const std::size_t frames = total > skip ? total - skip : 0;
  pcm.samples.resize(frames * format.channels);
  for (std::size_t i = 0; i < frames; ++i) {
    for (std::size_t ch = 0; ch < format.channels; ++ch) {
      pcm.samples[i * format.channels + ch] = to_s16(planar[ch][i + skip]);
    }
  }
  return pcm;
}

}  // namespace fo4::audio
