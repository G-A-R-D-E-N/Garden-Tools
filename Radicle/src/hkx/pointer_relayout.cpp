#include "fo4/hkx/pointer_relayout.hpp"

#include "fo4/hkx/decode.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/relayout_widths.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace fo4::hkx {
namespace {

using Bytes = std::vector<std::byte>;
using K = HkxMemberKind;

constexpr std::size_t kHeaderSize = 0x40;
constexpr std::size_t kSectionHeaderSize = 0x40;
constexpr std::size_t kTagSize = 20;
constexpr std::size_t kTailSize = 16;
constexpr std::size_t kMaxSections = 64;
constexpr std::uint32_t kMagicLow = 0x57E0E057u;
constexpr std::uint32_t kMagicHigh = 0x10C0C010u;
constexpr std::uint32_t kFileVersion = 11;
constexpr std::uint32_t kMaxOffset = 0x7FFFFFFFu;
constexpr int kMaxDepth = 12;

struct Refused {
  std::string reason;
};

[[noreturn]] void refuse(std::string reason) { throw Refused{std::move(reason)}; }

std::uint32_t get16(std::span<const std::byte> b, std::size_t at) {
  return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8);
}

std::uint32_t get32(std::span<const std::byte> b, std::size_t at) {
  return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
         (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

void put32(Bytes& b, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) b[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
}

void append32(Bytes& b, std::uint32_t value) {
  b.resize(b.size() + 4);
  put32(b, b.size() - 4, value);
}

std::size_t round_up(std::size_t value, std::size_t to) {
  return to <= 1 ? value : (value + to - 1) / to * to;
}

void pad16(Bytes& b) { b.resize(round_up(b.size(), 16), std::byte{0xFF}); }

struct Section {
  std::array<std::byte, kTagSize> tag_bytes{};
  std::array<std::byte, kTailSize> tail{};
  std::string tag;
  Bytes data;
  Bytes local;
  Bytes global;
  Bytes virt;
  Bytes exports;
  Bytes imports;
};

struct Image {
  std::array<std::byte, kHeaderSize> header{};
  Bytes predicates;
  std::array<std::byte, 4> rules{};
  std::uint32_t contents_offset{0};
  std::uint32_t contents_section{0};
  std::vector<Section> sections;
};

void check_header(std::span<const std::byte> in) {
  if (in.size() < kHeaderSize) refuse("the file is shorter than a packfile header");
  if (get32(in, 0) != kMagicLow || get32(in, 4) != kMagicHigh) refuse("the packfile magic is wrong");
  if (get32(in, 0xC) != kFileVersion) refuse("the packfile version is not 11");
  const auto width = static_cast<unsigned>(in[0x10]);
  if ((width != 4 && width != 8) || in[0x11] != std::byte{1} || in[0x12] != std::byte{0} || in[0x13] != std::byte{1}) {
    refuse("the layout rules are not a little-endian 4 or 8 byte pointer with canonical packing");
  }
}

Image read_image(std::span<const std::byte> in) {
  check_header(in);
  Image image;
  std::copy_n(in.begin(), kHeaderSize, image.header.begin());
  std::copy_n(in.begin() + 0x10, 4, image.rules.begin());
  image.contents_section = get32(in, 0x18);
  image.contents_offset = get32(in, 0x1C);
  const std::size_t count = get32(in, 0x14);
  const std::size_t table_at = kHeaderSize + get16(in, 0x3E);
  if (count == 0 || count > kMaxSections) refuse("the section count is not between 1 and 64");
  if (table_at > in.size() || (in.size() - table_at) / kSectionHeaderSize < count) {
    refuse("the section table runs past the end of the file");
  }
  image.predicates.assign(in.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                          in.begin() + static_cast<std::ptrdiff_t>(table_at));
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t at = table_at + i * kSectionHeaderSize;
    Section section;
    std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(at), kTagSize, section.tag_bytes.begin());
    std::copy_n(in.begin() + static_cast<std::ptrdiff_t>(at + 0x30), kTailSize, section.tail.begin());
    for (const std::byte c : section.tag_bytes) {
      if (c == std::byte{0}) break;
      section.tag.push_back(static_cast<char>(c));
    }
    const std::size_t start = get32(in, at + 0x14);
    std::array<std::size_t, 6> marks{};
    for (std::size_t k = 0; k < marks.size(); ++k) {
      marks[k] = get32(in, at + 0x18 + 4 * k);
      if (k > 0 && marks[k] < marks[k - 1]) refuse("section " + section.tag + " has marks out of order");
    }
    if (start > in.size() || marks[5] > in.size() - start) refuse("section " + section.tag + " runs past the end of the file");
    const auto slice = [&](std::size_t from, std::size_t to) {
      return Bytes(in.begin() + static_cast<std::ptrdiff_t>(start + from), in.begin() + static_cast<std::ptrdiff_t>(start + to));
    };
    section.data = slice(0, marks[0]);
    section.local = slice(marks[0], marks[1]);
    section.global = slice(marks[1], marks[2]);
    section.virt = slice(marks[2], marks[3]);
    section.exports = slice(marks[3], marks[4]);
    section.imports = slice(marks[4], marks[5]);
    image.sections.push_back(std::move(section));
  }
  return image;
}

