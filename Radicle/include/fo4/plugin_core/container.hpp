#pragma once

#include "fo4/flags.hpp"
#include "fo4/headers.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fo4 {

struct OriginalByteRange {
  const std::vector<std::byte>* owner{nullptr};
  std::size_t offset{0};
  std::size_t size{0};

  [[nodiscard]] constexpr bool empty() const noexcept { return size == 0; }
  constexpr void clear() noexcept {
    owner = nullptr;
    offset = 0;
    size = 0;
  }
};

struct Subrecord {
  Signature signature{};
  std::vector<std::byte> data{};
  bool extended_size{false};
};

class SubrecordList {
public:
  using value_type = Subrecord;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = Subrecord&;
  using const_reference = const Subrecord&;
  using iterator = Subrecord*;
  using const_iterator = const Subrecord*;

  SubrecordList() = default;
  SubrecordList(std::vector<Subrecord> items)
      : items_(new std::vector<Subrecord>(std::move(items))) {}
  SubrecordList(std::shared_ptr<const std::vector<std::byte>> source, std::size_t record_offset,
                std::size_t data_size, bool compressed)
      : source_(std::move(source)),
        record_offset_(record_offset),
        data_size_(data_size),
        compressed_(compressed) {}

  SubrecordList(const SubrecordList& other)
      : source_(other.source_),
        record_offset_(other.record_offset_),
        data_size_(other.data_size_),
        compressed_(other.compressed_),
        mutated_(other.mutated_.load(std::memory_order_relaxed)) {
    if (auto* items = other.items_.load()) items_.store(new std::vector<Subrecord>(*items));
  }
  SubrecordList(SubrecordList&& other) noexcept
      : source_(std::move(other.source_)),
        record_offset_(other.record_offset_),
        data_size_(other.data_size_),
        compressed_(other.compressed_),
        items_(other.items_.exchange(nullptr)),
        mutated_(other.mutated_.load(std::memory_order_relaxed)) {}
  SubrecordList& operator=(const SubrecordList& other) {
    if (this != &other) *this = SubrecordList(other);
    return *this;
  }
  SubrecordList& operator=(SubrecordList&& other) noexcept {
    if (this != &other) {
      delete items_.exchange(other.items_.exchange(nullptr));
      source_ = std::move(other.source_);
      record_offset_ = other.record_offset_;
      data_size_ = other.data_size_;
      compressed_ = other.compressed_;
      mutated_.store(other.mutated_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
  }
  SubrecordList& operator=(std::initializer_list<Subrecord> items) {
    *this = SubrecordList(std::vector<Subrecord>(items));
    return *this;
  }
  ~SubrecordList() { delete items_.load(); }

  [[nodiscard]] bool materialized() const noexcept {
    return source_ == nullptr || items_.load() != nullptr;
  }
  void detach() {
    storage();
    source_.reset();
  }
  void own_record_bytes();
  bool release();
  [[nodiscard]] std::optional<std::vector<std::byte>> first_nonempty_data(Signature signature) const;

  size_type size() const { return storage().size(); }
  bool empty() const { return storage().empty(); }
  size_type capacity() const { return storage().capacity(); }
  void reserve(size_type count) { storage().reserve(count); }
  void clear() { storage().clear(); }

  iterator begin() { return storage().data(); }
  const_iterator begin() const { return storage().data(); }
  const_iterator cbegin() const { return begin(); }
  iterator end() {
    auto& items = storage();
    return items.data() + items.size();
  }
  const_iterator end() const {
    const auto& items = storage();
    return items.data() + items.size();
  }
  const_iterator cend() const { return end(); }

  Subrecord* data() { return storage().data(); }
  const Subrecord* data() const { return storage().data(); }
  reference operator[](size_type index) { return storage()[index]; }
  const_reference operator[](size_type index) const { return storage()[index]; }
  reference at(size_type index) { return storage().at(index); }
  const_reference at(size_type index) const { return storage().at(index); }
  reference front() { return storage().front(); }
  const_reference front() const { return storage().front(); }
  reference back() { return storage().back(); }
  const_reference back() const { return storage().back(); }

  void push_back(const Subrecord& sub) { storage().push_back(sub); }
  void push_back(Subrecord&& sub) { storage().push_back(std::move(sub)); }
  template <class... Args>
  reference emplace_back(Args&&... args) {
    return storage().emplace_back(std::forward<Args>(args)...);
  }

  iterator insert(const_iterator position, const Subrecord& sub) {
    return insert_items(position, sub);
  }
  iterator insert(const_iterator position, Subrecord&& sub) {
    return insert_items(position, std::move(sub));
  }
  template <class It>
  iterator insert(const_iterator position, It first, It last) {
    return insert_items(position, first, last);
  }
  iterator erase(const_iterator position) { return erase(position, position + 1); }
  iterator erase(const_iterator first, const_iterator last) {
    auto& items = storage();
    const auto offset = first - items.data();
    const auto count = last - first;
    items.erase(items.cbegin() + offset, items.cbegin() + offset + count);
    return items.data() + offset;
  }

  operator std::vector<Subrecord>&() { return storage(); }
  operator const std::vector<Subrecord>&() const { return storage(); }

private:
  template <class... Args>
  iterator insert_items(const_iterator position, Args&&... args) {
    auto& items = storage();
    const auto offset = position - items.data();
    items.insert(items.cbegin() + offset, std::forward<Args>(args)...);
    return items.data() + offset;
  }

  std::vector<Subrecord>& storage() {
    mutated_.store(true, std::memory_order_relaxed);
    return materialize();
  }
  const std::vector<Subrecord>& storage() const { return materialize(); }
  std::vector<Subrecord>& materialize() const;
  std::vector<Subrecord> decode() const;

  std::shared_ptr<const std::vector<std::byte>> source_{};
  std::size_t record_offset_{0};
  std::size_t data_size_{0};
  bool compressed_{false};
  mutable std::atomic<std::vector<Subrecord>*> items_{nullptr};
  std::atomic<bool> mutated_{false};
};

struct RawCompressedPayload {
  std::uint32_t declared_size{0};
  std::vector<std::byte> bytes{};
};

struct Record {
  RecordHeader header{};
  FormKey form_key{};
  SubrecordList subrecords{};
  OriginalByteRange original_range{};
  std::vector<std::byte> decompressed_payload{};
  bool compressed{false};
  bool undecodable{false};
  std::uint32_t uncompressed_size{0};
  // Invariant: when set on a compressed record the writer stores these bytes verbatim after the declared size and ignores the subrecords.
  std::optional<RawCompressedPayload> raw_compressed{};
};

inline const Record& transient_view(const Record& record, std::optional<Record>& scratch) {
  if (record.subrecords.materialized()) return record;
  scratch.emplace(record);
  return *scratch;
}

inline void require_decodable(const Record& record) {
  if (!record.undecodable) return;
  throw std::invalid_argument("record " + record.header.signature.to_string() + " " +
                              record.form_key.to_string() + " is undecodable; edits are refused");
}

inline bool can_be_partial(Signature signature) {
  const auto view = signature.view();
  return view == "CELL" || view == "DIAL" || view == "QUST" || view == "WRLD";
}

inline bool is_partial_form(const Record& record) {
  return record.header.flags.test(RecordFlags::partial_form) &&
         can_be_partial(record.header.signature);
}

struct Group;

using GroupChild = std::variant<Record, Group>;

struct Group {
  GroupHeader header{};
  std::vector<GroupChild> children{};
};

struct MasterReference {
  PluginKey key;
  PluginKind kind{PluginKind::full};
  std::uint64_t data_size{0};
  std::vector<std::byte> data_bytes{};
};

struct PluginHeader {
  float version{0.0f};
  std::uint32_t record_count{0};
  std::uint32_t next_form_id{0};
  std::string author;
  std::string description;
  std::vector<MasterReference> masters;
  std::vector<RawFormId> overrides;
  std::optional<std::uint32_t> intv;
  std::optional<std::uint32_t> incc;
  RecordFlags tes4_flags{};
  std::uint16_t form_version{0};
};

struct Plugin {
  std::filesystem::path path;
  PluginKey key;
  Record tes4;
  PluginHeader header;
  std::vector<Group> groups;
  std::shared_ptr<const std::vector<std::byte>> original_data;
  OriginalByteRange original_range;
  std::uint64_t original_structure_fingerprint{0};
  std::optional<Record> tes4_after_hedr_repair;
  std::vector<std::byte> tes4_repaired_bytes;
};

[[nodiscard]] std::span<const std::byte> plugin_original_bytes(const Plugin& plugin);
[[nodiscard]] std::span<const std::byte> record_original_bytes(const Plugin& plugin,
                                                                const Record& record);

}  // namespace fo4
