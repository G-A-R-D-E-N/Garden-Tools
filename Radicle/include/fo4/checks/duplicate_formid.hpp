#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace fo4 {

inline constexpr std::size_t kDuplicateFormIdHeaderLength = 24;
inline constexpr int kDuplicateFormIdMaxGroupDepth = 128;

struct DuplicateFormIdEntry {
  std::uint32_t raw_form_id{0};
  std::size_t count{0};
  std::vector<std::string> record_types;
};

struct DuplicateFormIdScanResult {
  std::vector<DuplicateFormIdEntry> duplicates;
  std::optional<std::string> error;
  bool is_io_error{false};
};

DuplicateFormIdScanResult scan_duplicate_form_ids(std::span<const std::byte> bytes);
DuplicateFormIdScanResult scan_duplicate_form_ids_file(const std::filesystem::path& path);

class DuplicateFormIdCache {
 public:
  DuplicateFormIdScanResult scan(const std::filesystem::path& path);
  std::optional<DuplicateFormIdScanResult> try_get_cached(const std::filesystem::path& path);
  void invalidate(const std::filesystem::path& path);

  using BeforePublishHook = std::function<void()>;
#ifdef FO4_TEST_HOOKS
  void set_before_publish_hook_for_test(BeforePublishHook hook);
#endif

 private:
  struct FileStamp {
    std::uint64_t file_size{0};
    std::int64_t last_write_ns{0};
    std::uint64_t generation{0};

    bool operator==(const FileStamp& other) const noexcept = default;
  };

  struct CacheEntry {
    FileStamp stamp;
    DuplicateFormIdScanResult result;
  };

  static std::string cache_key(const std::filesystem::path& path);
  static bool try_stat(const std::filesystem::path& path,
                       std::uint64_t& out_size,
                       std::int64_t& out_write_ns);
  std::uint64_t current_generation_locked(const std::string& key) const;

  std::mutex mutex_;
  std::unordered_map<std::string, CacheEntry> entries_;
  std::unordered_map<std::string, std::uint64_t> generations_;
  BeforePublishHook before_publish_hook_;
};

}  // namespace fo4
