"""Tiny helpers for reading the dumped guest image (ewj/image.bin) with capstone."""
import struct, sys
import capstone
BASE = 0x82000000
import os
IMG = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ewj', 'image.bin'), 'rb').read()
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
def u32(a): return struct.unpack('>I', IMG[a-BASE:a-BASE+4])[0]
def dis(a, n):
    for i in range(n):
        ad = a + 4*i
        w = IMG[ad-BASE:ad-BASE+4]
        ins = list(md.disasm(w, ad))
        print(f'{ad:08X}: {w.hex()}  ' + (f'{ins[0].mnemonic} {ins[0].op_str}' if ins else '.long'))
if __name__ == '__main__':
    dis(int(sys.argv[1], 16), int(sys.argv[2]))

def pdata_starts():
    """Function starts listed in .pdata (0x82691800, 8-byte entries: begin, packed lengths).
    Config [functions] entries override .pdata sizes (which cover SEH funclets), so
    generated lists must leave these addresses alone."""
    starts = set()
    for a in range(0x82691800, 0x82691800 + 0x4EB60, 8):
        b = u32(a)
        if b:
            starts.add(b)
    return starts
