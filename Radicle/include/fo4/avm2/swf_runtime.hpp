#pragma once

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/swf/swf.hpp>

#include <fo4/abc/abc.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fo4::avm2 {

struct SwfPlacementRow {
  enum class Kind : std::uint8_t { Place, Remove };

  Kind kind{Kind::Place};
  std::uint16_t depth{0};
  std::uint16_t character_id{0};
  std::string name;
  double scale_x{1.0};
  double scale_y{1.0};
  double translate_x{0.0};
  double translate_y{0.0};
  bool has_clip_depth{false};
  std::uint16_t clip_depth{0};
};

/* Invariant: frames[i] must hold the rows that run when frame i+1 becomes current. */
struct SwfTimelineRows {
  std::uint32_t frame_count{1};
  std::vector<TimelineFrameLabel> labels;
  std::vector<std::vector<SwfPlacementRow>> frames;
};

struct SwfMovieData {
  SwfTimelineRows root;
  std::map<std::uint16_t, SwfTimelineRows> sprites;
  std::map<std::uint16_t, std::string> symbol_classes;
  std::vector<FontResource> fonts;
  std::vector<SoundResource> sounds;
  std::string root_class_name;
  double frame_rate{24.0};
  double stage_width{0.0};
  double stage_height{0.0};
};

struct SwfRuntimeOptions {
  bool start_playing{true};
  std::size_t max_sprite_depth{32};
  std::size_t max_tags{200000};
};

/* Requires the file's parsed tags; builds the movie data with no AVM2 state involved. */
std::shared_ptr<SwfMovieData> read_swf_movie_data(const swf::SwfFile& file,
                                                  const SwfRuntimeOptions& options = {});

/* A linked runtime must build the root clip, using the linked document class when present. */
std::shared_ptr<MovieClip> build_movie_root(Runtime& runtime,
                                            const std::shared_ptr<SwfMovieData>& data,
                                            Class** out_root_class = nullptr);

/* The host driver must be the only clock; the same dt sequence must give the same frames. */
class SwfRuntime {
 public:
  /* Requires a linked runtime; applies frame 1 of the root timeline before returning. */
  static std::shared_ptr<SwfRuntime> create(Runtime& runtime, Interpreter& interpreter,
                                            const swf::SwfFile& file,
                                            const SwfRuntimeOptions& options = {});

  void advance(Interpreter& interpreter, double dt_seconds);
  /* The host must take one snapshot per rendered frame; it drains the sound queue. */
  FrameSnapshot take_snapshot();
  FrameSnapshot snapshot() const;
  SoundEventQueue& sound_events();
  /* Host input must go through these; keyboard targets the root clip and mouse the stage. */
  void key_event(Interpreter& interpreter, std::uint32_t key_code, bool down);
  void mouse_event(Interpreter& interpreter, double x, double y, std::int32_t button, bool down);
  void mouse_move(Interpreter& interpreter, double x, double y);
  void mouse_wheel(Interpreter& interpreter, double x, double y, std::int32_t delta);
  void char_event(Interpreter& interpreter, std::uint32_t char_code);
  void set_playing(bool playing);
  bool playing() const;

  const std::shared_ptr<MovieClip>& root() const noexcept { return root_; }
  const std::shared_ptr<DisplayObjectContainer>& stage() const noexcept { return stage_; }
  const std::shared_ptr<SwfMovieData>& data() const noexcept { return data_; }
  double accumulator() const noexcept { return accumulator_; }

 private:
  SwfRuntime() = default;

  std::shared_ptr<SwfMovieData> data_;
  std::deque<abc::AbcFile> abc_files_;
  Class* root_class_{nullptr};
  std::shared_ptr<DisplayObjectContainer> stage_;
  std::shared_ptr<MovieClip> root_;
  Runtime* runtime_{nullptr};
  SoundEventQueue sounds_fallback_;
  double accumulator_{0.0};
  bool playing_{true};
  bool started_{false};
};

}  // namespace fo4::avm2
