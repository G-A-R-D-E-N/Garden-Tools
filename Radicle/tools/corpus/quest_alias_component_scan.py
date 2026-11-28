#!/usr/bin/env python3

import argparse
import collections
import glob
import os
import struct
import sys
import zlib


def subrecords(data):
    pos, extended, out = 0, None, []
    while pos + 6 <= len(data):
        signature = data[pos:pos + 4].decode("latin-1")
        size = struct.unpack_from("<H", data, pos + 4)[0]
        pos += 6
        if signature == "XXXX":
            extended = struct.unpack_from("<I", data, pos)[0]
            pos += 4
            continue
        if extended is not None:
            size, extended = extended, None
        out.append((signature, data[pos:pos + size]))
        pos += size
    return out


def records(buf, pos, end):
    while pos + 24 <= end:
        size = struct.unpack_from("<I", buf, pos + 4)[0]
        if buf[pos:pos + 4] == b"GRUP":
            yield from records(buf, pos + 24, pos + size)
            pos += size
            continue
        flags, form_id = struct.unpack_from("<II", buf, pos + 8)
        body = buf[pos + 24:pos + 24 + size]
        if flags & 0x40000:
            body = zlib.decompress(body[4:])
        yield buf[pos:pos + 4].decode("latin-1"), form_id, body
        pos += 24 + size


def load(path):
    buf = open(path, "rb").read()
    recs = list(records(buf, 0, len(buf)))
    masters = [d.split(b"\0")[0].decode("latin-1").lower() for s, d in subrecords(recs[0][2]) if s == "MAST"]
    return masters, recs


def main():
    parser = argparse.ArgumentParser(description="Count QUST ALCS/ALMI adjacency and the record types of COBJ FVPA and MISC CVPA components.")
    parser.add_argument("data_dir", help="Fallout 4 Data folder (read only)")
    parser.add_argument("--masters", nargs="+", default=["Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"])
    parser.add_argument("--quest-plugins", nargs="*", default=[], help="extra plugins scanned for QUST only, e.g. cc*.esl")
    args = parser.parse_args()

    plugins = {}
    for name in args.masters:
        plugins[name.lower()] = load(os.path.join(args.data_dir, name))
    signature_of = {}
    for name, (_, recs) in plugins.items():
        for signature, form_id, _ in recs:
            signature_of[(name, form_id & 0xFFFFFF)] = signature

    quests = collections.Counter()
    components = collections.Counter()
    quest_sources = dict(plugins)
    for pattern in args.quest_plugins:
        for path in glob.glob(os.path.join(args.data_dir, pattern)):
            quest_sources[os.path.basename(path).lower()] = load(path)
    for name, (masters, recs) in quest_sources.items():
        for signature, form_id, body in recs:
            if signature == "QUST":
                seq = [s for s, _ in subrecords(body)]
                quests["quests"] += 1
                for k, s in enumerate(seq):
                    if s != "ALCS":
                        continue
                    quests["alcs"] += 1
                    after = seq[k + 1:k + 3] + [None, None]
                    if after[0] == "ALMI":
                        quests["alcs_then_almi"] += 1
                        quests["adjacent_collection"] += after[1] == "ALCS"
                    else:
                        quests["adjacent_collection"] += after[0] == "ALCS"
            elif signature in ("COBJ", "MISC") and name in plugins:
                wanted = "FVPA" if signature == "COBJ" else "CVPA"
                for s, data in subrecords(body):
                    if s != wanted:
                        continue
                    for k in range(0, len(data) - 7, 8):
                        raw = struct.unpack_from("<I", data, k)[0]
                        owner = masters[raw >> 24] if (raw >> 24) < len(masters) else name
                        components[(signature, signature_of.get((owner, raw & 0xFFFFFF), "?"))] += 1
    print(dict(quests))
    for key, count in sorted(components.items()):
        print(key, count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
