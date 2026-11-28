#include "fo4/checks/duplicate_formid.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace fo4 {

namespace {

std::uint32_t read_u32_le(const std::byte* data) noexcept {
  const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[0]));
  const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[1]));
  const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[2]));
  const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[3]));
  return b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u);
}

std::string read_signature(const std::byte* data) {
  std::string sig(4, '?');
  for (std::size_t i = 0; i < 4; ++i) {
    const auto ch = static_cast<unsigned char>(data[i]);
    if (ch >= 0x20u && ch < 0x7Fu && ch != ',') {
      sig[i] = static_cast<char>(ch);
    }
  }
  return sig;
}

class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

  std::uint64_t size() const noexcept { return static_cast<std::uint64_t>(bytes_.size()); }

  bool read_header(std::uint64_t offset, std::array<std::byte, kDuplicateFormIdHeaderLength>& out) const noexcept {
    if (offset > size() || size() - offset < kDuplicateFormIdHeaderLength) {
      return false;
    }
    std::memcpy(out.data(), bytes_.data() + static_cast<std::size_t>(offset), kDuplicateFormIdHeaderLength);
    return true;
  }

 private:
  std::span<const std::byte> bytes_;
};

class FileReader {
 public:
  FileReader(std::ifstream& stream, std::uint64_t size) noexcept : stream_(stream), size_(size) {}

  std::uint64_t size() const noexcept { return size_; }

  bool read_header(std::uint64_t offset, std::array<std::byte, kDuplicateFormIdHeaderLength>& out) const {
    if (offset > size_ || size_ - offset < kDuplicateFormIdHeaderLength) {
      return false;
    }
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream_) {
      io_failed_ = true;
      return false;
    }
    stream_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (stream_.gcount() != static_cast<std::streamsize>(out.size()) || !stream_) {
      io_failed_ = true;
      return false;
    }
    return true;
  }

  bool io_failed() const noexcept { return io_failed_; }

 private:
  std::ifstream& stream_;
  std::uint64_t size_{0};
  mutable bool io_failed_{false};
};

template <typename Reader>
struct ScanState {
  const Reader& reader;
  std::unordered_map<std::uint32_t, std::string> first_types{};
  std::unordered_map<std::uint32_t, DuplicateFormIdEntry> duplicates{};
  std::optional<std::string> error{};
  bool io_error{false};
};

template <typename Reader>
bool scan_range(ScanState<Reader>& state, std::uint64_t start, std::uint64_t end, int depth) {
  if (depth > kDuplicateFormIdMaxGroupDepth) {
    state.error = "Plugin GRUP nesting exceeds maximum depth of 128.";
    return false;
  }

  std::uint64_t pos = start;
  std::array<std::byte, kDuplicateFormIdHeaderLength> header{};
  while (pos < end) {
    if (end - pos < kDuplicateFormIdHeaderLength) {
      state.error = "Truncated plugin record header.";
      return false;
    }
    if (!state.reader.read_header(pos, header)) {
      if constexpr (requires { state.reader.io_failed(); }) {
        if (state.reader.io_failed()) {
          state.io_error = true;
          state.error = "Failed to read plugin header.";
          return false;
        }
      }
      state.error = "Truncated plugin record header.";
      return false;
    }

    const std::string sig = read_signature(header.data());
    const std::uint32_t size = read_u32_le(header.data() + 4);

    if (sig == "GRUP") {
      if (size < kDuplicateFormIdHeaderLength) {
        state.error = "Invalid GRUP size in plugin.";
        return false;
      }
      if (size > end - pos) {
        state.error = "GRUP extends past end of parent range.";
        return false;
      }
      const std::uint64_t group_end = pos + size;
      if (!scan_range(state, pos + kDuplicateFormIdHeaderLength, group_end, depth + 1)) {
        return false;
      }
      pos = group_end;
      continue;
    }

    const std::uint64_t total_size = static_cast<std::uint64_t>(kDuplicateFormIdHeaderLength) + size;
    if (total_size > end - pos) {
      state.error = "Record extends past end of parent range.";
      return false;
    }
    const std::uint64_t record_end = pos + total_size;

    if (sig != "TES4") {
      const std::uint32_t raw_form_id = read_u32_le(header.data() + 12);
      auto [it, inserted] = state.first_types.try_emplace(raw_form_id, sig);
      if (!inserted) {
        auto& dup = state.duplicates[raw_form_id];
        if (dup.count == 0) {
          dup.raw_form_id = raw_form_id;
          dup.count = 2;
          dup.record_types.push_back(it->second);
          dup.record_types.push_back(sig);
        } else {
          dup.count += 1;
          dup.record_types.push_back(sig);
        }
      }
    }

    pos = record_end;
  }

  if (pos != end) {
    state.error = "Plugin scan did not end at expected boundary.";
    return false;
  }
  return true;
}

