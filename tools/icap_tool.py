#!/usr/bin/env python3
"""icap_tool - ispeziona e disassembla app ICAP (.APP) del firmware custom SQ6EMM (IC-705 / IC-9700).

Solo analisi. Per impacchettare si usa il packer ufficiale dell'SDK (ext/icap-sdk/tools/mkapp.py).
Nomi dei campi = struct icap_header di icap.h (SDK ufficiale, v1rc).

  icap_tool.py info    APP...              header, estensione modi, CRC, rilocazioni
  icap_tool.py disasm  APP [--all]         disassembla (ARM) con le rilocazioni annotate
  icap_tool.py strings APP                 stringhe con offset nell'immagine
"""
import argparse, struct, sys, zlib

HDR_FMT = '<4sHHIIIIIIII16sII'   # struct icap_header, 64 byte
HDR_FIELDS = ['magic', 'abi_version', 'hdr_size', 'models', 'fw_build', 'load_size', 'bss_size',
              'entry_off', 'reloc_count', 'stack_size', 'crc32', 'name', 'flags', 'seg2_size']
FLAGS = {0x1: 'SPLIT', 0x2: 'AUTOSTART', 0x4: 'BACKGROUND'}
RMODE = {0: 'keep', 1: 'usbd'}


def parse(d):
    vals = struct.unpack_from(HDR_FMT, d, 0)
    h = dict(zip(HDR_FIELDS, vals))
    if h['magic'] != b'ICAP':
        raise SystemExit('non è un file ICAP (magic %r)' % h['magic'])
    h['name'] = h['name'].split(b'\0')[0].decode('ascii', 'replace')
    hs = h['hdr_size']
    h['modes'] = []
    if hs >= 96:                       # struct icap_header_ext: 4 x {char name[6]; u8 radio_mode; u8 flags}
        for i in range(4):
            nm, rm, fl = struct.unpack_from('<6sBB', d, 64 + 8 * i)
            nm = nm.split(b'\0')[0].decode('ascii', 'replace')
            if nm:
                h['modes'].append((nm, rm, fl))
    img = d[hs:hs + h['load_size']]
    rel_off = hs + h['load_size']
    relocs = list(struct.unpack_from('<%dI' % h['reloc_count'], d, rel_off))
    seg2_off = rel_off + 4 * h['reloc_count']
    seg2 = d[seg2_off:seg2_off + h['seg2_size']] if h['flags'] & 1 else b''
    trailer = d[seg2_off + len(seg2):]
    h['crc_ok'] = (zlib.crc32(d[hs:]) & 0xffffffff) == h['crc32']   # CRC su ciò che segue header+estensione
    return h, img, relocs, seg2, trailer


def cmd_info(a):
    models = {1: '705', 2: '9700', 3: '705+9700'}
    for p in a.app:
        d = open(p, 'rb').read()
        h, img, rel, seg2, tr = parse(d)
        fl = '|'.join(n for b, n in FLAGS.items() if h['flags'] & b) or '0'
        print(f"{p}: '{h['name']}' abi={h['abi_version']} hdr={h['hdr_size']} models={models.get(h['models'], hex(h['models']))} "
              f"fw={h['fw_build']:#x} load=0x{h['load_size']:x} bss=0x{h['bss_size']:x} entry=0x{h['entry_off']:x} "
              f"stack=0x{h['stack_size']:x} relocs={h['reloc_count']} flags={fl}"
              + (f" seg2=0x{h['seg2_size']:x}" if h['flags'] & 1 else '')
              + f" crc={'OK' if h['crc_ok'] else 'ERRATO'}" + (f" trailer={len(tr)}B" if tr else ''))
        for nm, rm, f in h['modes']:
            print(f"    modo decoder: {nm} radio_mode={RMODE.get(rm, rm)} flags={f:#x}")
        bad = [r for r in rel if r + 4 > len(img)]
        if bad:
            print(f"    ATTENZIONE: {len(bad)} rilocazioni fuori dall'immagine")


def load_cs():
    try:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
    except ImportError:
        raise SystemExit('serve capstone: pip install capstone')
    return Cs(CS_ARCH_ARM, CS_MODE_ARM)


def cmd_disasm(a):
    d = open(a.app, 'rb').read()
    h, img, rel, data, tr = parse(d)
    rels = set(rel)
    md = load_cs()
    end = len(img) if a.all else min(len(img), 0x400)
    off = 0
    while off < end:
        if off in rels:
            v = struct.unpack_from('<I', img, off)[0]
            s = ''
            if v < len(img):
                e = img.find(b'\0', v)
                cand = img[v:e]
                if 1 < len(cand) < 80 and all(32 <= c < 127 for c in cand):
                    s = f'  ; "{cand.decode()}"'
            print(f'{off:06x}: .word 0x{v:x}  ; RELOC (immagine+0x{v:x}){s}')
            off += 4
            continue
        got = False
        for i in md.disasm(img[off:off + 4], off):
            print(f'{i.address:06x}: {i.bytes.hex():8s}  {i.mnemonic:8s} {i.op_str}')
            got = True
        if not got:
            print(f'{off:06x}: .word 0x{struct.unpack_from("<I", img, off)[0]:08x}')
        off += 4


def cmd_strings(a):
    d = open(a.app, 'rb').read()
    h, img, rel, data, tr = parse(d)
    for blob, base, tag in ((img, 0, 'img'), (data, 0, 'seg2')):
        cur = b''
        start = 0
        for i, c in enumerate(blob + b'\0'):
            if 32 <= c < 127 or c in (9, 10):
                if not cur:
                    start = i
                cur += bytes([c])
            else:
                if len(cur) >= 4:
                    print(f'{tag}+{start:06x}: {cur.decode()!r}')
                cur = b''


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    p = sp.add_parser('info'); p.add_argument('app', nargs='+'); p.set_defaults(fn=cmd_info)
    p = sp.add_parser('disasm'); p.add_argument('app'); p.add_argument('--all', action='store_true'); p.set_defaults(fn=cmd_disasm)
    p = sp.add_parser('strings'); p.add_argument('app'); p.set_defaults(fn=cmd_strings)
    a = ap.parse_args()
    a.fn(a)


if __name__ == '__main__':
    main()
