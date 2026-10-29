#pragma once

#include <fo4/avm1/avm1.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fo4::avm1 {

enum class Avm1RunStatus : std::uint8_t {
  Ok = 0,
  InvalidStream,
  Unsupported,
  StackUnderflow,
  ConstantOutOfRange,
  BadBranch,
  StepLimit,
  ResourceLimit,
  Thrown,
};

const char* run_status_name(Avm1RunStatus status) noexcept;

enum class Avm1DataKind : std::uint8_t {
  Undefined = 0,
  Null,
  Boolean,
  Number,
  String,
  Object,
  Function,
};

struct Avm1Object;
struct Avm1FunctionBody;

struct Avm1Data {
  Avm1DataKind kind{Avm1DataKind::Undefined};
  bool boolean{false};
  double number{0.0};
  std::string text;
  std::shared_ptr<Avm1Object> object;
  std::shared_ptr<const Avm1FunctionBody> function;
};

struct Avm1Result {
  Avm1RunStatus status{Avm1RunStatus::Ok};
  std::string message;
  std::uint8_t opcode{0};
  std::size_t offset{0};
  std::size_t steps{0};
  std::vector<std::string> trace;
  std::map<std::string, Avm1Data> variables;

  bool ok() const noexcept { return status == Avm1RunStatus::Ok; }
};

/* Requires 1-based frame numbers; frame_for_label must match labels ASCII case-insensitively. */
/* Requires an empty set_target path to reset the target to the original clip. */
struct Avm1TimelineHost {
  virtual ~Avm1TimelineHost() = default;
  virtual void goto_frame(std::uint32_t frame) = 0;
  virtual void play() = 0;
  virtual void stop() = 0;
  virtual std::uint32_t current_frame() = 0;
  virtual std::uint32_t total_frames() = 0;
  virtual std::optional<std::uint32_t> frame_for_label(const std::string& label) = 0;
  virtual Avm1Data get_property(std::uint8_t index) = 0;
  virtual void set_property(std::uint8_t index, const Avm1Data& value) = 0;
  virtual void set_target(const std::string& path) = 0;
};

struct Avm1Environment {
  std::uint32_t random_seed{0x2545F491u};
  std::uint32_t elapsed_ms{0};
  Avm1TimelineHost* timeline{nullptr};
};

/* Requires a stream from decode_actions; a stream that did not decode is refused before any record. */
/* Requires environment.random_seed to be nonzero, since a zero xorshift state stays zero. */
Avm1Result execute_actions(const Avm1Stream& stream, std::size_t max_steps = 1000000,
                           std::uint8_t swf_version = 0,
                           const Avm1Environment& environment = {});

}  // namespace fo4::avm1
