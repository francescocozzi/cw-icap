#!/usr/bin/env python3
"""icap_sim - esegue una app ICAP (.APP) in un emulatore ARM (Unicorn) con una struct icap_svc
finta (layout dell'SDK ufficiale v1rc, app/icap.h), alimentandola con un WAV a 8 kHz.
Serve a verificare SUL PC, prima della radio:
  - che il .APP (header 64 o 96 byte con modi registrati) sia impacchettato e rilocato bene,
  - che l'app usi i servizi come previsto (conteggio di ogni chiamata),
  - che controlli icap_should_stop() almeno ogni 500 ms (tempo simulato) e non chiami il TX,
  - che il decoder giri come codice ARM con VFP/NEON (lo stesso binario che va sulla SD).

Uso:
  icap_sim.py APP --wav test.wav [--cfg CW.CFG] [--start-mode CW] [--trace] [--overrun-every N]
                  [--expect "TESTO"]   (confronta il testo dopo ' : ': righe DECODE se avviata da
                                        un modo, altrimenti righe della finestra che contengono ' : ')

Tempo simulato: avanza con icap_sleep_ms e con i campioni consegnati da icap_audio_read
(n/8 ms); il calcolo dell'app non lo fa avanzare. Esce con codice != 0 se l'app ritorna != 0,
va in errore, chiama tx_*, o lascia passare più di 500 ms tra due icap_should_stop().
Se cambi icap.h (nuova versione dell'SDK) aggiorna SVC_LAYOUT qui sotto.
"""
import argparse, re, struct, sys, wave, zlib
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

BASE, STACK_TOP, SVC_BASE, HOOK_BASE, HEAP = 0x20100000, 0x20400000, 0x20500000, 0x20600000, 0x20700000
WINDOW = 0x200000            # finestra dell'app (immagine + bss + spazio libero)
HEAP_SIZE = 0x40000

# struct icap_svc (icap.h, SDK v1rc): offset -> nome. I campi dati (0x00..0x0c, 0x48, 0x4c)
# sono scritti a parte; tutti gli altri sono puntatori a funzione.
SVC_LAYOUT = {
    0x10: 'print', 0x14: 'set_line', 0x18: 'clear',
    0x1c: 'f_open', 0x20: 'f_read', 0x24: 'f_write', 0x28: 'f_close',
    0x2c: 'sleep_ms', 0x30: 'yield', 0x34: 'tick_ms',
    0x38: 'mem_info', 0x3c: 'radio_state', 0x40: 'should_stop', 0x44: 'task_id',
    # 0x48 heap_base, 0x4c heap_size (dati)
    0x50: 'clock_now', 0x54: 'audio_read', 0x58: 'audio_rate', 0x5c: 'decode_out',
    0x60: 'tx_begin', 0x64: 'tx_audio_write', 0x68: 'tx_end',
    0x6c: 'net_status', 0x70: 'net_open', 0x74: 'net_send', 0x78: 'net_recv', 0x7c: 'net_close',
    0x80: 'net_error', 0x84: 'popup', 0x88: 'remove', 0x8c: 'mode',
}
SVC_SIZE = 0x90
ABI_VERSION, RADIO_IC705 = 3, 0x0705

E_ARG, E_NOFD, E_IO, E_NAME, E_PERM, E_NOSYS, E_OVERRUN, E_BUSY, E_NONET = -1, -2, -3, -4, -5, -6, -7, -8, -9
O_WRITE, O_ROOT, O_RADIO = 1, 0x10, 0x20
NAME_RE = re.compile(r'^[A-Za-z0-9_\-][A-Za-z0-9_\-.]{0,46}[A-Za-z0-9_\-]$|^[A-Za-z0-9_\-]$')
STOP_MAX_MS = 500

HDR_FMT = '<4sHHIIIIIIII16sII'
HDR_FIELDS = ['magic', 'abi_version', 'hdr_size', 'models', 'fw_build', 'load_size', 'bss_size',
              'entry_off', 'reloc_count', 'stack_size', 'crc32', 'name', 'flags', 'seg2_size']


