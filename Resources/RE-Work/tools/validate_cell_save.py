#!/usr/bin/env python3
"""validate_cell_save.py - cross-check CELL subrecords in a .esm against the
doc 23 SaveRecord contract.

Walks the GRUP tree strictly (plugin_parse.py) instead of scanning for
record magic. The previous byte-scan walkers were unsound two ways:

  * tools/ variant: advanced past ANY 'CELL' match by its unvalidated size
    field; one garbage match threw the cursor past EOF and ended the scan
    ~7% into Fallout4.esm.
  * ghidra-dump variant: stepped the file on a fixed 4-byte stride believing
    record headers are 4-aligned. FO4 records are unaligned (subrecord =
    6+len bytes), so it saw only about 25% of headers. Doc 26 section 3's
    "9,741 exterior cells" was that artifact.

Buckets: SaveRecord (CELL) = FUN_140d44ba0; vt+0x1b0 = TESForm base save;
subclass virtuals vt+0x3b8/vt+0x3c0; LoadRecord ONLY = FUN_140d146d0.

XCLC audit (doc 26 section 3): [0:4] s32 gridX, [4:8] s32 gridY,
[8] u8 Force-Hide-Land (low nibble = Quad 1..4 hidden; high nibble NOT
validated because vanilla masters carry stray values there),
[9:12] three bytes of uninitialised padding the CK never clears (reused
heap allocation; measured as evidence, never failed).

Interior/exterior: CELL DATA bit 0 marks interior cells (2-byte payload,
perfect anti-correlation with XCLC presence on shipped masters). Record
flag bit 0 - TES5's "Is Interior Cell" - is never set in FO4 files, and
flag bit 1 is "has water", not exterior.

Usage:
  python3 validate_cell_save.py Fallout4.esm
  python3 validate_cell_save.py --json Fallout4.esm > report.json
"""

import json
import struct
import sys
from collections import Counter

from plugin_parse import Plugin

# Known save dispatch from CKRE doc 23 section 10a

SAVE_RECORD_CELL = {
    'VISI', 'RVIS', 'PCMB', 'DATA',
    'XCLC', 'XCLL',
    'CNAM', 'MHDT', 'LTMP', 'XCLW',
    'XCLR', 'XCLP', 'XCLS',
    'XOWN', 'XILW',
    'XCCM', 'XCMO', 'XCAS',
    'XEZN', 'XCRI', 'XPRI',
    'XLCN', 'XCIM', 'XWEM',
    'FULL', 'EDID',
    'XCWT',
    'XRNK', 'XILL', 'XGDR',
}

SAVE_TESFORM_BASE = {
    'VMAD', 'OBND', 'OBLT', 'OBTE', 'OBML',
    'PTRN', 'STCP',
}

SAVE_SUBCLASS = {
    'XAPD', 'XAPR',
    'XCZA', 'XCZC', 'XCZR',
    'XWCN', 'XWCS',
}

LOADRECORD_ONLY = {
    'LNAM', 'RNAM', 'TVDT', 'UVDT',
    'XACR', 'XACT', 'XALP', 'XAMC', 'XAMT',
    'XATP', 'XATR',
    'XBSD',
    'XCET', 'XCGD', 'XCHG',
    'XCMT', 'XCNT', 'XCPD',
    'XCVR',
    'PBTE', 'TTCP',
    'SVIS',
}

ALL_KNOWN = SAVE_RECORD_CELL | SAVE_TESFORM_BASE | SAVE_SUBCLASS | LOADRECORD_ONLY

XCLC_QUAD_NAMES = {1: 'Quad 1', 2: 'Quad 2', 4: 'Quad 3', 8: 'Quad 4'}


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


def collect_cells(esm_path):
    """Strict-walk the plugin; return a list of cell dicts."""
    plugin = Plugin(esm_path)
    plugin.load(index_subrecords_for=('CELL',))
    cells = []
    for rec in plugin.by_type('CELL'):
        subs = rec.index_subs()
        smap = {}
        for s in subs:
            smap.setdefault(s.type, []).append(s)
        body = rec.body()
        data_interior = None
        dlist = smap.get('DATA')
        if dlist:
            off = dlist[0].data_offset
            payload = body[off:off + dlist[0].size]
            if payload:
                data_interior = bool(payload[0] & 1)
        xclc = None
        xlist = smap.get('XCLC')
        if xlist:
            off = xlist[0].data_offset
            xclc = body[off:off + xlist[0].size]
        cells.append({
            'formID': rec.form_id,
            'flags': rec.flags,
            'compressed': rec.compressed,
            'interior': data_interior,
            'has_xclc': 'XCLC' in smap,
            'subs': [s.type for s in subs],
            'xclc': xclc,
        })
    plugin.close()
    return cells


