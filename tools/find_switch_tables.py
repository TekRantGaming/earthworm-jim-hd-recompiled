"""Declares every jump table in the image as a [[switch_tables]] entry.

ReXGlue sometimes recovers fewer cases than a table has (one table in
sub_82AAFB28 is bounds-checked against 32 but only case 0 was recovered), and a
switch index outside the recovered cases hits __builtin_trap: the game dies
with an illegal instruction. This reads each table the way the CPU does:

    cmplwi crN, rI, MAX      ; bgt default     (bound: MAX + 1 entries)
    ... lis r12, hi ; rlwinm r0, rI, 2, 0, 29 ; addi r12, r12, lo
    lwzx r0, r12, r0 ; mtctr r0 ; bctr          (absolute 32-bit entries)

and the compact forms MSVC/XDK emit for dense switches:

    lis r12, hi ; addi r12, r12, lo ; lbzx/lhzx r0, r12, rI (or r0 = rI*2)
    lis r12, base_hi ; addi r12, r12, base_lo ; rlwinm r0, r0, 2, ... ; add r12, r12, r0
    mtctr r12 ; bctr                             (entries are offsets from a base)

  python tools/find_switch_tables.py [--survey]
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from ppcdis import u32

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ewj')
CODE = [(0x826F0000, 0x827E55C0), (0x82819A00, 0x8382C467)]
BCTR, MTCTR_R0, MTCTR_R12 = 0x4E800420, 0x7C0903A6, 0x7D8903A6


def in_code(a):
    return (a & 3) == 0 and any(lo <= a < hi for lo, hi in CODE)


def simm(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def reg_value(a, reg, back=24):
    """Value of `reg` built by lis/addi (or lis/ori) in the `back` instructions before a."""
    lo = None
    for i in range(1, back + 1):
        w = u32(a - 4 * i)
        op, rd, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if rd != reg:
            continue
        if op == 14 and ra == reg and lo is None:      # addi reg, reg, lo
            lo = simm(w)
        elif op == 24 and ra == reg and lo is None:    # ori (rA is the target in ori)
            pass
        elif op == 15 and ra == 0:                     # lis reg, hi
            return ((w & 0xFFFF) << 16) + (lo or 0) & 0xFFFFFFFF
        else:
            return None
    return None


def bound(a, back=40):
    """(index register, entry count) from the nearest preceding 'cmplwi crN, rX, MAX'."""
    for i in range(1, back + 1):
        w = u32(a - 4 * i)
        if (w >> 26) == 10 and ((w >> 21) & 3) == 0:   # cmplwi (L=0)
            return (w >> 16) & 31, (w & 0xFFFF) + 1
    return None, None


def guarded_bound(a, back=16):
    """Entry count from the nearest 'cmplwi crN, rX, MAX' guarded by a 'bgt crN' just after it."""
    for i in range(1, back + 1):
        w = u32(a - 4 * i)
        if (w >> 26) != 10 or ((w >> 21) & 3) != 0:
            continue
        for j in range(1, 4):  # bgt on the same CR field within the next few instructions
            nxt = u32(a - 4 * i + 4 * j)
            if (nxt >> 26) == 16 and ((nxt >> 21) & 31) == 12 and ((nxt >> 16) & 31) == ((w >> 23) & 7) * 4 + 1:
                return (w & 0xFFFF) + 1
    return None


_STARTS = None


def function_extent(a):
    """[start, next start) of the function holding `a`, from the last codegen partition."""
    global _STARTS
    import bisect, json, re
    if _STARTS is None:
        part = open(os.path.join(ROOT, 'generated', 'default', 'codegen.partition.json')).read()
        _STARTS = sorted(int(k, 16) for k in re.findall(r'"([0-9A-F]{8})"\s*:', part))
    i = bisect.bisect_right(_STARTS, a)
    return _STARTS[i - 1], (_STARTS[i] if i < len(_STARTS) else a + 0x10000)


def inline_table(a, base):
    """Absolute table placed right after the bctr: read entries while they are code near it."""
    labels = []
    while len(labels) < 1024:
        t = u32(base + 4 * len(labels))
        if not (in_code(t) and abs(t - a) < 0x40000 and t > base):
            break
        labels.append(t)
    return labels


def find_index_reg(a, back=12):
    """rI in 'rlwinm r0, rI, 2, 0, 29' before a bctr (absolute form)."""
    for i in range(1, back + 1):
        w = u32(a - 4 * i)
        if (w >> 26) == 21 and ((w >> 16) & 31) == 0 and ((w >> 11) & 31) == 2:
            return (w >> 21) & 31
    return None


def tables():
    out, kinds = {}, collections.Counter()
    for lo, hi in CODE:
        for a in range(lo + 8, hi, 4):
            if u32(a) != BCTR:
                continue
            prev = u32(a - 4)
            if prev == MTCTR_R0 and u32(a - 8) == 0x7C0C002E:          # lwzx r0, r12, r0
                kinds['absolute'] += 1
                base = reg_value(a - 8, 12)
                idx = find_index_reg(a)
                creg, n = bound(a)
                if base == a + 4 and idx is not None:
                    labels = inline_table(a, base)
                    if labels:
                        out[a] = (idx, labels)
                        continue
                if base is None or idx is None or n is None or n > 4096:
                    kinds['absolute: unparsed'] += 1
                    continue
                labels = [u32(base + 4 * i) for i in range(n)]
                if not all(in_code(t) and abs(t - a) < 0x40000 for t in labels):
                    kinds['absolute: bad entries'] += 1
                    continue
                out[a] = (idx, labels)
            elif prev == MTCTR_R12:
                kinds['offset (mtctr r12)'] += 1
                t = offset_table(a)
                if t is None:
                    kinds['offset: unparsed'] += 1
                else:
                    out[a] = t
            else:
                kinds['other bctr'] += 1
    return out, kinds


def offset_table(a):
    """Compact tables:  lis r12,t ; addi r12,r12,t ; lbzx/lhzx r0,r12,rI ; slwi r0,r0,SH
                        lis r12,b ; nop ; addi r12,r12,b ; add r12,r12,r0 ; mtctr r12 ; bctr
    Entry count from 'cmplwi rI, MAX' or from a 'clrlwi rI, rX, MB' mask on the index."""
    if u32(a - 8) != 0x7D8C0214:                           # add r12, r12, r0
        return None
    base = reg_value(a - 8, 12, back=6)
    if base is None:
        return None
    shift, load = 0, None
    for i in range(3, 12):
        w = u32(a - 4 * i)
        op, rd, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if op == 21 and rd == 0 and ra == 0 and shift == 0:  # rlwinm r0, r0, SH, 0, 31-SH (slwi)
            shift = (w >> 11) & 31
        xo = (w >> 1) & 0x3FF
        if op == 31 and rd == 0 and ra == 12 and xo in (87, 279):  # lbzx / lhzx r0, r12, rI
            load = (a - 4 * i, (w >> 11) & 31, 1 if xo == 87 else 2)
            break
    if load is None:
        return None
    at, idx, size = load
    if idx == 0:
        # lhzx r0, r12, r0 with r0 = rI << 1 (slwi r0, rI, 1) a few instructions earlier.
        for i in range(1, 6):
            w = u32(at - 4 * i)
            if (w >> 26) == 21 and ((w >> 16) & 31) == 0 and ((w >> 11) & 31) == size - 1 and size > 1:
                idx = (w >> 21) & 31
                break
        else:
            return None
    table = reg_value(at, 12, back=4)
    if table is None:
        return None
    n = None
    for i in range(1, 24):                                 # bound on the index register
        w = u32(at - 4 * i)
        op, rd, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if op == 10 and ((w >> 21) & 3) == 0 and ra == idx:   # cmplwi crN, rI, MAX
            n = (w & 0xFFFF) + 1
            break
        if op == 21 and ra == idx and ((w >> 1) & 31) == 31:  # rlwinm rI, rX, SH, MB, 31: a mask
            mb = (w >> 6) & 31
            n = 1 << (32 - mb)
            break
    if n is None:
        n = guarded_bound(at)  # the compare is on a copy of the index register
    if n is None:
        # No bound near the dispatch (it is reached from a loop or another block):
        # read entries while they stay inside the containing function. Extra
        # cases are never taken; missing ones trap.
        lo, hi = function_extent(a)
        n = 0
        while n < 256:
            off = u32(table + size * n) >> (32 - 8 * size)
            t = (base + (off << shift)) & 0xFFFFFFFF
            if not lo <= t < hi or u32(t - 4) == 0:  # past the end, or into padding
                break
            n += 1
        if n == 0:
            return None
    if n > 4096:
        return None
    labels = []
    for k in range(n):
        off = u32(table + size * k) >> (32 - 8 * size)
        labels.append((base + (off << shift)) & 0xFFFFFFFF)
    if not all(in_code(t) and abs(t - a) < 0x40000 for t in labels):
        return None
    return idx, labels


def clamp_to_function(found):
    """Entries that land in another function (e.g. 4 of the 64 in the 6-bit-masked
    table at 0x82C062E4) cannot be cases of this switch: point them at the table's
    most common target, its default handler, instead of leaving an unresolved jump."""
    import collections
    fixed = 0
    for a, (reg, labels) in found.items():
        lo, hi = function_extent(a)
        default = collections.Counter(labels).most_common(1)[0][0]
        for i, t in enumerate(labels):
            if not lo <= t < hi:
                labels[i] = default
                fixed += 1
    return fixed


def main():
    found, kinds = tables()
    kinds['entries pointed at default (outside the function)'] = clamp_to_function(found)
    for k, v in sorted(kinds.items()):
        print(f'{k}: {v}')
    print(f'tables declared: {len(found)}')
    if '--survey' in sys.argv:
        return
    lines = ['# Auto-generated by tools/find_switch_tables.py: every jump table, read from the image.', '']
    for a, (reg, labels) in sorted(found.items()):
        lines.append('[[switch_tables]]')
        lines.append(f'address = 0x{a:08X}')
        lines.append(f'register = {reg}')
        lines.append('labels = [' + ', '.join(f'0x{t:08X}' for t in labels) + ']')
        lines.append('')
    open(os.path.join(ROOT, 'switch_tables.toml'), 'w').write('\n'.join(lines))


if __name__ == '__main__':
    main()
