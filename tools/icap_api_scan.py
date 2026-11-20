#!/usr/bin/env python3
"""Scan ICAP .APP files for indirect calls through the service table (ldr rX,[rY,#off]; blx rX)
and print, per offset, which apps use it (with the struct icap_svc field name from the official
SDK) and any string literal passed in r0..r2 nearby. Usage: icap_api_scan.py APP [APP...]
Useful to see how the reference apps use a service; the ABI itself is now app/icap.h."""
import os, sys, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icap_sim import SVC_LAYOUT
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB

def load(path):
    d = open(path, 'rb').read()
    hs = struct.unpack_from('<H', d, 6)[0]           # hdr_size (64, o 96 con i modi registrati)
    img = struct.unpack_from('<I', d, 0x10)[0]         # load_size
    nrel = struct.unpack_from('<I', d, 0x1c)[0]        # reloc_count
    name = d[0x28:0x38].split(b'\0')[0].decode()
    return name, d[hs:hs+img], hs

def cstr(image, off):
    if 0 <= off < len(image):
        e = image.find(b'\0', off)
        s = image[off:e]
        if 2 <= len(s) < 120 and all(32 <= c < 127 or c in (9, 10) for c in s):
            return s.decode()
    return None

uses = collections.defaultdict(lambda: collections.defaultdict(list))
for p in sys.argv[1:]:
    name, image, hs = load(p)
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM); md.detail = False
    insns = list(md.disasm(image, 0))
    regs_str = {}
    for k, i in enumerate(insns):
        # track "ldr rN, [pc, #imm]" loading an image-relative pointer to a string
        if i.mnemonic == 'ldr' and '[pc' in i.op_str:
            try:
                r = i.op_str.split(',')[0]
                imm = int(i.op_str.split('#')[1].rstrip(']'), 0)
                lit = i.address + 8 + imm
                val = struct.unpack_from('<I', image, lit)[0]
                s = cstr(image, val)
                if s: regs_str[r] = s
            except Exception: pass
        if i.mnemonic == 'blx' and i.op_str.startswith('r'):
            tgt = i.op_str
            # le chiamate protette da ICAP_HAS() caricano il puntatore prima di cmp/beq e
            # del setup degli argomenti: risali fino a 8 istruzioni, fermandoti se tgt
            # viene scritto da altro o se si attraversa un salto non condizionale
            for j in range(k-1, max(k-9, -1), -1):
                pj = insns[j]
                if pj.mnemonic in ('b', 'bl', 'bx', 'blx', 'pop'): break
                if pj.mnemonic == 'ldr' and pj.op_str.startswith(tgt + ',') and '#' in pj.op_str and '[pc' not in pj.op_str:
                    try: off = int(pj.op_str.split('#')[1].rstrip(']'), 0)
                    except ValueError: break
                    hint = [regs_str.get(r) for r in ('r0', 'r1', 'r2') if regs_str.get(r)]
                    uses[off][name].append(hint[0] if hint else '')
                    break
                if pj.op_str.startswith(tgt + ',') and pj.mnemonic not in ('cmp', 'cmn', 'tst', 'teq', 'str', 'strb', 'strh'):
                    break
            regs_str.clear()
        if i.mnemonic in ('bl', 'b') : regs_str.clear()

for off in sorted(uses):
    apps = uses[off]
    print(f"+0x{off:03x} {SVC_LAYOUT.get(off, '(dato)'):14s}: " + ", ".join(f"{a}x{len(v)}" for a, v in sorted(apps.items())))
    hints = sorted({h for v in apps.values() for h in v if h})[:4]
    for h in hints: print(f"          e.g. {h!r}")
