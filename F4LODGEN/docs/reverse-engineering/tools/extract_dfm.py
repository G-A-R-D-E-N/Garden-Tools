#!/usr/bin/env python3
"""Extract Delphi binary DFM form resources from a PE and dump a readable map.

Binary DFM layout (Delphi 2009+, e.g. this xLODGen build):
    'TPF0' signature
    component := classname(name-pascal-string) name(pascal-string)
                 props* 0x00 children* 0x00
    property  := propname(pascal-string) valuetype(1 byte) valuedata

Value types (TValueType order, this compiler generation):
    2=vaInt8 3=vaInt16 4=vaInt32 5=vaExtended 6=vaString 7=vaIdent
    8=vaFalse 9=vaTrue 10=vaBinary 11=vaSet 12=vaLString 13=vaNil
    14=vaCollection 15=vaSingle 16=vaCurrency 17=vaDate 18=vaWString
    19=vaInt64 20=vaUTF8String 21=vaDouble 22=vaQWord 23=vaUInt32
    24=vaUInt64 25=vaBytes 26=vaUInt8 27=vaUInt16 28=vaSetString

Strings are stored UTF-8 (Unicode Delphi). The component/children layout
is self-delimiting: every component is followed by its own 0x00 children
terminator, so `children* 0x00` reads while the next byte != 0.
"""
import struct
import sys


