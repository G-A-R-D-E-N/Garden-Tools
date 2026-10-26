#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::string_view k_key = "000800:Patch.esp";

Bytes words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes make_plugin(std::initializer_list<std::uint32_t> counts,
                  std::initializer_list<std::uint32_t> links) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto steps = fo4::test::begin_group(out, "FSTP", 0);
  const auto step = fo4::test::begin_record(out, "FSTP", 0, 0x01000815u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleStep"));
  fo4::test::end_record(out, step);
  fo4::test::end_group(out, steps);
  const auto group = fo4::test::begin_group(out, "FSTS", 0);
  const auto record = fo4::test::begin_record(out, "FSTS", 0, 0x01000800u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleSteps"));
  fo4::test::write_subrecord(out, "XCNT", words(counts));
  fo4::test::write_subrecord(out, "DATA", words(links));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes make_plugin(std::initializer_list<std::uint32_t> counts) {
  return make_plugin(counts, {0x01000815, 0x01000814, 0x01000812, 0x01000813, 0x01000811});
}

Bytes footstep_plugin(bool with_counter, std::initializer_list<std::uint32_t> counts) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto steps = fo4::test::begin_group(out, "FSTP", 0);
  const auto step = fo4::test::begin_record(out, "FSTP", 0, 0x01000815u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleStep"));
  fo4::test::end_record(out, step);
  fo4::test::end_group(out, steps);
  const auto group = fo4::test::begin_group(out, "FSTS", 0);
  const auto record = fo4::test::begin_record(out, "FSTS", 0, 0x01000800u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleSteps"));
  if (with_counter) fo4::test::write_subrecord(out, "XCNT", words(counts));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

template <typename Fn>
std::string thrown_message(Fn&& fn) {
  try {
    fn();
  } catch (const std::exception& error) {
    return error.what();
  }
  return {};
}

fo4::schema::FormLinkListDescriptor list(std::string_view path) {
  const auto descriptor =
      fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("FSTS"), path);
  EXPECT_TRUE(descriptor.has_value()) << path;
  return descriptor.value_or(fo4::schema::FormLinkListDescriptor{});
}

std::vector<fo4::FormKey> list_values(const fo4::Plugin& plugin,
                                      const fo4::FormKey& record,
                                      const fo4::schema::FormLinkListDescriptor& descriptor) {
  const auto values = fo4::schema::record_formlink_list_values(plugin, record, descriptor);
  EXPECT_TRUE(values.has_value());
  return values.value_or(std::vector<fo4::FormKey>{});
}

std::size_t list_size(const fo4::Plugin& plugin,
                      const fo4::FormKey& record,
                      const fo4::schema::FormLinkListDescriptor& descriptor) {
  const auto size = fo4::schema::record_formlink_list_size(plugin, record, descriptor);
  EXPECT_TRUE(size.has_value());
  return size.value_or(0);
}

std::vector<std::byte> subrecord_bytes(const fo4::Plugin& plugin,
                                       const fo4::FormKey& record,
                                       fo4::Signature signature) {
  const auto* found = fo4::find_record(plugin, record);
  EXPECT_NE(found, nullptr);
  if (found == nullptr) return {};
  for (const auto& subrecord : found->subrecords) {
    if (subrecord.signature == signature) return subrecord.data;
  }
  return {};
}

fo4::schema::ScalarFieldDescriptor row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("FSTS"), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

}  // namespace

TEST(FstsRows, XcntCountRowsUseTheXEditStructLayout) {
  const auto walking = row("FSTS\\WalkingCount");
  const auto running = row("FSTS\\RunningCount");
  const auto sprinting = row("FSTS\\SprintingCount");
  const auto sneaking = row("FSTS\\SneakingCount");
  const auto swimming = row("FSTS\\SwimmingCount");

  for (const auto* field : {&walking, &running, &sprinting, &sneaking, &swimming}) {
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("XCNT"));
    EXPECT_EQ(field->scalar.storage, fo4::schema::ScalarStorage::u32);
    EXPECT_TRUE(field->scalar.embedded);
    EXPECT_EQ(field->scalar.payload_size, std::size_t{20});
    EXPECT_EQ(field->scalar.minimum, 0.0);
    EXPECT_EQ(field->scalar.maximum, 4294967295.0);
    EXPECT_FALSE(field->scalar.address.repeated);
  }

  EXPECT_EQ(walking.scalar.offset, std::size_t{0});
  EXPECT_EQ(running.scalar.offset, std::size_t{4});
  EXPECT_EQ(sprinting.scalar.offset, std::size_t{8});
  EXPECT_EQ(sneaking.scalar.offset, std::size_t{12});
  EXPECT_EQ(swimming.scalar.offset, std::size_t{16});

  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("XCNT")).has_value());
}

