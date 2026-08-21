// FormID slots inside mixed-type subrecords. Offset is the FormID start.
// min_size is the smallest payload that still contains that slot.
// CNTO: FormID item at 0, int32 count at 4. xEdit wbStructExSK(CNTO).
{0x4F544E43u, 0, 8},   // CNTO
// LVLO: uint16 level, uint16 unused, FormID at 4, uint16 count, uint8 chance, pad.
{0x4F4C564Cu, 4, 8},   // LVLO
// XTEL: destination door FormID at 0, then 6 floats (and optional flags).
{0x4C455458u, 0, 4},   // XTEL
// XLOC: uint8 lock level, 3 pad, key FormID at 4, flags.
{0x434F4C58u, 4, 8},   // XLOC
// XESP: enable-parent FormID at 0, flags at 4.
{0x50534558u, 0, 8},   // XESP
// COED: owner FormID at 0, rank/global union, item condition float.
{0x44454F43u, 0, 4},   // COED
// DSTD: health/index/stage/flags, self-damage, explosion FormID at 8, debris at 12.
{0x44545344u, 8, 12},  // DSTD explosion
{0x44545344u, 12, 16}, // DSTD debris
// XNDP: navmesh FormID at 0, int16 triangle, pad.
{0x50444E58u, 0, 4},   // XNDP
