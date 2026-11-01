#include "fo4/audio/voice_path.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

using fo4::audio::VoicePath;
using fo4::audio::format_voice_path;
using fo4::audio::parse_voice_path;
using fo4::audio::voice_companion_path;

namespace {

constexpr std::string_view k_canonical = "Sound\\Voice\\DLCCoast.esm\\DLC03FemaleMariner\\00005C2B_1.fuz";

VoicePath parsed_or_fail(std::string_view path) {
  const auto parsed = parse_voice_path(path);
  EXPECT_TRUE(parsed.has_value()) << path;
  return parsed.value_or(VoicePath{});
}

}  // namespace

// Detects: a parser that must split the shipped Sound\Voice layout into its measured parts.
TEST(VoicePath, ParsesTheShippedHexStem) {
  const auto path = parsed_or_fail(k_canonical);
  EXPECT_EQ(path.plugin, "DLCCoast.esm");
  EXPECT_EQ(path.voice_type, "DLC03FemaleMariner");
  EXPECT_EQ(path.language, "");
  EXPECT_EQ(path.stem, "00005C2B_1");
  EXPECT_EQ(path.extension, "fuz");
  ASSERT_TRUE(path.info_form_id.has_value());
  ASSERT_TRUE(path.response_index.has_value());
  EXPECT_EQ(*path.info_form_id, 0x5C2Bu);
  EXPECT_EQ(*path.response_index, 1u);
  EXPECT_EQ(path.variant, "");
}

// Detects: a parser that must accept mixed-case form ids and the measured English language directory.
TEST(VoicePath, ParsesMixedCaseFormIdAndLanguageDirectory) {
  const auto path = parsed_or_fail("Sound\\Voice\\DLCCoast.esm\\DLC03MaleDiMA\\English\\0002dfdd_2.fuz");
  EXPECT_EQ(path.language, "English");
  ASSERT_TRUE(path.info_form_id.has_value());
  EXPECT_EQ(*path.info_form_id, 0x2DFDDu);
  EXPECT_EQ(*path.response_index, 2u);
}

// Detects: the two shipped seven-digit form ids, which must parse instead of being read as names.
TEST(VoicePath, ParsesTheSevenDigitFormIdAnomaly) {
  const auto path = parsed_or_fail("Sound\\Voice\\Fallout4.esm\\PlayerVoiceFemale01\\00AE7F7_1.hkx");
  ASSERT_TRUE(path.info_form_id.has_value());
  EXPECT_EQ(*path.info_form_id, 0xAE7F7u);
  EXPECT_EQ(path.extension, "hkx");
}

// Detects: the measured character-named voice files, which must parse without a form id.
TEST(VoicePath, ParsesCharacterNamedVoiceFiles) {
  const auto path = parsed_or_fail("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Abby_F.fuz");
  EXPECT_EQ(path.stem, "Abby_F");
  EXPECT_FALSE(path.info_form_id.has_value());
  EXPECT_FALSE(path.response_index.has_value());
}

// Detects: the measured ALT and ALL variant tails, which must stay attached to the response index.
TEST(VoicePath, ParsesVariantSuffixes) {
  const auto alt = parsed_or_fail("Sound\\Voice\\DLCCoast.esm\\DLC03FemaleMariner\\00005C2B_1_ALT01.fuz");
  EXPECT_EQ(alt.variant, "ALT01");
  EXPECT_EQ(*alt.response_index, 1u);
  const auto all = parsed_or_fail("Sound\\Voice\\Fallout4.esm\\MaleEvenToned\\0000A1B2_3_ALL.fuz");
  EXPECT_EQ(all.variant, "ALL");
}

// Detects: a parser that must treat both separators the same instead of requiring backslashes.
TEST(VoicePath, ParsesForwardSlashSeparators) {
  const auto path = parsed_or_fail("Sound/Voice/DLCCoast.esm/DLC03FemaleMariner/00005C2B_1.fuz");
  EXPECT_EQ(path.plugin, "DLCCoast.esm");
  EXPECT_EQ(*path.response_index, 1u);
}

// Detects: a writer that must emit the measured spelling with an eight-digit uppercase form id.
TEST(VoicePath, FormatsTheShippedSpelling) {
  EXPECT_EQ(format_voice_path("DLCCoast.esm", "DLC03FemaleMariner", 0x5C2Bu, 1), std::string{k_canonical});
  EXPECT_EQ(format_voice_path("DLCCoast.esm", "DLC03FemaleMariner", 0x5C2Bu, 1, "English", "ALT01", "hkx"),
            std::string{"Sound\\Voice\\DLCCoast.esm\\DLC03FemaleMariner\\English\\00005C2B_1_ALT01.hkx"});
  EXPECT_EQ(format_voice_path("DLCCoast.esm", "DLC03FemaleMariner", 0x5C2Bu, 1),
            format_voice_path("DLCCoast.esm", "DLC03FemaleMariner", 0x00005C2Bu, 1));
}

