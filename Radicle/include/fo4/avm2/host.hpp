#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::avm2 {

/* A host must supply callbacks at open time; a missing hook must report unavailable. */
struct HostCallbacks {
  /* A sibling SWF or XML load must resolve through this; a miss returns nullopt. */
  std::function<std::optional<std::vector<std::uint8_t>>(std::string_view path)> resolve_asset;
  /* SharedObject bytes must come from host storage; a miss returns nullopt. */
  std::function<std::optional<std::vector<std::uint8_t>>(std::string_view name)> read_storage;
  /* A SharedObject flush must write through this and report success. */
  std::function<bool(std::string_view name, std::vector<std::uint8_t> bytes)> write_storage;
  /* ExternalInterface and the Scaleform bridge must be forwarded here; nullopt means no answer. */
  std::function<std::optional<std::string>(std::string_view method,
                                           std::vector<std::string> arguments)>
      host_call;
};

struct HostStats {
  std::size_t asset_requests{0};
  std::size_t asset_misses{0};
  std::size_t storage_reads{0};
  std::size_t storage_writes{0};
  std::size_t host_calls{0};
  std::size_t host_call_misses{0};
};

/* A bridge must stay usable with no callbacks, so a bare runtime must report every request. */
class HostBridge {
 public:
  HostBridge() = default;
  explicit HostBridge(HostCallbacks callbacks) : callbacks_(std::move(callbacks)) {}

  void set_callbacks(HostCallbacks callbacks) { callbacks_ = std::move(callbacks); }
  const HostCallbacks& callbacks() const noexcept { return callbacks_; }
  const HostStats& stats() const noexcept { return stats_; }
  HostStats& stats() noexcept { return stats_; }

  std::optional<std::vector<std::uint8_t>> resolve_asset(std::string_view path);
  std::optional<std::string> call_host(std::string_view method,
                                       std::vector<std::string> arguments);
  std::optional<std::vector<std::uint8_t>> read_storage(std::string_view name);
  bool write_storage(std::string_view name, std::vector<std::uint8_t> bytes);

 private:
  HostCallbacks callbacks_{};
  HostStats stats_{};
};

inline std::optional<std::vector<std::uint8_t>> HostBridge::resolve_asset(std::string_view path) {
  ++stats_.asset_requests;
  if (!callbacks_.resolve_asset) {
    ++stats_.asset_misses;
    return std::nullopt;
  }
  std::optional<std::vector<std::uint8_t>> result = callbacks_.resolve_asset(path);
  if (!result.has_value()) {
    ++stats_.asset_misses;
  }
  return result;
}

inline std::optional<std::string> HostBridge::call_host(std::string_view method,
                                                       std::vector<std::string> arguments) {
  ++stats_.host_calls;
  if (!callbacks_.host_call) {
    ++stats_.host_call_misses;
    return std::nullopt;
  }
  std::optional<std::string> result = callbacks_.host_call(method, std::move(arguments));
  if (!result.has_value()) {
    ++stats_.host_call_misses;
  }
  return result;
}

inline std::optional<std::vector<std::uint8_t>> HostBridge::read_storage(std::string_view name) {
  ++stats_.storage_reads;
  if (!callbacks_.read_storage) {
    return std::nullopt;
  }
  return callbacks_.read_storage(name);
}

inline bool HostBridge::write_storage(std::string_view name, std::vector<std::uint8_t> bytes) {
  ++stats_.storage_writes;
  if (!callbacks_.write_storage) {
    return false;
  }
  return callbacks_.write_storage(name, std::move(bytes));
}

}  // namespace fo4::avm2
