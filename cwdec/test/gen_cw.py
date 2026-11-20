#!/usr/bin/env python3
"""Genera segnali CW di test (WAV mono 16 bit, 8 kHz) con condizioni realistiche.

Esempi:
  gen_cw.py out.wav "CQ CQ DE IZ7KHR K" --wpm 22 --pitch 650 --snr 10
  gen_cw.py out.wav "TEST" --wpm 18 --jitter 0.15 --qsb 0.3 --drift 20 --snr 3

--snr è il rapporto segnale/rumore nella banda di 2500 Hz (come i decoder digitali), in dB.
--jitter: variazione casuale delle durate (tasto verticale/bug), frazione dell'unità.
--qsb: profondità del fading lento (0..1). --drift: deriva del tono in Hz sull'intero file.
--farnsworth N: spaziature calcolate a N WPM (caratteri a --wpm).
--start S: scarta i primi S secondi (decoder acceso a trasmissione già in corso).
--filter BW: filtro di banda della radio (es. 250 per FIL3 in CW) centrato su --filter-center
  (default il pitch), applicato a segnale+rumore come fa la radio prima dell'audio delle app.
--len S: durata minima in secondi (completa con rumore). Testo vuoto + --len = solo rumore.
"""
import argparse, wave
import numpy as np

MORSE = {
    'A': '.-', 'B': '-...', 'C': '-.-.', 'D': '-..', 'E': '.', 'F': '..-.', 'G': '--.', 'H': '....',
    'I': '..', 'J': '.---', 'K': '-.-', 'L': '.-..', 'M': '--', 'N': '-.', 'O': '---', 'P': '.--.',
    'Q': '--.-', 'R': '.-.', 'S': '...', 'T': '-', 'U': '..-', 'V': '...-', 'W': '.--', 'X': '-..-',
    'Y': '-.--', 'Z': '--..', '0': '-----', '1': '.----', '2': '..---', '3': '...--', '4': '....-',
    '5': '.....', '6': '-....', '7': '--...', '8': '---..', '9': '----.', '.': '.-.-.-', ',': '--..--',
    '?': '..--..', '/': '-..-.', '=': '-...-', '+': '.-.-.', '-': '-....-',
}
FS = 8000


def keying(text, wpm, farnsworth, jitter, rng):
    unit = 1.2 / wpm
    sunit = unit if not farnsworth else 1.2 / farnsworth
    segs = []  # (on, seconds)
    def j(x):
        return max(0.2 * x, x * (1 + rng.normal(0, jitter))) if jitter else x
    segs.append((0, 0.5))
    words = text.upper().split()
    for wi, w in enumerate(words):
        for ci, ch in enumerate(w):
            code = MORSE.get(ch)
            if not code:
                continue
            for ei, e in enumerate(code):
                segs.append((1, j(unit if e == '.' else 3 * unit)))
                if ei < len(code) - 1:
                    segs.append((0, j(unit)))
            if ci < len(w) - 1:
                segs.append((0, j(3 * sunit)))
        if wi < len(words) - 1:
            segs.append((0, j(7 * sunit)))
    segs.append((0, 0.6))
    return segs


def render(segs, pitch, snr_db, qsb, drift, rise_ms, rng, fbw=0, fcenter=0, min_len=0):
    n = max(int(sum(s for _, s in segs) * FS) + 1, int(min_len * FS))
    env = np.zeros(n)
    t0 = 0
    for on, s in segs:
        t1 = t0 + int(round(s * FS))
        if on:
            env[t0:t1] = 1.0
        t0 = t1
    # fronti a coseno rialzato (niente click)
    k = max(1, int(rise_ms * 1e-3 * FS))
    win = np.hanning(2 * k)
    env = np.convolve(env, win / win.sum(), mode='same')
    t = np.arange(n) / FS
    f = pitch + drift * t / t[-1] if drift else pitch
    phase = 2 * np.pi * np.cumsum(np.full(n, 1.0) * f) / FS
    sig = env * np.sin(phase)
    if qsb:
        sig *= 1 - qsb * (0.5 + 0.5 * np.sin(2 * np.pi * 0.3 * t + rng.uniform(0, 6.28)))
    # rumore: potenza in 2500 Hz rispetto a un tono di ampiezza 1 (potenza 0.5)
    noise_psd_band = 0.5 / (10 ** (snr_db / 10))
    noise_var = noise_psd_band * (FS / 2) / 2500
    x = sig + rng.normal(0, np.sqrt(noise_var), n)
    if fbw:     # filtro IF della radio: Butterworth 6° ordine equivalente in banda base
        f = np.fft.rfftfreq(n, 1 / FS)
        h = 1 / np.sqrt(1 + ((f - fcenter) / (fbw / 2)) ** 12)
        x = np.fft.irfft(np.fft.rfft(x) * h, n)
    x *= 0.3 / max(1e-9, np.percentile(np.abs(x), 99.9))
    return np.clip(x * 32767, -32768, 32767).astype('<i2')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out'); ap.add_argument('text')
    ap.add_argument('--wpm', type=float, default=20)
    ap.add_argument('--farnsworth', type=float, default=0)
    ap.add_argument('--pitch', type=float, default=600)
    ap.add_argument('--snr', type=float, default=20)
    ap.add_argument('--jitter', type=float, default=0)
    ap.add_argument('--qsb', type=float, default=0)
    ap.add_argument('--drift', type=float, default=0)
    ap.add_argument('--rise', type=float, default=5, help='fronte in ms')
    ap.add_argument('--start', type=float, default=0, help='secondi scartati in testa')
    ap.add_argument('--filter', type=float, default=0, help='banda del filtro della radio in Hz (0 = nessuno)')
    ap.add_argument('--filter-center', type=float, default=0, help='centro del filtro in Hz (default: pitch)')
    ap.add_argument('--len', type=float, default=0, help='durata minima in secondi')
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)
    pcm = render(keying(a.text, a.wpm, a.farnsworth, a.jitter, rng), a.pitch, a.snr, a.qsb, a.drift, a.rise, rng,
                 a.filter, a.filter_center or a.pitch, a.len)
    pcm = pcm[int(a.start * FS):]
    with wave.open(a.out, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(FS); w.writeframes(pcm.tobytes())


if __name__ == '__main__':
    main()
