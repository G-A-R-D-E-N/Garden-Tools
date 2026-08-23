#!/usr/bin/env python3
"""plugin_parse.py — strict sequential ESP/ESM/ESL parser for FO4 plugins.

Walks the GRUP tree recursively instead of scanning for record magic, so
record extents always come from the headers (no resync heuristics, no false
positives inside compressed payloads).

Layout notes (all verified against shipped masters):
  * Record header is 24 B: type[4], dataSize u32, flags u32, formID u32,
    revision u32, version u16, unused u16.
  * GRUP header is 24 B: 'GRUP'[4], dataSize u32 (covers the whole group,
    header included), label[4], grupType u32, revision u32, version u16,
    unused u16.
  * Flag 0x00040000 = zlib-compressed body; on-disk body is a u32
    decompressed size followed by the raw deflate stream.
  * Subrecord: type[4], size u16; XXXX (size 4, payload u32) overrides the
    following subrecord's size.
  * CELL flag 0x0001 marks interior cells; exterior is NOT flag 0x0002
    (that one is "has water").

Usage:

    from plugin_parse import Plugin
    p = Plugin('Fallout4.esm')
    p.load(index_subrecords_for=('CELL',))
    p.records                      # list[Record]

Bodies are mmapped; Record.body() returns the decompressed payload bytes.
"""

import mmap
import struct
import zlib
from collections import Counter

GRUP_TOPLEVEL = 0
GRUP_WORLD_CHILDREN = 1
GRUP_INT_CELL_BLOCK = 2
GRUP_INT_CELL_SUBBLOCK = 3
GRUP_EXT_BLOCK = 4
GRUP_EXT_SUBBLOCK = 5
GRUP_CELL_CHILDREN = 6
GRUP_TOPIC_CHILDREN = 7
GRUP_CELL_PERSISTENT = 8
GRUP_CELL_TEMPORARY = 9
GRUP_CELL_VISIBLE_DISTANT = 10

FLAG_COMPRESSED = 0x00040000
FLAG_INTERIOR_CELL = 0x00000001

_REC_HDR = struct.Struct('<4sIIIIHH')     # type,size,flags,id,revision,v16,x16


class Subrecord:
    __slots__ = ('type', 'size', 'data_offset')

    def __init__(self, stype, size, data_offset):
        self.type = stype
        self.size = size
        self.data_offset = data_offset


class Record:
    __slots__ = ('type', 'flags', 'form_id', 'revision', 'version',
                 '_mm', 'body_offset', 'body_size', 'decomp_size', 'subs')

    def __init__(self, rtype, flags, form_id, revision, version,
                 mm, body_offset, body_size, decomp_size):
        self.type = rtype
        self.flags = flags
        self.form_id = form_id
        self.revision = revision
        self.version = version
        self._mm = mm
        # offset/size refer to the on-disk (possibly compressed) body
        self.body_offset = body_offset
        self.body_size = body_size
        self.decomp_size = decomp_size
        self.subs = None            # list[Subrecord] | None until indexed

    @property
    def compressed(self):
        return bool(self.flags & FLAG_COMPRESSED)

    @property
    def exterior(self):
        """FO4 marks neither interior nor exterior via record flags (bit 0,
        TES5's 'Is Interior Cell', is never set in shipped masters). The
        reliable marker is presence of XCLC — index_subs() first."""
        return self.subs is not None and any(s.type == 'XCLC' for s in self.subs)

    def body(self):
        """Decompressed record body."""
        raw = self._mm[self.body_offset:self.body_offset + self.body_size]
        if self.compressed:
            (declared,) = struct.unpack_from('<I', raw, 0)
            out = zlib.decompress(raw[4:])
            if len(out) != declared or declared != self.decomp_size:
                raise ValueError(
                    'decompressed-size mismatch in %s %08X '
                    '(hdr %d, prefix %d, got %d)'
                    % (self.type, self.form_id, self.decomp_size,
                       declared, len(out)))
            return out
        return raw

    def index_subs(self):
        """Parse the subrecord table (idempotent)."""
        if self.subs is not None:
            return self.subs
        data = self.body()
        subs = []
        pos, n = 0, len(data)
        override_next = None
        while pos + 6 <= n:
            stype = bytes(data[pos:pos + 4])
            (ssize,) = struct.unpack_from('<H', data, pos + 4)
            if stype == b'XXXX':
                if ssize != 4:
                    raise ValueError('XXXX size %d != 4 in %s %08X'
                                     % (ssize, self.type, self.form_id))
                (override_next,) = struct.unpack_from('<I', data, pos + 6)
                pos += 10
                continue
            if override_next is not None:
                ssize = override_next
                override_next = None
            end = pos + 6 + ssize
            if end > n:
                raise ValueError(
                    'subrecord %r overruns %s %08X body '
                    '(pos %d, size %d, body %d)'
                    % (stype, self.type, self.form_id, pos, ssize, n))
            subs.append(Subrecord(stype.decode('ascii'), ssize, pos + 6))
            pos = end
        self.subs = subs
        return subs

    def sub_map(self):
        """type -> list[Subrecord] for indexed records."""
        m = {}
        for s in self.index_subs():
            m.setdefault(s.type, []).append(s)
        return m

    def __repr__(self):
        return '<Record %s %08X rev%d v%d%s>' % (
            self.type, self.form_id, self.revision, self.version,
            ' z' if self.compressed else '')


