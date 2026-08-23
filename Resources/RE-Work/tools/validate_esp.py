#!/usr/bin/env python3
"""validate_esp.py - general .esm/.esp/.esl validator built on the strict
GRUP-tree walker (tools/plugin_parse.py). Roadmap Phase 8 -> M7 groundwork.

Checks, per plugin:

  structural   record/subrecord streams parse exactly to their header sizes;
               compressed bodies decompress to their declared size
  formids      every record's master-index byte is within range
               (0 = this file, 1..N = MAST list)
  strings      TES4 flag 0x80 (localized) implies FULL/DESC/SHRT payloads are
               exactly 4-byte IDs (doc 26 section 2a)
  contracts    per-type layouts proven in docs 23/26/27/28:
                 CELL  DATA 2B; XCLC 12B exterior-only, quad nibble
                 WRLD  DATA 1B; CLSZ/OFST parallel arrays; RNAM entry shape
                 PROJ  DNAM 93 B
                 EXPL  DATA 84 B canonical (52/56/60/64 legacy accepted)
                 HAZD  DNAM 52 B
                 ARMO/WEAP/NPC_/FURN  template block grammar (doc 26 s1):
                   one OBTE u32 == OBTS body count, <=1 pre-OBTE FULL,
                   every in-block FULL directly followed by OBTS,
                   OBTS size == 0x12 + 4*K + 7*I + 24*P
                 QUST   subrecord types within LoadRecord dispatch + alias
                        family (doc 28)

Exit code 1 if any ERROR-severity issue is found (CI-friendly), 2 on usage/
parse errors. INFO findings do not affect the exit code.

Usage:
  python3 validate_esp.py Fallout4.esm [more plugins...]
  python3 validate_esp.py --json plugin.esp
"""

import json
import struct
import sys

from plugin_parse import Plugin

LSTRING_FIELDS = {'FULL', 'DESC', 'SHRT', 'ITXT', 'NNAM'}

QUST_TAGS = {
    'ALCS', 'ALLS', 'ALST', 'ANAM', 'BNAM', 'CNAM', 'CTDA', 'DATA', 'DNAM',
    'EATA', 'EDID', 'ENAM', 'FLTR', 'FNAM', 'FULL', 'GNAM', 'INDX', 'LNAM',
    'NAM2', 'NEXT', 'OBLT', 'OBML', 'OBND', 'OBTE', 'PBND', 'PTRN', 'QNAM',
    'QOBJ', 'QSDT', 'QSTA', 'QTGL', 'RNAM', 'RTGL', 'SCDA', 'SCHR', 'SCRO',
    'SCRV', 'SCTX', 'SCVR', 'SLSD', 'SNAM', 'STCP', 'TCHR', 'VMAD', 'XNAM',
    # computed-tag alias/display family observed in shipped masters (doc 28)
    'ALID', 'ALED', 'ALUA', 'ALEQ', 'ALEA', 'ALFL', 'ALPC', 'ALFR', 'ALFA',
    'ALRT', 'ALFC', 'ALFV', 'ALCO', 'ALCA', 'ALCL', 'ALLA', 'ALDN', 'ALFE',
    'ALFD', 'ALMI', 'ALSP', 'ALFI', 'ALCC', 'ALNA', 'ALNT', 'ALDI', 'ECOR',
    'CIS1', 'CIS2', 'KSIZ', 'KWDA', 'COCT', 'CNTO', 'NNAM', 'VTCK',
}

EXPL_DATA_SIZES = {84, 52, 56, 60, 64}


def check_cell(rec, smap, body, add):
    d = smap.get('DATA')
    if not d or d[0].size != 2:
        add('CELL-DATA-SIZE', 'ERROR')
        return
    interior = bool(body[d[0].data_offset] & 1)
    x = smap.get('XCLC')
    if x and interior:
        add('XCLC-ON-INTERIOR', 'ERROR')
    if x and x[0].size != 12:
        add('XCLC-LENGTH', 'ERROR')


def check_wrld(rec, smap, body, add):
    d = smap.get('DATA')
    if not d or d[0].size != 1:
        add('WRLD-DATA-SIZE', 'ERROR')
    clsz = smap.get('CLSZ')
    ofst = smap.get('OFST')
    if clsz and ofst and clsz[0].size != ofst[0].size:
        add('CLSZ-OFST-MISMATCH', 'ERROR')
    for s in smap.get('RNAM', []) or []:
        n = struct.unpack_from('<I', body, s.data_offset + 4)[0]
        if s.size != 8 + 8 * n:
            add('RNAM-BAD-SHAPE', 'ERROR')


