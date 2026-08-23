#!/usr/bin/env python3
"""dnam_survey.py - field survey of PROJ DNAM / EXPL DATA / HAZD DNAM payloads.

For each target subrecord, across every plugin given on the command line:

  * payload size histogram
  * per-offset classification over all observed payloads:
      - constant byte share
      - share of payloads where the dword decodes as a "plausible float"
        (abs value in [1e-4, 1e6] or zero, exponent sane)
      - share where the top byte is a valid master/file index AND the low
        three bytes are nonzero (formID shape)
      - resolved record-type distribution for formID-shaped dwords
      - distinct-value count

Usage: python3 dnam_survey.py <plugin.esm|esp> [...]
"""

import os
import struct
import sys
from collections import Counter, defaultdict

from plugin_parse import Plugin

TARGETS = {
    'PROJ': ('DNAM',),
    'EXPL': ('DATA',),
    'HAZD': ('DNAM',),
}


def plausible_float(x):
    if x == 0:
        return True
    a = abs(x)
    return 1e-4 <= a <= 1e6


def main():
    acc = defaultdict(lambda: {
        'sizes': Counter(),
        'const': [Counter() for _ in range(96)],
        'n': 0,
        'fid': defaultdict(Counter),
    })

    for path in sys.argv[1:]:
        try:
            p = Plugin(path)
            p.load(index_subrecords_for=tuple(TARGETS))
        except Exception as e:
            print('# skip %s: %s' % (path, e), file=sys.stderr)
            continue
        nmaster = len(p.masters)
        # formid -> type map incl. masters (index 0 = this file)
        fmap = {}
        own = {r.form_id: r.type for r in p.records}
        for mi, m in enumerate(p.masters):
            base = (mi + 1) << 24
            for fid, t in _master_types(os.path.join(os.path.dirname(path), m)).items():
                fmap[base | fid] = t
        for r in p.records:
            spec = TARGETS.get(r.type)
            if not spec:
                continue
            smap = r.sub_map()
            for stype in spec:
                for s in smap.get(stype, []):
                    key = (r.type, stype)
                    a = acc[key]
                    b = r.body()[s.data_offset:s.data_offset + s.size]
                    a['sizes'][len(b)] += 1
                    a['n'] += 1
                    for off in range(min(len(b), 96) & ~3):
                        dw = b[off:off + 4]
                        if len(dw) < 4:
                            break
                        (u,) = struct.unpack('<I', dw)
                        a['const'][off][dw] += 1
                        hi = u >> 24
                        lo = u & 0x00FFFFFF
                        if hi <= nmaster and lo != 0:
                            t = fmap.get(u) or own.get(u)
                            a['fid'][off][t] += 1

    for key in sorted(acc):
        a = acc[key]
        print('\n=== %s %s (%d payloads from %d files) ==='
              % (key[0], key[1], a['n'], len(sys.argv) - 1))
        print('sizes:', dict(a['sizes']))
        maxlen = max(a['sizes'])
        print('%-4s %-7s %-7s %-7s %-6s %s'
              % ('off', 'const%', 'float%', 'fid%', '#val', 'top formID types'))
        for off in range(0, maxlen, 4):
            c = a['const'][off]
            tot = sum(c.values())
            if not tot:
                continue
            consts = sum(v for k, v in c.items() if v == tot)
            floats = sum(v for k, v in c.items()
                         if plausible_float(struct.unpack('<I', k)[0]))
            fids = a['fid'].get(off, Counter())
            fidtot = sum(fids.values())
            top = ', '.join('%s:%d' % (t or '?', v)
                            for t, v in fids.most_common(3))
            print('%-4x %-7.2f %-7.2f %-7.2f %-6d %s'
                  % (off, 100.0 * consts / tot, 100.0 * floats / tot,
                     100.0 * fidtot / tot, len(c), top))


_type_cache = {}


def _master_types(path):
    """Return {local formID: record type} for one master file."""
    key = os.path.abspath(path)
    if key in _type_cache:
        return _type_cache[key]
    out = {}
    try:
        mp = Plugin(path)
        mp.load()
        for r in mp.records:
            out[r.form_id] = r.type
        mp.close()
    except Exception:
        pass
    _type_cache[key] = out
    return out


if __name__ == '__main__':
    main()
