"""Finds the CRT setjmp/longjmp candidates in the dumped image: functions that
store (setjmp) or load (longjmp) r14-r31 at increasing offsets from r3."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from ppcdis import dis, u32

CODE = [(0x826F0000, 0x827E55C0), (0x82819A00, 0x8382C467)]


def run_of(a, opcode):
    """Length of a run of `opcode rN, d(r3)` with N = 14, 15, ... from `a` (std is opcode 62, ld 58)."""
    n = 0
    while True:
        w = u32(a + 4 * n)
        if (w >> 26) != opcode or ((w >> 16) & 31) != 3 or ((w >> 21) & 31) != 14 + n:
            return n
        n += 1


for lo, hi in CODE:
    for a in range(lo, hi, 4):
        for opcode, kind in ((62, 'setjmp (std r14.. to r3)'), (58, 'longjmp (ld r14.. from r3)')):
            if run_of(a, opcode) >= 18:
                start = a
                while u32(start - 4) not in (0, 0x4E800020) and a - start < 0x40:
                    start -= 4
                print(f'{kind}: run at {a:08X}, function starts near {start:08X}')
                dis(start, (a - start) // 4 + 22)
                print()
