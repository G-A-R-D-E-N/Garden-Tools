#pragma once

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/text.hpp>
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
#include <string_view>
#include <vector>

namespace fo4::avm2 {

struct SwfPlacementRow {
  enum class Kind : std::uint8_t { Place, Remove };

  Kind kind{Kind::Place};
  std::uint16_t depth{0};
  bool move{false};
  bool has_character{false};
  std::uint16_t character_id{0};
  std::string name;
  bool has_matrix{false};
  double scale_x{1.0};
  double scale_y{1.0};
  double rotate_skew0{0.0};
  double rotate_skew1{0.0};
  double translate_x{0.0};
  double translate_y{0.0};
  bool has_clip_depth{false};
  std::uint16_t clip_depth{0};
  bool has_color_transform{false};
  double color_multiply[4]{1.0, 1.0, 1.0, 1.0};
  double color_add[4]{0.0, 0.0, 0.0, 0.0};
  bool has_ratio{false};
  std::uint16_t ratio{0};
  bool has_blend_mode{false};
  std::uint8_t blend_mode{0};
  bool has_visible{false};
  bool visible{true};
  bool has_bitmap_cache{false};
  bool bitmap_cache{false};
  bool has_filters{false};
  std::vector<swf::SwfFilter> filters;
};

/* Invariant: frames[i] must hold the rows that run when frame i+1 becomes current. */
struct SwfTimelineRows {
  std::uint32_t frame_count{1};
  std::vector<TimelineFrameLabel> labels;
  std::vector<std::vector<SwfPlacementRow>> frames;
};

struct SwfCharacterBounds {
  double origin_x{0.0};
  double origin_y{0.0};
  double width{0.0};
  double height{0.0};
};

struct SwfTextFieldData {
  std::string text;
  bool html{false};
  bool word_wrap{false};
  bool multiline{false};
  TextFieldAutoSize auto_size{TextFieldAutoSize::none};
  TextFormatData format;
};

struct SwfMovieData {
  SwfTimelineRows root;
  std::map<std::uint16_t, SwfTimelineRows> sprites;
  std::map<std::uint16_t, std::string> symbol_classes;
  /* Invariant: a character appears here only when its definition tag carries drawn bounds. */
  std::map<std::uint16_t, SwfCharacterBounds> character_bounds;
  /* Invariant: a character appears here only when its shape tag carries fill edges. */
  std::map<std::uint16_t, ShapeGeometry> character_shapes;
  std::map<std::uint16_t, SwfTextFieldData> text_fields;
  std::map<std::uint16_t, std::vector<StaticTextRecord>> static_texts;
  /* Invariant: font_owner is unique per created movie and never 0. */
  std::uint64_t font_owner{0};
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
  bool bgs_host{false};
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
                                            const SwfRuntimeOptions& options = {},
                                            std::shared_ptr<ApplicationDomainObject> domain = nullptr);
  /* A failed create must leave the reason available here for host diagnostics. */
  static const std::string& last_error();
  /* Requires that no text of this movie is read after the call. */
  void release_fonts();

  void advance(Interpreter& interpreter, double dt_seconds);
  /* Requires a linked, created movie; an absent or non-method member fails with its reason and must not throw. */
  ExecutionResult call_root(Interpreter& interpreter, std::string_view name,
                            const std::vector<Value>& args);
  /* The host must take one snapshot per rendered frame; it drains the sound queue. */
  FrameSnapshot take_snapshot();
  FrameSnapshot snapshot() const;
  SoundEventQueue& sound_events();
  /* Host input must go through these; keys go to the stage focus, mouse to the hit object. */
  void key_event(Interpreter& interpreter, std::uint32_t key_code, bool down);
  void mouse_event(Interpreter& interpreter, double x, double y, std::int32_t button, bool down);
  void mouse_move(Interpreter& interpreter, double x, double y);
  void mouse_wheel(Interpreter& interpreter, double x, double y, std::int32_t delta);
  void char_event(Interpreter& interpreter, std::uint32_t char_code);
  void set_playing(bool playing);
  bool playing() const;
  const std::string& frame_error() const noexcept { return frame_error_; }

  const std::shared_ptr<MovieClip>& root() const noexcept { return root_; }
  const std::shared_ptr<DisplayObjectContainer>& stage() const noexcept { return stage_; }
  const std::shared_ptr<SwfMovieData>& data() const noexcept { return data_; }
  double accumulator() const noexcept { return accumulator_; }

 private:
  SwfRuntime() = default;

  std::shared_ptr<SwfMovieData> data_;
  std::deque<abc::AbcFile> abc_files_;
  std::size_t next_script_{0};
  Class* root_class_{nullptr};
  std::shared_ptr<DisplayObjectContainer> stage_;
  std::shared_ptr<MovieClip> root_;
  Runtime* runtime_{nullptr};
  std::shared_ptr<FontRegistry> fonts_;
  SoundEventQueue sounds_fallback_;
  double accumulator_{0.0};
  std::vector<std::weak_ptr<DisplayObject>> roll_chain_;
  std::weak_ptr<DisplayObject> mouse_down_;
  bool playing_{true};
  bool started_{false};
  std::string frame_error_;
};

}  // namespace fo4::avm2
