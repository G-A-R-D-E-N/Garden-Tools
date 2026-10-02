import sys

runs = []
for cp in range(0x80, 0x10000):
    if 0xD800 <= cp <= 0xDFFF:
        continue
    upper = chr(cp).upper()
    if len(upper) != 1 or ord(upper) == cp:
        continue
    delta = ord(upper) - cp
    if runs:
        first, last, step, d = runs[-1]
        gap = cp - last
        if d == delta and gap in (1, 2) and (first == last or step == gap):
            runs[-1] = [first, cp, gap, d]
            continue
    runs.append([cp, cp, 1, delta])

with open(sys.argv[1], "w", newline="\n") as out:
    for first, last, step, delta in runs:
        out.write(f"{{0x{first:04X}, 0x{last:04X}, {step}, {delta}}},\n")
