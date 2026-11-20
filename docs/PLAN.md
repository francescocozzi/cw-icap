# Piano di sviluppo

## Fase 0 — Ambiente (½ giornata)
- [x] `./tools/fetch_upstream.sh` e `pip install capstone pyelftools unicorn numpy` (venv `.venv`)
- [x] `make -C cwdec test`, `make -C app`, `make -C app sim` tutti verdi (2026-11-19, bundle v8)
- [ ] Opzionale: Arm GNU Toolchain (`arm-none-eabi-gcc`) e Ghidra per l'analisi del loader
- [ ] Radio: firmware SQ6EMM release ICAP installato con il web patcher, bundle app ufficiale
      sulla SD, FT8 funzionante (così sai che il loader va)

## Fase 1 — Decoder sul PC (il grosso del lavoro, tutto senza radio)
Base attuale: passa la suite. Migliorie in ordine di valore:
- [ ] **Registrazioni reali**: registra l'audio USB del 705 in CW (8 kHz mono, es. con
      `arecord -r 8000 -c 1 -f S16_LE`) su bande vere (contest, QRQ, QRP con QSB) e mettile in
      `cwdec/test/real/` con il testo atteso; aggiungi a run_suite un set "real"
- [x] Partenza a freddo: il primo carattere era sbagliato in 10 casi su 16. Tre cause: pitch
      agganciato al rumore e lento a cambiare (4 blocchi = 200 ms, più di una linea), frammenti
      presi al pitch sbagliato, punto/linea decisi con la velocità iniziale. Ora: linea di
      ritardo 128 ms, aggancio immediato se il bin non è un picco, gate di presenza tono
      (anche a pitch fisso), classificazione a fine carattere. Misura su 10 seed per 8 casi:
      0/80 inizi sbagliati; avvio a QSO in corso (casi `start_mid_qso*`) senza perdite
- [ ] Tasto verticale e bug con jitter forte: classificazione punto/linea con più contesto
      (es. decisione per carattere sul miglior albero, o Viterbi su durate)
- [ ] QSB profondo: AGC più veloce sull'envelope o soglia relativa al picco locale
- [ ] Segnali multipli: oggi segue il più forte; valutare il blocco del pitch al CW PITCH
      della radio (in CW il tono demodulato è noto) come default
- [ ] Costo: misurare cicli per campione (`make -C app sim` + contatore istruzioni in icap_sim)

## Fase 1b — Migrazione all'SDK ufficiale (fatta, 2026-11-20)
- [x] `app/icap.h` = icap.h dell'SDK; wrapper `icap_*()`; app.ld, icap_start.S, mkapp.py e flag dell'SDK
- [x] CW.APP: testo con `icap_decode_out()`, stato con `icap_set_line()` (8 righe), modo `CW:keep`,
      `--background`, `icap_should_stop()` a ogni giro (≤ 20 ms nell'emulatore)
- [x] icap_sim.py sul layout di `struct icap_svc` e sull'header a 96 byte; `make -C app sim` verifica
      il testo su DECODE, anche con buchi audio
- [x] docs/ICAP_ABI.md: confronto ricostruzione / SDK

## Fase 2 — Prima esecuzione sulla radio (sola lettura)
L'ABI non è più un'ipotesi; restano da misurare le cose che l'SDK non documenta.
- [ ] Copia `app/build/PROBE.APP` in `\IC-705\Apps\` (SD di riserva con il bundle originale),
      **radio in CW**, lancia da MENU > SET > Apps
- [ ] Fotografa finestra e DECODE durante gli 8 s del righello → `WIN_W` e `DEC_TEXT_W` in main.c
- [ ] Leggi `PROBE.TXT`: `mode_code` con la radio in CW (per un avviso in CW.APP), `max_per_call`,
      cadenza audio, overrun, `size`, servizi presenti
- [ ] Modo decoder `CW:keep` (docs/ICAP_ABI.md, "Modo CW"): scegliendolo dal tasto RTTY, in che modo
      resta la radio? Si apre DECODE? Se resta in RTTY-R, valutare l'avvio solo da Apps o un nome
      diverso dal modo CW della radio

## Fase 3 — CW.APP sulla radio
- [ ] Ricevi un segnale CW noto (generatore, beacon, W1AW, secondo trasmettitore su dummy load)
- [ ] Confronta il testo con fldigi/CW Skimmer sullo stesso audio USB
- [ ] Controlla la resa su DECODE: righe, ora (RTC locale), refresh; finestra con riga in corso
- [ ] Interfaccia: pulsante per azzerare il testo? log `CWLOG.TXT` con ora (`icap_clock_now`) e
      frequenza (`@FREQ`, tiene conto del transverter)
- [ ] IC-9700: stesso binario (`--models 3`); verifica

## Fase 4 — Pubblicazione
- [ ] Spotting: valutare `@SPOT` (PSKReporter accetta il modo `CW`?) solo per nominativi decodificati
      con certezza; è un servizio di sola scrittura, non TX
- [ ] Proporre CW.APP a SQ6EMM per il bundle (MIT come l'SDK), oppure pubblicarla separata
      indicando versione di SDK, firmware e runtime supportati
