// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

namespace fo4::audio::xwma_dsp {

inline int log2_floor(unsigned v) {
  int n = 0;
  while (v > 1) {
    v >>= 1;
    ++n;
  }
  return n;
}

class BitReader {
 public:
  BitReader() = default;
  BitReader(const std::uint8_t* data, std::int64_t bits) : data_(data), bits_(bits) {}

  std::int64_t position() const { return pos_; }
  std::int64_t left() const { return bits_ - pos_; }
  void skip(std::int64_t n) { pos_ += n; }

  unsigned get1() {
    unsigned bit = 0;
    if (pos_ < bits_) {
      bit = (data_[pos_ >> 3] >> (7 - (pos_ & 7))) & 1u;
    }
    ++pos_;
    return bit;
  }

  std::uint32_t get(int n) {
    std::uint32_t value = 0;
    while (n > 0) {
      const int avail = 8 - static_cast<int>(pos_ & 7);
      const int take = std::min(avail, n);
      const auto valid = static_cast<int>(std::clamp<std::int64_t>(bits_ - pos_, 0, take));
      std::uint32_t chunk = 0;
      if (valid > 0) {
        const std::uint32_t byte = data_[pos_ >> 3];
        chunk = ((byte >> (avail - valid)) & ((1u << valid) - 1u)) << (take - valid);
      }
      value = (value << take) | chunk;
      pos_ += take;
      n -= take;
    }
    return value;
  }

 private:
  const std::uint8_t* data_{nullptr};
  std::int64_t bits_{0};
  std::int64_t pos_{0};
};

class Vlc {
 public:
  Vlc() : child_(2, 0) {}

  void add(std::uint32_t code, int len, int symbol) {
    std::size_t node = 0;
    for (int i = len - 1; i >= 1; --i) {
      const std::size_t slot = node * 2 + ((code >> i) & 1u);
      if (child_[slot] == 0) {
        child_[slot] = static_cast<std::int32_t>(child_.size() / 2);
        child_.push_back(0);
        child_.push_back(0);
      }
      node = static_cast<std::size_t>(child_[slot]);
    }
    child_[node * 2 + (code & 1u)] = -(symbol + 1);
  }

  int decode(BitReader& reader) const {
    std::size_t node = 0;
    for (int depth = 0; depth < 32; ++depth) {
      const std::int32_t c = child_[node * 2 + reader.get1()];
      if (c == 0) {
        return -1;
      }
      if (c < 0) {
        return -c - 1;
      }
      node = static_cast<std::size_t>(c);
    }
    return -1;
  }

 private:
  std::vector<std::int32_t> child_;
};

class Fft {
 public:
  explicit Fft(int n) : n_(n), twiddle_(static_cast<std::size_t>(n / 2)), rev_(static_cast<std::size_t>(n)) {
    for (int i = 0; i < n / 2; ++i) {
      twiddle_[static_cast<std::size_t>(i)] = std::polar(1.0, -2.0 * std::numbers::pi * i / n);
    }
    const int bits = log2_floor(static_cast<unsigned>(n));
    for (int i = 0; i < n; ++i) {
      int r = 0;
      for (int b = 0; b < bits; ++b) {
        r |= ((i >> b) & 1) << (bits - 1 - b);
      }
      rev_[static_cast<std::size_t>(i)] = r;
    }
  }

  void forward(std::complex<double>* data) const {
    for (int i = 0; i < n_; ++i) {
      const int r = rev_[static_cast<std::size_t>(i)];
      if (r > i) {
        std::swap(data[i], data[r]);
      }
    }
    for (int size = 2; size <= n_; size <<= 1) {
      const int half = size / 2;
      const int step = n_ / size;
      for (int start = 0; start < n_; start += size) {
        for (int k = 0; k < half; ++k) {
          const auto t = data[start + k + half] * twiddle_[static_cast<std::size_t>(k * step)];
          data[start + k + half] = data[start + k] - t;
          data[start + k] += t;
        }
      }
    }
  }

 private:
  int n_;
  std::vector<std::complex<double>> twiddle_;
  std::vector<int> rev_;
};

class Imdct {
 public:
  explicit Imdct(int n)
      : n_(n),
        fft_(n / 2),
        pre_(static_cast<std::size_t>(n / 2)),
        post_(static_cast<std::size_t>(n / 2)),
        work_(static_cast<std::size_t>(n / 2)),
        dct_(static_cast<std::size_t>(n)) {
    for (int i = 0; i < n / 2; ++i) {
      pre_[static_cast<std::size_t>(i)] = std::polar(1.0, -std::numbers::pi * (4 * i + 1) / (4.0 * n));
      post_[static_cast<std::size_t>(i)] = std::polar(1.0, -std::numbers::pi * i / n);
    }
  }

  void run(const float* in, float* out) {
    const int h = n_ / 2;
    for (int i = 0; i < h; ++i) {
      work_[static_cast<std::size_t>(i)] =
          std::complex<double>(in[2 * i], in[n_ - 1 - 2 * i]) * pre_[static_cast<std::size_t>(i)];
    }
    fft_.forward(work_.data());
    for (int k = 0; k < h; ++k) {
      const auto u = work_[static_cast<std::size_t>(k)] * post_[static_cast<std::size_t>(k)];
      dct_[static_cast<std::size_t>(2 * k)] = u.real();
      dct_[static_cast<std::size_t>(n_ - 1 - 2 * k)] = -u.imag();
    }
    for (int i = 0; i < 2 * n_; ++i) {
      double v;
      if (i < h) {
        v = dct_[static_cast<std::size_t>(i + h)];
      } else if (i < 3 * h) {
        v = -dct_[static_cast<std::size_t>(3 * h - 1 - i)];
      } else {
        v = -dct_[static_cast<std::size_t>(i - 3 * h)];
      }
      out[i] = static_cast<float>(v);
    }
  }

 private:
  int n_;
  Fft fft_;
  std::vector<std::complex<double>> pre_;
  std::vector<std::complex<double>> post_;
  std::vector<std::complex<double>> work_;
  std::vector<double> dct_;
};


}  // namespace fo4::audio::xwma_dsp