class Parser:
    def __init__(self, data, base):
        self.d = data
        self.p = 0
        self.base = base  # file offset of the blob start (for reporting)

    def tell(self):
        return self.base + self.p

    def byte(self):
        b = self.d[self.p]
        self.p += 1
        return b

    def peek(self):
        return self.d[self.p]

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def i8(self):
        v = struct.unpack_from("<b", self.d, self.p)[0]
        self.p += 1
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.d, self.p)[0]
        self.p += 2
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def i64(self):
        v = struct.unpack_from("<q", self.d, self.p)[0]
        self.p += 8
        return v

    def pstr(self):
        """ReadStr: 1-byte-length string (class/property names, vaString/vaIdent)."""
        ln = self.byte()
        raw = self.d[self.p:self.p + ln]
        self.p += ln
        return raw.decode("utf-8", errors="replace")

    def rawstr4(self):
        """4-byte-length string (used by vaLString/vaWString/vaBytes...)."""
        ln = self.u32()
        raw = self.d[self.p:self.p + ln]
        self.p += ln
        return raw

    def value(self, vt):
        t = vt
        if t == 1:   # vaList: [type byte + data]* terminated by 0x00
            items = []
            while self.peek() != 0:
                vt2 = self.byte()
                items.append(self.value(vt2))
            self.byte()
            return "(" + ", ".join(items) + ")"
        if t == 2:   # vaInt8
            return f"{self.i8()}"
        if t == 3:   # vaInt16
            return f"{self.i16()}"
        if t == 4:   # vaInt32
            return f"{self.i32()}"
        if t == 5:   # vaExtended (10-byte x87)
            self.p += 10
            return "<extended>"
        if t in (6, 7):  # vaString / vaIdent
            return self.pstr()
        if t == 8:
            return "False"
        if t == 9:
            return "True"
        if t == 10:  # vaBinary: 4-byte len + raw
            ln = self.u32()
            self.p += ln
            return f"<binary {ln}B>"
        if t == 11:  # vaSet: idents until empty string
            items = []
            while self.peek() != 0:
                items.append(self.pstr())
            self.byte()  # trailing 0
            return f"[{', '.join(items)}]"
        if t == 12:  # vaLString: 4-byte len + UTF-8
            return self.rawstr4().decode("utf-8", errors="replace")
        if t == 13:  # vaNil
            return "nil"
        if t == 14:  # vaCollection
            # vaCollection = [item: vaList + props + 0x00]* + 0x00
            # (each item's props-end doubles as its terminator)
            items = []
            while self.peek() != 0:
                first = self.byte()
                if first != 1:
                    # named item with an index value first (rare)
                    self.value(first)
                sub = []
                while True:
                    pn = self.pstr()
                    if pn == "":
                        break
                    vv = self.byte()
                    sub.append((pn, self.value(vv)))
                items.append(sub)
            self.byte()
            return f"<collection {len(items)} items>"
        if t == 15:  # vaSingle
            v = struct.unpack_from("<f", self.d, self.p)[0]
            self.p += 4
            return f"{v:g}"
        if t == 16:  # vaCurrency
            self.p += 8
            return "<currency>"
        if t == 17:  # vaDate
            self.p += 8
            return "<date>"
        if t == 18:  # vaWString: 4-byte len (chars) + UTF-16LE
            ln = self.u32()
            raw = self.d[self.p:self.p + ln * 2]
            self.p += ln * 2
            try:
                return raw.decode("utf-16-le", errors="replace")
            except Exception:
                return repr(raw)
        if t == 19:  # vaInt64
            return f"{self.i64()}"
        if t == 20:  # vaUTF8String: 4-byte len + UTF-8
            return self.rawstr4().decode("utf-8", errors="replace")
        if t == 21:  # vaDouble
            v = struct.unpack_from("<d", self.d, self.p)[0]
            self.p += 8
            return f"{v:g}"
        if t == 22:  # vaQWord
            v = struct.unpack_from("<Q", self.d, self.p)[0]
            self.p += 8
            return str(v)
        if t == 23:  # vaUInt32
            return str(self.u32())
        if t == 24:  # vaUInt64
            v = struct.unpack_from("<Q", self.d, self.p)[0]
            self.p += 8
            return str(v)
        if t == 25:  # vaBytes: 4-byte len + raw
            ln = self.u32()
            self.p += ln
            return f"<bytes {ln}B>"
        if t == 26:  # vaUInt8
            return str(self.byte())
        if t == 27:  # vaUInt16
            return str(self.u16())
        if t == 28:  # vaSetString
            return self.pstr()
        raise ValueError(f"unknown value type {t:#x} at {self.tell():#x}")

    def component(self, depth, out):
        cls = self.pstr()
        if cls == "":
            return None
        name = self.pstr()
        props = []
        while True:
            pn = self.pstr()
            if pn == "":
                break
            vt = self.byte()
            try:
                vv = self.value(vt)
            except ValueError as e:
                raise ValueError(f"{e} (in {cls} '{name}' prop '{pn}')")
            props.append((pn, vv))
        children = []
        while self.peek() != 0:
            children.append(self.component(depth + 1, out))
        self.byte()  # children terminator
        out.append((cls, name, props, children))
        return (cls, name, props, children)

    def parse(self):
        sig = self.d[self.p:self.p + 4]
        self.p += 4
        if sig != b"TPF0":
            raise ValueError(f"bad signature {sig!r}")
        root = self.component(0, [])
        # report blob end; there may be alignment/padding after
        return root


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "xLODGen.exe"
    data = open(path, "rb").read()
    offs = []
    start = 0
    magic = b"TPF0"
    while True:
        i = data.find(magic, start)
        if i < 0:
            break
        offs.append(i)
        start = i + 1

    print(f"# DFM blobs in {path}: {len(offs)}\n")
    for off in offs:
        # bound the blob: next TPF0 or a sanity window
        nxt = data.find(magic, off + 4)
        end = nxt if nxt > 0 else min(off + 0x10000, len(data))
        p = Parser(data[off:end], off)
        try:
            root = p.parse()
            if root is None:
                print(f"===== 0x{off:08x}  (not a DFM: empty classname) =====")
                continue
            print(f"===== 0x{off:08x}  {root[0]} '{root[1]}'  (parsed {p.tell() - off} bytes of {end - off}) =====")
            dump(root, 0, data)
        except (ValueError, IndexError, struct.error) as e:
            print(f"===== 0x{off:08x}  PARSE ERROR: {e} =====")


def dump(node, indent, data, maxprops=40):
    cls, name, props, children = node
    pad = "  " * indent
    cap = ""
    for pn, pv in props:
        if pn in ("Caption", "Hint", "Text") and isinstance(pv, str):
            cap = pv
            break
    cap = cap.replace("\n", "\\n")
    if len(cap) > 90:
        cap = cap[:87] + "..."
    print(f"{pad}{cls} '{name}'  {cap!r}")
    shown = 0
    for pn, pv in props:
        if pn in ("Caption", "Hint", "Text"):
            continue
        if shown < maxprops:
            s = f"{pv}" if isinstance(pv, str) else str(pv)
            if len(s) > 80:
                s = s[:77] + "..."
            print(f"{pad}    {pn} = {s}")
            shown += 1
    if shown == maxprops and len(props) > maxprops:
        print(f"{pad}    ... (+{len(props) - maxprops} more props)")
    for c in children:
        dump(c, indent + 1, data)


if __name__ == "__main__":
    main()
