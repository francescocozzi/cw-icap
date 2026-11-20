# CW decoder per IC-705 / IC-9700 — istruzioni per Claude Code

Progetto di Francesco Cozzi (IZ7KHR). Lingua di lavoro: **italiano** (commenti, docs, messaggi
di commit in italiano; identificatori C in inglese).

## Obiettivo

Un decoder CW (Morse) che gira **dentro la radio** come app SD del firmware custom di
Dawid SQ6EMM (https://github.com/sq6emm/icom-custom-firmware), con il testo a schermo.

Punto chiave di architettura: **non modifichiamo il firmware**. Il firmware SQ6EMM (release
ICAP) contiene un loader che esegue file `.APP` dalla cartella `\IC-705\Apps\` (o
`\IC-9700\Apps\`) della SD, lanciati da MENU > SET > Apps o come "modo decoder" dal tasto
RTTY. FT8/Q65/JT4/PI4/WSPR sono app così. Dal 2026-11-20 esiste l'**SDK ufficiale**
(https://github.com/sq6emm/icap-sdk, MIT): l'API è `icap.h` e la usiamo così com'è. Il nostro deliverable è `CW.APP`: niente patch del `.dat`, niente rischio di brick oltre
a quello di un'app che si blocca (si spegne la radio e si toglie il file dalla SD).

## Struttura

```
cwdec/            decoder portabile in C (nessuna libc, nessuna malloc, float VFP)
  cwdec.[ch]      la libreria: mixer I/Q -> envelope -> soglie adattive -> timing -> albero Morse
  host/           cwdec_wav: decodifica un WAV 8 kHz su PC
  test/           gen_cw.py (segnali sintetici), run_suite.py (regressione CER)
app/              l'app ICAP
  icap.h          copia IDENTICA dell'icap.h dell'SDK (mai modificarla; `make -C app check-sdk`)
  main.c          CW.APP: icap_audio_read -> cwdec -> icap_decode_out (testo) + icap_set_line (stato)
  probe.c         PROBE.APP: primo test sulla radio, misura ciò che l'SDK non dice
  support.c       memset/memcpy/memmove (il compilatore può emetterli)
  CW.CFG          configurazione d'esempio (pitch, bw, wpm, squelch)
ext/icap-sdk/     SDK ufficiale (non versionato): tools/app.ld, tools/icap_start.S, tools/mkapp.py
tools/
  icap_tool.py    info / disasm / strings (solo analisi; il pack lo fa mkapp.py dell'SDK)
  icap_api_scan.py  censisce le chiamate a struct icap_svc nelle app di riferimento
  icap_sim.py     emulatore (Unicorn) con struct icap_svc finta (SVC_LAYOUT) e audio da WAV
  fetch_upstream.sh scarica SDK, repo SQ6EMM e bundle delle app in ext/ (non versionato)
docs/             PLAN.md (fasi), ICAP_FORMAT.md, ICAP_ABI.md, FORUM_MESSAGE.md
```

## Comandi

```sh
./tools/fetch_upstream.sh            # una volta: SDK + repo SQ6EMM + app di riferimento in ext/
python3 -m venv .venv && . .venv/bin/activate   # i Makefile chiamano python3: tieni il venv attivo
pip install capstone pyelftools unicorn numpy
brew install arm-none-eabi-gcc arm-none-eabi-binutils   # toolchain dell'SDK (macOS)

make -C cwdec && make -C cwdec test  # decoder su PC + suite di regressione (deve passare)
make -C cwdec sanitize && python3 cwdec/test/run_suite.py --bin cwdec/build/cwdec_wav
make -C app                          # build/CW.APP e build/PROBE.APP (arm-none-eabi-gcc, flag SDK)
make -C app TOOLCHAIN=clang          # alternativa non ufficiale (clang + ld.lld, senza libgcc)
make -C app check-sdk                # app/icap.h == ext/icap-sdk/icap.h
make -C app sim                      # emulatore: decodifica attesa, anche con buchi audio
python3 tools/icap_sim.py app/build/CW.APP --wav file.wav --start-mode CW --trace
python3 tools/icap_tool.py info ext/apps/705/*.APP
python3 tools/icap_tool.py disasm ext/apps/705/CLOCK.APP --all
```

## Regole

1. **Mai codice di trasmissione.** `icap_tx_begin/icap_tx_write/icap_tx_end` (slot 0x60–0x68),
   `icap_tx_available()`, il file virtuale `@TX`: il decoder è solo ricezione, non usarli e non
   indagarli con test sulla radio. Il simulatore segnala come violazione ogni chiamata TX.
2. **Non distribuire firmware Icom** né immagini decodificate. In `ext/` stanno solo copie
   locali (in .gitignore). SDK e `icap.h` sono MIT (la copia in `app/` è lecita). Le app di
   riferimento hanno licenze proprie: clock, fetch, speed MIT; digi-common, ft8, jt4, pi4,
   q65, wspr **GPL-3.0**. Studiarle sì; copiarne codice in questo progetto (MIT) no.
3. **L'ABI è `app/icap.h` dell'SDK, non si tocca.** Si usano solo i wrapper `icap_*()` (dopo
   `icap_use(svc)`), che controllano già `svc->size` per i servizi v1rc. Quando l'SDK cambia:
   copia il nuovo `icap.h` in `app/` e aggiorna nello stesso commit `SVC_LAYOUT` in
   `tools/icap_sim.py` e `docs/ICAP_ABI.md`. Il comportamento non documentato (larghezza righe,
   audio in CW, KEEP) resta ipotesi finché non è provato sulla radio.
4. **Le app non possono cambiare frequenza né modo della radio** (regola dell'SDK). Si leggono
   soltanto (`icap_radio_state`, `@FREQ`); il codice modo è interno alla radio, solo per log.
   L'unico effetto sul modo è `radio_mode` del modo decoder registrato (`--mode CW:keep`).
5. **`icap_should_stop()` almeno ogni 500 ms** (dopo ~3 s il loader uccide l'app): niente
   attese lunghe senza controllo, niente elaborazioni lunghe in un colpo. `icap_close()` di ogni
   file prima di uscire. Il simulatore misura l'intervallo massimo e fallisce oltre 500 ms.
6. **Vincoli del codice che gira sulla radio** (cwdec/ e app/):
   - niente libc, niente malloc, niente `math.h`; stato tutto statico;
   - il Cortex-A9 **non ha divisione intera hardware**. Come nell'SDK si linka `-lgcc`, quindi
     `/`, `%` e l'aritmetica a 64 bit compilano (routine software `__aeabi_*`) ma costano: mai nel
     percorso per campione. Controlla con `arm-none-eabi-nm app/build/cw.elf | grep aeabi`;
   - flag dell'SDK (`-mword-relocations -fno-section-anchors -fno-pic`, link con `app.ld` e
     `--emit-relocs`): il loader rilocca solo word a 32 bit e `mkapp.py` rifiuta MOVW/MOVT
     assoluti e puntatori fuori dall'app;
   - float singola precisione di default (VFPv3 + NEON); `double` ammessi dove la precisione
     serve davvero (es. SGP4 / calcoli orbitali), mai nel percorso per campione del decoder;
   - budget: il decoder deve restare ben sotto l'1% della CPU; non bloccare il task UI
     (refresh schermo max ~5 Hz).
7. **Il decoder si sviluppa sul PC.** Ogni modifica a `cwdec/` passa da `make -C cwdec test`
   (nessun caso "must" in FAIL) e poi da `make -C app sim` (stesso risultato sul binario ARM).
   Se aggiungi una capacità, aggiungi il caso di test in `run_suite.py` che la dimostra.
8. Prima di proporre di copiare un `.APP` sulla radio: build pulita, `icap_tool.py info`
   con CRC OK, simulazione riuscita. Ricorda all'utente di tenere una SD con il bundle
   originale e che l'app si toglie semplicemente cancellando il file.

## Stato attuale (aggiornalo quando cambia)

- SDK ufficiale ICAP v1rc (icap-sdk 1ca9f04, firmware 705E 20261119v14 / runtime SD 20261119v14, installati sulla radio di IZ7KHR il 2026-11-20; CLOCK.APP funziona):
  progetto migrato (icap.h, app.ld, icap_start.S, mkapp.py, flag di compilazione). La
  ricostruzione precedente è confrontata con l'SDK in docs/ICAP_ABI.md.
- Provato sulla radio: il menu Apps elenca al massimo 8 app (il bundle v14 ne ha 8). Sulla SD
  PI4 è stata tolta (copia in `ext/sd-backup/`) per far posto a PROBE; per CW.APP serve un altro
  posto. Copiare sulla SD da terminale con `cp -X` (niente file `._*`).
- PROBE.APP eseguita sulla radio (docs/ICAP_ABI.md, "Misure di PROBE.APP"): codice modo CW 0x22,
  audio 8 kHz in blocchi ≤60 campioni senza overrun, livello basso (picco ~580) ma sufficiente; finestra di 8 righe da ~39 caratteri (font
  proporzionale, testo tagliato a destra).
- CW.APP provata sulla radio da Apps, senza antenna: `decode_out` da Apps scorre la finestra
  come `print` (ora usato solo da modo decoder, storico di 4 righe nella finestra) e il rumore
  filtrato dal FIL3 veniva decodificato (corretto: contrasto del picco sul contorno, casi
  `noise_*`/`fil*` nella suite, terza simulazione in `make -C app sim`).
- `CW:keep` dal tasto RTTY (provato): la radio resta in RTTY-R, si apre "CW DECODE"; premendo CW
  il decoder resta attivo e il modo torna giusto. CW.APP scrive "premi CW" se il modo non è 0x22.
- Ancora da provare sulla radio (fase 2): larghezza della schermata DECODE nativa, dipendenza
  dell'audio da AF gain e filtro, decodifica di un segnale reale e testo su CW DECODE.
- Decoder: passa la suite (CER 0–3% fino a SNR 3 dB/2500 Hz, 12–45 WPM, Farnsworth, deriva,
  avvio a QSO in corso). Partenza a freddo risolta: linea di ritardo 128 ms davanti al rivelatore,
  aggancio pitch immediato, gate di presenza tono dal banco Goertzel (anche a pitch fisso),
  punto/linea decisi a fine carattere. Noti: tasto bug con jitter forte (~24% CER);
  QSB profondo (~13%). Vedi docs/PLAN.md fase 1.
- CW.APP: modo decoder `CW` (keep), `--background`, testo su DECODE e stato su 8 righe.
  Compila col toolchain SDK (11,6 KB, bss 3,6 KB, nessuna `__aeabi_*`), gira nell'emulatore e
  decodifica anche con buchi audio. Mai eseguita su radio.