TEST(FstsRows, ReadsTheFiveCategoryCounts) {
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  const std::array<std::pair<std::string_view, std::string_view>, 5> expected{{
      {"FSTS\\WalkingCount", "1"},
      {"FSTS\\RunningCount", "2"},
      {"FSTS\\SprintingCount", "0"},
      {"FSTS\\SneakingCount", "1"},
      {"FSTS\\SwimmingCount", "1"},
  }};
  for (const auto& [path, value] : expected) {
    const auto field = row(path);
    EXPECT_EQ(fo4::record_field_count(plugin, record, field), std::optional<std::size_t>{1}) << path;
    const auto* found = fo4::find_record(plugin, record);
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(found->subrecords.size(), std::size_t{3});
    const auto bytes = fo4::schema::scalar_field_bytes(found->subrecords[1].data, field.scalar);
    EXPECT_EQ(fo4::schema::decode_scalar(bytes, field.scalar), value) << path;
  }

  EXPECT_EQ(fo4::record_field_count(plugin, record, "FSTS\\Footsteps"), std::optional<std::size_t>{5});
}

TEST(FstsRows, RefusesGenericEditsOfTheDerivedCounts) {
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  EXPECT_THROW(fo4::set_record_scalar(plugin, record, row("FSTS\\WalkingCount"), 0, "2"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record, row("FSTS\\SwimmingCount"), 0, "3"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_string(plugin, record, row("FSTS\\SneakingCount"), 0, "1"),
               fo4::schema::ScalarError);

  const auto fsts = fo4::Signature::from_string("FSTS");
  EXPECT_TRUE(fo4::schema::is_derived_scalar_row(fsts, fo4::Signature::from_string("XCNT")));
  EXPECT_FALSE(fo4::schema::is_derived_scalar_row(fsts, fo4::Signature::from_string("DATA")));
  EXPECT_FALSE(fo4::schema::is_derived_scalar_row(fo4::Signature::from_string("SNDR"),
                                                 fo4::Signature::from_string("XCNT")));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* found = fo4::find_record(reread, record);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->subrecords[1].data, words({1, 2, 0, 1, 1}));
}

TEST(FstsRows, ShortXcntPayloadsHideTheCountsPastTheirEnd) {
  const auto sneaking = row("FSTS\\SneakingCount");
  const auto swimming = row("FSTS\\SwimmingCount");

  const auto twelve = words({1, 2, 0});
  const auto twenty = words({1, 2, 0, 1, 1});
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(twelve, sneaking.scalar).empty());
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(twelve, swimming.scalar).empty());
  EXPECT_EQ(fo4::schema::scalar_field_bytes(twenty, sneaking.scalar).size(), std::size_t{4});
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(twenty, swimming.scalar),
                                       swimming.scalar),
            "1");
}

