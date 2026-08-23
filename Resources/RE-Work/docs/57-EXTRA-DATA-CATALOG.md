# 57 - Extra-data tag catalog (complete switch map)

Every case of `BSExtraDataList::Load` (`FUN_1407d8100`,
extradatalist.cpp), extracted mechanically from the uncapped
decompile and cross-referenced with shipped frequencies across all
four masters (REFR/ACHR/PHZD/PGRE/PMIS records).

Columns: **4CC** | extra-type ID written to obj+0x12 (hex) | alloc
size (obj bytes) | construction class (PDB-corroborated where named)
| on-disk read widths | vanilla record count.

Read-width legend: values are byte counts passed to the stream reader;
`4*` marks the version-gated 4-vs-1 dual read; `var` = size-prefixed.
Helper = first dedicated consumer called instead of inline parsing
(full grammar lives behind that call).

| 4CC | type | alloc | class | reads | helper | shipped |
|---|---|---|---|---|---|---:|
| XTNM | - | - | - | 4 | - | 52 |
| XRFG | - | - | - | 4 | - | 70585 |
| XCZC | - | - | - | 4,4 | - | 1 |
| XCZA | - | - | - | 4 | - | 2 |
| XLOC | - | 0x18 | - | 0x10 | - | 1795 |
| XSPC | - | - | - | 4 | - | 2 |
| XPSC | - | - | - | 4 | - | 0 |
| XFVC | - | - | - | - | - | 2 |
| XAMC | - | - | - | 4 | - | 5 |
| XIS2 | - | - | - | - | - | 663 |
| XPPA | - | - | - | - | - | 10176 |
| XRGB | - | - | - | 4,4 | - | 1130 |
| XLIB | - | - | - | 4,4 | - | 2554 |
| XGLB | - | - | - | 4 | - | 0 |
| XAPD | - | 0x30 | - | 4,1,0x10,4,0x20,0x18 | - | 4366 |
| XORD | - | 0x10 | - | 0x10,4,0x20,0x18 | - | 1 |
| XPRD | - | 0x28 | - | 4,0x20,0x18 | - | 10176 |
| XBSD | - | 0x30 | - | 0x20,0x18 | - | 9841 |
| XPOD | 0x73 | - | ExtraPortalRefData | 8 | - | 1747 |
| XPDD | 0xc0 | 0x20 | ExtraProjectedDecalData | 8 | - | 5402 |
| XSED | - | - | - | 4,1 | - | 0 |
| XCGD | 0xb4 | 0x28 | ExtraGIDBuffer | - | - | 0 |
| XRGD | - | - | - | 0xc | - | 12897 |
| XLOD | - | - | - | 0xc | - | 0 |
| XLCM | - | - | - | 4,4,4 | - | 7742 |
| XWEM | - | - | - | 4,4 | - | 0 |
| XCIM | - | - | - | 4,4 | - | 0 |
| XTIM | - | - | - | 4 | - | 0 |
| XCCM | - | - | - | 4 | - | 0 |
| XILL | - | - | - | 4 | - | 0 |
| XPSL | - | - | - | - | - | 0 |
| XPTL | - | - | - | 0xc | - | 0 |
| XCVL | - | - | - | 0xc | - | 5 |
| XTEL | - | 0x20 | - | - | - | 1207 |
| XTRI | - | 0x30 | - | 4,4 | - | 1378 |
| XPLK | - | 0x30 | - | 4 | - | 244 |
| XRNK | - | 0x20 | - | 4 | - | 1700 |
| XMRK | - | 0x20 | - | - | - | 415 |
| XPRI | - | - | - | 4 | - | 0 |
| XCHG | - | - | - | 4 | - | 0 |
| XLIG | 0x91 | - | ExtraLightData | 0x18 | - | 58542 |
| XWPG | - | - | - | - | - | 6 |
| XEMI | - | - | - | 4 | - | 27956 |
| XCRI | - | - | - | - | - | 0 |
| XLKR | - | - | - | 4,8 | - | 87139 |
| XCMO | - | - | - | 4 | - | 0 |
| XPSN | - | - | - | 4 | - | 0 |
| XPRM | - | - | - | 0x20 | - | 29614 |
| XRTM | - | - | - | 4 | - | 0 |
| XLCN | - | - | - | 4 | - | 837 |
| XWCN | - | 0x30 | - | 4 | - | 179 |
| XWCU | - | 0x30 | - | - | - | 179 |
| XWPN | - | - | - | - | - | 155 |
| XOWN | - | 0xc | - | 4,0x10 | - | 7340 |
| XEZN | - | 0xc | - | 4,0x10 | - | 4478 |
| XMBO | - | 0xc | - | 0x10 | - | 1847 |
| XRDO | - | - | - | 0x10 | - | 104 |
| XMSP | - | 0x30 | - | 4,4,0xc,4 | - | 238132 |
| XMBR | - | 0x30 | - | 4,0xc,4 | - | 978 |
| XACR | - | 0x30 | - | 0xc,4 | - | 0 |
| XGDR | - | - | - | 4 | - | 0 |
| XESP | - | - | - | 8 | - | 39099 |
| XOCP | - | - | ExtraNavMeshPortal | 8,2,8,4 | - | 50 |
| XNDP | - | - | ExtraNavMeshPortal | 8,2,8,4 | - | 1189 |
| XALP | - | - | - | 2,8,4 | - | 2825 |
| XCLP | - | - | - | 8,4 | - | 0 |
| XASP | - | - | - | 4 | - | 11 |
| XLRT | - | - | - | 4,4,4 | - | 9069 |
| XACT | - | - | - | 4,4,4 | - | 1072 |
| XLKT | - | - | - | 4,4 | - | 127 |
| XHLT | - | - | - | 4,4 | - | 2 |
| XCNT | - | - | - | 4 | - | 133 |
| XCWT | - | - | - | 4 | - | 0 |
| XILW | 0xcb | 0x30 | ExtraInteriorLODWorldspace | - | - | 0 |
| XHTW | - | - | - | 4 | - | 2 |
| XLTW | - | - | - | 4 | - | 0 |
| XRDS | - | - | - | 4 | - | 59288 |
| XCVR | - | 0x20 | - | 0xc,4,4,4,4,4 | - | 1 |
| XCLR | - | 0x20 | - | 4,4,4,4,4,8 | - | 0 |
| XRMR | - | 0x28 | - | 4,4,4,4,4,8 | - | 1106 |
| XHOR | - | 0x30 | - | 4,8,4 | - | 0 |
| XAPR | - | 0x30 | - | 8,4 | - | 4654 |
| XATR | - | - | - | 4 | - | 5677 |
| XPWR | - | - | - | 4,4,4,4 | - | 0 |
| XLYR | - | - | - | 4,4,4,4 | - | 526004 |
| XCZR | - | - | - | 4,4,4 | - | 1 |
| XCAS | - | - | - | 4 | - | 0 |

