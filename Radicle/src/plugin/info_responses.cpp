#include "fo4/plugin/info_responses.hpp"

#include <cstring>
#include <span>

namespace fo4 {
namespace {

constexpr auto k_trda = Signature::from_string("TRDA");
constexpr auto k_ctda = Signature::from_string("CTDA");
constexpr auto k_rnam = Signature::from_string("RNAM");

std::uint16_t u16_at(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::uint32_t u32_at(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::int32_t i32_at(std::span<const std::byte> data, std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

bool ends_response(Signature signature) {
  return signature == k_trda || signature == k_ctda || signature == k_rnam;
}

}  // namespace

std::vector<InfoResponse> read_info_responses(const Record& record) {
  const auto& subs = record.subrecords;
  std::vector<InfoResponse> responses;
  for (std::size_t at = 0; at < subs.size(); ++at) {
    if (subs[at].signature != k_trda) continue;
    InfoResponse response;
    response.trda = at;
    const auto data = std::span<const std::byte>{subs[at].data};
    response.malformed = data.size() != k_info_response_size;
    // The number sits at offset 4, so a short header still reports it.
    if (data.size() >= 5) response.number = std::to_integer<std::uint8_t>(data[4]);
    std::size_t end = at + 1;
    while (end < subs.size() && !ends_response(subs[end].signature)) ++end;
    response.first = at + 1;
    response.last = end;
    if (data.size() == k_info_response_size) {
      response.emotion = u32_at(data, 0);
      response.sound = u32_at(data, 5);
      response.unknown = std::to_integer<std::uint8_t>(data[9]);
      response.interrupt_percent = u16_at(data, 10);
      response.camera_target_alias = i32_at(data, 12);
      response.camera_location_alias = i32_at(data, 16);
    }
    responses.push_back(response);
    // Constraint: resume at the terminator so a body is never walked twice.
    at = end - 1;
  }
  return responses;
}

const Subrecord* info_response_subrecord(const Record& record, const InfoResponse& response,
                                         Signature signature) {
  const auto& subs = record.subrecords;
  for (std::size_t at = response.first; at < response.last && at < subs.size(); ++at) {
    if (subs[at].signature == signature) return &subs[at];
  }
  return nullptr;
}

}  // namespace fo4