Bytes write_image(const Image& image) {
  Bytes out(image.header.begin(), image.header.end());
  std::copy(image.rules.begin(), image.rules.end(), out.begin() + 0x10);
  put32(out, 0x1C, image.contents_offset);
  out.insert(out.end(), image.predicates.begin(), image.predicates.end());
  const std::size_t table_at = out.size();
  out.resize(table_at + image.sections.size() * kSectionHeaderSize, std::byte{0});
  for (std::size_t i = 0; i < image.sections.size(); ++i) {
    const Section& section = image.sections[i];
    pad16(out);
    const std::size_t start = out.size();
    std::array<std::uint32_t, 6> marks{};
    const auto part = [&](const Bytes& bytes, std::size_t slot, bool pad) {
      out.insert(out.end(), bytes.begin(), bytes.end());
      if (pad) pad16(out);
      marks[slot] = static_cast<std::uint32_t>(out.size() - start);
    };
    part(section.data, 0, true);
    part(section.local, 1, true);
    part(section.global, 2, true);
    part(section.virt, 3, true);
    part(section.exports, 4, false);
    part(section.imports, 5, false);
    const std::size_t at = table_at + i * kSectionHeaderSize;
    std::copy(section.tag_bytes.begin(), section.tag_bytes.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
    put32(out, at + 0x14, static_cast<std::uint32_t>(start));
    for (std::size_t k = 0; k < marks.size(); ++k) put32(out, at + 0x18 + 4 * k, marks[k]);
    std::copy(section.tail.begin(), section.tail.end(), out.begin() + static_cast<std::ptrdiff_t>(at + 0x30));
  }
  return out;
}

template <std::size_t N>
std::vector<std::array<std::uint32_t, N>> rows_of(const Bytes& raw, const std::string& what) {
  constexpr std::size_t kEntry = N * 4;
  const std::size_t whole = raw.size() - raw.size() % kEntry;
  for (std::size_t i = whole; i < raw.size(); ++i) {
    if (raw[i] != std::byte{0xFF}) refuse(what + " ends in a partial entry");
  }
  std::vector<std::array<std::uint32_t, N>> rows;
  for (std::size_t at = 0; at < whole; at += kEntry) {
    std::array<std::uint32_t, N> row{};
    bool filler = true;
    for (std::size_t n = 0; n < N; ++n) {
      row[n] = get32(raw, at + 4 * n);
      if (row[n] != 0xFFFFFFFFu) filler = false;
    }
    if (filler) continue;
    for (const std::uint32_t value : row) {
      if (value > kMaxOffset) refuse(what + " holds a value out of range");
    }
    rows.push_back(row);
  }
  return rows;
}

template <std::size_t N>
Bytes bytes_of(const std::vector<std::array<std::uint32_t, N>>& rows) {
  Bytes out;
  for (const auto& row : rows) {
    for (const std::uint32_t value : row) append32(out, value);
  }
  return out;
}

struct NameRecord {
  std::uint32_t signature;
  std::string name;
};

std::map<std::uint32_t, NameRecord> parse_class_names(const Bytes& blob) {
  std::map<std::uint32_t, NameRecord> out;
  std::size_t at = 0;
  while (at + 5 < blob.size()) {
    std::size_t end = at + 5;
    while (end < blob.size() && blob[end] != std::byte{0}) ++end;
    if (end == blob.size()) break;
    if (blob[at + 4] != std::byte{9}) refuse("the class name table has a record without the 0x09 separator");
    NameRecord record;
    record.signature = get32(blob, at);
    for (std::size_t i = at + 5; i < end; ++i) record.name.push_back(static_cast<char>(blob[i]));
    out.emplace(static_cast<std::uint32_t>(at + 5), std::move(record));
    at = end + 1;
  }
  for (std::size_t i = at; i < blob.size(); ++i) {
    if (blob[i] != std::byte{0xFF}) refuse("the class name table has trailing bytes");
  }
  return out;
}

void check_signatures(const std::map<std::uint32_t, NameRecord>& names) {
  for (const auto& entry : names) {
    const NameRecord& record = entry.second;
    const HkxClassLayout* layout = find_layout(record.name);
    if (layout != nullptr && layout->signature != record.signature) {
      char text[64];
      std::snprintf(text, sizeof text, " has signature 0x%08x, the layout table has 0x%08x", record.signature, layout->signature);
      refuse("class " + record.name + text);
    }
  }
}

struct Shape {
  const HkxDecodeClass* decode{nullptr};
  const HkxRelayoutClass* widths{nullptr};
};

std::optional<Shape> shape_of(std::string_view name) {
  const HkxDecodeClass* decode = find_decode_class(name);
  if (decode == nullptr) return std::nullopt;
  for (const HkxRelayoutClass& widths : relayout_classes()) {
    if (widths.name != name) continue;
    if (widths.wide_size != decode->size || widths.offsets.size() != decode->members.size()) return std::nullopt;
    for (std::size_t i = 0; i < widths.offsets.size(); ++i) {
      if (widths.offsets[i].wide != decode->members[i].offset) return std::nullopt;
    }
    return Shape{decode, &widths};
  }
  return std::nullopt;
}

std::uint32_t class_size(const Shape& shape, std::uint32_t width) {
  return width == 8 ? shape.widths->wide_size : shape.widths->narrow_size;
}

std::uint32_t member_offset(const Shape& shape, std::size_t index, std::uint32_t width) {
  return width == 8 ? shape.widths->offsets[index].wide : shape.widths->offsets[index].narrow;
}

enum class Role : std::uint8_t {
  Value,
  Pointer,
  StringPointer,
  StructArray,
  PointerArray,
  StringArray,
  ValueArray,
  Unmodelled
};

bool is_value(K kind) {
  switch (kind) {
    case K::Bool:
    case K::Int8:
    case K::Int16:
    case K::Int32:
    case K::Int64:
    case K::Uint8:
    case K::Uint16:
    case K::Uint32:
    case K::Uint64:
    case K::Real:
    case K::Vector4:
    case K::Quaternion:
    case K::QsTransform:
    case K::Transform:
      return true;
    default:
      return false;
  }
}

Role role_of(const HkxMemberType& member) {
  switch (member.kind) {
    case K::StringPtr:
      return Role::StringPointer;
    case K::Pointer:
      return Role::Pointer;
    case K::Array:
      if (member.element == K::Pointer) return Role::PointerArray;
      if (member.element == K::StringPtr) return Role::StringArray;
      if (member.element == K::Struct) return Role::StructArray;
      return is_value(member.element) ? Role::ValueArray : Role::Unmodelled;
    default:
      return is_value(member.kind) ? Role::Value : Role::Unmodelled;
  }
}

enum class Kind : std::uint8_t { Object, StructArray, PointerArray, StringArray, ValueArray, String, ElementString };

bool is_array(Kind kind) {
  return kind == Kind::StructArray || kind == Kind::PointerArray || kind == Kind::StringArray || kind == Kind::ValueArray;
}

bool is_string(Kind kind) { return kind == Kind::String || kind == Kind::ElementString; }

struct Block {
  Kind kind{Kind::Object};
  std::uint32_t src{0};
  std::uint32_t dst{0};
  std::uint32_t count{0};
  std::uint32_t src_stride{0};
  std::uint32_t dst_stride{0};
  Shape shape;
  std::size_t source_length() const { return static_cast<std::size_t>(count) * src_stride; }
  std::size_t target_length() const { return static_cast<std::size_t>(count) * dst_stride; }
};

struct Instance {
  std::uint32_t src;
  Shape shape;
  std::string name;
};

class Relayout {
 public:
  Relayout(Image& image, std::uint32_t source_width, std::uint32_t target_width, HkxRelayoutResult& result)
      : image_(image), sw_(source_width), tw_(target_width), result_(result) {}

  void run();

 private:
  Image& image_;
  std::uint32_t sw_;
  std::uint32_t tw_;
  HkxRelayoutResult& result_;
  std::size_t data_index_{0};
  std::size_t names_index_{0};
  Bytes source_;
  Bytes target_;
  std::vector<Block> blocks_;
  std::map<std::uint32_t, std::size_t> seen_;
  std::unordered_map<std::uint32_t, std::uint32_t> aims_;
  std::unordered_map<std::uint32_t, std::uint32_t> sites_;
  std::unordered_map<std::uint32_t, std::uint32_t> base_;

  std::span<const std::byte> data() const { return source_; }
  std::string describe(const Shape& shape, const HkxMemberType& member) const {
    return std::string(shape.decode->name) + "." + std::string(member.name);
  }
  void check_sections();
  void discover(std::uint32_t at, const Shape& shape, int depth);
  void discover_member(std::uint32_t at, const Shape& shape, std::size_t index, int depth);
  bool add(Block block);
  void add_string(std::uint32_t at, Kind kind);
  std::optional<std::pair<std::uint32_t, std::uint32_t>> array_at(std::uint32_t at) const;
  void place();
  void transcode_object(std::uint32_t from, std::uint32_t to, const Shape& shape);
};

void Relayout::check_sections() {
  for (const Section& section : image_.sections) {
    if (!section.exports.empty() || !section.imports.empty()) refuse("the packfile has exports or imports");
  }
  const Section& names = image_.sections[names_index_];
  if (!rows_of<2>(names.local, "the class name local fixups").empty() || !rows_of<3>(names.global, "the class name global fixups").empty() ||
      !rows_of<3>(names.virt, "the class name virtual fixups").empty()) {
    refuse("the class name section has fixups");
  }
  for (std::size_t i = 0; i < image_.sections.size(); ++i) {
    if (i == data_index_ || i == names_index_) continue;
    const Section& section = image_.sections[i];
    if (!section.data.empty() || !rows_of<2>(section.local, "local fixups").empty() ||
        !rows_of<3>(section.global, "global fixups").empty() || !rows_of<3>(section.virt, "virtual fixups").empty()) {
      refuse("section " + section.tag + " holds data or fixups this port does not move");
    }
  }
  if (image_.contents_section != data_index_) refuse("the contents object is not in the __data__ section");
}

bool Relayout::add(Block block) {
  const auto found = seen_.find(block.src);
  if (found == seen_.end()) {
    seen_.emplace(block.src, blocks_.size());
    blocks_.push_back(std::move(block));
    return true;
  }
  const Block& old = blocks_[found->second];
  if (is_string(old.kind) && is_string(block.kind)) return false;
  if (old.kind != block.kind || old.count != block.count || old.src_stride != block.src_stride) {
    refuse("two blocks start at offset " + std::to_string(block.src) + " with different shapes");
  }
  return false;
}

void Relayout::add_string(std::uint32_t at, Kind kind) {
  if (at >= source_.size()) refuse("a string pointer leaves the data section");
  std::size_t end = at;
  while (end < source_.size() && source_[end] != std::byte{0}) ++end;
  if (end == source_.size()) refuse("a string is not terminated inside the data section");
  Block block;
  block.kind = kind;
  block.src = at;
  block.count = static_cast<std::uint32_t>(end - at + 1);
  block.src_stride = 1;
  block.dst_stride = 1;
  add(std::move(block));
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> Relayout::array_at(std::uint32_t at) const {
  if (static_cast<std::size_t>(at) + sw_ + 4 > source_.size()) return std::nullopt;
  const std::uint32_t count = get32(data(), at + sw_);
  if (count > kMaxOffset) return std::nullopt;
  const auto found = aims_.find(at);
  if (found == aims_.end()) {
    for (std::uint32_t i = 0; i < sw_; ++i) {
      if (source_[at + i] != std::byte{0}) return std::nullopt;
    }
    if (count != 0) return std::nullopt;
    return std::make_pair(std::uint32_t{0}, std::uint32_t{0});
  }
  const std::uint32_t to = found->second;
  if (to > source_.size() || count > source_.size() - to) return std::nullopt;
  return std::make_pair(to, count);
}

void Relayout::discover_member(std::uint32_t at, const Shape& shape, std::size_t index, int depth) {
  const HkxMemberType& member = shape.decode->members[index];
  const Role role = role_of(member);
  if (role == Role::Unmodelled) refuse("member " + describe(shape, member) + " has a kind this port does not model");
  if (role == Role::Value || role == Role::Pointer) return;
  if (role == Role::StringPointer) {
    const auto found = aims_.find(at);
    if (found != aims_.end()) add_string(found->second, Kind::String);
    return;
  }
  const auto array = array_at(at);
  if (!array) refuse("array " + describe(shape, member) + " has a size or pointer that does not fit the data section, or has elements but no fixup");
  const bool fixed = aims_.contains(at);
  if (array->second == 0) {
    if (fixed) refuse("array " + describe(shape, member) + " is empty but has a fixup");
    return;
  }
  Block block;
  block.src = array->first;
  block.count = array->second;
  if (role == Role::StructArray) {
    const auto element = shape_of(member.struct_class);
    if (!element) refuse("class " + std::string(member.struct_class) + " has no verified 4-byte layout");
    block.kind = Kind::StructArray;
    block.shape = *element;
    block.src_stride = class_size(*element, sw_);
    block.dst_stride = class_size(*element, tw_);
    if (block.source_length() > source_.size() - block.src) refuse("array " + describe(shape, member) + " leaves the data section");
    const Block copy = block;
    if (!add(block)) return;
    for (std::uint32_t e = 0; e < copy.count; ++e) discover(copy.src + e * copy.src_stride, *element, depth + 1);
    return;
  }
  if (role == Role::PointerArray || role == Role::StringArray) {
    block.kind = role == Role::PointerArray ? Kind::PointerArray : Kind::StringArray;
    block.src_stride = sw_;
    block.dst_stride = tw_;
  } else {
    block.kind = Kind::ValueArray;
    block.src_stride = kind_size(member.element);
    block.dst_stride = block.src_stride;
  }
  if (block.source_length() > source_.size() - block.src) refuse("array " + describe(shape, member) + " leaves the data section");
  const Block copy = block;
  if (!add(block)) return;
  if (role != Role::StringArray) return;
  for (std::uint32_t e = 0; e < copy.count; ++e) {
    const auto found = aims_.find(copy.src + e * copy.src_stride);
    if (found != aims_.end()) add_string(found->second, Kind::ElementString);
  }
}

void Relayout::discover(std::uint32_t at, const Shape& shape, int depth) {
  if (depth > kMaxDepth) refuse("struct arrays nest deeper than " + std::to_string(kMaxDepth));
  for (std::size_t i = 0; i < shape.decode->members.size(); ++i) {
    discover_member(at + member_offset(shape, i, sw_), shape, i, depth);
  }
}

void Relayout::place() {
  std::size_t cursor = 0;
  Kind previous = Kind::Object;
  bool first = true;
  for (Block& block : blocks_) {
    std::size_t align = 16;
    if (block.kind == Kind::ElementString) align = 2;
    else if (block.kind == Kind::String && !first && is_array(previous)) align = 1;
    cursor = round_up(cursor, align);
    block.dst = static_cast<std::uint32_t>(cursor);
    cursor += block.target_length();
    previous = block.kind;
    first = false;
  }
  target_.assign(round_up(cursor, 16), std::byte{0});
  for (const Block& block : blocks_) {
    base_[block.src] = block.dst;
    if (block.kind != Kind::StructArray) continue;
    for (std::uint32_t e = 0; e < block.count; ++e) base_[block.src + e * block.src_stride] = block.dst + e * block.dst_stride;
  }
}

void Relayout::transcode_object(std::uint32_t from, std::uint32_t to, const Shape& shape) {
  std::vector<bool> covered(class_size(shape, sw_), false);
  const auto cover = [&](std::uint32_t at, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) covered[at + i] = true;
  };
  for (std::size_t i = 0; i < shape.decode->members.size(); ++i) {
    const HkxMemberType& member = shape.decode->members[i];
    const std::uint32_t s = from + member_offset(shape, i, sw_);
    const std::uint32_t t = to + member_offset(shape, i, tw_);
    const std::uint32_t local = member_offset(shape, i, sw_);
    const Role role = role_of(member);
    if (role == Role::Value) {
      const std::uint32_t width = kind_size(member.kind);
      std::copy_n(source_.begin() + s, width, target_.begin() + t);
      cover(local, width);
      continue;
    }
    const bool array = role != Role::Pointer && role != Role::StringPointer;
    cover(local, sw_ + (array ? 8u : 0u));
    sites_[s] = t;
    if (!aims_.contains(s)) {
      for (std::uint32_t k = 0; k < sw_; ++k) {
        if (source_[s + k] != std::byte{0}) refuse("pointer " + describe(shape, member) + " is not zero but has no fixup");
      }
    }
    if (!array) continue;
    const std::uint32_t size = get32(data(), s + sw_);
    const std::uint32_t flags = get32(data(), s + sw_ + 4) & 0xC0000000u;
    put32(target_, t + tw_, size);
    put32(target_, t + tw_ + 4, flags | size);
  }
  for (std::size_t i = 0; i < covered.size(); ++i) {
    if (!covered[i] && source_[from + i] != std::byte{0}) {
      refuse("class " + std::string(shape.decode->name) + " has a non-zero byte at +" + std::to_string(i) +
             " outside the members a file stores");
    }
  }
}

void Relayout::run() {
  const auto find = [&](std::string_view tag) -> std::optional<std::size_t> {
    for (std::size_t i = 0; i < image_.sections.size(); ++i) {
      if (image_.sections[i].tag == tag) return i;
    }
    return std::nullopt;
  };
  const auto data_found = find("__data__");
  const auto names_found = find("__classnames__");
  if (!data_found || !names_found) refuse("the packfile lacks a __data__ or __classnames__ section");
  data_index_ = *data_found;
  names_index_ = *names_found;
  Section& data_section = image_.sections[data_index_];
  Section& names = image_.sections[names_index_];

  const auto records = parse_class_names(names.data);
  check_signatures(records);
  if (sw_ == tw_) {
    result_.unchanged = true;
    return;
  }
  check_sections();

  const auto local = rows_of<2>(data_section.local, "the local fixups");
  const auto global = rows_of<3>(data_section.global, "the global fixups");
  const auto virt = rows_of<3>(data_section.virt, "the virtual fixups");
  for (const auto& row : global) {
    if (row[1] == data_index_) continue;
    if (row[1] == names_index_ && row[2] < names.data.size()) continue;
    refuse("a global fixup points outside __data__ and __classnames__");
  }
  source_ = data_section.data;
  result_.source_data_size = source_.size();
  for (const auto& row : local) {
    if (!aims_.emplace(row[0], row[1]).second) refuse("two local fixups start at offset " + std::to_string(row[0]));
  }
  for (const auto& row : global) {
    if (row[1] == data_index_ && !aims_.emplace(row[0], row[2]).second) refuse("two fixups start at offset " + std::to_string(row[0]));
  }

  std::vector<Instance> instances;
  std::set<std::uint32_t> starts;
  std::set<std::string> classes;
  for (const auto& row : virt) {
    if (!starts.insert(row[0]).second) refuse("two virtual fixups start at offset " + std::to_string(row[0]));
    if (row[1] != names_index_) refuse("a virtual fixup does not point into the class name section");
    const auto record = records.find(row[2]);
    if (record == records.end()) refuse("a virtual fixup does not point at the start of a class name record");
    const auto shape = shape_of(record->second.name);
    if (!shape) refuse("class " + record->second.name + " has no verified 4-byte layout");
    if (shape->decode->signature != record->second.signature) refuse("class " + record->second.name + " has an unexpected signature");
    if (static_cast<std::size_t>(row[0]) + class_size(*shape, sw_) > source_.size()) {
      refuse("an instance of " + record->second.name + " does not fit the data section");
    }
    instances.push_back({row[0], *shape, record->second.name});
    classes.insert(record->second.name);
  }
  for (const Instance& instance : instances) {
    Block block;
    block.kind = Kind::Object;
    block.src = instance.src;
    block.count = 1;
    block.src_stride = class_size(instance.shape, sw_);
    block.dst_stride = class_size(instance.shape, tw_);
    block.shape = instance.shape;
    if (!add(block)) refuse("two objects start at offset " + std::to_string(instance.src));
    discover(instance.src, instance.shape, 0);
  }

  std::vector<std::pair<std::size_t, std::size_t>> spans;
  for (const Block& block : blocks_) spans.emplace_back(block.src, block.src + block.source_length());
  std::sort(spans.begin(), spans.end());
  for (std::size_t i = 1; i < spans.size(); ++i) {
    if (spans[i].first < spans[i - 1].second) refuse("two blocks overlap at offset " + std::to_string(spans[i].first));
  }
  std::vector<bool> owned(source_.size(), false);
  for (const auto& span : spans) {
    for (std::size_t i = span.first; i < span.second; ++i) owned[i] = true;
  }
  for (std::size_t i = 0; i < owned.size(); ++i) {
    if (!owned[i] && source_[i] != std::byte{0} && source_[i] != std::byte{0xFF}) {
      refuse("the data section holds a byte at offset " + std::to_string(i) + " that no verified object owns");
    }
  }

  place();
  if (!base_.contains(image_.contents_offset)) refuse("the contents object is not one of the converted blocks");

  for (const Block& block : blocks_) {
    switch (block.kind) {
      case Kind::Object:
        transcode_object(block.src, block.dst, block.shape);
        break;
      case Kind::StructArray:
        for (std::uint32_t e = 0; e < block.count; ++e) {
          transcode_object(block.src + e * block.src_stride, block.dst + e * block.dst_stride, block.shape);
        }
        break;
      case Kind::PointerArray:
      case Kind::StringArray:
        for (std::uint32_t e = 0; e < block.count; ++e) {
          const std::uint32_t s = block.src + e * block.src_stride;
          if (!aims_.contains(s)) {
            for (std::uint32_t k = 0; k < sw_; ++k) {
              if (source_[s + k] != std::byte{0}) refuse("a pointer array element is not zero but has no fixup");
            }
          }
          sites_[s] = block.dst + e * block.dst_stride;
        }
        break;
      default:
        std::copy_n(source_.begin() + block.src, block.source_length(), target_.begin() + block.dst);
        break;
    }
  }

  std::vector<std::array<std::uint32_t, 2>> next_local;
  std::vector<std::array<std::uint32_t, 3>> next_global;
  std::vector<std::array<std::uint32_t, 3>> next_virtual;
  for (const auto& row : local) {
    const auto site = sites_.find(row[0]);
    const auto to = base_.find(row[1]);
    if (site == sites_.end() || to == base_.end()) refuse("a local fixup does not start at a relocated pointer or end at a relocated block");
    next_local.push_back({site->second, to->second});
  }
  for (const auto& row : global) {
    const auto site = sites_.find(row[0]);
    if (site == sites_.end()) refuse("a global fixup does not start at a relocated pointer");
    std::uint32_t to = row[2];
    if (row[1] == data_index_) {
      const auto moved = base_.find(row[2]);
      if (moved == base_.end()) refuse("a same-section global fixup does not end at a relocated block");
      to = moved->second;
    }
    next_global.push_back({site->second, row[1], to});
  }
  for (const auto& row : virt) {
    const auto moved = base_.find(row[0]);
    if (moved == base_.end()) refuse("a virtual fixup does not start at a relocated object");
    next_virtual.push_back({moved->second, row[1], row[2]});
    result_.object_moves.emplace_back(row[0], moved->second);
  }

  result_.object_count = instances.size();
  result_.block_count = blocks_.size();
  result_.local_fixups = local.size();
  result_.global_fixups = global.size();
  result_.virtual_fixups = virt.size();
  result_.target_data_size = target_.size();
  result_.classes.assign(classes.begin(), classes.end());
  image_.contents_offset = base_.at(image_.contents_offset);
  data_section.data = std::move(target_);
  data_section.local = bytes_of(next_local);
  data_section.global = bytes_of(next_global);
  data_section.virt = bytes_of(next_virtual);
  image_.rules = {static_cast<std::byte>(tw_), std::byte{1}, std::byte{0}, std::byte{1}};
}

std::string read_text(const Bytes& data, std::uint32_t at) {
  std::string text;
  for (std::size_t i = at; i < data.size(); ++i) {
    if (data[i] == std::byte{0}) break;
    text.push_back(static_cast<char>(data[i]));
  }
  return text;
}

std::unordered_map<std::uint32_t, std::uint32_t> data_fixups(const Section& data, std::uint32_t data_index) {
  std::unordered_map<std::uint32_t, std::uint32_t> out;
  for (const auto& row : rows_of<2>(data.local, "the local fixups")) out.emplace(row[0], row[1]);
  for (const auto& row : rows_of<3>(data.global, "the global fixups")) {
    if (row[1] == data_index) out.emplace(row[0], row[2]);
  }
  return out;
}

std::string member_path(const Shape& shape, const HkxMemberType& member) {
  return std::string(shape.decode->name) + "." + std::string(member.name);
}

std::string verify_narrow_layout_crosscheck_impl(std::span<const std::byte> wide, std::span<const std::byte> narrow,
                                                 std::span<const std::pair<std::uint32_t, std::uint32_t>> moves) {
  try {
    if (wide.size() < 0x11 || narrow.size() < 0x11 || wide[0x10] != std::byte{8} || narrow[0x10] != std::byte{4}) {
      return "the cross-check needs an 8-byte source and a 4-byte result";
    }
    const Image wide_image = read_image(wide);
    const Image narrow_image = read_image(narrow);
    std::optional<std::size_t> wide_data;
    std::optional<std::size_t> narrow_data;
    std::optional<std::size_t> wide_names;
    for (std::size_t i = 0; i < wide_image.sections.size(); ++i) {
      if (wide_image.sections[i].tag == "__data__") wide_data = i;
      if (wide_image.sections[i].tag == "__classnames__") wide_names = i;
    }
    for (std::size_t i = 0; i < narrow_image.sections.size(); ++i) {
      if (narrow_image.sections[i].tag == "__data__") narrow_data = i;
    }
    if (!wide_data || !narrow_data || !wide_names) return "the cross-check could not find the data sections";

    const Section& wide_section = wide_image.sections[*wide_data];
    const Section& narrow_section = narrow_image.sections[*narrow_data];
    const auto names = parse_class_names(wide_image.sections[*wide_names].data);
    const auto wide_virt = rows_of<3>(wide_section.virt, "the virtual fixups");
    const auto wide_fixups = data_fixups(wide_section, static_cast<std::uint32_t>(*wide_data));
    const auto narrow_fixups = data_fixups(narrow_section, static_cast<std::uint32_t>(*narrow_data));

    std::map<std::uint32_t, std::uint32_t> at_narrow;
    for (const auto& [from, to] : moves) at_narrow[from] = to;

    for (const auto& row : wide_virt) {
      const auto record = names.find(row[2]);
      if (record == names.end()) return "the cross-check could not name an object";
      const auto shape = shape_of(record->second.name);
      if (!shape) return "the cross-check met an unverified class";
      const auto found = at_narrow.find(row[0]);
      if (found == at_narrow.end()) return "the cross-check lacks a narrowed offset for an object";
      const std::uint32_t from = row[0];
      const std::uint32_t to = found->second;

      for (std::size_t i = 0; i < shape->decode->members.size(); ++i) {
        const HkxMemberType& member = shape->decode->members[i];
        const Role role = role_of(member);
        const std::uint32_t w_at = from + member.offset;
        const std::uint32_t n_at = to + shape->widths->offsets[i].narrow;

        if (role == Role::Value) {
          const std::uint32_t width = kind_size(member.kind);
          if (w_at + width > wide_section.data.size() || n_at + width > narrow_section.data.size()) {
            return "the cross-check ran past the data section at " + member_path(*shape, member);
          }
          if (!std::equal(wide_section.data.begin() + w_at, wide_section.data.begin() + w_at + width,
                          narrow_section.data.begin() + n_at)) {
            return "the narrowed member " + member_path(*shape, member) +
                   " does not match the 8-byte source at the decode-table offset";
          }
          continue;
        }

        const bool array = role != Role::Pointer && role != Role::StringPointer;
        if (array) {
          if (get32(wide_section.data, w_at + 8) != get32(narrow_section.data, n_at + 4)) {
            return "the narrowed array size of " + member_path(*shape, member) + " does not match";
          }
          if ((get32(wide_section.data, w_at + 12) & 0xC0000000u) != (get32(narrow_section.data, n_at + 8) & 0xC0000000u)) {
            return "the narrowed array flags of " + member_path(*shape, member) + " do not match";
          }
        }

        if (role == Role::StringPointer) {
          const auto wf = wide_fixups.find(w_at);
          const auto nf = narrow_fixups.find(n_at);
          if ((wf == wide_fixups.end()) != (nf == narrow_fixups.end())) {
            return "the narrowed string pointer of " + member_path(*shape, member) +
                   " does not match the fixup presence in the 8-byte source";
          }
          if (wf == wide_fixups.end()) continue;
          if (read_text(wide_section.data, wf->second) != read_text(narrow_section.data, nf->second)) {
            return "the narrowed string of " + member_path(*shape, member) + " does not match";
          }
        }
      }
    }
    return {};
  } catch (const Refused& refused) {
    return refused.reason;
  }
}

}  // namespace

