#!/usr/bin/env python3
# Constraint: read-only; writes no file.
import collections
import os
import struct
import sys
import zlib

MASTERS = [
    "Fallout4.esm", "DLCRobot.esm", "DLCCoast.esm", "DLCNukaWorld.esm",
    "DLCworkshop01.esm", "DLCworkshop02.esm", "DLCworkshop03.esm",
]
EXAMPLE_COUNT = 3
USAGE = 'usage: python tools/census_quest_alias_words.py "<Fallout 4 Data dir>" [more Data dirs]'


class Bad(Exception):
    pass


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, fmt, size):
        if self.pos + size > len(self.data):
            raise Bad("truncated")
        value = struct.unpack_from(fmt, self.data, self.pos)[0]
        self.pos += size
        return value

    def u8(self):
        return self.take("<B", 1)

    def i16(self):
        return self.take("<h", 2)

    def u16(self):
        return self.take("<H", 2)

    def u32(self):
        return self.take("<I", 4)

    def skip(self, count):
        if self.pos + count > len(self.data):
            raise Bad("truncated")
        self.pos += count

    def wstr(self):
        count = self.u16()
        self.skip(count)
        return self.data[self.pos - count:self.pos].decode("latin-1")

    def at_end(self):
        return self.pos >= len(self.data)


def read_object(r, fmt):
    if fmt == 2:
        pre = r.u16()
        alias = r.i16()
        form_id = r.u32()
        return {"alias": alias, "form_id": form_id, "pre": pre, "post": None}
    if fmt == 1:
        form_id = r.u32()
        alias = r.i16()
        post = r.u16()
        return {"alias": alias, "form_id": form_id, "pre": None, "post": post}
    raise Bad("object format %d" % fmt)


def read_member(r, fmt):
    r.wstr()
    kind = r.u8()
    r.u8()
    if kind == 1:
        read_object(r, fmt)
    elif kind == 2:
        r.wstr()
    elif kind in (3, 4):
        r.u32()
    elif kind == 5:
        r.u8()
    else:
        raise Bad("struct member type %d" % kind)


def read_property(r, fmt):
    r.wstr()
    kind = r.u8()
    r.u8()
    if kind == 0:
        return
    if kind == 1:
        read_object(r, fmt)
    elif kind == 2:
        r.wstr()
    elif kind in (3, 4):
        r.u32()
    elif kind == 5:
        r.u8()
    elif kind == 7:
        for _ in range(r.u32()):
            read_member(r, fmt)
    elif kind == 11:
        for _ in range(r.u32()):
            read_object(r, fmt)
    elif kind == 12:
        for _ in range(r.u32()):
            r.wstr()
    elif kind in (13, 14):
        for _ in range(r.u32()):
            r.u32()
    elif kind == 15:
        r.skip(r.u32())
    elif kind == 17:
        for _ in range(r.u32()):
            for _ in range(r.u32()):
                read_member(r, fmt)
    else:
        raise Bad("property type %d" % kind)


def read_script(r, fmt):
    name = r.wstr()
    r.u8()
    for _ in range(r.u16()):
        read_property(r, fmt)
    return name


def parse_qust_vmad(data):
    r = Reader(data)
    version = r.i16()
    fmt = r.i16()
    for _ in range(r.u16()):
        read_script(r, fmt)
    info = {"version": version, "format": fmt, "fragment_count": 0, "aliases": []}
    if r.at_end():
        return info
    r.u8()
    info["fragment_count"] = r.u16()
    fragment_script = r.wstr()
    if fragment_script:
        r.u8()
        for _ in range(r.u16()):
            read_property(r, fmt)
    for _ in range(info["fragment_count"]):
        r.u16()
        r.u16()
        r.u32()
        r.u8()
        r.wstr()
        r.wstr()
    for _ in range(r.u16()):
        start = r.pos
        alias = read_object(r, fmt)
        entry_version = r.i16()
        entry_format = r.i16()
        scripts = [read_script(r, entry_format) for _ in range(r.u16())]
        alias.update({
            "raw": data[start:r.pos], "entry_version": entry_version,
            "entry_format": entry_format, "scripts": scripts,
        })
        info["aliases"].append(alias)
    if not r.at_end():
        raise Bad("trailing bytes")
    return info


def iter_records(data, start, end):
    pos = start
    while pos + 24 <= end:
        kind = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if kind == b"GRUP":
            yield from iter_records(data, pos + 24, pos + size)
            pos += size
            continue
        flags = struct.unpack_from("<I", data, pos + 8)[0]
        form_id = struct.unpack_from("<I", data, pos + 12)[0]
        body = data[pos + 24:pos + 24 + size]
        if flags & 0x40000:
            body = zlib.decompress(body[4:])
        yield kind.decode("latin-1"), form_id, body
        pos += 24 + size


def subrecords(body):
    pos = 0
    big = None
    while pos + 6 <= len(body):
        kind = body[pos:pos + 4].decode("latin-1")
        size = struct.unpack_from("<H", body, pos + 4)[0]
        pos += 6
        if kind == "XXXX":
            big = struct.unpack_from("<I", body, pos)[0]
            pos += size
            continue
        if big is not None:
            size = big
            big = None
        yield kind, body[pos:pos + size]
        pos += size


