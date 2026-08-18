#include "f4lod/object_lod.hpp"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using f4lod::ObjectLodSafety;

    assert(f4lod::object_lod_level_index(4U) == 0U);
    assert(f4lod::object_lod_level_index(8U) == 1U);
    assert(f4lod::object_lod_level_index(16U) == 2U);
    assert(f4lod::object_lod_level_index(32U) == 3U);
    assert(!f4lod::object_lod_level_index(2U).has_value());

    const auto origin = f4lod::object_lod_cell_from_world(0.0F, 4095.0F);
    assert(origin.has_value());
    assert(origin->x == 0);
    assert(origin->y == 0);

    const auto positive = f4lod::object_lod_cell_from_world(4096.0F, 8192.0F);
    assert(positive.has_value());
    assert(positive->x == 1);
    assert(positive->y == 2);

    const auto negative = f4lod::object_lod_cell_from_world(-0.25F, -4096.25F);
    assert(negative.has_value());
    assert(negative->x == -1);
    assert(negative->y == -2);

    assert(!f4lod::object_lod_cell_from_world(
        std::numeric_limits<float>::infinity(), 0.0F).has_value());
    assert(!f4lod::object_lod_cell_from_world(
        std::numeric_limits<float>::quiet_NaN(), 0.0F).has_value());

    f4lod::ObjectLodDescriptor descriptor;
    descriptor.authored_lod_models[0] = "meshes/lod/example_lod_0.nif";
    descriptor.authored_lod_models[2] = "meshes/lod/example_lod_2.nif";
    assert(f4lod::object_lod_has_authored_model(descriptor, 4U));
    assert(!f4lod::object_lod_has_authored_model(descriptor, 8U));
    assert(f4lod::object_lod_has_authored_model(descriptor, 16U));
    assert(!f4lod::object_lod_has_authored_model(descriptor, 64U));

    assert(f4lod::classify_object_lod_safety(true, true, false, false) ==
           ObjectLodSafety::safe_static);
    assert(f4lod::classify_object_lod_safety(true, true, false, true) ==
           ObjectLodSafety::enable_state_sensitive);
    assert(f4lod::classify_object_lod_safety(true, true, true, true) ==
           ObjectLodSafety::potentially_stateful);
    assert(f4lod::classify_object_lod_safety(true, false, false, false) ==
           ObjectLodSafety::missing_authored_lod);
    assert(f4lod::classify_object_lod_safety(false, true, false, false) ==
           ObjectLodSafety::unsupported_asset_material);
    assert(f4lod::classify_object_lod_safety(false, false, true, true, true) ==
           ObjectLodSafety::forced);

    assert(std::string(f4lod::object_lod_safety_name(ObjectLodSafety::safe_static)) ==
           "safe-static");
    assert(std::string(f4lod::object_lod_safety_name(ObjectLodSafety::enable_state_sensitive)) ==
           "enable-state-sensitive");

    return 0;
}
