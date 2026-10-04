# Sound record read surface (#340)

Compatibility layout evidence: TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`.
These declarations establish stored-field compatibility, not runtime playback.
The authenticated private runtime evidence index at commit
`f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a`, section a-340, confirms CK tag
inventories but explicitly lacks the SNDR component-hook byte grammar.

- SNDR lines 9406–9450: CNAM selects Standard `1EEF540A`, Compound
  `54651A43`, or AutoWeapon `ED157AE3`. GNAM category, SNAM alternate,
  ONAM output model and repeated DNAM descriptor links are four bytes.
  Repeated ANAM file paths use the shared helper at Common lines 9200–9203.
  Standard BNAM is six bytes: signed frequency shift/variance, unsigned
  priority/dB variance, and attenuation hundredths as u16. AutoWeapon BNAM
  is a four-byte base-descriptor link; the decider is FO4 lines 1556–1576.
  LNAM has four stored bytes; the loop byte is exposed raw, as are the
  remaining control bytes. ITMC and the ITMS/INTV/FNAM/ITME rate blocks
  describe auto-weapon file alternatives.
- SOUN lines 11961–11970: SDSC is its SNDR link; REPT stores two floats
  and an optional stackable byte (`SetOptionalFrom(2)`): accept eight or nine
  bytes without inventing the missing byte's default.
- MUSC lines 8665–8684: FNAM u32 flags, PNAM priority/ducking as two u16,
  WNAM float fade duration, TNAM packed four-byte MUST links.
- FSTP lines 8686–8690: DATA is an IPDS link, ANAM a string tag.
- FSTS lines 8692–8718: XCNT has five u32 counts (walking through swimming),
  while DATA stores matching packed FSTP links in reverse category order.
- ACTI lines 5648–5649: SNAM/VNAM are looping/activation SNDR links.

The 2026-10-04 independent installed-data survey measured 7,427 SNDR,
339 SOUN, 134 MUSC, 175 FSTP and 42 FSTS versions across all seven masters.
SNDR kinds: 6,730 Standard, 673 Compound, 24 AutoWeapon. Every Standard
BNAM is six bytes, every AutoWeapon BNAM four, and Compound has none.
LNAM is four bytes on 6,730 records. All 1,742 DNAM occurrences are four
bytes. The 24 auto-weapon records have 120 empty ITMS/ITME markers and
120 four-byte INTV values. SOUN REPT has 66 nine-byte and three eight-byte
variants. No Bethesda payloads are retained in the survey output.

Runtime descriptor selection, category mixing, alternates, loop execution,
side-chain/rumble semantics and attenuation scaling remain unverified. Readers
expose the stored fields and links; they do not choose a playback file or invent
an audio engine. Unknown descriptor kinds/sizes fail closed and all untouched
bytes remain in the original container.
The remaining runtime questions are tracked in
[#399](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/399).

The native environment read functions are in `environment/sound.hpp`; the
separate `c_api_sound.h` exposes owned descriptor/music/footstep-set arrays and
fixed marker/footstep views. The generator reads that header and C# copies the
arrays before releasing their holders. ABI 9 was selected after checking main
at 4, #383's reserved 5/6/7, and #398's 8. These independent PRs need ABI and
generated-output reconciliation before merging.

`test_sound_real.cpp` visits every sound-record version through both native
and C ABI getters, then compares untouched serialization with the original
master bytes. It skips cleanly with game data unset. Native synthetic cases
exercise union selection, malformed groups, optional tails and reversed
category ordering; managed getter tests cover all five record families.
Following the requested CI-only validation workflow, no completed local
sound build/test result is claimed. Exact-head CI results are reported on the
PR when available.

GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` has no SOUN/SNDR
consumer or marker-to-descriptor conversion path. Record vocabulary and generic
reference discovery are present; generic Godot audio commands do not consume
these forms. Its #144 audio runtime must supply that path before the requested
GardenEngine probe can exercise it.
