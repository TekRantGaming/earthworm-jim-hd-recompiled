"""Add every 'target not in any function' address from a codegen log to a TOML [functions] file."""
import re, sys
log, out = sys.argv[1], sys.argv[2]
targets = sorted({int(m.group(1), 16) for m in re.finditer(r'(0x[0-9A-F]{8}) from 0x[0-9A-F]{8}: b\S* .*target not in any function',
                                                         open(log, encoding='utf-8', errors='ignore').read())})
existing = set()
try:
    for line in open(out):
        m = re.match(r'(0x[0-9A-Fa-f]{8})\s*=', line)
        if m: existing.add(int(m.group(1), 16))
except FileNotFoundError:
    pass
new = [t for t in targets if t not in existing]
with open(out, 'a') as f:
    if not existing: f.write('# Tail-call-only entry points the analyzer did not discover (seeded from codegen errors).\n[functions]\n')
    for t in new: f.write(f'0x{t:08X} = {{}}\n')
print(f'{len(new)} new, {len(existing)+len(new)} total')