// Detects: a parser and writer pair that must round-trip the measured spelling unchanged.
TEST(VoicePath, RoundTripsParsedPaths) {
  for (const std::string_view text : std::array<std::string_view, 3>{k_canonical,
                                      "Sound\\Voice\\Fallout4.esm\\MaleEvenToned\\0000A1B2_3_ALL.fuz",
                                      "Sound\\Voice\\DLCCoast.esm\\DLC03MaleDiMA\\English\\0002DFDD_2.fuz"}) {
    const auto parsed = parsed_or_fail(text);
    ASSERT_TRUE(parsed.info_form_id.has_value()) << text;
    const auto formatted = format_voice_path(parsed.plugin, parsed.voice_type, *parsed.info_form_id,
                                             *parsed.response_index, parsed.language, parsed.variant,
                                             parsed.extension);
    EXPECT_EQ(formatted, text) << text;
    EXPECT_EQ(parse_voice_path(formatted), parsed) << text;
  }
  const auto named = parsed_or_fail("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Abby_F.fuz");
  EXPECT_FALSE(named.info_form_id.has_value());
  EXPECT_EQ(voice_companion_path(named), std::string{"Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Abby_F.hkx"});
}

// Detects: the shipped anomalies that must normalise to the eight-digit writer spelling instead of round-tripping.
TEST(VoicePath, NormalisesLowercaseAndSevenDigitFormIds) {
  const auto lower = parsed_or_fail("Sound\\Voice\\DLCCoast.esm\\DLC03MaleDiMA\\English\\0002dfdd_2.fuz");
  EXPECT_EQ(format_voice_path(lower.plugin, lower.voice_type, *lower.info_form_id, *lower.response_index,
                              lower.language, lower.variant, lower.extension),
            std::string{"Sound\\Voice\\DLCCoast.esm\\DLC03MaleDiMA\\English\\0002DFDD_2.fuz"});
  const auto short_id = parsed_or_fail("Sound\\Voice\\Fallout4.esm\\PlayerVoiceFemale01\\00AE7F7_1.hkx");
  EXPECT_EQ(format_voice_path(short_id.plugin, short_id.voice_type, *short_id.info_form_id,
                              *short_id.response_index, short_id.language, short_id.variant,
                              short_id.extension),
            std::string{"Sound\\Voice\\Fallout4.esm\\PlayerVoiceFemale01\\000AE7F7_1.hkx"});
}

// Detects: a response index with a leading zero, which the writer could not reproduce and must be refused.
TEST(VoicePath, RejectsLeadingZeroResponseIndex) {
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Fallout4.esm\\MaleEvenToned\\0000A1B2_01.fuz").has_value());
}

// Detects: the measured fuz/hkx twin pairing, which must swap only the extension.
TEST(VoicePath, CompanionSwapsExtension) {
  const auto fuz = parsed_or_fail(k_canonical);
  EXPECT_EQ(voice_companion_path(fuz), std::string{"Sound\\Voice\\DLCCoast.esm\\DLC03FemaleMariner\\00005C2B_1.hkx"});
  const auto hkx = parsed_or_fail("Sound\\Voice\\DLCCoast.esm\\DLC03FemaleMariner\\00005C2B_1.hkx");
  EXPECT_EQ(voice_companion_path(hkx), std::string{k_canonical});
  const auto named = parsed_or_fail("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Abby_F.fuz");
  EXPECT_EQ(voice_companion_path(named), std::string{"Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Abby_F.hkx"});
}

// Detects: paths outside the Sound\Voice tree, which must be refused rather than guessed at.
TEST(VoicePath, RejectsPathsOutsideSoundVoice) {
  EXPECT_FALSE(parse_voice_path("Meshes\\Actors\\Abby_F.fuz").has_value());
  EXPECT_FALSE(parse_voice_path("Sound\\Music\\Abby_F.fuz").has_value());
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Abby_F.fuz").has_value());
  EXPECT_FALSE(parse_voice_path("").has_value());
}

// Detects: extensions the measured tree does not contain, which must be refused.
TEST(VoicePath, RejectsUnknownExtensions) {
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Abby_F.wav").has_value());
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Abby_F").has_value());
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\.fuz").has_value());
}

// Detects: empty, dot and dot-dot components, which must not survive parsing.
TEST(VoicePath, RejectsEmptyAndDotComponents) {
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\\\VType\\Abby_F.fuz").has_value());
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Fallout4.esm\\..\\Abby_F.fuz").has_value());
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Fallout4.esm\\.\\Abby_F.fuz").has_value());
}

// Detects: a zero response index, which the shipped data never contains and which must be refused.
TEST(VoicePath, RejectsResponseIndexZero) {
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Fallout4.esm\\MaleEvenToned\\0000A1B2_0.fuz").has_value());
}

// Detects: a trailing separator and too many components, which must not be read as a valid path.
TEST(VoicePath, RejectsMalformedDepth) {
  EXPECT_FALSE(parse_voice_path("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Abby_F.fuz\\").has_value());
  EXPECT_FALSE(
      parse_voice_path("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\English\\Extra\\Abby_F.fuz").has_value());
}

// Detects: writer inputs that must be refused instead of producing a malformed path.
TEST(VoicePath, RejectsUnformattableInputs) {
  EXPECT_THROW(format_voice_path("", "VType", 1, 1), std::invalid_argument);
  EXPECT_THROW(format_voice_path("Fallout4.esm", "", 1, 1), std::invalid_argument);
  EXPECT_THROW(format_voice_path("Fallout4.esm", "VType", 1, 0), std::invalid_argument);
  EXPECT_THROW(format_voice_path("Fallout4.esm", "VType", 1, 1, "", "", "wav"), std::invalid_argument);
  EXPECT_THROW(format_voice_path("a\\b.esm", "VType", 1, 1), std::invalid_argument);
  EXPECT_THROW(format_voice_path("Fallout4.esm", "VType", 1, 1, "", "ALT-1"), std::invalid_argument);
}