def audit_xclc(cells):
    """Byte 8 low nibble is the only flag content; bytes 9-11 are padding."""
    issues = []
    quad_hist = Counter()
    b8_hist = Counter()
    pad_hist = Counter()
    len_hist = Counter()
    ext_with_xclc = 0
    int_with_xclc = 0
    mismatches = []
    for c in cells:
        has = c['has_xclc']
        interior = c['interior']
        if interior is not None and has != (not interior):
            mismatches.append(c['formID'])
        if not has:
            continue
        if interior:
            int_with_xclc += 1
            issues.append(('INTERIOR-XCLC', c['formID'],
                           'XCLC on interior cell'))
        else:
            ext_with_xclc += 1
        p = c['xclc']
        len_hist[len(p)] += 1
        if len(p) != 12:
            issues.append(('BAD-LENGTH', c['formID'],
                           'XCLC len=%d (expected 12)' % len(p)))
            continue
        quad_hist[p[8] & 0xF] += 1
        b8_hist[p[8]] += 1
        pad_hist[bytes(p[9:12])] += 1
    ok = not issues
    stats = {
        'exterior_with_xclc': ext_with_xclc,
        'interior_with_xclc': int_with_xclc,
        'data_bit0_vs_xclc_mismatches': mismatches[:20],
        'quad_histogram': {'0x%X' % k: v
                           for k, v in sorted(quad_hist.items())},
        'byte8_histogram': {'0x%02X' % k: v
                            for k, v in sorted(b8_hist.items())},
        'padding_distinct_values': len(pad_hist),
        'padding_top_values': [('0x' + k.hex(), v)
                               for k, v in pad_hist.most_common(5)],
        'length_histogram': dict(sorted(len_hist.items())),
    }
    return ok, issues, stats


def format_xclc_audit(ok, issues, stats):
    lines = ["=== XCLC Force-Hide-Land audit (doc 26 section 3) ==="]
    lines.append("Exterior cells with XCLC: %d" % stats['exterior_with_xclc'])
    lines.append("Interior cells with XCLC: %d" % stats['interior_with_xclc'])
    if stats['data_bit0_vs_xclc_mismatches']:
        lines.append("DATA-bit0 vs XCLC mismatches: %s"
                     % ", ".join("%08X" % f
                                 for f in stats['data_bit0_vs_xclc_mismatches']))
    if stats['length_histogram']:
        lines.append("Payload lengths: " + ", ".join(
            "%dB x%d" % (k, v)
            for k, v in sorted(stats['length_histogram'].items())))
    lines.append("Force-Hide-Land byte 8 values:")
    for k, v in stats['byte8_histogram'].items():
        q = int(k, 16) & 0xF
        names = [XCLC_QUAD_NAMES[1 << i]
                 for i in range(4) if q & (1 << i)] or ['none']
        note = "" if int(k, 16) == q else "  (high nibble set: padding)"
        lines.append("    %s x%-6d (%s)%s" % (k, v, ", ".join(names), note))
    lines.append("Bytes 9-11 (uninitialised padding): %d distinct values; top: %s"
                 % (stats['padding_distinct_values'],
                    ", ".join("%s x%d" % (k, v)
                              for k, v in stats['padding_top_values'])))
    if not ok:
        lines.append("XCLC ISSUES:")
        for kind, fid, detail in issues:
            lines.append("  %-16s CELL %08X: %s" % (kind, fid, detail))
    else:
        lines.append("XCLC: all payloads valid (low-nibble quads, exterior-only)")
    return lines


def main():
    json_out = '--json' in sys.argv
    args = [a for a in sys.argv[1:] if a != '--json']

    if not args:
        print(__doc__)
        sys.exit(1)

    esm_path = args[0]
    cells = collect_cells(esm_path)

    ext_cells = [c for c in cells if c['interior'] is False]
    int_cells = [c for c in cells if c['interior'] is True]

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
    comp_ext = sum(1 for c in ext_cells if c['compressed'])
    comp_int = sum(1 for c in int_cells if c['compressed'])

    xclc_ok, xclc_issues, xclc_stats = audit_xclc(cells)

    if json_out:
        report = {
            'total_cells': len(cells),
            'exterior': len(ext_cells),
            'interior': len(int_cells),
            'compressed_exterior': comp_ext,
            'compressed_interior': comp_int,
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
                'issues': [{'kind': k, 'formID': '0x%08X' % f, 'detail': d}
                           for k, f, d in xclc_issues],
                'stats': xclc_stats,
            },
        }
        print(json.dumps(report, indent=2))
        return

    print(f"Parsed {len(cells)} CELL records "
          f"({len(ext_cells)} exterior, {len(int_cells)} interior); "
          f"compressed {comp_ext} ext / {comp_int} int")
    print(f"\n{'4CC':6s} {'INT':>7s} {'EXT':>7s} {'ALL':>7s} {'SavePath':>24s}")
    print("-" * 60)
    for st, count in sorted(all_counter.items(), key=lambda x: -x[1]):
        print(f"{st:6s} {int_counter.get(st, 0):>7d} "
              f"{ext_counter.get(st, 0):>7d} {count:>7d} {classify(st):>24s}")

    print("\n=== Discrepancies ===")
    if unknowns:
        print(f"UNKNOWN (in real cells, not in dispatch): {sorted(unknowns)}")
    else:
        print("None -- all subrecords accounted for.")
    if missing:
        print(f"MISSING (in SaveRecord but not in real cells): {sorted(missing)}")
    else:
        print("None -- all SaveRecord subrecords present in real cells.")

    print("\n=== Coverage ===")
    print(f"Real subrecords:     {len(real_set)}")
    print(f"In SaveRecord:       {len(real_set & SAVE_RECORD_CELL)}")
    print(f"In TESForm base:     {len(real_set & SAVE_TESFORM_BASE)}")
    print(f"LoadRecord only:     {len(real_set & LOADRECORD_ONLY)}")
    print(f"Unknown:             {len(unknowns)}")

    print("\n" + "\n".join(format_xclc_audit(xclc_ok, xclc_issues, xclc_stats)))

    if xclc_ok:
        print("\nXCLC VERDICT: PASS")
    else:
        print("\nXCLC VERDICT: FAIL (%d issue(s))" % len(xclc_issues))


if __name__ == '__main__':
    main()