class ParseError(ValueError):
    pass


class Plugin:
    """Full-tree parser for one plugin file."""

    def __init__(self, path):
        self.path = path
        self.tes4 = None
        self.masters = []
        self.header_version = None
        self.num_records = None
        self.next_form_id_base = None
        self.records = []
        self.grups = Counter()

    def load(self, index_subrecords_for=()):
        want = frozenset(t if isinstance(t, bytes) else t.encode('ascii')
                         for t in index_subrecords_for)
        f = open(self.path, 'rb')
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        self._f, self._mm = f, mm

        if mm[0:4] != b'TES4':
            raise ParseError('%s: bad magic %r' % (self.path, bytes(mm[0:4])))
        (_, size, flags, fid, rev, ver, _pad) = _REC_HDR.unpack_from(mm, 0)
        self.tes4 = Record('TES4', flags, fid, rev, ver, mm, 24, size, size)
        end_hdr = 24 + size
        if end_hdr > len(mm):
            raise ParseError('%s: TES4 header overruns file' % self.path)
        self._parse_tes4_fields(mm, end_hdr)

        pos = [end_hdr]
        limit = len(mm)

        def walk(end, depth=0):
            if depth > 16:
                raise ParseError('%s: GRUP nesting > 16' % self.path)
            while True:
                p = pos[0]
                if p + 24 > end:
                    if p != end:
                        raise ParseError(
                            '%s: %d trailing bytes in container at 0x%X'
                            % (self.path, end - p, p))
                    return
                sig = bytes(mm[p:p + 4])
                if sig == b'GRUP':
                    gsize = struct.unpack_from('<I', mm, p + 4)[0]
                    (gtype,) = struct.unpack_from('<I', mm, p + 12)
                    self.grups[gtype] += 1
                    if gsize < 24 or p + gsize > end:
                        raise ParseError(
                            '%s: bad GRUP at 0x%X (type %d, size %d)'
                            % (self.path, p, gtype, gsize))
                    pos[0] = p + 24
                    walk(p + gsize, depth + 1)
                else:
                    (_, rsize, rflags, rid, rrev, rver, _) = \
                        _REC_HDR.unpack_from(mm, p)
                    body_off = p + 24
                    dsize = rsize
                    if rflags & FLAG_COMPRESSED:
                        if rsize < 4 or body_off + rsize > end:
                            raise ParseError(
                                '%s: truncated compressed record %r at 0x%X'
                                % (self.path, sig, p))
                        (dsize,) = struct.unpack_from('<I', mm, body_off)
                    else:
                        if body_off + rsize > end:
                            raise ParseError(
                                '%s: record %r overruns container at 0x%X'
                                % (self.path, sig, p))
                    rec = Record(sig.decode('ascii', 'replace'), rflags, rid,
                                 rrev, rver, mm, body_off, rsize, dsize)
                    self.records.append(rec)
                    if sig in want:
                        rec.index_subs()
                    pos[0] = body_off + rsize

        walk(limit)
        return self

    def close(self):
        if getattr(self, '_mm', None) is not None:
            self._mm.close()
            self._f.close()
            self._mm = None

    def _parse_tes4_fields(self, mm, end):
        pos = 24
        while pos + 6 <= end:
            stype = bytes(mm[pos:pos + 4])
            (ssize,) = struct.unpack_from('<H', mm, pos + 4)
            val = mm[pos + 6:pos + 6 + ssize]
            if stype == b'MAST':
                self.masters.append(bytes(val).rstrip(b'\x00').decode('utf-8'))
            elif stype == b'HEDR' and ssize >= 12:
                ver, entries, nid = struct.unpack_from('<fII', val, 0)
                self.header_version = ver
                self.num_records = entries
                self.next_form_id_base = nid
            elif stype == b'ONAM':
                pass
            pos += 6 + ssize

    def by_type(self, rtype):
        return [r for r in self.records if r.type == rtype]
