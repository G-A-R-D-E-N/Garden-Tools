#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::avm2 {

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

class DisplayObject : public Object, public std::enable_shared_from_this<DisplayObject> {
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
  double width() const noexcept { return width_; }
  void set_width(double value) noexcept { width_ = value; }
  double height() const noexcept { return height_; }
  void set_height(double value) noexcept { height_ = value; }
  bool visible() const noexcept { return visible_; }
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

  virtual bool is_container() const noexcept { return false; }
  virtual bool is_movie_clip() const noexcept { return false; }

  void set_display_class(Class* display_class) noexcept { display_class_ = display_class; }
  Class* display_class() const noexcept { return display_class_; }

  bool assigned() const noexcept { return assigned_; }
  void set_assigned(bool assigned) noexcept { assigned_ = assigned; }

 private:
  Class* display_class_{nullptr};
  double x_{0.0};
  double y_{0.0};
  double scale_x_{1.0};
  double scale_y_{1.0};
  double rotation_{0.0};
  double alpha_{1.0};
  double width_{0.0};
  double height_{0.0};
  bool visible_{true};
  bool tab_enabled_{false};
  bool tab_children_{false};
  bool mouse_enabled_{true};
  bool mouse_children_{true};
  bool button_mode_{false};
  bool assigned_{false};
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

  /* Requires a linked runtime; the result reports a failed frame script by status. */
  ExecutionResult goto_frame(Interpreter& interpreter, std::uint32_t frame, bool play);
  /* Requires a linked runtime; advances one frame and runs the frame lifecycle hooks in order. */
  ExecutionResult advance_frame(Interpreter& interpreter);

  /* The lifecycle order must stay construct, frameScript, enterFrame, exitFrame. */
  ExecutionResult run_frame_lifecycle(Interpreter& interpreter);

 private:
  ExecutionResult run_frame_script(Interpreter& interpreter);

  std::shared_ptr<TimelineDefinition> timeline_;
  std::uint32_t current_frame_{1};
  bool playing_{true};
};

/* Requires a linked runtime; resolves a display class by rendered name. */
Class* find_display_class(const struct Runtime& runtime, std::string_view rendered_name);

/* Requires a runtime whose display classes are installed; builds the display object for a class. */
std::shared_ptr<DisplayObject> make_display_object(Runtime& runtime, Class& klass);

/* Requires the property name to be a public package name; returns true when handled. */
bool read_display_property(Interpreter& interpreter, DisplayObject& object,
                           const Multiname& multiname, Value& out);
bool write_display_property(DisplayObject& object, const Multiname& multiname, const Value& value);

}  // namespace fo4::avm2
