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
  - XCLC Force-Hide-Land audit (doc 26 §3): quad flags 0-3 only,
    allowed values, exterior-only rule, and the payload byte layout

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
    'XRNK', 'XILL', 'XGDR',                        # owner rank / lock list / god rays
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


def parse_subrecord_stream(payload):
    """Parse a record body into (subrecord_tags, xclc_payload, data_flags).

    Returns None if the subrecord walk does not land exactly on the record
    end (false b'CELL' matches inside compressed payloads or GRUP data are
    rejected this way). data_flags is byte 0 of the DATA subrecord (FO4:
    2-byte payload; bit 0 = Interior per xEdit wbDefinitionsFO4).
    """
    subrecs = []
    xclc = None
    data_flags = None
    sp = 0
    while sp < len(payload) - 6:
        st = payload[sp:sp+4].decode('ascii', 'ignore')
        sz = struct.unpack('<H', payload[sp+4:sp+6])[0]
        if st == 'XXXX' and sz == 4:
            subrecs.append('XXXX')
            sp += 10
        else:
            subrecs.append(st)
            if st == 'XCLC':
                xclc = payload[sp+6:sp+6+sz]
            elif st == 'DATA' and sz >= 1:
                data_flags = payload[sp+6]
            sp += 6 + sz
    if sp != len(payload):
        return None
    return subrecs, xclc, data_flags


def parse_cells(esm_path):
    """Parse all CELL records from an .esm file. Returns list of dicts.

    Byte-steps every 4-aligned offset (record headers are 4-aligned in the
    file) and accepts a record only when its subrecord stream parses cleanly
    to EOF. A record is exterior when DATA bit 0 (Interior) is clear;
    XCLC presence is a perfect cross-check (0 mismatches on Fallout4.esm).
    """
    data = open(esm_path, "rb").read()
    cells = []
    pos = 0

    while pos < len(data) - 24:
        if data[pos:pos+4] == b'CELL':
            rec_size = struct.unpack('<I', data[pos+4:pos+8])[0]
            rec_flags = struct.unpack('<I', data[pos+8:pos+12])[0]
            rec_formid = struct.unpack('<I', data[pos+12:pos+16])[0]

            if 0 < rec_size < 0x1000000 and rec_size < len(data) - pos - 24:
                is_comp = bool(rec_flags & 0x40000)

                if is_comp:
                    try:
                        payload = zlib.decompress(data[pos+28:pos+24+rec_size])
                    except Exception:
                        pos += 4
                        continue
                else:
                    payload = data[pos+24:pos+24+rec_size]

                parsed = parse_subrecord_stream(payload)
                if parsed is None:
                    pos += 4
                    continue
                subrecs, xclc, data_flags = parsed

                cells.append({
                    'formID': rec_formid,
                    'flags': rec_flags,
                    'exterior': (data_flags is not None
                                 and (data_flags & 1) == 0),
                    'compressed': is_comp,
                    'subs': subrecs,
                    'xclc': xclc,
                })

        pos += 4

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


# ── XCLC Force-Hide-Land audit (doc 26 §3, xEdit wbDefinitionsFO4) ──
#
# XCLC is exactly 12 bytes on FO4 exterior CELL records:
#   [0:4]  s32 gridX
#   [4:8]  s32 gridY
#   [8]    u8  Force-Hide-Land byte; low nibble = Quad 1..4 hidden
#   [9:12] u8[3] padding -- the CK reader (FUN_140d146d0) stores byte 8
#          into cell+0x1E and skips these 3 bytes; SaveRecord
#          (FUN_140d44ba0) writes the same 12-byte shape.
#
# NOTE: xEdit displays the trailing 4 bytes as a single u32 with
# wbFlags(Quad 1..4), but the real payloads carry nonzero bytes 9-11
# (9,725/9,741 cells on Fallout4.esm), so only byte 8 is meaningful --
# the CK never reads the upper three.
#
# Rules checked:
#   * XCLC only on exterior cells (never interior)
#   * payload length exactly 12
#   * byte 8 low nibble only: high nibble must be zero (value 0x00-0x0F)
#   * distribution of quad values reported; vanilla master is 0x0 or 0xF

