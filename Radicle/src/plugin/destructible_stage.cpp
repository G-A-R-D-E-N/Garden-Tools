#include "fo4/plugin/destructible_stage.hpp"

#include "record_edit.hpp"

#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace fo4 {
namespace {

constexpr std::size_t k_max_stages = 0xFF;

constexpr Signature sig_dest = Signature::from_string("DEST");
constexpr Signature sig_damc = Signature::from_string("DAMC");
constexpr Signature sig_dstd = Signature::from_string("DSTD");
constexpr Signature sig_dstf = Signature::from_string("DSTF");

bool stage_member(Signature signature) {
  constexpr Signature members[] = {
      sig_dstd, Signature::from_string("DSTA"), Signature::from_string("DMDL"),
      Signature::from_string("DMDT"), Signature::from_string("DMDC"),
      Signature::from_string("DMDS"), sig_dstf,
  };
  return std::find(std::begin(members), std::end(members), signature) != std::end(members);
}

bool destructible_carrier(Signature record) {
  const std::string path = record.to_string() + "\\DestructionStageHealthPercent";
  return schema::scalar_field_descriptor(record, path).has_value();
}

std::optional<std::size_t> header_index(const Record& record) {
  std::optional<std::size_t> found;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto& subrecord = record.subrecords[i];
    if (subrecord.signature != sig_dest) continue;
    if (subrecord.data.size() != 8 || found.has_value()) return std::nullopt;
    found = i;
  }
  return found;
}

std::size_t stage_start(const Record& record) {
  std::size_t found = record.subrecords.size();
  for (std::size_t i = record.subrecords.size(); i > 0; --i) {
    const auto signature = record.subrecords[i - 1].signature;
    if (signature != sig_dest && signature != sig_damc) continue;
    found = i;
    break;
  }
  return found;
}

struct StageSpan {
  std::size_t first{0};
  std::size_t end{0};
};

std::vector<StageSpan> stage_spans(const Record& record) {
  std::vector<std::size_t> starts;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig_dstd) starts.push_back(i);
  }
  std::vector<StageSpan> spans;
  for (std::size_t k = 0; k < starts.size(); ++k) {
    std::size_t end = k + 1 < starts.size() ? starts[k + 1] : starts[k] + 1;
    if (k + 1 == starts.size()) {
      while (end < record.subrecords.size() && stage_member(record.subrecords[end].signature)) ++end;
    }
    spans.push_back(StageSpan{starts[k], end});
  }
  return spans;
}

}  // namespace

DestructibleStageMutationStatus insert_destructible_stage(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         std::size_t index) {
  auto* target = detail::locate_mutable_record(plugin, record_key);
  if (target == nullptr) return DestructibleStageMutationStatus::record_missing;
  if (!destructible_carrier(target->header.signature)) {
    return DestructibleStageMutationStatus::record_mismatch;
  }
  const auto header = header_index(*target);
  if (!header.has_value()) return DestructibleStageMutationStatus::header_missing;
  const auto spans = stage_spans(*target);
  if (index > spans.size()) return DestructibleStageMutationStatus::index_out_of_range;
  if (spans.size() >= k_max_stages) return DestructibleStageMutationStatus::too_many_stages;

  const std::size_t position =
      index < spans.size() ? spans[index].first
      : spans.empty()     ? stage_start(*target)
                          : spans.back().end;

  std::vector<Subrecord> created;
  Subrecord stage_data;
  stage_data.signature = sig_dstd;
  stage_data.data.assign(20, std::byte{0});
  created.push_back(std::move(stage_data));
  Subrecord end_marker;
  end_marker.signature = sig_dstf;
  created.push_back(std::move(end_marker));

  target->subrecords[*header].data[4] = static_cast<std::byte>(spans.size() + 1);
  target->subrecords.insert(target->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            std::make_move_iterator(created.begin()),
                            std::make_move_iterator(created.end()));
  detail::mark_record_modified(plugin, *target);
  return DestructibleStageMutationStatus::updated;
}

DestructibleStageMutationStatus remove_destructible_stage(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         std::size_t index) {
  auto* target = detail::locate_mutable_record(plugin, record_key);
  if (target == nullptr) return DestructibleStageMutationStatus::record_missing;
  if (!destructible_carrier(target->header.signature)) {
    return DestructibleStageMutationStatus::record_mismatch;
  }
  const auto header = header_index(*target);
  if (!header.has_value()) return DestructibleStageMutationStatus::header_missing;
  const auto spans = stage_spans(*target);
  if (index >= spans.size()) return DestructibleStageMutationStatus::index_out_of_range;

  const auto span = spans[index];
  target->subrecords[*header].data[4] = static_cast<std::byte>(spans.size() - 1);
  const auto first = target->subrecords.begin() + static_cast<std::ptrdiff_t>(span.first);
  target->subrecords.erase(first, first + static_cast<std::ptrdiff_t>(span.end - span.first));
  detail::mark_record_modified(plugin, *target);
  return DestructibleStageMutationStatus::updated;
}

}  // namespace fo4
