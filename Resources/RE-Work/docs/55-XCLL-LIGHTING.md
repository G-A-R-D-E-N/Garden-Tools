# 55 - XCLL interior-lighting block

Measured over 702+579+10 records (Fallout4 + Coast). Three on-disk
sizes: **136 B (modern majority)**, 92 B (legacy subset), 128 B (rare).
All floats sane across every record; no uninitialized garbage anywhere.

## 1. Field map (136-B form)

Offsets are per-u32; "color" = little-endian RGBX (low 3 bytes, high
byte observed 0x00):

| offset | type | observed | inference |
|---|---|---|---|
| +000 | color | varies (e.g. 0x10181b) | ambient color |
| +004 | u32 | 12 uniq, mostly 0 | ambient spare |
| +008 | color | varies (grays) | directional/fog color A |
| +012 | f32 | ~450 typical | dimmer/fade |
| +016 | f32 | ~2000 typical | fog far |
| +020..+032 | mixed | few values | near-fog params |
| +036 | f32 | 0.7 common | falloff exponent |
| +040..+063 | 3× (color,color) | paired dark/light | fog/color pairs (near,far) ×3 |
| +064 | u32 | always 0 | reserved |
| +068 | f32 | always 1.0 | scale |
| +072 | color | varies | another stage color |
| +076 | f32 | 0.7 | exponent copy |
| +080 | u32 | 0x7ff seen | bitfield |
| +084 | u32 | 0 | reserved |
| +088 | u16/u32 | small (1248) | near distance |
| +092..+095 | - | - | (only 136-B) |
| +096 | f32 | 10000 | far plane |
| +100/+104 | color | varies | far-stage colors |
| +108..+124 | 4× f32 | all 1.0 | unit scales |
| +128 | u32 | 0 | reserved |
| +132 | f32 | 10000 | far copy |

Fields marked *inference* are named from value behavior; positions are
exact measurements.

## 2. Size variants

* The 92-B records are a prefix-compatible subset (same leading
  structure, no tail).
* 128-B records differ only in the final section.
* Writers targeting parity with shipped content should emit 136 B;
  readers must accept all three.

## 3. Validation

Every one of the 34 u32 lanes holds sane values across all sampled
records (floats within ±10^6, no denormal garbage) - the block is fully
initialized by the writer, unlike e.g. VTXT's middle lane.