def _simple_size(sub_type, ok_sizes, label):
    def check(rec, smap, body, add):
        for s in smap.get(sub_type, []) or []:
            if s.size not in ok_sizes:
                add(label, 'ERROR')
    return check


def check_template(rec, smap, body, add):
    subs = rec.subs
    types = [s.type for s in subs]
    obte = [i for i, t in enumerate(types) if t == 'OBTE']
    if not obte:
        return
    first = obte[0]
    count = struct.unpack_from('<I', body, subs[first].data_offset)[0]
    nbodies = sum(1 for t in types if t == 'OBTS')
    if count != nbodies:
        add('TEMPLATE-COUNT-MISMATCH', 'ERROR')
    pre_full = sum(1 for i in range(first) if types[i] == 'FULL')
    if pre_full > 1:
        add('TEMPLATE-PRE-FULL', 'ERROR')
    try:
        stop = types.index('STOP')
    except ValueError:
        stop = len(types)
    for i in range(first + 1, stop):
        if types[i] == 'FULL' and (i + 1 >= stop or types[i + 1] != 'OBTS'):
            add('TEMPLATE-FULL-PLACEMENT', 'ERROR')
    for s in smap.get('OBTS', []) or []:
        pay = body[s.data_offset:s.data_offset + s.size]
        if len(pay) < 0x12:
            add('OBTS-SHORT', 'ERROR')
            continue
        k = pay[0x0F]
        incs = struct.unpack_from('<I', pay, 0)[0]
        tail = len(pay) - (0x12 + 4 * k)
        if incs * 7 > tail or (tail - 7 * incs) % 24 != 0:
            add('OBTS-SIZE-FORMULA', 'ERROR')


def check_qust(rec, smap, body, add):
    for s in rec.subs:
        if s.type not in QUST_TAGS:
            add('QUST-UNKNOWN-TAG:%s' % s.type, 'INFO')


def check_magic(rec, smap, body, add):
    efid = len(smap.get('EFID', []) or [])
    efit = len(smap.get('EFIT', []) or [])
    if efid != efit:
        add('EFFECT-PAIR-MISMATCH', 'ERROR')


def check_leveled(rec, smap, body, add):
    types = [s.type for s in rec.subs]
    if 'LVLD' not in types or 'LVLF' not in types:
        add('LEVELD-HEADER', 'ERROR')
    llct = smap.get('LLCT')
    lvlo = len(smap.get('LVLO', []) or [])
    if llct:
        n = body[llct[0].data_offset]
        if n != lvlo:
            add('LLCT-COUNT-MISMATCH', 'ERROR')


def check_scol(rec, smap, body, add):
    onam = len(smap.get('ONAM', []) or [])
    data = len(smap.get('DATA', []) or [])
    if onam != data:
        add('SCOL-PAIR-MISMATCH', 'ERROR')


def check_land(rec, smap, body, add):
    for s in smap.get('VNML', []) or []:
        if s.size != 3267:
            add('LAND-VNML-SIZE', 'ERROR')
    for s in smap.get('VHGT', []) or []:
        if s.size != 1096:
            add('LAND-VHGT-SIZE', 'ERROR')
    vclr = smap.get('VCLR')
    if vclr and vclr[0].size != 3267:
        add('LAND-VCLR-SIZE', 'ERROR')
    for t in ('BTXT', 'ATXT'):
        for s in smap.get(t, []) or []:
            if s.size != 8:
                add('LAND-%s-SIZE' % t, 'ERROR')
    for s in smap.get('VTXT', []) or []:
        if s.size % 8:
            add('LAND-VTXT-SIZE', 'ERROR')


def check_mswp(rec, smap, body, add):
    if len(smap.get('BNAM', []) or []) != len(smap.get('SNAM', []) or []):
        add('MSWP-PAIR-MISMATCH', 'ERROR')


def check_cobj(rec, smap, body, add):
    if not smap.get('INTV'):
        add('COBJ-MISSING-INTV', 'ERROR')


def check_mgef(rec, smap, body, add):
    for t in ('EDID', 'DATA', 'SNDD', 'DNAM'):
        if not smap.get(t):
            add('MGEF-MISSING-%s' % t, 'ERROR')


