#!/usr/bin/env python3
"""validate_cell_save.py — cross-check CELL subrecords in a .esm against doc 23 SaveRecord contract.

Parses every CELL record in the given .esm (handling zlib compression),
collects the subrecord inventory, and classifies each 4CC against the
known save paths:

  SaveRecord (CELL)   — directly written by FUN_140d44ba0
  vt+0x1b0 (TESForm)  — shared TESForm base save virtual
  LoadRecord ONLY     — read by FUN_140d146d0 but not written by SaveRecord
  UNKNOWN             — not in the known dispatch

Reports:
  - Full inventory table with INT/EXT counts
  - Subrecords in real files but unaccounted in the save path
  - SaveRecord subrecords never seen in any real cell
  - Coverage percentages

Usage:
  python3 validate_cell_save.py Fallout4.esm
  python3 validate_cell_save.py --json Fallout4.esm > report.json
"""

import struct, zlib, sys, json
from collections import Counter

# ── Known save dispatch from CKRE doc 23 §10a ──

SAVE_RECORD_CELL = {
    # Directly written by CELL::SaveRecord (FUN_140d44ba0)
    # Exterior path: VISI, RVIS, PCMB, DATA, XCLC, CNAM, MHDT, LTMP, XCLW
    # Interior path: VISI, RVIS, PCMB, DATA, XCLL, LTMP, XCLW (+ XEZN, XOWN, XILW, etc.)
    'VISI', 'RVIS', 'PCMB', 'DATA',               # core cell data
    'XCLC', 'XCLL',                                # coords (ext) / lighting (int)
    'CNAM', 'MHDT', 'LTMP', 'XCLW',                # flags/template/water
    'XCLR', 'XCLP', 'XCLS',                        # region/lighting regions
    'XOWN', 'XILW',                                # ownership/water
    'XCCM', 'XCMO', 'XCAS',                        # climate/sky
    'XEZN', 'XCRI', 'XPRI',                        # encounter zone / crime / priority
    'XLCN', 'XCIM', 'XWEM',                        # location / imagespace / water
    'FULL', 'EDID',                                # name / editor ID
    'XCWT',                                        # cell water type (FO4 — discovered 2026-08-22)
}

SAVE_TESFORM_BASE = {
    # Written by shared TESForm::vt+0x1b0 save virtual (tesform.cpp)
    'VMAD', 'OBND', 'OBLT', 'OBTE', 'OBML',
    'PTRN', 'STCP',
}

# Subrecords dispatched through subclass save virtuals (vt+0x3b8, vt+0x3c0)
SAVE_SUBCLASS = {
    'XAPD', 'XAPR',  # parent references
    'XCZA', 'XCZC', 'XCZR',  # occlusion
    'XWCN', 'XWCS',  # water current
}

LOADRECORD_ONLY = {
    # Read by LoadRecord but NOT in any CELL save path (handled by children/REFRs)
    'LNAM', 'RNAM', 'TVDT', 'UVDT',                   # navmesh/previs links
    'XACR', 'XACT', 'XALP', 'XAMC', 'XAMT',           # actor/placement data
    'XATP', 'XATR',
    'XBSD',                                              # blood splatter
    'XCET', 'XCGD', 'XCHG',                            # encounter/grid data
    'XCMT', 'XCNT', 'XCPD',                            # music/encounter/pack
    'XCVR',                                              # cover
    'PBTE', 'TTCP',                                      # parent/target
    'SVIS',                                              # sub-visibility
}

ALL_KNOWN = SAVE_RECORD_CELL | SAVE_TESFORM_BASE | SAVE_SUBCLASS | LOADRECORD_ONLY


def parse_cells(esm_path):
    """Parse all CELL records from an .esm file. Returns list of dicts."""
    data = open(esm_path, "rb").read()
    cells = []
    pos = 0

    while pos < len(data) - 24:
        if data[pos:pos+4] == b'CELL':
            rec_size = struct.unpack('<I', data[pos+4:pos+8])[0]
            rec_flags = struct.unpack('<I', data[pos+8:pos+12])[0]
            rec_formid = struct.unpack('<I', data[pos+12:pos+16])[0]

            if 0 < rec_size < 0x100000 and rec_size < len(data) - pos - 24:
                is_ext = bool(rec_flags & 2)
                is_comp = bool(rec_flags & 0x40000)

                if is_comp:
                    try:
                        payload = zlib.decompress(data[pos+28:pos+24+rec_size])
                    except Exception:
                        pos += 24 + rec_size
                        continue
                else:
                    payload = data[pos+24:pos+24+rec_size]

                subrecs = []
                sp = 0
                while sp < len(payload) - 6:
                    st = payload[sp:sp+4].decode('ascii', 'ignore')
                    sz = struct.unpack('<H', payload[sp+4:sp+6])[0]
                    if st == 'XXXX' and sz == 4:
                        subrecs.append('XXXX')
                        sp += 10
                    else:
                        subrecs.append(st)
                        sp += 6 + sz

                cells.append({
                    'formID': rec_formid,
                    'flags': rec_flags,
                    'exterior': is_ext,
                    'compressed': is_comp,
                    'subs': subrecs,
                })

            pos += 24 + rec_size
        else:
            pos += 1

    return cells


