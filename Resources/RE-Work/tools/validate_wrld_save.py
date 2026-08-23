#!/usr/bin/env python3
"""validate_wrld_save.py - cross-check WRLD subrecords in .esm/.esp plugins
against the doc 23 section 10b SaveRecord/LoadRecord contract.

Mirrors tools/validate_cell_save.py; walks plugins strictly via
tools/plugin_parse.py.

Buckets (doc 23 section 10b):

  SaveRecord (WRLD)   - written inline by FUN_140e54420 (vt slot 27)
  Iterator save       - CLSZ/OFST/RNAM arrays dispatched through the
                        vl+0x3b8/vl+0x3c0 iterators rather than inline
  Base-class save     - TESForm/TESFullName header virtuals
  LoadRecord ONLY     - read by FUN_140e4a750 but not written by any path

Extra audits:

  * DATA payload must be exactly 1 byte (FO4 split bounds/parent out of
    DATA); flag histogram reported.
  * CLSZ and OFST ship as parallel u32 arrays - per-record byte lengths
    must match (they do on all shipped masters).
  * WNAM parent formID histogram.
  * RNAM payloads start with a REGN formID; repetition count per worldspace.

Usage:
  python3 validate_wrld_save.py Fallout4.esm [more.esm ...]
  python3 validate_wrld_save.py --json Fallout4.esm
"""

import json
import struct
import sys
from collections import Counter

from plugin_parse import Plugin

SAVE_RECORD_WRLD = {
    'CNAM', 'DATA', 'DNAM', 'LTMP', 'MHDT', 'MNAM',
    'NAM0', 'NAM2', 'NAM3', 'NAM4', 'NAM9', 'NAMA',
    'ONAM', 'PNAM', 'WCTR', 'WLEV', 'WNAM', 'XEZN', 'XLCN', 'ZNAM',
    'NAM1', 'NAM5', 'NAM6', 'NAM7', 'NAM8',
}

SAVE_ITERATOR = {
    'CLSZ', 'OFST', 'RNAM',
}

SAVE_BASE = {
    'EDID', 'FULL', 'OBND', 'PTRN', 'STCP', 'VMAD',
}

LOADRECORD_ONLY = {
    'ATDM', 'BTDM', 'TNAM', 'UNAM', 'WMAD',
    'XWEM',   # water env cubemap path string; save path unproven
    'ICON',   # present in shipped masters; write path unproven
}

ALL_KNOWN = SAVE_RECORD_WRLD | SAVE_ITERATOR | SAVE_BASE | LOADRECORD_ONLY

WRLD_FLAG_NAMES = [
    (0x01, 'SmallWorld'),
    (0x02, 'CanNotFastTravel'),
    (0x04, 'NoLODWater'),
    (0x08, 'NoLODNoise'),
    (0x10, 'NoLandscape'),
    (0x20, 'NoSky'),
    (0x40, 'FixedDimensions'),
    (0x80, 'NoGrass'),
]


def classify(subrecord):
    if subrecord in SAVE_RECORD_WRLD:
        return "SaveRecord (WRLD)"
    if subrecord in SAVE_ITERATOR:
        return "Iterator save"
    if subrecord in SAVE_BASE:
        return "Base-class save"
    if subrecord in LOADRECORD_ONLY:
        return "LoadRecord ONLY"
    return "UNKNOWN"


def collect(path):
    plugin = Plugin(path)
    plugin.load(index_subrecords_for=('WRLD',))
    own = {r.form_id: r.type for r in plugin.records}
    wrlds = []
    for rec in plugin.by_type('WRLD'):
        subs = rec.index_subs()
        types = [s.type for s in subs]
        smap = {}
        for s in subs:
            smap.setdefault(s.type, []).append(s)
        body = rec.body()
        entry = {
            'formID': rec.form_id,
            'file': path,
            'compressed': rec.compressed,
            'types': types,
            'issues': [],
        }
        d = smap.get('DATA')
        if not d or d[0].size != 1:
            entry['issues'].append(('DATA-SIZE', len(d[0].size and [1]) and d[0].size if d else -1))
        else:
            off = d[0].data_offset
            entry['dataFlags'] = body[off]
        clsz = smap.get('CLSZ')
        ofst = smap.get('OFST')
        if clsz and ofst:
            if clsz[0].size != ofst[0].size:
                entry['issues'].append(('CLSZ-OFST-MISMATCH',
                                        '%d vs %d' % (clsz[0].size, ofst[0].size)))
            else:
                entry['mapArrayBytes'] = clsz[0].size
        wnam = smap.get('WNAM')
        if wnam:
            off = wnam[0].data_offset
            entry['parent'] = struct.unpack_from('<I', body, off)[0]
        rnam = smap.get('RNAM')
        if rnam:
            # Empirical shape (all shipped masters): [u16 ox][u16 oy][u32 n]
            # then n x ([u32 REFR formID][u16 cellX][u16 cellY]). The formIDs
            # are placed references of the owning file, never REGN ids.
            total_entries = 0
            bad_len = 0
            bad_id = 0
            for s in rnam:
                pay = body[s.data_offset:s.data_offset + s.size]
                if len(pay) < 8:
                    bad_len += 1
                    continue
                (n,) = struct.unpack_from('<I', pay, 4)
                if len(pay) != 8 + 8 * n:
                    bad_len += 1
                    continue
                total_entries += n
                for k in range(n):
                    fid = struct.unpack_from('<I', pay, 8 + 8 * k)[0]
                    if fid and fid not in own:
                        bad_id += 1
            entry['rnamCount'] = len(rnam)
            entry['rnamEntries'] = total_entries
            if bad_len:
                entry['issues'].append(('RNAM-BAD-SHAPE', bad_len))
            if bad_id:
                entry['issues'].append(('RNAM-UNRESOLVED-REFR', bad_id))
        wrlds.append(entry)
    plugin.close()
    return wrlds