XCLC_QUAD_NAMES = {1: 'Quad 1', 2: 'Quad 2', 4: 'Quad 3', 8: 'Quad 4'}


def audit_xclc(cells):
    """Audit XCLC payloads; return (ok, issues, stats).

    issues: list of (kind, formID, detail) tuples.
    stats: dict of quad-value -> count, plus lengths and placement.
    """
    issues = []
    quad_hist = Counter()
    len_hist = Counter()
    ext_with_xclc = 0
    int_with_xclc = 0
    int_xclc_ids = []
    for c in cells:
        if c['xclc'] is None:
            continue
        if c['exterior']:
            ext_with_xclc += 1
        else:
            int_with_xclc += 1
            int_xclc_ids.append(c['formID'])
        p = c['xclc']
        len_hist[len(p)] += 1
        if len(p) != 12:
            issues.append(('BAD-LENGTH', c['formID'],
                           'XCLC len=%d (expected 12)' % len(p)))
            continue
        if not c['exterior']:
            issues.append(('INTERIOR-XCLC', c['formID'],
                           'XCLC on interior cell (exterior-only rule)'))
        grid_x, grid_y = struct.unpack_from('<ii', p, 0)
        b8 = p[8]
        quad = b8 & 0xF
        quad_hist[quad] += 1
        if b8 & 0xF0:
            issues.append(('BAD-FLAGS', c['formID'],
                           'Force-Hide-Land byte 0x%02X has bits above '
                           'quad 3 (grid %d,%d)' % (b8, grid_x, grid_y)))
    ok = not issues
    stats = {
        'exterior_with_xclc': ext_with_xclc,
        'interior_with_xclc': int_with_xclc,
        'interior_xclc_ids': int_xclc_ids,
        'quad_histogram': {('0x%X' % k): v
                           for k, v in sorted(quad_hist.items())},
        'length_histogram': dict(sorted(len_hist.items())),
    }
    return ok, issues, stats


def format_xclc_audit(ok, issues, stats):
    """Human-readable XCLC audit block."""
    lines = ["=== XCLC Force-Hide-Land audit (doc 26 §3) ==="]
    lines.append("Exterior cells with XCLC: %d" %
                 stats['exterior_with_xclc'])
    lines.append("Interior cells with XCLC: %d" %
                 stats['interior_with_xclc'])
    if stats['length_histogram']:
        lines.append("Payload lengths: " + ", ".join(
            "%dB x%d" % (k, v)
            for k, v in sorted(stats['length_histogram'].items())))
    if stats['quad_histogram']:
        lines.append("Force-Hide-Land quad values:")
        for k, v in stats['quad_histogram'].items():
            q = int(k, 16)
            names = [XCLC_QUAD_NAMES[1 << i]
                     for i in range(4) if q & (1 << i)] or ['none']
            lines.append("    %s x%d  (%s)" % (k, v, ", ".join(names)))
    if not ok:
        lines.append("XCLC ISSUES:")
        for kind, fid, detail in issues:
            lines.append("  %-16s CELL %08X: %s" % (kind, fid, detail))
    else:
        lines.append("XCLC: all payloads valid (quad bits only, exterior-only)")
    return lines


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

    xclc_ok, xclc_issues, xclc_stats = audit_xclc(cells)

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
            'xclc': {
                'valid': xclc_ok,
                'issues': [{'kind': k, 'formID': '0x%08X' % f,
                            'detail': d}
                           for k, f, d in xclc_issues],
                'stats': xclc_stats,
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

    print("\n" + "\n".join(format_xclc_audit(xclc_ok, xclc_issues,
                                        xclc_stats)))

    if xclc_ok:
        print("\nXCLC VERDICT: PASS")
    else:
        print("\nXCLC VERDICT: FAIL (%d issue(s))" % len(xclc_issues))


if __name__ == '__main__':
    main()