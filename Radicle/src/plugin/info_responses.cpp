#include "fo4/plugin/info_responses.hpp"
#include "fo4/binary/raw.hpp"

#include <span>

namespace fo4 {
namespace {

using fo4::binary::raw::read_i32;
using fo4::binary::raw::read_u16;
using fo4::binary::raw::read_u32;

bool ends_response(Signature signature) {
  return signature == sig_trda || signature == sig_ctda || signature == sig_rnam;
}

}  // namespace

std::vector<InfoResponse> read_info_responses(const Record& record) {
  const auto& subs = record.subrecords;
  std::vector<InfoResponse> responses;
  for (std::size_t at = 0; at < subs.size(); ++at) {
    if (subs[at].signature != sig_trda) continue;
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
      response.emotion = read_u32(data, 0);
      response.sound = read_u32(data, 5);
      response.unknown = std::to_integer<std::uint8_t>(data[9]);
      response.interrupt_percent = read_u16(data, 10);
      response.camera_target_alias = read_i32(data, 12);
      response.camera_location_alias = read_i32(data, 16);
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