CONTRACTS = {
    'CELL': check_cell,
    'WRLD': check_wrld,
    'PROJ': _simple_size('DNAM', {93}, 'PROJ-DNAM-SIZE'),
    'HAZD': _simple_size('DNAM', {52}, 'HAZD-DNAM-SIZE'),
    'EXPL': _simple_size('DATA', EXPL_DATA_SIZES, 'EXPL-DATA-SIZE'),
    'ARMO': check_template,
    'WEAP': check_template,
    'NPC_': check_template,
    'FURN': check_template,
    'QUST': check_qust,
    'SPEL': check_magic,
    'ALCH': check_magic,
    'ENCH': check_magic,
    'INGR': check_magic,
    'LVLI': check_leveled,
    'LVLN': check_leveled,
    'SCOL': check_scol,
    'MGEF': check_mgef,
    'LAND': check_land,
    'MSWP': check_mswp,
    'COBJ': check_cobj,
}


def validate(path):
    """Return (report dict, error_count)."""
    plugin = Plugin(path)
    plugin.load(index_subrecords_for=tuple(CONTRACTS))
    issues = []
    counts = {}
    nmasters = len(plugin.masters)
    localized = bool(plugin.tes4.flags & 0x80)

    def add(fid, rtype, tag, sev):
        issues.append({'formID': '%08X' % fid, 'type': rtype,
                       'tag': tag, 'severity': sev})

    for rec in plugin.records:
        counts[rec.type] = counts.get(rec.type, 0) + 1
        mi = rec.form_id >> 24
        if mi > nmasters:
            add(rec.form_id, rec.type, 'MASTER-INDEX-OUT-OF-RANGE', 'ERROR')
        checker = CONTRACTS.get(rec.type)
        if checker is None:
            continue
        try:
            subs = rec.index_subs()
        except Exception as e:
            add(rec.form_id, rec.type, 'SUBSTREAM-OVERRUN:%s' % e, 'ERROR')
            continue
        smap = {}
        for s in subs:
            smap.setdefault(s.type, []).append(s)
        if localized:
            for f in LSTRING_FIELDS & set(smap):
                if all(s.size == 4 for s in smap[f]):
                    continue
                add(rec.form_id, rec.type,
                    'LOCALIZED-LSTRING-NOT-ID32:' + f, 'ERROR')
        body = None
        try:
            body = rec.body()
        except Exception as e:
            add(rec.form_id, rec.type, 'BODY-DECOMPRESS:%s' % e, 'ERROR')
            continue
        checker(rec, smap, body,
                lambda tag, sev, fid=rec.form_id, rt=rec.type: add(fid, rt, tag, sev))

    errors = sum(1 for i in issues if i['severity'] == 'ERROR')
    report = {
        'plugin': path,
        'localized': localized,
        'masters': plugin.masters,
        'records_total': len(plugin.records),
        'records_by_type': dict(sorted(counts.items(),
                                       key=lambda kv: -kv[1])),
        'issues': issues,
        'error_count': errors,
    }
    plugin.close()
    return report, errors


def main():
    args = sys.argv[1:]
    json_out = '--json' in args
    paths = [a for a in args if not a.startswith('--')]
    if not paths:
        print(__doc__)
        sys.exit(2)

    total_errors = 0
    reports = []
    for path in paths:
        try:
            report, errors = validate(path)
        except Exception as e:
            print('%s: PARSE FAILED: %s' % (path, e))
            total_errors += 1
            continue
        reports.append(report)
        total_errors += errors
        if json_out:
            continue
        print('%s: %d records (%d types), %d master(s), %s'
              % (path.split('/')[-1], report['records_total'],
                 len(report['records_by_type']), len(report['masters']),
                 'localized' if report['localized'] else 'plain'))
        if not report['issues']:
            print('  PASS - no contract issues')
            continue
        bytag = {}
        for i in report['issues']:
            key = (i['severity'], i['tag'].split(':')[0])
            bytag.setdefault(key, []).append(i)
        for (sev, tag), items in sorted(bytag.items()):
            sample = ', '.join(i['formID'] for i in items[:3])
            print('  %s %s x%d (e.g. %s)'
                  % ('ERROR' if sev == 'ERROR' else 'info ', tag,
                     len(items), sample))
        del report

    if json_out:
        print(json.dumps(reports, indent=2))
    sys.exit(1 if total_errors else 0)


if __name__ == '__main__':
    main()