def main():
    json_out = '--json' in sys.argv
    args = [a for a in sys.argv[1:] if a != '--json']
    if not args:
        print(__doc__)
        sys.exit(1)

    all_wrlds = []
    type_counter = Counter()
    flag_counter = Counter()
    parent_counter = Counter()
    for path in args:
        for w in collect(path):
            all_wrlds.append(w)
            seen = set(w['types'])
            type_counter.update(seen)
            if 'dataFlags' in w:
                flag_counter[w['dataFlags']] += 1
            if 'parent' in w:
                parent_counter['%08X' % w['parent']] += 1

    known_set = set(type_counter)
    unknowns = known_set - ALL_KNOWN
    missing = ALL_KNOWN - known_set

    issue_total = sum(len(w['issues']) for w in all_wrlds)

    if json_out:
        print(json.dumps({
            'total_wrld': len(all_wrlds),
            'subrecord_census': {t: type_counter[t] for t in sorted(type_counter)},
            'unknowns': sorted(unknowns),
            'dispatch_not_seen': sorted(missing),
            'data_flag_histogram': {'0x%02X' % k: v
                                    for k, v in sorted(flag_counter.items())},
            'wnam_parent_histogram': dict(parent_counter),
            'worldspaces': [{'formID': '%08X' % w['formID'],
                             'file': w['file'].split('/')[-1],
                             'types': w['types'],
                             'issues': w['issues'],
                             **{k: v for k, v in w.items()
                                if k in ('mapArrayBytes', 'rnamCount',
                                         'rnamEntries', 'dataFlags',
                                         'parent')}}
                            for w in all_wrlds],
        }, indent=2))
        return

    print("Parsed %d WRLD records from %d file(s); %d subrecord issue(s)"
          % (len(all_wrlds), len(args), issue_total))
    print("\n%-6s %-7s %-22s %s" % ('4CC', '#wrlds', 'SavePath', 'notes'))
    print("-" * 70)
    for t, c in type_counter.most_common():
        notes = ''
        if t == 'XWEM':
            notes = '58 B blob, load-only per contract'
        elif t == 'PNAM':
            notes = '2 B'
        elif t in ('CLSZ', 'OFST'):
            notes = 'parallel u32 array'
        elif t == 'RNAM':
            notes = 'variable-size region structs'
        print("%-6s %-7d %-22s %s" % (t, c, classify(t), notes))

    print("\n=== Discrepancies ===")
    if unknowns:
        print("UNKNOWN (in real WRLDs, not in dispatch):", sorted(unknowns))
    else:
        print("None -- every subrecord has a contract entry.")
    never = sorted(missing & (SAVE_RECORD_WRLD | LOADRECORD_ONLY))
    if never:
        print("In dispatch but absent from these masters:", never)

    print("\n=== DATA flags ===")
    for k, v in sorted(flag_counter.items()):
        names = [n for m, n in WRLD_FLAG_NAMES if k & m] or ['none']
        print("  0x%02X x%d  (%s)" % (k, v, ", ".join(names)))

    print("\n=== WNAM parents ===")
    for k, v in sorted(parent_counter.items(), key=lambda kv: -kv[1]):
        label = 'Commonwealth' if k == '0000003C' else ''
        print("  %s x%d %s" % (k, v, label))

    print("\n=== Per-worldspace ===")
    for w in all_wrlds:
        extras = []
        if 'mapArrayBytes' in w:
            extras.append('map arrays %dB' % w['mapArrayBytes'])
        if 'rnamEntries' in w:
            extras.append('%d RNAMs / %d cell-region entries'
                          % (w['rnamCount'], w['rnamEntries']))
        if w['issues']:
            extras.append('ISSUES: %s' % w['issues'])
        print("  %s %08X  %s" % (w['file'].split('/')[-1], w['formID'],
                                 '; '.join(extras)))


if __name__ == '__main__':
    main()
