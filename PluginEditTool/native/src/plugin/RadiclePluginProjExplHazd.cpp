#include "RadiclePluginLifecycleFields.hpp"
#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {

using namespace lifecycle;

Result<fo4::FormKey> RadiclePluginBackend::createProjectileLocked(fo4::Plugin& mutated,
                                                     const nlohmann::json& payload) const {
    bool valid = false;
    const auto editorId = optionalStringField(payload, "editorId", valid);
    if (!valid) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PROJ editorId must be a string or null"
        });
    }
    const auto name = optionalStringField(payload, "name", valid);
    if (!valid) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PROJ name must be a string or null"
        });
    }
    const auto muzzleFlashModel = optionalStringField(payload, "muzzleFlashModel", valid);
    if (!valid) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PROJ muzzleFlashModel must be a string or null"
        });
    }

    fo4::forms::ProjectileCreation creation;
    creation.editor_id = editorId.value_or(std::string{});
    creation.name = name;
    creation.muzzle_flash_model = muzzleFlashModel.value_or(std::string{});
    if (!lifecycleBoundsField(payload, creation.bounds) ||
        !lifecycleUnsignedField(payload, "flags", creation.flags, creation.flags) ||
        !lifecycleUnsignedField(payload, "type", creation.type, creation.type) ||
        !lifecycleFloatField(payload, "gravity", creation.gravity, creation.gravity) ||
        !lifecycleFloatField(payload, "speed", creation.speed, creation.speed) ||
        !lifecycleFloatField(payload, "range", creation.range, creation.range) ||
        !lifecycleFloatField(
            payload, "explosionProximity", creation.explosion_proximity,
            creation.explosion_proximity) ||
        !lifecycleFloatField(
            payload, "explosionTimer", creation.explosion_timer, creation.explosion_timer) ||
        !lifecycleFloatField(
            payload, "muzzleFlashDuration", creation.muzzle_flash_duration,
            creation.muzzle_flash_duration) ||
        !lifecycleFloatField(
            payload, "fadeOutTime", creation.fade_out_time, creation.fade_out_time) ||
        !lifecycleFloatField(payload, "force", creation.force, creation.force) ||
        !lifecycleFloatField(
            payload, "coneSpread", creation.cone_spread, creation.cone_spread) ||
        !lifecycleFloatField(
            payload, "collisionRadius", creation.collision_radius,
            creation.collision_radius) ||
        !lifecycleFloatField(payload, "lifetime", creation.lifetime, creation.lifetime) ||
        !lifecycleFloatField(
            payload, "relaunchInterval", creation.relaunch_interval,
            creation.relaunch_interval) ||
        !lifecycleUnsignedField(
            payload, "tracerFrequency", creation.tracer_frequency,
            creation.tracer_frequency) ||
        !lifecycleUnsignedField(
            payload, "soundLevel", creation.sound_level, creation.sound_level)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "PROJ numeric fields must fit their measured DNAM storage"
        });
    }
    if (const auto error = validateProvidedScalars(payload, {
            {"flags", "PROJ\\Flags"},
            {"gravity", "PROJ\\Gravity"},
            {"speed", "PROJ\\Speed"},
            {"range", "PROJ\\Range"},
            {"explosionProximity", "PROJ\\ExplosionProximity"},
            {"explosionTimer", "PROJ\\ExplosionTimer"},
            {"muzzleFlashDuration", "PROJ\\MuzzleFlashDuration"},
            {"fadeOutTime", "PROJ\\FadeOutTime"},
            {"force", "PROJ\\Force"},
            {"coneSpread", "PROJ\\ConeSpread"},
            {"collisionRadius", "PROJ\\CollisionRadius"},
            {"lifetime", "PROJ\\Lifetime"},
            {"relaunchInterval", "PROJ\\RelaunchInterval"},
            {"tracerFrequency", "PROJ\\TracerFrequency"},
            {"soundLevel", "PROJ\\SoundLevel"}})) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(payload, "light", creation.light)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "muzzleFlashLight", creation.muzzle_flash_light)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "explosionType", creation.explosion_type)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "activeSoundLoop", creation.active_sound_loop)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "countdownSound", creation.countdown_sound)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "deactivateSound", creation.deactivate_sound)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "defaultWeaponSource", creation.default_weapon_source)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(payload, "decalData", creation.decal_data)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "collisionLayer", creation.collision_layer)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "vatsProjectile", creation.vats_projectile)) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(graph_, creation.light, {"LIGH"}, "light")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.muzzle_flash_light, {"LIGH"}, "muzzleFlashLight")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.explosion_type, {"EXPL"}, "explosionType")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.active_sound_loop, {"SNDR"}, "activeSoundLoop")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.countdown_sound, {"SNDR"}, "countdownSound")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.deactivate_sound, {"SNDR"}, "deactivateSound")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.default_weapon_source, {"WEAP"}, "defaultWeaponSource")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.decal_data, {"TXST"}, "decalData")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.collision_layer, {"COLL"}, "collisionLayer")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.vats_projectile, {"PROJ"}, "vatsProjectile")) {
        return std::unexpected(*error);
    }

    ensureOwnerMaster(mutated, creation.light.owner());
    ensureOwnerMaster(mutated, creation.muzzle_flash_light.owner());
    ensureOwnerMaster(mutated, creation.explosion_type.owner());
    ensureOwnerMaster(mutated, creation.active_sound_loop.owner());
    ensureOwnerMaster(mutated, creation.countdown_sound.owner());
    ensureOwnerMaster(mutated, creation.deactivate_sound.owner());
    ensureOwnerMaster(mutated, creation.default_weapon_source.owner());
    ensureOwnerMaster(mutated, creation.decal_data.owner());
    ensureOwnerMaster(mutated, creation.collision_layer.owner());
    ensureOwnerMaster(mutated, creation.vats_projectile.owner());
    return fo4::forms::create_projectile(mutated, creation);
}