template <typename Reader>
DuplicateFormIdScanResult finalize_scan(const Reader& reader) {
  ScanState<Reader> state{reader, {}, {}, {}, false};
  DuplicateFormIdScanResult result;
  if (!scan_range(state, 0, reader.size(), 0)) {
    result.error = std::move(state.error);
    result.is_io_error = state.io_error;
    return result;
  }

  result.duplicates.reserve(state.duplicates.size());
  for (auto& [raw_id, entry] : state.duplicates) {
    result.duplicates.push_back(std::move(entry));
  }
  std::sort(result.duplicates.begin(), result.duplicates.end(),
            [](const DuplicateFormIdEntry& a, const DuplicateFormIdEntry& b) {
              return a.raw_form_id < b.raw_form_id;
            });
  return result;
}

}  // namespace

DuplicateFormIdScanResult scan_duplicate_form_ids(std::span<const std::byte> bytes) {
  ByteReader reader(bytes);
  return finalize_scan(reader);
}

DuplicateFormIdScanResult scan_duplicate_form_ids_file(const std::filesystem::path& path) {
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return {{}, ec.message(), true};
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return {{}, "Failed to open plugin file: " + path.string(), true};
  }
  FileReader reader(stream, static_cast<std::uint64_t>(file_size));
  return finalize_scan(reader);
}

std::string DuplicateFormIdCache::cache_key(const std::filesystem::path& path) {
  std::error_code ec;
  auto abs_path = std::filesystem::absolute(path, ec);
  if (ec) {
    abs_path = path;
  }
  auto normalized = abs_path.lexically_normal();
#if defined(_WIN32)
  auto utf8 = normalized.u8string();
  std::string out(reinterpret_cast<const char*>(utf8.data()), utf8.size());
  for (char& ch : out) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return out;
#else
  return normalized.string();
#endif
}

bool DuplicateFormIdCache::try_stat(const std::filesystem::path& path,
                                    std::uint64_t& out_size,
                                    std::int64_t& out_write_ns) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(path, ec);
  if (ec) {
    return false;
  }
  const auto ftime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return false;
  }
  out_size = static_cast<std::uint64_t>(sz);
  out_write_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ftime.time_since_epoch()).count();
  return true;
}

std::uint64_t DuplicateFormIdCache::current_generation_locked(const std::string& key) const {
  const auto it = generations_.find(key);
  return it == generations_.end() ? 0 : it->second;
}

std::optional<DuplicateFormIdScanResult> DuplicateFormIdCache::try_get_cached(
    const std::filesystem::path& path) {
  const std::string key = cache_key(path);
  std::uint64_t size = 0;
  std::int64_t write_ns = 0;
  if (!try_stat(path, size, write_ns)) {
    return std::nullopt;
  }

  std::lock_guard lock(mutex_);
  const auto it = entries_.find(key);
  if (it == entries_.end()) {
    return std::nullopt;
  }
  const FileStamp expected{size, write_ns, current_generation_locked(key)};
  if (it->second.stamp != expected) {
    return std::nullopt;
  }
  return it->second.result;
}

void DuplicateFormIdCache::invalidate(const std::filesystem::path& path) {
  const std::string key = cache_key(path);
  std::lock_guard lock(mutex_);
  generations_[key] += 1;
  entries_.erase(key);
}

#ifdef FO4_TEST_HOOKS
void DuplicateFormIdCache::set_before_publish_hook_for_test(BeforePublishHook hook) {
  std::lock_guard lock(mutex_);
  before_publish_hook_ = std::move(hook);
}
#endif

DuplicateFormIdScanResult DuplicateFormIdCache::scan(const std::filesystem::path& path) {
  const std::string key = cache_key(path);
  std::uint64_t size_before = 0;
  std::int64_t write_ns_before = 0;
  if (!try_stat(path, size_before, write_ns_before)) {
    return {{}, "Failed to stat plugin file: " + path.string(), true};
  }

  std::uint64_t gen_before = 0;
  BeforePublishHook hook;
  {
    std::lock_guard lock(mutex_);
    gen_before = current_generation_locked(key);
    const FileStamp expected{size_before, write_ns_before, gen_before};
    if (const auto it = entries_.find(key); it != entries_.end() && it->second.stamp == expected) {
      return it->second.result;
    }
    hook = before_publish_hook_;
  }

  DuplicateFormIdScanResult scanned = scan_duplicate_form_ids_file(path);
  if (scanned.is_io_error) {
    return scanned;
  }

  if (hook) {
    hook();
  }

  std::uint64_t size_after = 0;
  std::int64_t write_ns_after = 0;
  const bool stat_ok = try_stat(path, size_after, write_ns_after);

  std::lock_guard lock(mutex_);
  const std::uint64_t gen_after = current_generation_locked(key);
  if (!stat_ok || size_after != size_before || write_ns_after != write_ns_before || gen_after != gen_before) {
    return {{},
            "Plugin changed or was invalidated while duplicate FormIDs were being scanned; retry validation.",
            false};
  }

  entries_[key] = CacheEntry{{size_before, write_ns_before, gen_before}, scanned};
  return scanned;
}

}  // namespace fo4
