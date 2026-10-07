"""Lists callers of a guest function, following lis/addi/mtctr/bctr veneers.
   python tools/find_callers.py 0x827E5610 [...]"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ppcdis import u32
LO, HI = 0x826F0000, 0x8382C000

def veneers_to(target):
    out = []
    for a in range(LO, HI, 4):
        w = u32(a)
        if (w >> 16) == 0x3D60 and u32(a + 8) == 0x7D6903A6 and u32(a + 12) == 0x4E800420:
            w2 = u32(a + 4)
            if (w2 >> 16) == 0x396B:
                lo = w2 & 0xFFFF
                val = (((w & 0xFFFF) << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
                if val == target: out.append(a)
    return out

def branches_to(targets):
    hits = []
    for a in range(LO, HI, 4):
        w = u32(a)
        if (w >> 26) == 18 and not (w & 2):
            off = w & 0x03FFFFFC
            if off & 0x02000000: off -= 0x04000000
            t = (a + off) & 0xFFFFFFFF
            if t in targets: hits.append((a, 'bl' if w & 1 else 'b', t))
    return hits

if __name__ == '__main__':
    for arg in sys.argv[1:]:
        t = int(arg, 16)
        ts = {t, *veneers_to(t)}
        print(f'{t:08X}: veneers {[hex(v) for v in sorted(ts - {t})]}')
        for a, kind, tt in branches_to(ts): print(f'   {kind} at {a:08X} -> {tt:08X}')
