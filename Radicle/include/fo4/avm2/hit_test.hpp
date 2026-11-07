#pragma once

#include <fo4/avm2/display.hpp>

#include <memory>

namespace fo4::avm2 {

/* Requires a stage point; a hidden ancestor, a singular transform or zero size never hits. */
bool hit_test_point(const DisplayObject& object, double x, double y);

/* Requires a stage point; with shape_flag the decoded fill geometry decides when present. */
bool hit_test_point(const DisplayObject& object, double x, double y, bool shape_flag);

/* Requires both objects to be display objects; compares their axis-aligned stage bounds. */
bool hit_test_object(const DisplayObject& first, const DisplayObject& second);

/* Requires a stage point; returns the topmost visible, mouse-enabled object under it, or null. */
std::shared_ptr<DisplayObject> mouse_target_at(const std::shared_ptr<DisplayObject>& root, double x,
                                               double y);

/* Requires a stage point; reports false when an ancestor transform is singular. */
bool stage_to_local(const DisplayObject& object, double x, double y, double& local_x,
                    double& local_y);

}  // namespace fo4::avm2