TEST(FstsRows, CategoryListsSliceThePackedFootstepArray) {
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  const auto swimming = list("FSTS\\SwimmingFootsteps");
  const auto sneaking = list("FSTS\\SneakingFootsteps");
  const auto sprinting = list("FSTS\\SprintingFootsteps");
  const auto running = list("FSTS\\RunningFootsteps");
  const auto walking = list("FSTS\\WalkingFootsteps");

  EXPECT_EQ(swimming.counter, fo4::Signature::from_string("XCNT"));
  EXPECT_EQ(swimming.subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(swimming.counter_payload_size, std::size_t{20});
  EXPECT_EQ(swimming.segment_count, std::size_t{5});
  EXPECT_EQ(walking.segment_index, std::size_t{4});
  EXPECT_EQ(swimming.segment_index, std::size_t{0});

  EXPECT_EQ(list_size(plugin, record, swimming), std::size_t{1});
  EXPECT_EQ(list_size(plugin, record, sneaking), std::size_t{1});
  EXPECT_EQ(list_size(plugin, record, sprinting), std::size_t{0});
  EXPECT_EQ(list_size(plugin, record, running), std::size_t{2});
  EXPECT_EQ(list_size(plugin, record, walking), std::size_t{1});

  const auto swimming_step = fo4::FormKey::parse("000815:Patch.esp");
  const auto sneaking_step = fo4::FormKey::parse("000814:Patch.esp");
  const auto running_steps = std::vector<fo4::FormKey>{fo4::FormKey::parse("000812:Patch.esp"),
                                                       fo4::FormKey::parse("000813:Patch.esp")};
  const auto walking_step = fo4::FormKey::parse("000811:Patch.esp");
  EXPECT_EQ(list_values(plugin, record, swimming), (std::vector<fo4::FormKey>{swimming_step}));
  EXPECT_EQ(list_values(plugin, record, sneaking), (std::vector<fo4::FormKey>{sneaking_step}));
  EXPECT_TRUE(list_values(plugin, record, sprinting).empty());
  EXPECT_EQ(list_values(plugin, record, running), running_steps);
  EXPECT_EQ(list_values(plugin, record, walking), (std::vector<fo4::FormKey>{walking_step}));
}

TEST(FstsRows, CategoryInsertRewritesItsCountAndShiftsOnlyItsBytes) {
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto walking = list("FSTS\\WalkingFootsteps");
  const auto running = list("FSTS\\RunningFootsteps");
  const auto step = fo4::FormKey::parse("000815:Patch.esp");

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record, walking, 0, step),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({2, 2, 0, 1, 1}));
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")),
            words({0x01000815, 0x01000814, 0x01000812, 0x01000813, 0x01000815, 0x01000811}));
  EXPECT_EQ(list_values(plugin, record, walking),
            (std::vector<fo4::FormKey>{step, fo4::FormKey::parse("000811:Patch.esp")}));
  EXPECT_EQ(list_size(plugin, record, running), std::size_t{2});
  EXPECT_EQ(list_values(plugin, record, running).at(0), fo4::FormKey::parse("000812:Patch.esp"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_bytes(reread, record, fo4::Signature::from_string("DATA")),
            words({0x01000815, 0x01000814, 0x01000812, 0x01000813, 0x01000815, 0x01000811}));
  EXPECT_EQ(list_size(reread, record, walking), std::size_t{2});
}

TEST(FstsRows, CategoryRemoveAndClearRewriteOnlyTheirOwnCount) {
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto walking = list("FSTS\\WalkingFootsteps");
  const auto sprinting = list("FSTS\\SprintingFootsteps");

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, record, walking, 0),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({0, 2, 0, 1, 1}));
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")),
            words({0x01000815, 0x01000814, 0x01000812, 0x01000813}));
  EXPECT_EQ(list_size(plugin, record, walking), std::size_t{0});
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, record, walking, 0),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);

  EXPECT_EQ(fo4::schema::clear_record_formlink_list(plugin, record, sprinting),
            fo4::schema::FormLinkListMutationStatus::unchanged);
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({0, 2, 0, 1, 1}));

  const auto swimming = list("FSTS\\SwimmingFootsteps");
  auto emptied = fo4::read_plugin(make_plugin({0, 0, 0, 0, 1}, {0x01000811}), "Patch.esp");
  EXPECT_EQ(fo4::schema::clear_record_formlink_list(emptied, record, swimming),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(subrecord_bytes(emptied, record, fo4::Signature::from_string("XCNT")),
            words({0, 0, 0, 0, 0}));
  EXPECT_TRUE(subrecord_bytes(emptied, record, fo4::Signature::from_string("DATA")).empty());
  EXPECT_EQ(list_size(emptied, record, swimming), std::size_t{0});
}