def classify(subrecord):
    if subrecord in SAVE_RECORD_CELL:
        return "SaveRecord (CELL)"
    if subrecord in SAVE_TESFORM_BASE:
        return "vt+0x1b0 (TESForm)"
    if subrecord in SAVE_SUBCLASS:
        return "Subclass virtual"
    if subrecord in LOADRECORD_ONLY:
        return "LoadRecord ONLY"
    return "UNKNOWN"


def main():
    json_out = '--json' in sys.argv
    args = [a for a in sys.argv[1:] if a != '--json']

    if not args:
        print(__doc__)
        sys.exit(1)

    esm_path = args[0]
    cells = parse_cells(esm_path)

    ext_cells = [c for c in cells if c['exterior']]
    int_cells = [c for c in cells if not c['exterior']]

    # Aggregate unique subrecords
    all_counter = Counter()
    ext_counter = Counter()
    int_counter = Counter()
    for c in cells:
        all_counter.update(set(c['subs']))
    for c in ext_cells:
        ext_counter.update(set(c['subs']))
    for c in int_cells:
        int_counter.update(set(c['subs']))

    real_set = set(all_counter.keys())
    unknowns = real_set - ALL_KNOWN
    missing = SAVE_RECORD_CELL - real_set

    if json_out:
        report = {
            'total_cells': len(cells),
            'exterior': len(ext_cells),
            'interior': len(int_cells),
            'unique_subrecords': len(real_set),
            'inventory': [
                {
                    'subrecord': s,
                    'interior_count': int_counter.get(s, 0),
                    'exterior_count': ext_counter.get(s, 0),
                    'total_count': all_counter[s],
                    'save_path': classify(s),
                }
                for s in sorted(all_counter, key=lambda x: -all_counter[x])
            ],
            'unknowns': sorted(unknowns),
            'missing_from_save': sorted(missing),
            'coverage': {
                'real_in_save': len(real_set & SAVE_RECORD_CELL),
                'real_in_tesform': len(real_set & SAVE_TESFORM_BASE),
                'real_in_loadonly': len(real_set & LOADRECORD_ONLY),
                'real_in_unknown': len(unknowns),
                'save_not_in_real': len(missing),
            },
        }
        print(json.dumps(report, indent=2))
        return

    print(f"Parsed {len(cells)} CELL records ({len(ext_cells)} exterior, {len(int_cells)} interior)")
    print(f"\n{'4CC':6s} {'INT':>6s} {'EXT':>6s} {'ALL':>6s} {'SavePath':>24s}")
    print("-" * 60)
    for st, count in sorted(all_counter.items(), key=lambda x: -x[1]):
        ic = int_counter.get(st, 0)
        ec = ext_counter.get(st, 0)
        print(f"{st:6s} {ic:>6d} {ec:>6d} {count:>6d} {classify(st):>24s}")

    print(f"\n=== Discrepancies ===")
    if unknowns:
        print(f"UNKNOWN (in real cells, not in dispatch): {sorted(unknowns)}")
    else:
        print("None — all subrecords accounted for.")

    if missing:
        print(f"MISSING (in SaveRecord but not in real cells): {sorted(missing)}")
    else:
        print("None — all SaveRecord subrecords present in real cells.")

    print(f"\n=== Coverage ===")
    print(f"Real subrecords:     {len(real_set)}")
    print(f"In SaveRecord:       {len(real_set & SAVE_RECORD_CELL)}")
    print(f"In TESForm base:     {len(real_set & SAVE_TESFORM_BASE)}")
    print(f"LoadRecord only:     {len(real_set & LOADRECORD_ONLY)}")
    print(f"Unknown:             {len(unknowns)}")


if __name__ == '__main__':
    main()