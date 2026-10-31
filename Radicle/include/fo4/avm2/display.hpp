#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::avm2 {

class Runtime;

class Interpreter;

struct TimelineFrameLabel {
  std::string name;
  std::uint32_t frame{1};
};

/* Invariant: frame numbers are 1-based, matching MovieClip::currentFrame. */
struct TimelineDefinition {
  std::uint32_t frame_count{1};
  std::vector<TimelineFrameLabel> labels;
  std::map<std::uint32_t, std::uint32_t> frame_scripts;
  const abc::AbcFile* frame_script_file{nullptr};

  std::uint32_t frame_for_label(const std::string& name) const;
};

class DisplayObject : public Object {
 public:
  explicit DisplayObject(Class* display_class);

  double x() const noexcept { return x_; }
  void set_x(double value) noexcept { x_ = value; }
  double y() const noexcept { return y_; }
  void set_y(double value) noexcept { y_ = value; }
  double scale_x() const noexcept { return scale_x_; }
  void set_scale_x(double value) noexcept { scale_x_ = value; }
  double scale_y() const noexcept { return scale_y_; }
  void set_scale_y(double value) noexcept { scale_y_ = value; }
  double rotation() const noexcept { return rotation_; }
  void set_rotation(double value) noexcept { rotation_ = value; }
  double alpha() const noexcept { return alpha_; }
  void set_alpha(double value) noexcept { alpha_ = value; }

  const std::vector<Value>& filters() const noexcept { return filters_; }
  std::vector<Value>& filters() noexcept { return filters_; }
  /* A placed object must keep its colour transform so the snapshot reports it faithfully. */
  const double* color_multiply() const noexcept { return color_multiply_; }
  const double* color_add() const noexcept { return color_add_; }
  void set_color_multiply(const double values[4]) noexcept {
    for (int index = 0; index < 4; ++index) color_multiply_[index] = values[index];
  }
  void set_color_add(const double values[4]) noexcept {
    for (int index = 0; index < 4; ++index) color_add_[index] = values[index];
  }

  /* A drawable must keep one Graphics object for its lifetime, because scripts keep drawing into it. */
  const Value& graphics() const noexcept { return graphics_; }
  void set_graphics(Value value) { graphics_ = std::move(value); }
  bool stage_focus_rect() const noexcept { return stage_focus_rect_; }
  void set_stage_focus_rect(bool value) noexcept { stage_focus_rect_ = value; }
  /* Invariant: focus is held weakly, so the stage never keeps a removed object alive. */
  std::shared_ptr<DisplayObject> focus() const { return focus_.lock(); }
  void set_focus(const std::shared_ptr<DisplayObject>& target) { focus_ = target; }

  /* A Bitmap must keep the BitmapData the script assigned, so a snapshot can report it. */
  const Value& bitmap_data() const noexcept { return bitmap_data_; }
  void set_bitmap_data(Value value) { bitmap_data_ = std::move(value); }
  double width() const noexcept { return width_; }
  void set_width(double value) noexcept { width_ = value; }
  double height() const noexcept { return height_; }
  void set_height(double value) noexcept { height_ = value; }
  double origin_x() const noexcept { return origin_x_; }
  double origin_y() const noexcept { return origin_y_; }
  void set_origin(double x, double y) noexcept {
    origin_x_ = x;
    origin_y_ = y;
  }
  bool visible() const noexcept { return visible_; }
  double mouse_x() const noexcept { return mouse_x_; }
  double mouse_y() const noexcept { return mouse_y_; }
  void set_mouse_position(double x, double y) noexcept {
    mouse_x_ = x;
    mouse_y_ = y;
  }
  void set_visible(bool value) noexcept { visible_ = value; }
  bool tab_enabled() const noexcept { return tab_enabled_; }
  void set_tab_enabled(bool value) noexcept { tab_enabled_ = value; }
  bool tab_children() const noexcept { return tab_children_; }
  void set_tab_children(bool value) noexcept { tab_children_ = value; }
  bool mouse_enabled() const noexcept { return mouse_enabled_; }
  void set_mouse_enabled(bool value) noexcept { mouse_enabled_ = value; }
  bool mouse_children() const noexcept { return mouse_children_; }
  void set_mouse_children(bool value) noexcept { mouse_children_ = value; }
  bool button_mode() const noexcept { return button_mode_; }
  void set_button_mode(bool value) noexcept { button_mode_ = value; }
  const std::string& name() const noexcept { return name_; }
  void set_name(std::string name) { name_ = std::move(name); }

