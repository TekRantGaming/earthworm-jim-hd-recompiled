"""Finds strings and 32-bit values in the dumped guest image.

  python tools/find_refs.py str "update:\\"      every occurrence of a string
  python tools/find_refs.py ptr 0x83391F80      data words equal to the value, and lis/addi pairs building it
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from ppcdis import BASE, IMG, u32


def find_all(needle, limit=20):
    hits, i = [], -1
    while len(hits) < limit:
        i = IMG.find(needle, i + 1)
        if i < 0:
            break
        hits.append(BASE + i)
    return hits


def lis_addi_refs(value, lo=0x826F0000, hi=0x8382C000):
    hits, lis = [], [None] * 32
    for a in range(lo, hi, 4):
        w = u32(a)
        op, rd, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if op == 15 and ra == 0:
            lis[rd] = ((w & 0xFFFF) << 16) & 0xFFFFFFFF
            continue
        if op in (14, 24) and lis[ra] is not None:
            imm = w & 0xFFFF
            v = (lis[ra] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF if op == 14 else lis[ra] | imm
            if v == value:
                hits.append(a)
        if w in (0x4E800020, 0x4E800420) or ((w >> 26) == 18 and (w & 3) == 0):
            lis = [None] * 32
    return hits


if __name__ == '__main__':
    kind, arg = sys.argv[1], sys.argv[2]
    if kind == 'str':
        s = arg.encode().decode('unicode_escape').encode('latin-1')
        for a in find_all(s):
            end = IMG.find(b'\0', a - BASE)
            print(f'{a:08X}: {IMG[a - BASE:end]!r}')
    else:
        v = int(arg, 16)
        print('data:', [f'{a:08X}' for a in find_all(struct.pack('>I', v))])
        print('lis/addi:', [f'{a:08X}' for a in lis_addi_refs(v)])