Result<fo4::FormKey> RadiclePluginBackend::createExplosionLocked(fo4::Plugin& mutated,
                                                     const nlohmann::json& payload) const {
    bool valid = false;
    const auto editorId = optionalStringField(payload, "editorId", valid);
    if (!valid) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "EXPL editorId must be a string or null"
        });
    }
    const auto name = optionalStringField(payload, "name", valid);
    if (!valid) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "EXPL name must be a string or null"
        });
    }

    fo4::forms::ExplosionCreation creation;
    creation.editor_id = editorId.value_or(std::string{});
    creation.name = name;
    if (!lifecycleBoundsField(payload, creation.bounds) ||
        !lifecycleFloatField(payload, "force", creation.force, creation.force) ||
        !lifecycleFloatField(payload, "damage", creation.damage, creation.damage) ||
        !lifecycleFloatField(
            payload, "innerRadius", creation.inner_radius, creation.inner_radius) ||
        !lifecycleFloatField(
            payload, "outerRadius", creation.outer_radius, creation.outer_radius) ||
        !lifecycleFloatField(
            payload, "imageSpaceRadius", creation.image_space_radius,
            creation.image_space_radius) ||
        !lifecycleFloatField(
            payload, "verticalOffsetMult", creation.vertical_offset_mult,
            creation.vertical_offset_mult) ||
        !lifecycleUnsignedField(payload, "flags", creation.flags, creation.flags) ||
        !lifecycleUnsignedField(
            payload, "soundLevel", creation.sound_level, creation.sound_level) ||
        !lifecycleFloatField(
            payload, "placedObjectFadeDelay", creation.placed_object_fade_delay,
            creation.placed_object_fade_delay) ||
        !lifecycleUnsignedField(
            payload, "staggerMagnitude", creation.stagger_magnitude,
            creation.stagger_magnitude) ||
        !lifecycleVec3Field(
            payload, "spawnProjectileVector", creation.spawn_projectile_vector) ||
        !lifecycleFloatField(
            payload, "projectileSpread", creation.projectile_spread,
            creation.projectile_spread) ||
        !lifecycleUnsignedField(
            payload, "projectileCount", creation.projectile_count,
            creation.projectile_count)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "EXPL numeric fields must fit their measured DATA storage"
        });
    }
    if (const auto error = validateProvidedScalars(payload, {
            {"force", "EXPL\\Force"},
            {"damage", "EXPL\\Damage"},
            {"innerRadius", "EXPL\\InnerRadius"},
            {"outerRadius", "EXPL\\OuterRadius"},
            {"imageSpaceRadius", "EXPL\\ImageSpaceRadius"},
            {"verticalOffsetMult", "EXPL\\VerticalOffsetMult"},
            {"flags", "EXPL\\Flags"},
            {"soundLevel", "EXPL\\SoundLevel"},
            {"placedObjectFadeDelay", "EXPL\\PlacedObjectFadeDelay"},
            {"staggerMagnitude", "EXPL\\StaggerMagnitude"},
            {"projectileSpread", "EXPL\\ProjectileSpread"},
            {"projectileCount", "EXPL\\ProjectileCount"}})) {
        return std::unexpected(*error);
    }
    if (const auto error = validateProvidedVec3(
            payload,
            "spawnProjectileVector",
            "EXPL\\SpawnProjectileVectorX",
            "EXPL\\SpawnProjectileVectorY",
            "EXPL\\SpawnProjectileVectorZ")) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(payload, "light", creation.light)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(payload, "sound1", creation.sound1)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(payload, "sound2", creation.sound2)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "impactDataSet", creation.impact_data_set)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "impactPlacedObject", creation.impact_placed_object)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "spawnProjectile", creation.spawn_projectile)) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(graph_, creation.light, {"LIGH"}, "light")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(graph_, creation.sound1, {"SNDR"}, "sound1")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(graph_, creation.sound2, {"SNDR"}, "sound2")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.impact_data_set, {"IPDS"}, "impactDataSet")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.impact_placed_object, {}, "impactPlacedObject")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.spawn_projectile, {"PROJ"}, "spawnProjectile")) {
        return std::unexpected(*error);
    }

    ensureOwnerMaster(mutated, creation.light.owner());
    ensureOwnerMaster(mutated, creation.sound1.owner());
    ensureOwnerMaster(mutated, creation.sound2.owner());
    ensureOwnerMaster(mutated, creation.impact_data_set.owner());
    ensureOwnerMaster(mutated, creation.impact_placed_object.owner());
    ensureOwnerMaster(mutated, creation.spawn_projectile.owner());
    return fo4::forms::create_explosion(mutated, creation);
}

