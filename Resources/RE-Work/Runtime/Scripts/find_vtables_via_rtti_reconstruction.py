"""Recover MSVC x64 vtable RVAs by hand-walking RTTI, without Ghidra.

Use when a target binary's RTTI data is physically present but a Ghidra project shows
zero VTABLE_/RTTI_ symbols for it (analyzer miss, not missing data) -- confirmed on
1.11.221.0. Chain: mangled TypeDescriptor name (".?AV<Class>@@"/".?AU<Class>@@") -> back
up 16 bytes to the TypeDescriptor -> every 4-byte image-relative RVA ref to it that sits
0xC bytes after a CompleteObjectLocator signature dword == 1 -> every 8-byte VA ref to
that COL (the vtable's hidden "meta" slot, immediately followed by the real vtable).

Each class typically yields 1-2 candidates (main + secondary/fixup vtable for multiple
inheritance). Validated 2026-07-17: for HUDCompass/HUDAmmoCounter/HUDCrosshair, the FIRST
(lowest file-offset) candidate exactly matched known-good hardcoded OG addresses in
PrismaUI_F4's VanillaUISuppressor.cpp, 3/3 -- take the first candidate unless a class
needs re-validation the same way.

Usage: python find_vtables_via_rtti_reconstruction.py <exe_path> <ClassName> [ClassName ...]
"""
import struct
import sys

IMAGE_BASE = 0x140000000


def get_sections(path):
    with open(path, 'rb') as f:
        data = f.read(4096)
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    nsec = struct.unpack_from('<H', data, e_lfanew + 6)[0]
    opt_size = struct.unpack_from('<H', data, e_lfanew + 20)[0]
    sec_table_off = e_lfanew + 24 + opt_size
    with open(path, 'rb') as f:
        f.seek(sec_table_off)
        sec_data = f.read(40 * nsec)
    sections = []
    for i in range(nsec):
        entry = sec_data[i * 40:(i + 1) * 40]
        vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', entry, 8)
        sections.append((vaddr, vsize, rawptr, rawsize))
    return sections


def fileoff_to_rva(sections, off):
    for vaddr, vsize, rawptr, rawsize in sections:
        if rawptr <= off < rawptr + rawsize:
            return vaddr + (off - rawptr)
    return None


def find_vtables_for_class(data, sections, class_name):
    results = []
    for prefix in (b'.?AV', b'.?AU'):
        mangled = prefix + class_name.encode() + b'@@\x00'
        start = 0
        while True:
            name_off = data.find(mangled, start)
            if name_off == -1:
                break
            start = name_off + 1
            name_rva = fileoff_to_rva(sections, name_off)
            if name_rva is None:
                continue
            td_rva = name_rva - 16
            td_rva_bytes = struct.pack('<I', td_rva)
            idx = 0
            while True:
                idx = data.find(td_rva_bytes, idx)
                if idx == -1:
                    break
                col_off = idx - 0xC
                if col_off >= 0:
                    sig = struct.unpack_from('<I', data, col_off)[0]
                    if sig == 1:
                        col_rva = fileoff_to_rva(sections, col_off)
                        if col_rva is not None:
                            col_va = IMAGE_BASE + col_rva
                            col_va_bytes = struct.pack('<Q', col_va)
                            vidx = 0
                            while True:
                                vidx = data.find(col_va_bytes, vidx)
                                if vidx == -1:
                                    break
                                vtable_rva = fileoff_to_rva(sections, vidx)
                                if vtable_rva is not None:
                                    results.append((prefix.decode(), td_rva, col_rva, vtable_rva + 8))
                                vidx += 1
                idx += 1
    return results


def main():
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <exe_path> <ClassName> [ClassName ...]")
        sys.exit(1)

    path = sys.argv[1]
    classes = sys.argv[2:]

    with open(path, 'rb') as f:
        data = f.read()
    sections = get_sections(path)

    for cls in classes:
        res = find_vtables_for_class(data, sections, cls)
        if not res:
            print(f"{cls}: NOT FOUND (no RTTI name string match)")
            continue
        for prefix, td_rva, col_rva, vt_rva in res:
            print(f"{cls}: [{prefix}] TD_rva={hex(td_rva)} COL_rva={hex(col_rva)} "
                  f"VTABLE_rva={hex(vt_rva)} VTABLE_va={hex(IMAGE_BASE + vt_rva)}")


if __name__ == "__main__":
    main()
