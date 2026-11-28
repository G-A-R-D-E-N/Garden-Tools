#include <fo4/actor_state/actor_state.hpp>

#include <fo4/binary/checked.hpp>
#include <fo4/binary/cursor.hpp>
#include <fo4/error.hpp>

#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>

namespace fo4::actor_state {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'R', 'A', 'S', 'V'};
constexpr std::uint8_t kMaxRefKind = static_cast<std::uint8_t>(RefKind::CreatedReference);

void check_form(const FormKey& key) {
  if (key.plugin.size() > kMaxStringBytes) throw std::invalid_argument("plugin name is above the string cap");
  if (key.plugin.empty() && key.local_id != 0) throw std::invalid_argument("an empty form key must have local id 0");
}

void check_count(std::size_t count) {
  if (count > kMaxListEntries) throw std::invalid_argument("list is above the entry cap");
}

class Writer {
public:
  void u8(std::uint8_t value) { bytes_.push_back(value); }

  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value & 0xFFu));
    u8(static_cast<std::uint8_t>(value >> 8));
  }

  void u32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) u8(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
  }

  void u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    u32(static_cast<std::uint32_t>(value >> 32));
  }

  void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }

  void f32(float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u32(bits);
  }

  void text(const std::string& value) {
    if (value.size() > kMaxStringBytes) throw std::invalid_argument("string is above the string cap");
    u32(static_cast<std::uint32_t>(value.size()));
    for (const char ch : value) u8(static_cast<std::uint8_t>(ch));
  }

  void form(const FormKey& key) {
    check_form(key);
    text(key.plugin);
    u32(key.local_id);
  }

  void ref(const RefIdentity& identity) {
    if (static_cast<std::uint8_t>(identity.kind) > kMaxRefKind) throw std::invalid_argument("unknown reference kind");
    u8(static_cast<std::uint8_t>(identity.kind));
    form(identity.form);
    u32(identity.created_id);
    form(identity.base);
    u64(identity.spawn_seed);
  }

  void count(std::size_t value) {
    check_count(value);
    u32(static_cast<std::uint32_t>(value));
  }

  void raw(const std::vector<std::uint8_t>& value) { bytes_.insert(bytes_.end(), value.begin(), value.end()); }

  std::vector<std::uint8_t>& bytes() { return bytes_; }

private:
  std::vector<std::uint8_t> bytes_;
};

std::uint32_t read_le32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return fo4::binary::checked::read_u32(std::as_bytes(bytes), offset);
}

class Reader {
public:
  explicit Reader(std::span<const std::uint8_t> payload)
      : cursor_(std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size()),
                kHeaderBytes) {}

  std::uint8_t u8() { return cursor_.read_u8(); }
  std::uint16_t u16() { return cursor_.read_u16(); }
  std::uint32_t u32() { return cursor_.read_u32(); }

  std::uint64_t u64() {
    const std::uint64_t low = u32();
    const std::uint64_t high = u32();
    return low | (high << 32);
  }

  std::int32_t i32() { return cursor_.read_i32(); }
  float f32() { return cursor_.read_f32(); }

  std::string text() {
    const std::size_t offset = cursor_.absolute_offset();
    const std::uint32_t length = u32();
    if (length > kMaxStringBytes) throw fo4::parse_error(offset, "string is above the string cap");
    const auto bytes = cursor_.read_bytes(length);
    std::string out;
    out.reserve(bytes.size());
    for (const std::byte value : bytes) out.push_back(static_cast<char>(std::to_integer<std::uint8_t>(value)));
    return out;
  }

  FormKey form() {
    const std::size_t offset = cursor_.absolute_offset();
    FormKey key;
    key.plugin = text();
    key.local_id = u32();
    if (key.plugin.empty() && key.local_id != 0) {
      throw fo4::parse_error(offset, "an empty form key must have local id 0");
    }
    return key;
  }

  RefIdentity ref() {
    const std::size_t offset = cursor_.absolute_offset();
    RefIdentity identity;
    const std::uint8_t kind = u8();
    if (kind > kMaxRefKind) throw fo4::parse_error(offset, "unknown reference kind");
    identity.kind = static_cast<RefKind>(kind);
    identity.form = form();
    identity.created_id = u32();
    identity.base = form();
    identity.spawn_seed = u64();
    return identity;
  }

  std::size_t count() {
    const std::size_t offset = cursor_.absolute_offset();
    const std::uint32_t value = u32();
    if (value > kMaxListEntries) throw fo4::parse_error(offset, "list is above the entry cap");
    return value;
  }

  std::vector<std::uint8_t> rest() {
    const auto bytes = cursor_.read_bytes(cursor_.remaining());
    std::vector<std::uint8_t> out;
    out.reserve(bytes.size());
    for (const std::byte value : bytes) out.push_back(std::to_integer<std::uint8_t>(value));
    return out;
  }

  std::size_t remaining() const noexcept { return cursor_.remaining(); }

