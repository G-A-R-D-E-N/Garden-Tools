#!/usr/bin/env python3
"""Parse CommonLibF4 headers -> authoritative primary-vtable slot->method map per class.
Line shape:  virtual <ret> <Method>(<args>) [const] [override] [= 0];   // <hexslot>[ - note]
We take the primary vtable only: slots in document order, stopping at the first BACKWARD slot
jump (that marks a secondary multiple-inheritance vtable that restarts numbering)."""
import os, re, json

ROOT = r"E:\F4SE OG\CommonLibF4\include"
# virtual ... Name(...)... ; // NN   -- capture the pre-'(' text and the hex slot
LINE = re.compile(r'^\s*virtual\b(?P<decl>.*?)\)\s*(?:const)?\s*(?:noexcept)?\s*(?:override)?\s*(?:=\s*0)?\s*;\s*//\s*(?P<slot>[0-9A-Fa-f]+)\b')
CLASS = re.compile(r'^\s*class\s+(?:__declspec\([^)]*\)\s+)?([A-Za-z_]\w*)\b')

def method_name(decl):
    # decl = text between 'virtual' and the first ')'. The method name is the identifier
    # immediately BEFORE the first '(' (return-type tokens precede it, args follow inside).
    p = decl.find('(')
    if p < 0:
        return ''
    s = decl[:p]
    i = len(s) - 1
    while i >= 0 and s[i].isspace():
        i -= 1
    end = i + 1
    while i >= 0 and (s[i].isalnum() or s[i] == '_' or s[i] == '~'):
        i -= 1
    return s[i+1:end]

results = {}   # class -> {slotInt: method}
for dp, _, files in os.walk(ROOT):
    for fn in files:
        if not fn.endswith('.h'):
            continue
        path = os.path.join(dp, fn)
        try:
            lines = open(path, encoding='utf-8', errors='ignore').read().splitlines()
        except Exception:
            continue
        cur_class = None
        slots = {}
        last_slot = -1
        stopped = False
        for ln in lines:
            cm = CLASS.match(ln)
            if cm:
                # commit previous class
                if cur_class and slots:
                    results.setdefault(cur_class, {}).update(slots)
                cur_class = cm.group(1)
                slots = {}
                last_slot = -1
                stopped = False
                continue
            if cur_class is None or stopped:
                continue
            m = LINE.match(ln)
            if not m:
                continue
            slot = int(m.group('slot'), 16)
            if slot > 0x400:   # implausible as a vtable slot -> it's an address/other hex in the comment
                continue
            name = method_name(m.group('decl'))
            if not name:
                continue
            # primary vtable: stop at a backward jump (secondary MI vtable restart)
            if slot < last_slot - 2:   # tolerate tiny out-of-order (overload pairs like 18/19)
                stopped = True
                continue
            slots[slot] = name
            last_slot = max(last_slot, slot)
        if cur_class and slots:
            results.setdefault(cur_class, {}).update(slots)

# Keep only classes with >=3 named slots (enough to detect a shift meaningfully)
filtered = {c: v for c, v in results.items() if len(v) >= 3}
print(f"parsed {len(results)} classes with slot comments; {len(filtered)} with >=3 slots")
out = r"C:\Users\Game\AppData\Local\Temp\claude\e--F4SE-OG\d82cbba8-0386-4842-be2a-484840ba69a4\scratchpad\commonlib_vtables.json"
with open(out, 'w', encoding='utf-8') as f:
    json.dump({c: {str(k): v for k, v in sorted(vv.items())} for c, vv in filtered.items()}, f, indent=0)
print("wrote", out)
# sample
for c in ['hknpShape', 'Actor', 'TESObjectREFR', 'PlayerCharacter']:
    if c in filtered:
        items = sorted(filtered[c].items())[:6]
        print(f"  {c}: " + ", ".join(f"[{k:x}]={v}" for k, v in items))
