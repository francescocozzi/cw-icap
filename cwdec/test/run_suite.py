#!/usr/bin/env python3
"""Suite di regressione del decoder: genera i casi, decodifica, misura il CER
(character error rate = distanza di Levenshtein / lunghezza del testo atteso).

Uso:  run_suite.py [--bin ../build/cwdec_wav] [--keep DIR] [--max-cer 0.05]
Esce con codice 1 se un caso "must" supera la soglia: usalo prima di ogni commit.
"""
import argparse, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, 'gen_cw.py')

TEXT = "CQ CQ DE IZ7KHR IZ7KHR PSE K = UR RST 599 5NN IN BARI JN81 = TNX FER QSO 73"

# nome, argomenti per gen_cw, argomenti per il decoder, obbligatorio?
# Con --start il testo iniziale è perso per costruzione: il CER non conta i caratteri
# di riferimento saltati in testa (ma conta la spazzatura decodificata prima del primo giusto).
CASES = [
    ("clean_20wpm",        "--wpm 20 --snr 30",                         "", True),
    ("clean_12wpm",        "--wpm 12 --snr 30",                         "", True),
    ("clean_35wpm",        "--wpm 35 --snr 30",                         "", True),
    ("pitch_450_auto",     "--wpm 22 --pitch 450 --snr 20",             "", True),
    ("pitch_900_auto",     "--wpm 22 --pitch 900 --snr 20",             "", True),
    ("pitch_fixed_600",    "--wpm 25 --pitch 600 --snr 10",             "--pitch 600", True),
    ("snr_10db",           "--wpm 20 --snr 10",                         "", True),
    ("snr_6db",            "--wpm 20 --snr 6",                          "", False),
    ("snr_3db",            "--wpm 20 --snr 3",                          "", False),
    ("snr_0db_fixed",      "--wpm 18 --snr 0",                          "--pitch 600 --bw 60", False),
    ("handkey_jitter",     "--wpm 18 --jitter 0.12 --snr 20",           "", True),
    ("bug_jitter_heavy",   "--wpm 22 --jitter 0.25 --snr 20",           "", False),
    ("farnsworth_18_10",   "--wpm 18 --farnsworth 10 --snr 25",         "", True),
    ("qsb_deep",           "--wpm 20 --qsb 0.7 --snr 15",               "", False),
    ("drift_40hz",         "--wpm 20 --drift 40 --snr 20",              "", True),
    ("fast_45wpm",         "--wpm 45 --snr 25",                         "", False),
    ("start_mid_qso",      "--wpm 22 --snr 15 --start 2.0",             "", True),
    ("start_mid_qso_fixed", "--wpm 22 --snr 15 --start 2.0",            "--pitch 600", True),
    # audio della radio dopo il filtro IF (FIL3 = 250 Hz, FIL2 = 500 Hz in CW); SNR sempre su 2500 Hz
    ("fil250_snr0",        "--wpm 20 --snr 0 --filter 250",             "", True),
    ("fil250_snr-3",       "--wpm 20 --snr -3 --filter 250",            "", False),
    ("fil500_snr0",        "--wpm 20 --snr 0 --filter 500",             "", True),
]

# solo rumore (radio senza antenna o banda vuota): il decoder deve restare zitto.
# nome, argomenti per gen_cw (60 s), argomenti per il decoder, caratteri ammessi, obbligatorio?
# Il rumore filtrato dal FIL3 della radio riempie una decina di bin del banco Goertzel e
# sembrava un tono (prima della correzione: ~120 caratteri al minuto, provato sulla radio).
NOISE_CASES = [
    ("noise_white",        "--snr 10 --len 60 --seed 3",                "", 2, True),
    ("noise_fil250",       "--snr 10 --len 60 --seed 3 --filter 250",   "", 4, True),
    ("noise_fil250_b",     "--snr 10 --len 60 --seed 4 --filter 250",   "", 4, True),
    ("noise_fil500",       "--snr 10 --len 60 --seed 3 --filter 500",   "", 2, True),
    ("noise_fil250_fixed", "--snr 10 --len 60 --seed 3 --filter 250",   "--pitch 600", 4, True),
]


def lev(a, b, free_prefix=False):
    """Levenshtein; con free_prefix saltare l'inizio di a (riferimento) non costa nulla."""
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [0 if free_prefix else i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def norm(s):
    return " ".join(s.upper().replace("<", " <").split())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', default=os.path.join(HERE, '..', 'build', 'cwdec_wav'))
    ap.add_argument('--keep', default=None, help='cartella dove lasciare i WAV generati')
    ap.add_argument('--max-cer', type=float, default=0.05)
    a = ap.parse_args()
    outdir = a.keep or tempfile.mkdtemp(prefix='cwsuite_')
    os.makedirs(outdir, exist_ok=True)
    fail = 0
    print(f"{'caso':22s} {'CER':>6s}  esito   decodificato")
    for name, gargs, dargs, must in CASES:
        wav = os.path.join(outdir, name + '.wav')
        subprocess.run([sys.executable, GEN, wav, TEXT] + gargs.split(), check=True)
        out = subprocess.run([a.bin, wav] + dargs.split(), capture_output=True, text=True, check=True).stdout
        ref, got = norm(TEXT), norm(out)
        late = '--start' in gargs
        cer = lev(ref, got, free_prefix=late) / len(ref)
        ok = cer <= a.max_cer
        tag = 'OK  ' if ok else ('FAIL' if must else 'warn')
        if must and not ok:
            fail += 1
        print(f"{name:22s} {cer:6.1%}  {tag}    {got[:70]}")
    for name, gargs, dargs, maxc, must in NOISE_CASES:
        wav = os.path.join(outdir, name + '.wav')
        subprocess.run([sys.executable, GEN, wav, ""] + gargs.split(), check=True)
        out = subprocess.run([a.bin, wav] + dargs.split(), capture_output=True, text=True, check=True).stdout
        got = norm(out)
        n = len(got.replace(" ", ""))
        ok = n <= maxc
        tag = 'OK  ' if ok else ('FAIL' if must else 'warn')
        if must and not ok:
            fail += 1
        print(f"{name:22s} {n:3d} ch  {tag}    {got[:70]}")
    print(f"\n{'TUTTI I CASI OBBLIGATORI OK' if not fail else f'{fail} casi obbligatori falliti'}  (WAV in {outdir})")
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