def load_app(path):
    d = open(path, 'rb').read()
    h = dict(zip(HDR_FIELDS, struct.unpack_from(HDR_FMT, d, 0)))
    if h['magic'] != b'ICAP':
        raise SystemExit('non è un .APP')
    hs = h['hdr_size']
    if hs not in (64, 96):
        raise SystemExit(f'hdr_size {hs} non supportato')
    if zlib.crc32(d[hs:]) & 0xffffffff != h['crc32']:
        raise SystemExit('CRC errato')
    if h['flags'] & 1:
        raise SystemExit('immagine a due segmenti (SPLIT) non supportata dal simulatore')
    modes = []
    if hs == 96:
        for i in range(4):
            nm, rm, fl = struct.unpack_from('<6sBB', d, 64 + 8 * i)
            nm = nm.split(b'\0')[0].decode('ascii', 'replace')
            if nm:
                modes.append((nm, rm))
    img = bytearray(d[hs:hs + h['load_size']])
    relocs = struct.unpack_from('<%dI' % h['reloc_count'], d, hs + h['load_size'])
    return h, img, relocs, modes


class Sim:
    def __init__(self, app, pcm, files, a):
        self.pcm, self.pos, self.files, self.trace = pcm, 0, files, a.trace
        self.overrun_every, self.reads = a.overrun_every, 0
        self.max_per_call = a.max_per_call
        self.start_mode = a.start_mode or ''
        self.freq, self.radio_mode = a.freq, a.radio_mode
        self.fds, self.next_fd = {}, 3
        self.written = {}
        self.ms = 0.0
        self.lines, self.panel, self.decodes, self.calls = {}, [], [], {}
        self.done = False
        self.stop_last, self.stop_max = 0.0, 0.0
        self.violations = []

        h, img, relocs, self.modes = load_app(app)
        self.hdr = h
        if self.start_mode and self.start_mode not in [m for m, _ in self.modes]:
            raise SystemExit(f'--start-mode {self.start_mode}: modo non registrato nell\'header ({self.modes})')
        for r in relocs:
            v, = struct.unpack_from('<I', img, r)
            struct.pack_into('<I', img, r, v + BASE)
        self.image_end = BASE + h['load_size']
        self.bss_end = self.image_end + h['bss_size']
        self.stack_lo = STACK_TOP - max(h['stack_size'], 0x1000)

        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9) if hasattr(uc, 'ctl_set_cpu_model') else None
        uc.mem_map(BASE, WINDOW)
        uc.mem_write(BASE, bytes(img))                    # bss già a zero
        uc.mem_map(STACK_TOP - 0x100000, 0x101000)         # +1 pagina: letture di stringhe vicino al top
        uc.mem_map(SVC_BASE, 0x1000)
        uc.mem_map(HOOK_BASE, 0x1000)
        uc.mem_map(HEAP, HEAP_SIZE)
        uc.mem_write(HOOK_BASE, b'\x1e\xff\x2f\xe1' * 0x400)   # bx lr
        svc = bytearray(SVC_SIZE)
        struct.pack_into('<IIII', svc, 0x00, ABI_VERSION, RADIO_IC705, 0, SVC_SIZE)
        struct.pack_into('<II', svc, 0x48, HEAP, HEAP_SIZE)
        for off in SVC_LAYOUT:
            struct.pack_into('<I', svc, off, HOOK_BASE + off)
        uc.mem_write(SVC_BASE, bytes(svc))
        # VFP/NEON: sulla radio li accende il trap della prima istruzione VFP di icap_start.S
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xf << 20))
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        uc.hook_add(UC_HOOK_CODE, self.on_hook, begin=HOOK_BASE, end=HOOK_BASE + 0xfff)

    # ---- helper ----
    def cstr(self, addr, n=512):
        b = b''
        for i in range(n):                                # byte per byte: non sconfina in memoria non mappata
            try:
                c = bytes(self.uc.mem_read(addr + i, 1))
            except UcError:
                break
            if c == b'\0':
                break
            b += c
        return b.decode('utf-8', 'replace')

    def in_app(self, addr, n):
        """i buffer passati ai servizi devono stare nella memoria dell'app (immagine/bss/stack/heap)"""
        return any(lo <= addr and addr + n <= hi for lo, hi in
                   ((BASE, BASE + WINDOW), (self.stack_lo, STACK_TOP), (HEAP, HEAP + HEAP_SIZE)))

    def ret(self, v):
        self.uc.reg_write(UC_ARM_REG_R0, v & 0xffffffff)

    def log(self, s):
        if self.trace:
            print(f'[{self.ms / 1000:7.2f}s] {s}')

    def vfile(self, fn):
        return {'@FREQ': b'f=7030000 usb=1 rx=main trv=0\n', '@CLOCK': b'L=0 P=60 O=0 N=0\n',
                '@MYCALL': b'N0CALL\n', '@MYLOC': b'\n'}.get(fn)

    # ---- servizi ----
    def on_hook(self, uc, address, size, _):
        off = address - HOOK_BASE
        name = SVC_LAYOUT.get(off, f'?{off:#x}')
        r0, r1, r2, r3 = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
        self.calls[name] = self.calls.get(name, 0) + 1
        s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v

        if name == 'print':
            self.panel.append(self.cstr(r0)); self.log(f'print: {self.panel[-1]}')
        elif name == 'set_line':
            line, s = s32(r0), self.cstr(r1)
            if not 0 <= line <= 7:
                self.violations.append(f'set_line({line}): righe valide 0..7')
            if self.lines.get(line) != s:
                self.lines[line] = s; self.log(f'riga {line}: {s}')
        elif name == 'clear':
            self.lines.clear(); self.panel.clear()
        elif name == 'f_open':
            fn, mode = self.cstr(r0), r1
            v = self.vfile(fn)
            if len([f for f in self.fds.values()]) >= 4:
                self.ret(E_NOFD)
            elif fn.startswith('@'):
                if mode & O_WRITE or v is None:
                    self.ret(E_NAME)                    # @SPOT e simili non simulati
                else:
                    fd = self.next_fd; self.next_fd += 1
                    self.fds[fd] = [v, 0]; self.ret(fd)
            elif not NAME_RE.match(fn):
                self.ret(E_NAME)
            elif mode & O_WRITE:                        # scrittura: raccolta in self.written
                fd = self.next_fd; self.next_fd += 1
                self.fds[fd] = [bytearray(), 0, fn]; self.ret(fd)
            elif fn in self.files:
                fd = self.next_fd; self.next_fd += 1
                self.fds[fd] = [self.files[fn], 0]; self.ret(fd)
            else:
                self.ret(E_IO)
        elif name == 'f_read':
            f = self.fds.get(s32(r0))
            if not f or len(f) == 3:
                self.ret(E_ARG)
            elif not self.in_app(r1, r2):
                self.violations.append(f'f_read: buffer {r1:#x} fuori dalla memoria dell\'app'); self.ret(E_ARG)
            else:
                chunk = f[0][f[1]:f[1] + r2]; f[1] += len(chunk)
                uc.mem_write(r1, bytes(chunk)); self.ret(len(chunk))
        elif name == 'f_write':
            f = self.fds.get(s32(r0))
            if not f or len(f) < 3:
                self.ret(E_ARG)
            else:
                f[0] += bytes(uc.mem_read(r1, r2)); self.ret(r2)
        elif name == 'f_close':
            f = self.fds.pop(s32(r0), None)
            if f and len(f) == 3:
                self.written[f[2]] = bytes(f[0])
            self.ret(0 if f else E_ARG)
        elif name == 'remove':
            fn = self.cstr(r0)
            self.ret(0 if self.files.pop(fn, None) is not None or self.written.pop(fn, None) is not None else E_IO)
        elif name == 'sleep_ms':
            self.ms += r0
        elif name == 'yield':
            pass
        elif name == 'tick_ms':
            self.ret(int(self.ms))
        elif name == 'mem_info':
            free_lo = (self.bss_end + 7) & ~7
            uc.mem_write(r0, struct.pack('<8I', BASE, BASE + WINDOW, self.image_end, self.bss_end,
                                         self.stack_lo, STACK_TOP, free_lo, BASE + WINDOW))
            self.ret(0)
        elif name == 'radio_state':
            uc.mem_write(r0, struct.pack('<IIBBxx', self.freq, 0, self.radio_mode, 0)); self.ret(0)
        elif name == 'should_stop':
            gap = self.ms - self.stop_last
            self.stop_max = max(self.stop_max, gap)
            self.stop_last = self.ms
            self.ret(1 if self.done else 0)
        elif name == 'task_id':
            self.ret(42)
        elif name == 'clock_now':
            t = 12 * 3600 + 34 * 60 + int(self.ms / 1000)      # 2026-11-20 12:34:00 + tempo simulato
            uc.mem_write(r0, struct.pack('<HBBBBBB', 2026, 11, 20, t // 3600 % 24, t // 60 % 60, t % 60, 5))
            self.ret(0)
        elif name == 'audio_rate':
            self.ret(8000)
        elif name == 'audio_read':
            self.reads += 1
            n = min(s32(r1), self.max_per_call, len(self.pcm) - self.pos)
            if self.overrun_every and self.reads % self.overrun_every == 0:
                self.pos += 160; self.ms += 20                 # 20 ms persi
                self.ret(E_OVERRUN)
            elif n <= 0:
                self.done = True                               # fine del WAV: chiede lo stop
                self.ret(0)
            else:
                uc.mem_write(r0, struct.pack('<%dh' % n, *self.pcm[self.pos:self.pos + n]))
                self.pos += n
                self.ms += n / 8.0
                self.ret(n)
        elif name == 'decode_out':
            s = self.cstr(r0)
            self.decodes.append(s); self.log(f'DECODE: {s}')
            if not self.start_mode and len(self.decodes) == 1:
                # IC-705 v14: avviata da Apps, decode_out si comporta come print() e fa scorrere
                # la finestra, mescolandosi con le righe di set_line (provato sulla radio)
                self.violations.append('decode_out con app avviata da Apps: sulla radio fa scorrere '
                                       'la finestra come print()')
        elif name in ('tx_begin', 'tx_audio_write', 'tx_end'):
            self.violations.append(f'{name}: l\'app ha chiamato il TX')
            self.ret(E_PERM)
        elif name.startswith('net_'):
            self.ret(E_NONET if name != 'net_error' else 0)
        elif name == 'popup':
            print('[popup]', self.cstr(r0)); self.ret(0)
        elif name == 'mode':
            b = self.start_mode.encode()[:max(0, r1 - 1)]
            uc.mem_write(r0, b + b'\0'); self.ret(len(b))
        else:
            self.violations.append(f'chiamata a slot sconosciuto {name}')
            self.ret(E_NOSYS)

    def run(self, max_seconds):
        uc = self.uc
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP - 16)
        uc.reg_write(UC_ARM_REG_R0, SVC_BASE)
        uc.reg_write(UC_ARM_REG_LR, HOOK_BASE + 0xff0)    # ritorno da app_main
        try:
            uc.emu_start(BASE + self.hdr['entry_off'], HOOK_BASE + 0xff0, count=int(max_seconds * 8000 * 2500))
        except UcError as e:
            pc = uc.reg_read(UC_ARM_REG_PC)
            print(f'ERRORE emulazione: {e} a pc=0x{pc:x} (immagine+0x{pc - BASE:x})', file=sys.stderr)
            return 2
        r0 = uc.reg_read(UC_ARM_REG_R0)
        return r0 - (1 << 32) if r0 & 0x80000000 else r0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('app'); ap.add_argument('--wav', required=True)
    ap.add_argument('--cfg', help='file da esporre come <NOME>.CFG nella cartella dell\'app')
    ap.add_argument('--start-mode', help='simula l\'avvio dal modo decoder registrato (icap_mode)')
    ap.add_argument('--freq', type=int, default=7030000, help='icap_radio_state: frequenza (Hz)')
    ap.add_argument('--radio-mode', type=lambda x: int(x, 0), default=0x22, help='icap_radio_state: codice modo (0x22 = CW su IC-705, misurato con PROBE)')
    ap.add_argument('--max-per-call', type=int, default=60,
                    help='campioni massimi per audio_read (60 sulla radio, misurato con PROBE)')
    ap.add_argument('--overrun-every', type=int, default=0, help='ICAP_E_OVERRUN ogni N letture audio')
    ap.add_argument('--expect', help='testo atteso su DECODE (parti dopo " : ", spazi normalizzati)')
    ap.add_argument('--trace', action='store_true')
    ap.add_argument('--max-seconds', type=float, default=300)
    a = ap.parse_args()
    with wave.open(a.wav) as w:
        if w.getframerate() != 8000 or w.getnchannels() != 1 or w.getsampwidth() != 2:
            raise SystemExit('serve WAV mono 16 bit 8 kHz')
        raw = w.readframes(w.getnframes())
    pcm = list(struct.unpack('<%dh' % (len(raw) // 2), raw))
    h, _, _, _ = load_app(a.app)
    appname = h['name'].split(b'\0')[0].decode()
    files = {f'{appname}.CFG': open(a.cfg, 'rb').read()} if a.cfg else {}
    sim = Sim(a.app, pcm, files, a)
    rc = sim.run(a.max_seconds)

    print(f"--- {appname}: modi registrati {sim.modes or 'nessuno'}, avvio "
          f"{'dal modo ' + sim.start_mode if sim.start_mode else 'da menu Apps'} ---")
    print('--- finestra (set_line) ---')
    for k in sorted(sim.lines):
        print(f'{k}: {sim.lines[k]}')
    if sim.panel:
        print('--- pannello (print) ---'); print('\n'.join(sim.panel[-8:]))
    print(f'--- DECODE ({len(sim.decodes)} righe) ---')
    for s in sim.decodes:
        print(s)
    print('--- chiamate ---')
    print(', '.join(f'{k}={v}' for k, v in sorted(sim.calls.items())))
    for fn, data in sim.written.items():
        print(f'--- file scritto dall\'app: {fn} ({len(data)} B) ---')
        print(data.decode('latin-1')[:3000])
    print(f'intervallo massimo tra icap_should_stop(): {sim.stop_max:.0f} ms (tempo simulato, limite {STOP_MAX_MS})')
    if sim.fds:
        sim.violations.append(f'{len(sim.fds)} file lasciati aperti')
    if sim.stop_max > STOP_MAX_MS:
        sim.violations.append(f'icap_should_stop() non controllato per {sim.stop_max:.0f} ms')
    if a.expect is not None:
        src, what = sim.decodes, 'DECODE'
        if not sim.start_mode:
            src, what = [sim.lines[k] for k in sorted(sim.lines) if ' : ' in sim.lines[k]], 'finestra'
        got = ' '.join(' '.join(s.split(' : ', 1)[-1] for s in src).split())
        if got != ' '.join(a.expect.upper().split()):
            sim.violations.append(f'{what}: testo diverso dall\'atteso: {got!r}')
        else:
            print(f'{what}: il testo coincide con quello atteso')
    for v in sim.violations:
        print('VIOLAZIONE:', v)
    print(f'app_main ha restituito {rc}')
    sys.exit(0 if rc == 0 and not sim.violations else 1)


if __name__ == '__main__':
    main()
