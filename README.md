# cw-icap — decoder CW per IC-705 / IC-9700 (app SD per il firmware SQ6EMM)

Kit di partenza per sviluppare con Claude Code un decoder CW che gira **sulla radio**, come
app `.APP` del loader ICAP incluso nel firmware custom di SQ6EMM. Non serve modificare il
firmware: si installa una volta la release ICAP con il web patcher di SQ6EMM, poi si copia
`CW.APP` in `\IC-705\Apps\` e si lancia da MENU > SET > Apps (o dal modo decoder "CW" sul tasto
RTTY). Si costruisce con l'SDK ufficiale ICAP di SQ6EMM (https://github.com/sq6emm/icap-sdk).

## Cosa c'è già e funziona

| Pezzo | Stato |
|---|---|
| Decoder CW in C (`cwdec/`) | Funziona: CER 0–3% su segnali sintetici fino a 3 dB SNR, 12–45 WPM, Farnsworth, deriva |
| Banco di prova su PC | Generatore di segnali + suite di regressione con CER |
| SDK | `icap.h`, linker script, stub d'ingresso e packer ufficiali (v1rc) |
| Toolchain | `make -C app` produce `CW.APP` e `PROBE.APP` con arm-none-eabi-gcc e i flag dell'SDK |
| Emulatore | `tools/icap_sim.py` esegue il `.APP` ARM con una `struct icap_svc` finta: CW.APP decodifica |
| Sulla radio | **Mai provata**: primo passo PROBE.APP (fase 2 di docs/PLAN.md) |

## Partire

```sh
unzip cw-icap-kit.zip && cd cw-icap
git init && git add -A && git commit -m "Kit iniziale"
./tools/fetch_upstream.sh
python3 -m venv .venv && . .venv/bin/activate
pip install capstone pyelftools unicorn numpy
brew install arm-none-eabi-gcc arm-none-eabi-binutils    # macOS
make -C cwdec test && make -C app && make -C app sim
claude          # Claude Code legge CLAUDE.md
```

Requisiti: python3, un compilatore C per il PC, `arm-none-eabi-gcc` (come l'SDK; in alternativa
non ufficiale `clang` + `ld.lld` con `make -C app TOOLCHAIN=clang`), git, unzip.

Primi prompt utili in Claude Code:
- `/decoder-test` — verifica completa decoder + app nell'emulatore
- "Leggi docs/PLAN.md e lavoriamo sulla fase 1: migliora la partenza a freddo"
- `/re-api audio_read` — come le app di riferimento usano un servizio (comportamento non documentato)

## Sicurezza

- CW.APP è solo ricezione; le funzioni di trasmissione dell'API non vanno usate.
- Le app non possono cambiare frequenza né modo della radio (regola dell'SDK).
- Primo avvio sulla radio: **PROBE.APP**, che legge e basta (fase 2 del piano).
- Se un'app si blocca: spegni, togli il file dalla SD. Il firmware non viene toccato.
- Non pubblicare firmware Icom né immagini decodificate.

Licenza consigliata: MIT (come l'SDK). Le app GPL-3.0 dell'SDK (FT8, JT4, ...) si studiano ma non si copiano qui.