  DisplayObject* parent() const noexcept { return parent_; }
  void set_parent(DisplayObject* parent) noexcept { parent_ = parent; }
  DisplayObject* root() const noexcept;
  DisplayObject* stage() const noexcept;

  double stage_width() const noexcept { return stage_width_; }
  void set_stage_width(double value) noexcept { stage_width_ = value; }
  double stage_height() const noexcept { return stage_height_; }
  void set_stage_height(double value) noexcept { stage_height_ = value; }
  double frame_rate() const noexcept { return frame_rate_; }
  void set_frame_rate(double value) noexcept { frame_rate_ = value; }

  virtual bool is_container() const noexcept { return false; }
  virtual bool is_movie_clip() const noexcept { return false; }
  virtual bool is_text_field() const noexcept { return false; }
  virtual bool is_static_text() const noexcept { return false; }

  void set_display_class(Class* display_class) noexcept { display_class_ = display_class; }
  Class* display_class() const noexcept { return display_class_; }

  std::uint32_t character_id() const noexcept { return character_id_; }
  void set_character_id(std::uint32_t character_id) noexcept { character_id_ = character_id; }
  std::int32_t clip_depth() const noexcept { return clip_depth_; }
  void set_clip_depth(std::int32_t clip_depth) noexcept { clip_depth_ = clip_depth; }
  std::uint8_t blend_mode() const noexcept { return blend_mode_; }
  void set_blend_mode(std::uint8_t blend_mode) noexcept { blend_mode_ = blend_mode; }
  /* Invariant: absent for a display object that no placement gave a morph ratio. */
  std::optional<std::uint16_t> ratio() const noexcept { return ratio_; }
  void set_ratio(std::optional<std::uint16_t> ratio) noexcept { ratio_ = ratio; }
  bool cache_as_bitmap() const noexcept { return cache_as_bitmap_; }
  void set_cache_as_bitmap(bool value) noexcept { cache_as_bitmap_ = value; }
  /* Invariant: the decoded PlaceObject3 filter list, kept apart from the AS3 filters array. */
  const std::vector<swf::SwfFilter>& placed_filters() const noexcept { return placed_filters_; }
  void set_placed_filters(std::vector<swf::SwfFilter> filters) {
    placed_filters_ = std::move(filters);
  }

  bool assigned() const noexcept { return assigned_; }
  void set_assigned(bool assigned) noexcept { assigned_ = assigned; }

 private:
  Class* display_class_{nullptr};
  std::uint32_t character_id_{0};
  std::int32_t clip_depth_{0};
  std::uint8_t blend_mode_{0};
  std::optional<std::uint16_t> ratio_;
  bool cache_as_bitmap_{false};
  std::vector<swf::SwfFilter> placed_filters_;
  double x_{0.0};
  double y_{0.0};
  double scale_x_{1.0};
  double scale_y_{1.0};
  double rotation_{0.0};
  double alpha_{1.0};
  double mouse_x_{0.0};
  double mouse_y_{0.0};
  std::vector<Value> filters_;
  Value bitmap_data_;
  Value graphics_;
  std::weak_ptr<DisplayObject> focus_;
  bool stage_focus_rect_{true};
  double color_multiply_[4]{1.0, 1.0, 1.0, 1.0};
  double color_add_[4]{0.0, 0.0, 0.0, 0.0};
  double width_{0.0};
  double height_{0.0};
  double origin_x_{0.0};
  double origin_y_{0.0};
  bool visible_{true};
  bool tab_enabled_{false};
  bool tab_children_{false};
  bool mouse_enabled_{true};
  bool mouse_children_{true};
  bool button_mode_{false};
  bool assigned_{false};
  double stage_width_{0.0};
  double stage_height_{0.0};
  double frame_rate_{24.0};
  DisplayObject* parent_{nullptr};
  std::string name_;
};

