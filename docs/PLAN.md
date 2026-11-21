# Development plan

## Phase 0 — Environment
- [x] `./tools/fetch_upstream.sh` and `pip install capstone pyelftools unicorn numpy` (venv `.venv`)
- [x] `make -C cwdec test`, `make -C app`, `make -C app sim` all green (2026-11-19, bundle v8)
- [x] Radio: SQ6EMM firmware with ICAP installed with the web patcher (20261119v14), official
      app bundle on the SD card, CLOCK working

## Phase 1 — Decoder on the PC (most of the work, no radio needed)
Current baseline: passes the suite. Improvements in order of value:
- [ ] **Real recordings**: record the 705's USB audio in CW (8 kHz mono, e.g. with
      `arecord -r 8000 -c 1 -f S16_LE`) on real bands (contests, QRQ, QRP with QSB), put them in
      `cwdec/test/real/` with the expected text and add a "real" set to run_suite
- [x] Cold start: the first character was wrong in 10 cases out of 16. Three causes: pitch locked
      on noise and slow to change (4 blocks = 200 ms, longer than a dash), fragments taken at the
      wrong pitch, dot/dash decided with the initial speed. Now: 128 ms delay line, immediate
      lock if the bin is not a peak, tone-presence gate (also with fixed pitch), classification
      at the end of the character. Measured on 10 seeds × 8 cases: 0/80 wrong starts; starting
      in the middle of a QSO (cases `start_mid_qso*`) without losses
- [x] IF-filtered noise (narrow CW filter) no longer decoded: the peak must also stand above the
      neighbouring bins (cases `noise_*`, `fil*`)
- [ ] Straight key and bug with strong jitter: dot/dash classification with more context
      (e.g. per-character decision on the best tree, or Viterbi on durations)
- [ ] Deep QSB: faster AGC on the envelope or threshold relative to the local peak
- [ ] Multiple signals: today it follows the strongest; consider locking the pitch to the radio's
      CW PITCH (in CW the demodulated tone is known) as default
- [ ] Cost: measure cycles per sample (`make -C app sim` + instruction counter in icap_sim)

## Phase 1b — Migration to the official SDK (done, 2026-11-20)
- [x] `app/icap.h` = the SDK's icap.h; `icap_*()` wrappers; the SDK's app.ld, icap_start.S, mkapp.py and flags
- [x] CW.APP: text with `icap_decode_out()`, status with `icap_set_line()` (8 lines), mode `CW:keep`,
      `--background`, `icap_should_stop()` on every loop (≤ 20 ms in the emulator)
- [x] icap_sim.py on the `struct icap_svc` layout and the 96-byte header; `make -C app sim` checks
      the DECODE text, also with audio gaps
- [x] docs/ICAP_ABI.md: reconstruction compared with the SDK

## Phase 2 — First run on the radio (read only) — done 2026-11-20
- [x] `PROBE.APP` on the radio in CW, started from MENU > SET > Apps (the Apps menu shows at most
      8 apps: PI4 removed to make room)
- [x] Ruler photo → `WIN_W` 39 in main.c
- [x] `PROBE.TXT`: CW mode code 0x22, at most 60 samples per call, no overruns, all v1rc services
- [x] `CW:keep` decoder mode from the RTTY button: the radio stays in RTTY-R, "CW DECODE" opens;
      pressing CW brings the radio back to CW with the decoder still running
- [ ] AF gain dependence of the audio level

## Phase 3 — CW.APP on the radio
- [x] Real on-air signals: good reception (2026-11-21)
- [x] DECODE output: one word per call (the v14 loader makes every call a separate line)
- [ ] Compare the text with fldigi/CW Skimmer on the same USB audio
- [ ] Width of the native DECODE screen
- [ ] Interface: button to clear the text? `CWLOG.TXT` log with time (`icap_clock_now`) and
      frequency (`@FREQ`, accounts for the transverter)
- [ ] IC-9700: same binary (`--models 3`); to verify

## Phase 4 — Publication
- [x] Public repository (MIT)
- [ ] Spotting: consider `@SPOT` (does PSKReporter accept mode `CW`?) only for callsigns decoded
      with certainty; it is a write-only service, not TX
- [ ] Propose CW.APP to SQ6EMM for the bundle (MIT like the SDK), stating the supported SDK,
      firmware and runtime versions
