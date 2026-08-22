OBTE/OBTS object-template fixtures
==================================

Real Fallout4.esm template blocks for the doc-26 / xEdit OBTSReq layout,
committed so the --obts oracle mode is reproducible without the game files.

Use with:

    python3 validate_ckini.py --obts tools/samples/obts/payloads
    python3 validate_ckini.py --obts tools/samples/obts/bad

Each .bin is a serialized subrecord stream starting with OBTE and ending with
STOP, in the on-disk form `[4CC tag][u16 size][payload]` (the same encoding a
TES4 record body uses):

  OBTE  4 B  u32 combination count (== number of OBTS bodies)
  [per combination, markers optional and record-type dependent:]
    OBTF  0 B  empty marker (may be absent; ARMO sometimes emits it between)
    FULL  4 B  localized-string ID (present on NPC_/WEAP, ABSENT on ARMO)
    OBTS  var  template body (see layout below)
  STOP  0 B  terminator

OBTS body (BGSMod::Template block, bgsmod_template.cpp):

  offset  size  field
  0x00    4     u32 Include Count
  0x04    4     u32 Property Count
  0x08    1     u8  Level Min
  0x09    1     u8  (unused)
  0x0A    1     u8  Level Max
  0x0B    1     u8  (unused)
  0x0C    2     s16 Addon Index
  0x0E    1     u8  Default
  0x0F    1     u8  Keyword Count        (u8, NOT u32 -- verified 665/665)
  0x10   4*K   u32 keyword formIDs
  0x10+4K     1  u8  Min Level For Ranks
  0x11+4K     1  u8  Alt Levels Per Tier
  0x12+4K     7*I include rows { u32 OMOD formID; u8 AttachPoint;
                                 u8 Optional; u8 DontUseAll }
  ...         24*P property rows { u8 ValueType; u8[3] unused;
                                   u8 FunctionType; u8[3] unused;
                                   u16 Property; u16 unused;
                                   u32 Value1; u32 Value2; f32 Step }

Keyword-count width is the single divergence from xEdit's ``-4`` array marker
(real files use a u8 at body+0x0F); the 7-byte include and 24-byte property
rows match xEdit's DefTemplate exactly.

Exercise coverage:

payloads/
  ARMO_00225A5E_minimal.bin   single combo, 18-byte body (0 includes,
                              0 properties, 0 keywords) -- smallest shape
  ARMO_0022DA6A_props.bin     single combo with a 24-byte property row
  armo_0024A400_6combos.bin   6 combos, NO FULL subrecords (ARMO shape),
                              keyword-free 50-byte bodies
  WEAP_0009983B_kw2.bin       23 combos with FULL, includes + the only
                              kw=2 body in the master, addon-index sweep
  npc_002216CC_16combos.bin   16 combos with FULL, mixed bodies: kw=0/1,
                              inc 1..20, props, attach-point variety

bad/  (all must FAIL -- regression negatives for the oracle)
  obte-count-mismatch.bin     OBTE=2 but only 1 OBTS combo
  missing-stop.bin            block without the STOP terminator
  obts-truncated-header.bin   OBTS body shorter than the 16-byte header
  keyword-overrun.bin         kw_count=5 with only an 18-byte body
  include-overrun.bin         inc=10 with body space for one include row