private:
  fo4::binary::Cursor cursor_;
};

}  // namespace

std::vector<std::uint8_t> encode(const ActorStateRecord& record) {
  Writer payload;
  payload.ref(record.ref);
  payload.u32(record.flags);
  payload.form(record.cell);
  for (const float value : record.transform.position) payload.f32(value);
  for (const float value : record.transform.rotation) payload.f32(value);
  payload.f32(record.transform.scale);

  payload.count(record.inventory.size());
  for (const InventoryDelta& delta : record.inventory) {
    payload.form(delta.item);
    payload.i32(delta.count);
  }
  payload.count(record.actor_values.size());
  for (const ActorValueModifier& modifier : record.actor_values) {
    payload.text(modifier.actor_value);
    payload.f32(modifier.value);
  }
  payload.count(record.factions.size());
  for (const FactionRank& faction : record.factions) {
    payload.form(faction.faction);
    payload.i32(faction.rank);
  }
  payload.form(record.forced_package);
  payload.form(record.current_package);
  payload.u8(record.combat_target.has_value() ? 1 : 0);
  if (record.combat_target.has_value()) payload.ref(*record.combat_target);
  payload.i32(record.bounty);
  payload.u16(record.spawn_level);
  payload.raw(record.unknown_tail);

  if (payload.bytes().size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("payload is too large for the header");
  }
  Writer out;
  for (const std::uint8_t byte : kMagic) out.u8(byte);
  out.u32(kFormatVersion);
  out.u32(static_cast<std::uint32_t>(payload.bytes().size()));
  out.raw(payload.bytes());
  return std::move(out.bytes());
}

ActorStateRecord decode(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kHeaderBytes) throw fo4::parse_error(0, "truncated header");
  for (std::size_t i = 0; i < kMagic.size(); ++i) {
    if (bytes[i] != kMagic[i]) throw fo4::parse_error(i, "bad magic");
  }
  if (read_le32(bytes, 4) != kFormatVersion) throw fo4::parse_error(4, "unsupported format version");
  const std::uint32_t payload_size = read_le32(bytes, 8);
  if (payload_size != bytes.size() - kHeaderBytes) {
    throw fo4::parse_error(8, "payload size does not match the blob");
  }

  Reader in(bytes.subspan(kHeaderBytes));
  ActorStateRecord record;
  record.ref = in.ref();
  record.flags = in.u32();
  record.cell = in.form();
  for (float& value : record.transform.position) value = in.f32();
  for (float& value : record.transform.rotation) value = in.f32();
  record.transform.scale = in.f32();

  const std::size_t inventory_count = in.count();
  record.inventory.reserve(inventory_count);
  for (std::size_t i = 0; i < inventory_count; ++i) {
    InventoryDelta delta;
    delta.item = in.form();
    delta.count = in.i32();
    record.inventory.push_back(std::move(delta));
  }
  const std::size_t modifier_count = in.count();
  record.actor_values.reserve(modifier_count);
  for (std::size_t i = 0; i < modifier_count; ++i) {
    ActorValueModifier modifier;
    modifier.actor_value = in.text();
    modifier.value = in.f32();
    record.actor_values.push_back(std::move(modifier));
  }
  const std::size_t faction_count = in.count();
  record.factions.reserve(faction_count);
  for (std::size_t i = 0; i < faction_count; ++i) {
    FactionRank faction;
    faction.faction = in.form();
    faction.rank = in.i32();
    record.factions.push_back(std::move(faction));
  }
  record.forced_package = in.form();
  record.current_package = in.form();
  const std::size_t target_offset = bytes.size() - in.remaining();
  const std::uint8_t has_target = in.u8();
  if (has_target > 1) throw fo4::parse_error(target_offset, "combat target presence must be 0 or 1");
  if (has_target == 1) record.combat_target = in.ref();
  record.bounty = in.i32();
  record.spawn_level = in.u16();
  record.unknown_tail = in.rest();
  return record;
}

}  // namespace fo4::actor_state
