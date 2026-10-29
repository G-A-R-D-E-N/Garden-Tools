#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fo4::avm2 {

struct SnapshotSoundEvent {
  enum class Kind : std::uint8_t { Play = 0, Stop, Volume };

  Kind kind{Kind::Play};
  std::uint16_t sound_id{0};
  double volume{1.0};
  std::uint32_t loops{0};

  bool operator==(const SnapshotSoundEvent&) const = default;
};

/* A decoded DefineSound body must carry what the host needs to play it. */
struct SoundResource {
  std::uint16_t source_id{0};
  std::uint8_t format{0};
  std::uint8_t rate{0};
  bool sample_16bit{false};
  bool stereo{false};
  std::uint32_t sample_count{0};
  std::vector<std::uint8_t> payload;

  bool operator==(const SoundResource&) const = default;
};

/* The sound queue must be drained by exactly one snapshot, so a host never plays an event twice. */
class SoundEventQueue {
 public:
  void push(SnapshotSoundEvent event) { events_.push_back(std::move(event)); }
  void append_to(std::vector<SnapshotSoundEvent>& snapshot);
  void clear() noexcept { events_.clear(); }
  const std::vector<SnapshotSoundEvent>& events() const noexcept { return events_; }
  bool empty() const noexcept { return events_.empty(); }

 private:
  std::vector<SnapshotSoundEvent> events_;
};

}  // namespace fo4::avm2
