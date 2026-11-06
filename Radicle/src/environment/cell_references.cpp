#include "fo4/environment/cell_view.hpp"

#include "fo4/environment/placed.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/forms/static_collection.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::env {
namespace {

constexpr auto k_dodt = Signature::from_string("DODT");
constexpr auto k_scol = Signature::from_string("SCOL");
constexpr auto k_tx00 = Signature::from_string("TX00");
constexpr auto k_txst = Signature::from_string("TXST");

struct BaseTypeName {
  Signature signature;
  std::string_view name;
};

constexpr std::array<BaseTypeName, 10> k_base_type_names{{
    {Signature::from_string("STAT"), "Static"},
    {Signature::from_string("SCOL"), "StaticCollection"},
    {Signature::from_string("ACTI"), "Activator"},
    {Signature::from_string("CONT"), "Container"},
    {Signature::from_string("FURN"), "Furniture"},
    {Signature::from_string("LIGH"), "Light"},
    {Signature::from_string("NPC_"), "Npc"},
    {Signature::from_string("DOOR"), "Door"},
    {Signature::from_string("MSTT"), "MovableStatic"},
    {Signature::from_string("TXST"), "TextureSet"},
}};

std::string_view record_type_of(forms::PlacedReferenceKind kind) {
  if (kind == forms::PlacedReferenceKind::object) return "PlacedObject";
  if (kind == forms::PlacedReferenceKind::npc) return "PlacedNpc";
  return "PlacedTrap";
}

bool is_editor_marker_mesh(std::string model_path) {
  static const std::regex marker{R"((^|/)[a-z_]*marker[a-z0-9_]*\.nif$)"};
  std::ranges::replace(model_path, '\\', '/');
  std::ranges::transform(model_path, model_path.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return model_path.starts_with("markers/") || std::regex_search(model_path, marker);
}

float read_f32(const std::vector<std::byte>& data, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

const Subrecord* find_subrecord(const Record& record, Signature signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

std::optional<DecalInfo> texture_set_decal(const Record& record) {
  const auto* diffuse_subrecord = find_subrecord(record, k_tx00);
  if (diffuse_subrecord == nullptr) return std::nullopt;
  const auto diffuse = zstring_view(*diffuse_subrecord);
  if (!diffuse || diffuse->empty()) return std::nullopt;

  DecalInfo decal;
  decal.diffuse = std::string{*diffuse};
  if (const auto bounds = decode_object_bounds(record)) {
    const float width =
        std::fabs(static_cast<float>(bounds->max[0]) - static_cast<float>(bounds->min[0]));
    const float height =
        std::fabs(static_cast<float>(bounds->max[1]) - static_cast<float>(bounds->min[1]));
    if (width > 0.01f && height > 0.01f) {
      decal.width = width;
      decal.height = height;
      return decal;
    }
  }
  const auto* dodt = find_subrecord(record, k_dodt);
  if (dodt != nullptr && dodt->data.size() >= 12) {
    decal.width = read_f32(dodt->data, 0);
    decal.height = read_f32(dodt->data, 8);
  }
  return decal;
}

std::vector<ScolPart> scol_parts_of(const Environment& env, const RecordRef& ref) {
  std::vector<ScolPart> parts;
  for (auto& part : forms::decode_static_collection_parts(env.plugin(ref.plugin), *ref.record)) {
    const auto* static_ref = env.winner(part.static_record);
    if (static_ref == nullptr || part.placements.empty()) continue;
    const auto model = primary_model_path(*static_ref);
    if (!model || model->empty()) continue;
    parts.push_back(ScolPart{std::string{*model}, std::move(part.placements)});
  }
  return parts;
}

}  // namespace

std::string base_type_name(Signature signature) {
  for (const auto& entry : k_base_type_names) {
    if (entry.signature == signature) return std::string{entry.name};
  }
  return signature.to_string();
}

std::optional<BaseObjectInfo> describe_base_object(const Environment& env, const FormKey& key) {
  const auto* ref = env.winner(key);
  if (ref == nullptr) return std::nullopt;
  const auto& record = *ref->record;
  const auto meta = env.metadata(*ref);

  BaseObjectInfo info;
  info.key = record.form_key;
  info.signature = record.header.signature;
  info.editor_id = std::string{meta.editor_id.value_or(std::string_view{})};
  info.type_name = base_type_name(info.signature);
  if (const auto model = primary_model_path(*ref)) info.model_path = std::string{*model};
  info.editor_marker = !info.model_path.empty() && is_editor_marker_mesh(info.model_path);
  if (info.signature == k_txst) info.decal = texture_set_decal(record);
  if (info.signature == k_scol) info.scol_parts = scol_parts_of(env, *ref);
  return info;
}

CellReferences list_cell_references(const Environment& env, const FormKey& cell) {
  auto summary = describe_cell(env, cell);
  if (!summary) throw CellViewError("unknown cell " + cell.to_string());

  CellReferences result;
  std::map<FormKey, std::optional<BaseObjectInfo>> bases;
  for (const auto& child : env.cell_children(summary->key)) {
    const auto& ref = *child.ref;
    const auto placed =
        forms::decode_placed_reference(env.plugin(ref.plugin), *ref.record, child.persistent);
    if (!placed) continue;

    CellReferenceListing listing;
    listing.key = ref.record->form_key;
    listing.kind = placed->kind;
    listing.record_type = std::string{record_type_of(placed->kind)};
    listing.editor_id = placed->editor_id;
    listing.base = placed->base;
    if (placed->base) {
      auto cached = bases.find(*placed->base);
      if (cached == bases.end()) {
        cached = bases.emplace(*placed->base, describe_base_object(env, *placed->base)).first;
      }
      listing.base_info = cached->second;
    }
    listing.transform = placed->transform;
    listing.scale = placed->scale;
    listing.persistent = placed->persistent;
    listing.initially_disabled = ref.record->header.flags.test(RecordFlags::initially_disabled);
    listing.winning_plugin = ref.plugin;
    if (listing.base_info && !listing.base_info->model_path.empty()) ++result.with_model_count;
    result.references.push_back(std::move(listing));
  }
  std::sort(result.references.begin(), result.references.end(),
            [](const CellReferenceListing& a, const CellReferenceListing& b) {
              return a.key < b.key;
            });
  result.cell = std::move(*summary);
  return result;
}

}  // namespace fo4::env