class DisplayObjectContainer : public DisplayObject {
 public:
  explicit DisplayObjectContainer(Class* display_class);

  bool is_container() const noexcept override { return true; }

  const std::vector<std::shared_ptr<DisplayObject>>& children() const noexcept {
    return children_;
  }

  bool add_child(const std::shared_ptr<DisplayObject>& child);
  bool add_child_at(const std::shared_ptr<DisplayObject>& child, std::size_t index);
  bool remove_child(const std::shared_ptr<DisplayObject>& child);
  std::shared_ptr<DisplayObject> remove_child_at(std::size_t index);
  std::shared_ptr<DisplayObject> child_at(std::size_t index) const;
  std::shared_ptr<DisplayObject> child_by_name(const std::string& name) const;
  bool contains(const DisplayObject* child) const;
  bool set_child_index(const DisplayObject* child, std::size_t index);
  bool swap_children(const DisplayObject* first, const DisplayObject* second);
  std::size_t child_index(const DisplayObject* child) const;

 private:
  std::vector<std::shared_ptr<DisplayObject>> children_;
};

class MovieClip : public DisplayObjectContainer {
 public:
  explicit MovieClip(Class* display_class);

  bool is_movie_clip() const noexcept override { return true; }

  const std::shared_ptr<TimelineDefinition>& timeline() const noexcept { return timeline_; }
  void set_timeline(std::shared_ptr<TimelineDefinition> timeline) { timeline_ = std::move(timeline); }

  std::uint32_t current_frame() const noexcept { return current_frame_; }
  std::uint32_t total_frames() const noexcept;
  bool playing() const noexcept { return playing_; }
  void set_playing(bool playing) noexcept { playing_ = playing; }
  void set_frame_script(std::uint32_t frame, std::shared_ptr<FunctionObject> script);

  /* Requires a linked runtime; the result reports a failed frame script by status. */
  ExecutionResult goto_frame(Interpreter& interpreter, std::uint32_t frame, bool play);
  /* Requires a linked runtime; advances one frame and runs the frame lifecycle hooks in order. */
  ExecutionResult advance_frame(Interpreter& interpreter);
  /* Requires a linked runtime; advances one frame without frame events, which the stage sends. */
  ExecutionResult advance_timeline(Interpreter& interpreter);
  /* Places a frame and sets the playhead; runs no frame script and dispatches no event. */
  ExecutionResult place_frame(Interpreter& interpreter, std::uint32_t frame, bool play);
  /* Runs the current frame's script; the caller has placed the frame already. */
  ExecutionResult run_frame_actions(Interpreter& interpreter);

  /* enterFrame runs before placement and the frame script; exitFrame runs after them. */
  ExecutionResult run_frame_lifecycle(Interpreter& interpreter);

 protected:
  /* The bridge subclass must apply the timeline's PlaceObject/RemoveObject rows here. */
  virtual ExecutionResult apply_frame_state(Interpreter& interpreter);

 private:
  ExecutionResult run_frame_script(Interpreter& interpreter);
  void step_playhead();

  std::shared_ptr<TimelineDefinition> timeline_;
  std::map<std::uint32_t, std::shared_ptr<FunctionObject>> added_frame_scripts_;
  std::uint32_t current_frame_{1};
  bool playing_{true};
};

/* Requires a linked runtime; resolves a display class by rendered name. */
Class* find_display_class(const Runtime& runtime, std::string_view rendered_name);

/* Requires a runtime whose display classes are installed; builds the display object for a class. */
std::shared_ptr<DisplayObject> make_display_object(Runtime& runtime, Class& klass);

/* Requires the property name to be a public package name; returns true when handled. */
bool read_display_property(Interpreter& interpreter, DisplayObject& object,
                           const Multiname& multiname, Value& out);
bool write_display_property(DisplayObject& object, const Multiname& multiname, const Value& value);

}  // namespace fo4::avm2
