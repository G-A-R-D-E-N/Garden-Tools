#pragma once

#include "fo4/formid.hpp"

#include <cstdint>
#include <vector>

namespace fo4::seq {

/*
 * The Fallout 4 .seq container: the start-game-enabled quests a plugin adds.
 *
 * Evidence is recorded in Radicle/docs/SEQ_FORMAT.md. In short, five local .seq files
 * of 4, 8 and 12 bytes and this project's own SEQ writer all agree on a bare array of
 * little-endian uint32 runtime FormIDs with no header, no count and no magic.
 *
 * Entries are preserved exactly, in file order, as raw FormIDs. This reader never
 * resolves them: turning a master index into a plugin needs a load order, and one is
 * not part of the container.
 */
struct SeqFile {
  std::vector<RawFormId> entries;

  /*
   * Throws fo4::parse_error when the byte count is not a multiple of four. A zero-byte
   * input is read as an empty list; whether the engine accepts one is UNPROVEN.
   */
  static SeqFile fromBytes(const std::vector<std::uint8_t>& bytes);

  std::vector<std::uint8_t> toBytes() const;
};

}
