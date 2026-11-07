#include "fo4/schema/ctda.hpp"

#include "fo4/plugin/container.hpp"
#include "fo4/schema/condition_functions.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace fo4 {
namespace {

struct CtdaFunction {
  std::uint16_t index;
  std::uint8_t flags;
};

constexpr CtdaFunction k_ctda_functions[] = {
#include "fo4/schema/ctda_functions.inc"
};

constexpr std::uint8_t k_param1_formid = 1;
constexpr std::uint8_t k_param2_formid = 2;
constexpr std::size_t k_ctda_type = 0;
constexpr std::size_t k_ctda_compare = 4;
constexpr std::size_t k_ctda_function = 8;
constexpr std::size_t k_ctda_param1 = 12;
constexpr std::size_t k_ctda_param2 = 16;
constexpr std::size_t k_ctda_run_on = 20;
constexpr std::size_t k_ctda_reference = 24;
constexpr std::uint32_t k_run_on_quest_alias = 5;
constexpr std::uint16_t k_get_is_current_package = 161;
constexpr std::uint8_t k_param1_reference = 1;
constexpr std::uint8_t k_param2_reference = 2;

std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, 2);
  return value;
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

const CtdaFunction* find_ctda_function(std::uint16_t index) {
  const auto begin = std::begin(k_ctda_functions);
  const auto end = std::end(k_ctda_functions);
  const auto it = std::lower_bound(begin, end, index,
                                   [](const CtdaFunction& row, std::uint16_t value) {
                                     return row.index < value;
                                   });
  if (it == end || it->index != index) {
    return nullptr;
  }
  return &*it;
}

struct CtdaReferenceParams {
  std::uint16_t index;
  std::uint8_t mask;
};

constexpr CtdaReferenceParams k_ctda_reference_params[] = {
  {1, 1},  // GetDistance
  {27, 1},  // GetLineOfSight
  {32, 1},  // GetInSameCell
  {42, 1},  // SameFaction
  {43, 1},  // SameRace
  {44, 1},  // SameSex
  {45, 1},  // GetDetected
  {60, 2},  // GetFactionRankDifference
  {66, 1},  // GetShouldAttack
  {79, 1},  // WouldBeStealing
  {99, 1},  // GetHeadingAngle
  {122, 1},  // GetCrime
  {132, 1},  // WouldRefuseCommand
  {136, 1},  // GetIsReference
  {161, 1},  // GetIsCurrentPackage
  {162, 1},  // IsCurrentFurnitureRef
  {172, 1},  // GetTalkedToPCParam
  {180, 1},  // HasSameEditorLocationAsRef
  {230, 2},  // GetInCellParam
  {258, 1},  // HasAssociationType
  {259, 1},  // HasFamilyRelationship
  {261, 1},  // HasParentRelationship
  {370, 1},  // IsTalkingActivatorActor
  {378, 1},  // IsOwnedBy
  {403, 1},  // GetRelationshipRank
  {408, 1},  // IsKiller
  {414, 1},  // Exists
  {438, 1},  // HasKey
  {449, 1},  // GetFactionRelation
  {463, 1},  // IsPlayerGrabbedRef
  {477, 1},  // GetThreatRatio
  {512, 1},  // HasSharedPowerGrid
  {513, 1},  // IsCombatTarget
  {515, 1},  // GetVATSRightAreaFree
  {516, 1},  // GetVATSLeftAreaFree
  {517, 1},  // GetVATSBackAreaFree
  {518, 1},  // GetVATSFrontAreaFree
  {522, 1},  // GetVATSRightTargetVisible
  {523, 1},  // GetVATSLeftTargetVisible
  {524, 1},  // GetVATSBackTargetVisible
  {525, 1},  // GetVATSFrontTargetVisible
  {577, 3},  // IsCloserToAThanB
  {584, 1},  // GetRelativeAngle
  {603, 1},  // IsInSameCurrentLocationAsRef
  {624, 1},  // GetInContainer
  {639, 1},  // GetWithinDistance
  {650, 1},  // IsLinkedTo
  {652, 1},  // GetInSharedCrimeFaction
  {678, 1},  // ShouldAttackKill
  {705, 1},  // GetShouldHelp
  {719, 1},  // IsHostileToActor
  {720, 1},  // GetTargetHeight
  {753, 1},  // IsActorRefOwner
  {754, 1},  // HasActorRefOwner
  {772, 1},  // GetVATSRightMinusLeftAreaFree
  {773, 1},  // GetInIronSights
  {802, 1},  // GetWithinActivateDistance
  {804, 1},  // IsInSameSpace
  {806, 1},  // GetVATSBackRightAreaFree
  {807, 1},  // GetVATSBackLeftAreaFree
  {808, 1},  // GetVATSBackRightTargetVisible
  {809, 1},  // GetVATSBackLeftTargetVisible
  {810, 1},  // GetVATSTargetLimbVisible
};

std::uint8_t reference_param_mask(std::uint16_t index) {
  const auto begin = std::begin(k_ctda_reference_params);
  const auto end = std::end(k_ctda_reference_params);
  const auto it = std::lower_bound(begin, end, index,
                                   [](const CtdaReferenceParams& row, std::uint16_t value) {
                                     return row.index < value;
                                   });
  if (it == end || it->index != index) {
    return 0;
  }
  return it->mask;
}

}  // namespace

std::vector<std::size_t> ctda_formid_offsets(const Subrecord& sub) {
  std::vector<std::size_t> offsets;
  if (sub.signature != sig_ctda || sub.data.size() < 16) {
    return offsets;
  }
  const auto type = std::to_integer<std::uint8_t>(sub.data[k_ctda_type]);
  if ((type & k_ctda_flag_use_global) != 0) {
    offsets.push_back(k_ctda_compare);
  }
  const auto function = read_u16(sub.data, k_ctda_function);
  const auto* desc = find_ctda_function(function);
  if (desc != nullptr) {
    const auto mask = reference_param_mask(function);
    const bool aliases = (type & k_ctda_flag_aliases) != 0;
    const bool pack_data = (type & k_ctda_flag_pack_data) != 0;
    const bool package_on_quest_alias =
        function == k_get_is_current_package && sub.data.size() >= 24 &&
        read_u32(sub.data, k_ctda_run_on) == k_run_on_quest_alias;
    const bool param1_switched = (mask & k_param1_reference) != 0 &&
                                 (aliases ? !package_on_quest_alias : pack_data);
    const bool param2_switched = (mask & k_param2_reference) != 0 && (aliases || pack_data);
    if ((desc->flags & k_param1_formid) != 0 && !param1_switched) {
      offsets.push_back(k_ctda_param1);
    }
    if ((desc->flags & k_param2_formid) != 0 && sub.data.size() >= 20 && !param2_switched) {
      offsets.push_back(k_ctda_param2);
    }
  }
  if (sub.data.size() >= 28) {
    const auto* run_on = schema::condition_run_on(read_u32(sub.data, k_ctda_run_on));
    if (run_on != nullptr && run_on->reference_form_id) {
      offsets.push_back(k_ctda_reference);
    }
  }
  return offsets;
}

}  // namespace fo4
