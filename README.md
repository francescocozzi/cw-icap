# cw-icap — decoder CW per IC-705 / IC-9700 (app SD per il firmware SQ6EMM)

Decoder CW (Morse) che gira **sulla radio**, come
app `.APP` del loader ICAP incluso nel firmware custom di SQ6EMM. Non serve modificare il
firmware: si installa una volta la release ICAP con il web patcher di SQ6EMM, poi si copia
`CW.APP` in `\IC-705\Apps\` e si lancia da MENU > SET > Apps (o dal modo decoder "CW" sul tasto
RTTY). Si costruisce con l'SDK ufficiale ICAP di SQ6EMM (https://github.com/sq6emm/icap-sdk).

## Stato

| Pezzo | Stato |
|---|---|
| Decoder CW in C (`cwdec/`) | CER 0–3% su segnali sintetici fino a 3 dB SNR, 12–45 WPM, Farnsworth, deriva; non decodifica il rumore filtrato dall'IF |
| Banco di prova su PC | Generatore di segnali + suite di regressione con CER |
| App `CW.APP` | Modo decoder "CW" sul tasto RTTY (RTTY → RTTY-R → CW) o MENU > SET > Apps; testo nella finestra e sulla schermata DECODE |
| Emulatore | `tools/icap_sim.py` esegue il `.APP` ARM con una `struct icap_svc` finta e audio da WAV |
| Sulla radio | Provata su IC-705 (firmware SQ6EMM 20261119v14): ricezione sotto antenna buona |

Limiti noti (dettagli in `docs/ICAP_ABI.md`):
- il menu Apps del loader v14 mostra al massimo **8 app** (le prime 8 in ordine di directory):
  per aggiungere `CW.APP` al bundle completo bisogna toglierne una;
- dal tasto RTTY la radio resta in RTTY-R: premere **CW** dopo aver scelto il modo decoder;
- sulla schermata DECODE ogni chiamata è una riga a sé: il testo arriva **una parola per riga**
  (`out=word`) o a righe intere (`out=line`), non carattere per carattere.

## Installare sulla radio

Serve il firmware custom di SQ6EMM con il loader ICAP (https://github.com/sq6emm/icom-custom-firmware).
Tieni una copia della SD con il bundle originale. Copia `CW.APP` e `CW.CFG` in `\IC-705\Apps\`
(o `\IC-9700\Apps\`); da macOS usa `cp -X` e cancella i file `._*`. Per togliere l'app basta
cancellare il file. Opzioni in `CW.CFG`: `pitch`, `bw`, `wpm`, `squelch`, `out`.

## Compilare

```sh
./tools/fetch_upstream.sh                                # SDK ufficiale e app di riferimento in ext/
python3 -m venv .venv && . .venv/bin/activate
pip install capstone pyelftools unicorn numpy
brew install arm-none-eabi-gcc arm-none-eabi-binutils    # macOS
make -C cwdec test && make -C app && make -C app sim
```

Requisiti: python3, un compilatore C per il PC, `arm-none-eabi-gcc` (come l'SDK; in alternativa
non ufficiale `clang` + `ld.lld` con `make -C app TOOLCHAIN=clang`), git.
Il progetto si sviluppa con Claude Code (`CLAUDE.md`, comandi `/decoder-test` e `/re-api`).

## Sicurezza

- CW.APP è solo ricezione; le funzioni di trasmissione dell'API non vanno usate.
- Le app non possono cambiare frequenza né modo della radio (regola dell'SDK).
- Primo avvio sulla radio: **PROBE.APP**, che legge e basta (fase 2 del piano).
- Se un'app si blocca: spegni, togli il file dalla SD. Il firmware non viene toccato.
- Non pubblicare firmware Icom né immagini decodificate.

## Licenza\n\nMIT (vedi `LICENSE`), come l'SDK ICAP. `app/icap.h` è una copia dell'SDK di SQ6EMM (MIT).\nLe app GPL-3.0 dell'SDK (FT8, JT4, ...) si studiano ma non si copiano qui.