std::optional<std::uint8_t> packfile_pointer_size(std::span<const std::byte> bytes) {
  try {
    check_header(bytes);
  } catch (const Refused&) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(bytes[0x10]);
}

HkxRelayoutResult relayout_packfile(std::span<const std::byte> bytes, std::uint8_t target_pointer_size) {
  HkxRelayoutResult result;
  result.target_pointer_size = target_pointer_size;
  try {
    if (target_pointer_size != 4 && target_pointer_size != 8) refuse("the target pointer width must be 4 or 8");
    Image image = read_image(bytes);
    result.source_pointer_size = static_cast<std::uint8_t>(image.rules[0]);
    Relayout relayout(image, result.source_pointer_size, target_pointer_size, result);
    relayout.run();
    if (result.unchanged) {
      result.bytes.assign(bytes.begin(), bytes.end());
    } else {
      result.bytes = write_image(image);
    }
    result.ok = true;
  } catch (const Refused& refused) {
    result.ok = false;
    result.reason = refused.reason;
    result.bytes.clear();
    result.classes.clear();
    result.object_moves.clear();
  }
  return result;
}

std::optional<std::uint32_t> relayout_class_size(std::string_view class_name, std::uint8_t pointer_size) {
  if (pointer_size != 4 && pointer_size != 8) return std::nullopt;
  const auto shape = shape_of(class_name);
  if (!shape) return std::nullopt;
  return class_size(*shape, pointer_size);
}

std::string verify_narrow_layout_crosscheck(std::span<const std::byte> wide, std::span<const std::byte> narrow,
                                            std::span<const std::pair<std::uint32_t, std::uint32_t>> object_moves) {
  return verify_narrow_layout_crosscheck_impl(wide, narrow, object_moves);
}

}  // namespace fo4::hkx
