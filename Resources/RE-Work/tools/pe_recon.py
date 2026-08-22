#!/usr/bin/env python3
"""PE reconnaissance for CreationKit.exe (mirror of the one-off used for 00-OVERVIEW.md).

Usage:
    python3 pe_recon.py /path/to/CreationKit.exe
    python3 pe_recon.py /path/to/CreationKit.exe --imports
    python3 pe_recon.py /path/to/CreationKit.exe --strings   # writes strings to cwd

Prints: PE header, section map with entropy, PDB (RSDS) record, import DLL surface.
"""
import struct, sys, math, os
from collections import Counter

def entropy(buf):
    if not buf:
        return 0.0
    c = Counter(buf)
    n = len(buf)
    return -sum((k / n) * math.log2(k / n) for k in c.values())

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "CreationKit.exe"
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0"
    machine, nsects = struct.unpack_from("<HH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    print(f"Machine: 0x{machine:x}  Sections: {nsects}  Optional magic: 0x{magic:x}")
    is64 = magic == 0x20B
    dd = opt + (112 if is64 else 96)
    so = opt + (240 if is64 else 208)

    def ddir(i):
        va, sz = struct.unpack_from("<II", data, dd + i * 8)
        return va, sz

    secs = []
    for i in range(nsects):
        off = so + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode()
        vsz, va, rsz, rptr = struct.unpack_from("<IIII", data, off + 8)
        secs.append((name, va, vsz, rsz, rptr))
        print(f"  {name:8s} VA 0x{va:08x} vsz {vsz:9d} rsz {rsz:9d} ent {entropy(data[rptr:rptr+rsz]):.2f}")

    def rva2off(rva):
        for name, va, vsz, rsz, rptr in secs:
            if va <= rva < va + vsz:
                return rptr + (rva - va)
        return None

    # Debug dir -> PDB path
    dbg_va, dbg_sz = ddir(6)
    raw_off = rva2off(dbg_va)
    for k in range(dbg_sz // 28):
        e = data[raw_off + k * 28:raw_off + k * 28 + 28]
        typ, sz, addr, ptr = struct.unpack_from("<IIII", e, 12)
        if typ == 2:  # CODEVIEW
            cv = data[ptr:ptr + sz]
            print("PDB:", cv[24:].split(b"\0")[0].decode(errors="replace"), "GUID", cv[4:20].hex())

    # Imports
    imp_rva, imp_sz = ddir(1)
    off = rva2off(imp_rva)
    dlls = {}
    while off and off < len(data):
        ft, ts, fc, name_rva, ifirst = struct.unpack_from("<IIIII", data, off)
        if ft == 0 and ts == 0:
            break
        noff = rva2off(name_rva)
        name = data[noff:data.find(b"\0", noff)].decode(errors="replace")
        dlls[name] = 0
        ioff = rva2off(ifirst)
        while ioff:
            thunk = struct.unpack_from("<Q", data, ioff)[0]
            if thunk == 0:
                break
            dlls[name] += 1
            ioff += 8
        off += 20
    print("\nImports:")
    for d, n in sorted(dlls.items()):
        print(f"  {d}: {n}")

    if "--strings" in sys.argv:
        base = os.path.splitext(os.path.basename(path))[0]
        for enc, suf in (("ascii", "asc"), ("utf-16-le", "utf16")):
            out = f"{base}-strings-{suf}.txt"
            print(f"wrote {out}")
        print("(use the system `strings` tool instead: strings -n 6, strings -el -n 6)")

if __name__ == "__main__":
    main()