Result<fo4::FormKey> RadiclePluginBackend::createHazardLocked(fo4::Plugin& mutated,
                                                     const nlohmann::json& payload) const {
    bool valid = false;
    const auto editorId = optionalStringField(payload, "editorId", valid);
    if (!valid) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "HAZD editorId must be a string or null"
        });
    }
    const auto name = optionalStringField(payload, "name", valid);
    if (!valid) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "HAZD name must be a string or null"
        });
    }
    const auto model = optionalStringField(payload, "model", valid);
    if (!valid || !model.has_value() || model->empty()) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "HAZD creation requires a non-empty model path"
        });
    }

    fo4::forms::HazardCreation creation;
    creation.editor_id = editorId.value_or(std::string{});
    creation.name = name;
    creation.model = *model;
    if (!lifecycleBoundsField(payload, creation.bounds) ||
        !lifecycleUnsignedField(payload, "limit", creation.limit, creation.limit) ||
        !lifecycleFloatField(payload, "radius", creation.radius, creation.radius) ||
        !lifecycleFloatField(payload, "lifetime", creation.lifetime, creation.lifetime) ||
        !lifecycleFloatField(
            payload, "imageSpaceRadius", creation.image_space_radius,
            creation.image_space_radius) ||
        !lifecycleFloatField(
            payload, "targetInterval", creation.target_interval,
            creation.target_interval) ||
        !lifecycleUnsignedField(payload, "flags", creation.flags, creation.flags) ||
        !lifecycleFloatField(
            payload, "fullEffectRadius", creation.full_effect_radius,
            creation.full_effect_radius) ||
        !lifecycleFloatField(
            payload, "taperWeight", creation.taper_weight, creation.taper_weight) ||
        !lifecycleFloatField(
            payload, "taperCurve", creation.taper_curve, creation.taper_curve)) {
        return std::unexpected(Error{
            "InvalidRecordCreationPayload",
            "HAZD numeric fields must fit their measured DNAM storage"
        });
    }
    if (const auto error = validateProvidedScalars(payload, {
            {"limit", "HAZD\\Limit"},
            {"radius", "HAZD\\Radius"},
            {"lifetime", "HAZD\\Lifetime"},
            {"imageSpaceRadius", "HAZD\\ImageSpaceRadius"},
            {"targetInterval", "HAZD\\TargetInterval"},
            {"flags", "HAZD\\Flags"},
            {"fullEffectRadius", "HAZD\\FullEffectRadius"},
            {"taperWeight", "HAZD\\TaperWeight"},
            {"taperCurve", "HAZD\\TaperCurve"}})) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(payload, "spell", creation.spell)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(payload, "light", creation.light)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(
            payload, "impactDataSet", creation.impact_data_set)) {
        return std::unexpected(*error);
    }
    if (const auto error = parseFormKeyField(payload, "sound", creation.sound)) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.spell, {"SPEL", "ENCH"}, "spell")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(graph_, creation.light, {"LIGH"}, "light")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(
            graph_, creation.impact_data_set, {"IPDS"}, "impactDataSet")) {
        return std::unexpected(*error);
    }
    if (const auto error = checkLoadedSlot(graph_, creation.sound, {"SNDR"}, "sound")) {
        return std::unexpected(*error);
    }

    ensureOwnerMaster(mutated, creation.spell.owner());
    ensureOwnerMaster(mutated, creation.light.owner());
    ensureOwnerMaster(mutated, creation.impact_data_set.owner());
    ensureOwnerMaster(mutated, creation.sound.owner());
    return fo4::forms::create_hazard(mutated, creation);
}

}