TEST(FstsRows, CategoryListsRefuseACounterThatDoesNotMatchTheBody) {
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}, {0x01000815, 0x01000814, 0x01000812, 0x01000813}),
                                 "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto walking = list("FSTS\\WalkingFootsteps");

  EXPECT_THROW(fo4::schema::record_formlink_list_size(plugin, record, walking),
               fo4::schema::FormLinkListError);
  EXPECT_THROW(fo4::schema::record_formlink_list_values(plugin, record, walking),
               fo4::schema::FormLinkListError);
  EXPECT_THROW(fo4::schema::insert_record_formlink_list_element(
                   plugin, record, walking, 0, fo4::FormKey::parse("000815:Patch.esp")),
               fo4::schema::FormLinkListError);
  EXPECT_THROW(fo4::schema::remove_record_formlink_list_element(plugin, record, walking, 0),
               fo4::schema::FormLinkListError);
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({1, 2, 0, 1, 1}));
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")),
            words({0x01000815, 0x01000814, 0x01000812, 0x01000813}));

  auto short_counter = fo4::read_plugin(make_plugin({1, 2, 0}), "Patch.esp");
  EXPECT_THROW(fo4::schema::record_formlink_list_size(short_counter, record, walking),
               fo4::schema::FormLinkListError);
  EXPECT_THROW(fo4::schema::record_formlink_list_values(short_counter, record, walking),
               fo4::schema::FormLinkListError);
  EXPECT_EQ(subrecord_bytes(short_counter, record, fo4::Signature::from_string("XCNT")),
            words({1, 2, 0}));
  EXPECT_EQ(subrecord_bytes(short_counter, record, fo4::Signature::from_string("DATA")),
            words({0x01000815, 0x01000814, 0x01000812, 0x01000813, 0x01000811}));
}

TEST(FstsRows, EmptyCategoryListCreatesItsCounterOnTheFirstInsert) {
  // Constraint: a set with neither XCNT nor DATA must create the counter with its first link (regression: insertion_boundary_unavailable).
  auto plugin = fo4::read_plugin(footstep_plugin(false, {}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto walking = list("FSTS\\WalkingFootsteps");

  EXPECT_EQ(list_size(plugin, record, walking), std::size_t{0});
  EXPECT_TRUE(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")).empty());
  EXPECT_TRUE(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")).empty());

  const auto step = fo4::FormKey::parse("000815:Patch.esp");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record, walking, 0, step),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({0, 0, 0, 0, 1}));
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")),
            words({0x01000815}));

  const auto* found = fo4::find_record(plugin, record);
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->subrecords.size(), std::size_t{3});
  EXPECT_EQ(found->subrecords[1].signature, fo4::Signature::from_string("XCNT"));
  EXPECT_EQ(found->subrecords[2].signature, fo4::Signature::from_string("DATA"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(list_values(reread, record, walking), (std::vector<fo4::FormKey>{step}));
  EXPECT_EQ(subrecord_bytes(reread, record, fo4::Signature::from_string("XCNT")),
            words({0, 0, 0, 0, 1}));

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, record, walking, 0),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({0, 0, 0, 0, 0}));
  EXPECT_TRUE(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")).empty());
}

TEST(FstsRows, CategoryAppendStopsAtTheListBoundary) {
  // Constraint: an append must stop at index == size and a refusal must not step a count word (regression: refused append still wrote XCNT).
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto running = list("FSTS\\RunningFootsteps");
  const auto step = fo4::FormKey::parse("000815:Patch.esp");

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record, running, 2, step),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({1, 3, 0, 1, 1}));
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")),
            words({0x01000815, 0x01000814, 0x01000812, 0x01000813, 0x01000815, 0x01000811}));

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record, running, 4, step),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({1, 3, 0, 1, 1}));
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")),
            words({0x01000815, 0x01000814, 0x01000812, 0x01000813, 0x01000815, 0x01000811}));
}

