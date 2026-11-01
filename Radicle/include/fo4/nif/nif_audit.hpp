#pragma once

#include "fo4/nif/nif.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

enum class NifAuditSeverity { error, warning };

inline constexpr std::uint32_t k_nif_audit_no_block = 0xFFFFFFFFu;

struct NifAuditFinding {
  NifAuditSeverity severity{NifAuditSeverity::error};
  std::string_view rule;
  std::uint32_t block{k_nif_audit_no_block};
  std::string message;
};

inline constexpr std::string_view k_audit_root_count = "nif_root_count";
inline constexpr std::string_view k_audit_root_not_node = "root_not_node";
inline constexpr std::string_view k_audit_shape_outside_node = "shape_outside_node";
inline constexpr std::string_view k_audit_extra_ref = "extra_ref_out_of_range";
inline constexpr std::string_view k_audit_geometry_not_fo4 = "geometry_not_fo4";
inline constexpr std::string_view k_audit_shader_not_fo4 = "shader_not_fo4";
inline constexpr std::string_view k_audit_texture_slots = "texture_set_slot_count";
inline constexpr std::string_view k_audit_shader_name_space = "shader_name_whitespace";
inline constexpr std::string_view k_audit_texture_path_space = "texture_path_whitespace";
inline constexpr std::string_view k_audit_texture_path_absolute = "texture_path_absolute";
inline constexpr std::string_view k_audit_texture_path_parent = "texture_path_parent";
inline constexpr std::string_view k_audit_child_below_null = "child_reference_below_null";
inline constexpr std::string_view k_audit_child_duplicate = "child_duplicate";
inline constexpr std::string_view k_audit_child_cycle = "child_cycle";
inline constexpr std::string_view k_audit_child_multiple_parents = "child_multiple_parents";
inline constexpr std::string_view k_audit_shape_reference = "shape_reference_out_of_range";
inline constexpr std::string_view k_audit_skin_data = "skin_data_reference";
inline constexpr std::string_view k_audit_skin_bone = "skin_bone_reference";
inline constexpr std::string_view k_audit_skin_reference = "shape_skin_reference_type";
inline constexpr std::string_view k_audit_multi_bound = "multi_bound_reference";
inline constexpr std::string_view k_audit_skin_bone_count = "skin_bone_count";

std::vector<NifAuditFinding> audit_nif(const NifFile& nif);

}  // namespace fo4::nif