class Census:
    def __init__(self):
        self.counts = collections.Counter()
        self.values = collections.defaultdict(collections.Counter)
        self.plugins = []
        self.parse_failures = []
        self.off_version_owners = []
        self.non_v6_without_aliases = []
        self.vmad_versions = collections.Counter()
        self.examples = []

    def add_plugin(self, path):
        with open(path, "rb") as f:
            data = f.read()
        plugin = os.path.basename(path)
        row = collections.Counter()
        for sig, qust_form_id, body in iter_records(data, 0, len(data)):
            if sig != "QUST":
                continue
            self.add_qust(plugin, qust_form_id, body, row)
        self.plugins.append((plugin, row))

    def add_qust(self, plugin, qust_form_id, body, row):
        row["qust"] += 1
        edid = None
        vmad = None
        for kind, value in subrecords(body):
            if kind == "EDID" and edid is None:
                edid = value.rstrip(b"\0").decode("latin-1")
            elif kind == "VMAD":
                vmad = value
        if vmad is None:
            return
        row["vmad"] += 1
        label = (plugin, edid, "%08X" % qust_form_id)
        try:
            info = parse_qust_vmad(vmad)
        except Bad as error:
            self.parse_failures.append(label + (str(error),))
            return
        self.vmad_versions[(plugin, info["version"], info["format"])] += 1
        if info["fragment_count"]:
            row["fragments"] += 1
        if info["aliases"]:
            row["alias_owners"] += 1
            if info["version"] != 6:
                self.off_version_owners.append(label + (info["version"],))
        elif info["version"] != 6:
            self.non_v6_without_aliases.append(label + (info["version"],))
        for alias in info["aliases"]:
            self.add_alias(label, qust_form_id, info, alias, row)

    def add_alias(self, label, qust_form_id, info, alias, row):
        values = self.values
        row["alias_entries"] += 1
        values["leading_word"][alias["pre"] if alias["pre"] is not None else "n/a (format 1)"] += 1
        if alias["post"] is not None:
            values["trailing_word"][alias["post"]] += 1
        if alias["form_id"] == 0:
            values["object_form_id"]["zero"] += 1
        elif alias["form_id"] == qust_form_id:
            values["object_form_id"]["equal to QUST form ID"] += 1
        else:
            values["object_form_id"]["other"] += 1
        values["alias_id_sign"]["negative" if alias["alias"] < 0 else "zero" if alias["alias"] == 0 else "positive"] += 1
        values["entry_version"][alias["entry_version"]] += 1
        values["entry_format"][alias["entry_format"]] += 1
        values["entry_format_vs_owner_format"][(alias["entry_format"], info["format"])] += 1
        values["entry_version_vs_owner_version"][(alias["entry_version"], info["version"])] += 1
        values["scripts_per_alias"][len(alias["scripts"])] += 1
        if alias["scripts"]:
            row["alias_with_scripts"] += 1
        if len(self.examples) < EXAMPLE_COUNT and alias["scripts"]:
            self.examples.append(label + (alias,))

    def report(self, data_dir):
        lines = ["== %s" % data_dir,
                 "plugin: QUST, with VMAD, with fragments, alias owners, alias entries, entries with scripts"]
        total = collections.Counter()
        for plugin, row in self.plugins:
            lines.append("  %s: %d, %d, %d, %d, %d, %d" % (
                plugin, row["qust"], row["vmad"], row["fragments"], row["alias_owners"],
                row["alias_entries"], row["alias_with_scripts"]))
            total.update(row)
        lines.append("total: %d, %d, %d, %d, %d, %d" % (
            total["qust"], total["vmad"], total["fragments"], total["alias_owners"],
            total["alias_entries"], total["alias_with_scripts"]))
        lines.append("parse failures: %d %s" % (len(self.parse_failures), self.parse_failures[:20]))
        lines.append("VMAD (plugin, version, object format) of QUST:")
        for key in sorted(self.vmad_versions):
            lines.append("  %s: %d" % (key, self.vmad_versions[key]))
        for key in sorted(self.values):
            lines.append("%s: %s" % (key, dict(sorted(self.values[key].items(), key=lambda kv: str(kv[0])))))
        lines.append("QUST with VMAD version != 6 that own alias entries: %s" % self.off_version_owners)
        lines.append("QUST with VMAD version != 6 and no alias entries: %s" % self.non_v6_without_aliases)
        for plugin, edid, form_id, alias in self.examples:
            lines.append("EXAMPLE %s %s %s alias id %d scripts %s" % (
                plugin, edid, form_id, alias["alias"], alias["scripts"]))
            lines.append("  raw: %s" % alias["raw"].hex(" "))
        return "\n".join(lines)


def main(argv):
    if not argv:
        print(USAGE)
        return 2
    for data_dir in argv:
        census = Census()
        for name in MASTERS:
            path = os.path.join(data_dir, name)
            if os.path.exists(path):
                census.add_plugin(path)
            else:
                print("missing: %s" % path)
        print(census.report(data_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