TEST(FstsRows, CategoryCountMismatchFailsClosedWithTheExactMessage) {
  // Constraint: a counter that disagrees with the array must fail closed on the documented message without a partial write.
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1},
                                             {0x01000815, 0x01000814, 0x01000812, 0x01000813}),
                                 "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto walking = list("FSTS\\WalkingFootsteps");
  const auto step = fo4::FormKey::parse("000815:Patch.esp");

  EXPECT_EQ(thrown_message([&] { fo4::schema::record_formlink_list_size(plugin, record, walking); }),
            "FSTS\\WalkingFootsteps counters do not match the link array");
  EXPECT_EQ(thrown_message([&] { fo4::schema::record_formlink_list_values(plugin, record, walking); }),
            "FSTS\\WalkingFootsteps counters do not match the link array");
  EXPECT_EQ(thrown_message([&] {
              fo4::schema::insert_record_formlink_list_element(plugin, record, walking, 0, step);
            }),
            "FSTS\\WalkingFootsteps counters do not match the link array");
  EXPECT_EQ(thrown_message([&] {
              fo4::schema::remove_record_formlink_list_element(plugin, record, walking, 0);
            }),
            "FSTS\\WalkingFootsteps counters do not match the link array");
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({1, 2, 0, 1, 1}));
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")),
            words({0x01000815, 0x01000814, 0x01000812, 0x01000813}));

  auto short_counter = fo4::read_plugin(make_plugin({1, 2, 0}), "Patch.esp");
  EXPECT_EQ(thrown_message([&] {
              fo4::schema::record_formlink_list_size(short_counter, record, walking);
            }),
            "FSTS\\WalkingFootsteps counter subrecord has the wrong size");
  EXPECT_EQ(thrown_message([&] {
              fo4::schema::insert_record_formlink_list_element(short_counter, record, walking, 0,
                                                               step);
            }),
            "FSTS\\WalkingFootsteps counter subrecord has the wrong size");
  EXPECT_EQ(subrecord_bytes(short_counter, record, fo4::Signature::from_string("XCNT")),
            words({1, 2, 0}));
}

TEST(FstsRows, CounterWordsAtTheRepresentableMaximumDoNotWrap) {
  // Constraint: count words that total 2^32 must not wrap to zero and pass as an empty array.
  auto plugin = fo4::read_plugin(
      footstep_plugin(true, {0xFFFFFFFFu, 1, 0, 0, 0}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto walking = list("FSTS\\WalkingFootsteps");

  EXPECT_EQ(thrown_message([&] { fo4::schema::record_formlink_list_size(plugin, record, walking); }),
            "FSTS\\WalkingFootsteps counters do not match the link array");
  EXPECT_EQ(thrown_message([&] {
              fo4::schema::insert_record_formlink_list_element(
                  plugin, record, walking, 0, fo4::FormKey::parse("000815:Patch.esp"));
            }),
            "FSTS\\WalkingFootsteps counters do not match the link array");
  EXPECT_EQ(subrecord_bytes(plugin, record, fo4::Signature::from_string("XCNT")),
            words({0xFFFFFFFFu, 1, 0, 0, 0}));
  EXPECT_TRUE(subrecord_bytes(plugin, record, fo4::Signature::from_string("DATA")).empty());

  auto with_link = fo4::read_plugin(make_plugin({0xFFFFFFFFu, 1, 0, 0, 0}, {0x01000815}),
                                    "Patch.esp");
  EXPECT_EQ(thrown_message([&] { fo4::schema::record_formlink_list_size(with_link, record, walking); }),
            "FSTS\\WalkingFootsteps counters do not match the link array");
}