**Shipped:** 59 of 87 cases appear in vanilla (top:
XLYR(526004), XMSP(238132), XLKR(87139), XRFG(70585), XRDS(59288), XLIG(58542), XESP(39099), XPRM(29614)).
**Never ship** (28): XPSC XGLB XSED XCGD XLOD XWEM XCIM XTIM XCCM XILL XPSL XPTL XPRI XCHG XCRI XCMO XPSN XRTM XACR XGDR XCLP XCWT XILW XLTW XCLR XHOR XPWR XCAS

## Structural notes

* Every inline-constructed object writes its extra-type ID to
  +0x12 - the ID column doubles as the runtime `BSExtraData::GetType()`
  value (XCRI=0xC6, XLIG/XCHG family=0x91, XPOD=0x73, XPDD=0xc0,
  XSED/XCGD=0xb4, XOWN/XEZN/XMBO/XRDO/XLCN/XWCN/XWCU/XWPN family=0x21,
  XACT-family=0xcb).
* Tags with no alloc/type route into per-tag helper functions - those
  are complete mini-loaders (XLKR link table, XLYR layer stack, XMSP
  multi-sound, XPRM prompt, XRDS water damage, XCLR region list) and
  need their own passes for field-level maps.
* The three precombined tags (XWPG/XEMI/XCRI) share the
  ExtraCombinedRefs constructor (type 0xc6); XCRI's wire format is
  fully solved in doc 25 §6.
