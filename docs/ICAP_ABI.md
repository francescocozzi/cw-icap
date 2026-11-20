# ABI ICAP: ricostruzione contro SDK ufficiale

Dal 2026-11-20 l'ABI è quella dell'SDK ufficiale di SQ6EMM, https://github.com/sq6emm/icap-sdk
(MIT, stato **v1rc**, commit `1ca9f04`; scritto su firmware IC-705 E 20261119v8, IC-9700 20261119v6,
runtime SD 20261119v8; la release 20261119v14 non cambia l'SDK). `app/icap.h` ne è una copia identica (`make -C app check-sdk`).

Questo documento confronta la mappa che avevamo ricostruito dal disassemblato del bundle
20261119v8 con l'SDK. Serve a sapere quali conclusioni di prima erano sbagliate. Il codice ormai
usa solo i wrapper `icap_*()`.

Legenda esito: **=** giusto · **≈** funzione giusta, dettaglio sbagliato · **✗** sbagliato ·
**+** non individuato (campo nuovo per noi).

## struct icap_svc (passata in `r0` ad `app_main`)

| Off | Ricostruzione (livello) | SDK | Esito | Note |
|---|---|---|---|---|
| 0x00 | `abi` [C] | `abi_version` | = | 3 = v1rc |
| 0x04 | `_04` [?] | `radio_id` | + | `0x0705` / `0x9700` |
| 0x08 | `_08` [?] | `fw_build` | + | |
| 0x0c | `size` [C] | `size` | = | i wrapper lo confrontano con `offsetof` (macro `ICAP_HAS`) |
| 0x10 | `set_title` [P] | `print(s)` | ✗ | non è un titolo: aggiunge una riga al pannello e scorre |
| 0x14 | `set_line(line, s)` [C] | `set_line(line, s)` | = | righe **0..7** |
| 0x18 | `_18()` [?] "clear?" | `clear()` | = | l'ipotesi era giusta |
| 0x1c | `file_open(name, write)` [C] | `f_open(name, mode)` | ≈ | `mode` = `ICAP_O_READ`/`ICAP_O_WRITE`, OR `ICAP_O_ROOT`/`ICAP_O_RADIO`. "−2 = non trovato" era **sbagliato**: −2 è `ICAP_E_NOFD` (troppi file aperti, max 4) |
| 0x20 | `file_read` [C] | `f_read(fd, buf, u32)` | = | |
| 0x24 | `file_write` [C] | `f_write(fd, buf, u32)` | = | |
| 0x28 | `file_close` [C] | `f_close(fd)` | = | |
| 0x2c | `sleep_ms` [C] | `sleep_ms` | = | |
| 0x30 | `_30(u32)` [?] | `yield(void)` | ✗ | niente argomento |
| 0x34 | `tick_ms` [C] | `tick_ms` | = | |
| 0x38 | `_38` [?] | `mem_info(*mi)` | + | finestra dell'app, stack, spazio libero |
| 0x3c | `_3c` [?] | `radio_state(*rs)` | + | frequenza MAIN/SUB, codice modo (solo log), `is_tx` |
| 0x40 | `exit_requested` [C] | `should_stop` | = | **va chiamata almeno ogni 500 ms**; dopo ~3 s il loader uccide l'app |
| 0x44 | `_44` [?] | `task_id` | + | |
| 0x48 | `data_region` [C] | `heap_base` | = | heap grande separato, 0 se assente |
| 0x4c | `data_region_size` [C] | `heap_size` | = | |
| 0x50 | `get_clock` [C], "−6 = non impostato" | `clock_now(*t)` | ≈ | struct giusta (`icap_time`). −6 è `ICAP_E_NOSYS` (servizio assente), non "orologio non impostato" |
| 0x54 | `audio_read(buf, max)` [C], −7 = buco | `audio_read` | = | −7 = `ICAP_E_OVERRUN`. Restituisce 0 se non ci sono campioni |
| 0x58 | `audio_rate` [C] | `audio_rate` | = | 8000 |
| 0x5c | `log_line` [P] | `decode_out(utf8)` | ✗ | era proprio l'uscita verso la schermata DECODE che cercavamo (domanda aperta 5). CLOCK la chiama una volta al minuto per provarla |
| 0x60 | TX [P] | `tx_begin` | = | **non usare** (CLAUDE.md regola 1) |
| 0x64 | TX [?] | `tx_audio_write` | = | idem |
| 0x68 | TX [?] | `tx_end` | = | idem |
| 0x6c–0x80 | — | `net_status`, `net_open`, `net_send`, `net_recv`, `net_close`, `net_error` | + | WLAN (705) / LAN (9700), max 2 connessioni |
| 0x84 | — | `popup(text)` | + | finestra della radio, 7 righe × 31 caratteri |
| 0x88 | — | `remove(name, flags)` | + | |
| 0x8c | — | `mode(buf, max)` | + | nome del modo decoder che ha avviato l'app, 0 se avviata da Apps |

`size` = 0x90 nel firmware attuale. I servizi da 0x50 in poi possono mancare su firmware più
vecchi: i wrapper restituiscono `ICAP_E_NOSYS`.

**Bilancio.** Dei 15 campi marcati [C], 13 erano giusti senza riserve. Gli altri due (0x1c, 0x50)
avevano la funzione giusta ma un codice d'errore interpretato male. Dei tre [P], quello sul TX
era giusto e i due sulle uscite di testo erano sbagliati (0x10, 0x5c). La tabella si fermava a 0x6c.

## Header del file .APP (`struct icap_header`)

| Off | Ricostruzione | SDK | Esito |
|---|---|---|---|
| 0x04 | `ver` (u16) | `abi_version` | = |
| 0x06 | `hdr_size` | `hdr_size` (64, o 96 con l'estensione) | = |
| 0x08 | `abi_min` | `models`: bitmask 1 = IC-705, 2 = IC-9700 | ✗ (FT8 e Q65 hanno 1: build solo per il 705, non "ABI minima 1") |
| 0x0c | sempre 0 | `fw_build`: 0 = qualsiasi build | + |
| 0x10 | `image_size` | `load_size` | = |
| 0x14 | `bss_size` | `bss_size` | = |
| 0x18 | sempre 0 | `entry_off` (bit0 = Thumb) | + |
| 0x1c | `nrelocs` | `reloc_count` | = |
| 0x20 | `stack_size` | `stack_size` | = |
| 0x24 | `crc32` di ciò che segue l'header | `crc32` di immagine + rilocazioni (+ seg2), dopo l'eventuale estensione | = |
| 0x28 | `name[16]` | `name[16]` | = |
| 0x38 | `flags`: bit0 segmento dati, bit2 "?" | `ICAP_FLAG_SPLIT` 0x1, `ICAP_FLAG_AUTOSTART` 0x2, `ICAP_FLAG_BACKGROUND` 0x4 | ≈ (bit2 = EXIT nasconde la finestra e l'app continua) |
| 0x3c | `data_size` | `seg2_size` | = |
| 0x40 | estensione: "nome del modo + u16 = 1" | `struct icap_header_ext`: 4 × `{char name[6]; u8 radio_mode; u8 flags}` | ≈ (l'"u16 = 1" è `radio_mode = ICAP_RMODE_USBD`, `flags = 0`) |

## Codici d'errore

`ICAP_E_ARG` −1, `E_NOFD` −2, `E_IO` −3, `E_NAME` −4, `E_PERM` −5, `E_NOSYS` −6,
`E_OVERRUN` −7, `E_BUSY` −8, `E_NONET` −9, `E_DNS` −10, `E_CONN` −11, `E_TIMEOUT` −12.

## Limite del menu Apps (provato sulla radio, 2026-11-20)

Su IC-705 E con firmware e SD apps 20261119v14, MENU > SET > Apps elenca **al massimo 8 app**.
Il bundle v14 ne ha già 8 (APRS, CLOCK, FT8, JT4, PI4, Q65, SYSINFO, WSPR): PROBE.APP, nona,
non compariva; tolto PI4 è comparsa. L'SDK non lo documenta. Per provare le nostre app sulla SD
teniamo al massimo 8 file `.APP` in tutto (PI4 tolta, copia in `ext/sd-backup/`). I file `._*`
del Finder non c'entravano, ma vanno comunque evitati: copiare con `cp -X` o `dot_clean -m`.

## Misure di PROBE.APP sulla radio (IC-705 E, 20261119v14, 2026-11-20)

File originale: `ext/sd-backup/PROBE-1.TXT`. Radio in CW su 7027 kHz, app avviata da Apps.

- `fw_build` 0x07051405, `size` 0x90, tutti i servizi v1rc presenti.
- Finestra dell'app 96 KB (0x20831000..0x20849000). Stack 16 KB in cima alla finestra,
  anche se ne avevamo chiesti 8. Liberi circa 66 KB dopo il bss di PROBE. Heap separato da 240 KB.
  CW.APP (14,6 KB fra immagine e bss) ci sta senza problemi.
- **Codice modo CW = 0x22** (`icap_radio_state`). `@FREQ` in CW dà `usb=1`.
- **Audio:** 8000 Hz reali (79986 campioni in 10002 ms). Al massimo **60 campioni per chiamata**:
  le chiamate alternano un blocco (in media 40 campioni = 5 ms) e una lettura vuota. Nessun
  overrun, ciclo massimo 7 ms. Il simulatore ora consegna al massimo 60 campioni per chiamata
  (`--max-per-call`).
- **Livello basso:** picco 580, RMS ~143 su 32767 (circa −47 dBFS RMS), su banda con rumore e
  segnali. Il decoder lavora su soglie relative: in prova su PC decodifica il testo intero anche
  con il segnale 4 volte più basso (picco 150) e 15 volte più basso (picco 40). Resta da vedere se
  il livello dipende da AF gain.
- **Finestra (foto del righello, `ext/sd-backup/PROBE-righello.jpg`):** tutte le 8 righe sono
  visibili, tra una barra del titolo del firmware ("PROBE - running") e una di stato ("EXIT: stop").
  Il font è proporzionale: ci stanno circa **39 caratteri** fra maiuscole e cifre, poi la riga
  viene tagliata (niente a capo). In CW.APP: `WIN_W` 39, righe DECODE di 38 caratteri.
- **`decode_out` da Apps = `print`:** con l'app avviata da MENU > Apps, `icap_decode_out` non va
  su DECODE ma si comporta come `print()`: scrive la riga al cursore del pannello e, a pannello
  pieno, fa scorrere la finestra, trascinando con sé le righe scritte con `set_line`. Visto con
  PROBE (righello sulla riga 0) e con la prima CW.APP (righe di stato doppie, foto
  `ext/sd-backup/CW-rumore-fil3.jpeg`). CW.APP ora chiama `decode_out` solo se `icap_mode() > 0`
  e tiene lo storico nella finestra; il simulatore segnala `decode_out` da Apps come violazione.
- **Rumore dopo il filtro IF:** senza antenna, con FIL3 (stretto) in CW, la prima CW.APP
  decodificava il rumore (E/T a "200 WPM", SNR 15 dB, tono presente). Il controllo di presenza
  confrontava il picco con la media di tutto il banco Goertzel, e il rumore filtrato riempie solo
  i bin nella banda passante. Corretto in cwdec (picco anche sopra i bin a 75–150 Hz, `RING_K`),
  casi `noise_*` e `fil*` in run_suite.py.
- `clock_now` restituisce l'ora UTC, la stessa mostrata dalla radio (foto delle 11:15 UTC).
- Nella prima esecuzione di PROBE l'orologio era avanti; poi la radio si è allineata (UTC).
- `@CLOCK` ha un formato più ricco di quello del README (`K=… S=… L=1 P=60000 O=1000 N=…`):
  `P`/`O` sono in ms, non in s.

## Domande aperte di prima

1. **Righe dello schermo** → 8 (`set_line` 0..7), circa 39 caratteri (misurato). La larghezza
   della schermata DECODE nativa resta da vedere con CW.APP avviata dal tasto RTTY.
2. **Granularità di `audio_read`** → misurata: al massimo 60 campioni per chiamata, blocchi di ~5 ms.
3. **Audio in modo CW** (prima o dopo il filtro IF, dipendenza da AF gain) → non documentato.
   Si vede sulla radio con PROBE e CW.APP.
4. **flags bit2 / estensione 0x60** → risolta: `BACKGROUND` e registrazione dei modi decoder.
5. **Come si scrive su DECODE** → risolta: `decode_out` (0x5c), una riga per chiamata.

## Modo CW: KEEP o USBD

Un'app può registrare fino a 4 modi decoder. Sulla schermata MODE il tasto RTTY li alterna:
RTTY → RTTY-R → modi registrati → RTTY. Scegliendo un modo parte l'app. `radio_mode` dice cosa
fa la radio in quel momento:

- **`ICAP_RMODE_USBD`** (tutte le app del bundle): passa in USB-D e apre DECODE. Per il CW è il
  contrario di quello che vogliamo. Filtro largo, quindi più segnali e più rumore nel decoder
  (che regge, con la ricerca automatica del pitch 300–1300 Hz). Niente CW in trasmissione
  finché si resta in USB-D. Frequenza mostrata con l'offset della banda laterale invece che del CW.
- **`ICAP_RMODE_KEEP`**: "leave the radio's mode alone". **Scelto per CW.APP**
  (`--mode CW:keep` nel Makefile), perché è l'unico dei due che non toglie la radio dal CW.

Cosa l'SDK non diceva (le risposte sono sotto, "Provato sulla radio"):

1. Per arrivare al modo "CW" si passa da RTTY e RTTY-R. Con KEEP la radio potrebbe quindi
   restare in **RTTY-R** invece di tornare al CW di partenza. Le app non possono cambiare modo
   (regola 5 dell'SDK), quindi CW.APP non può rimediare da sola.
2. Non è detto che con KEEP si apra la schermata DECODE: il commento di `icap.h` la lega a USBD.
3. Il codice modo di CW in `icap_radio_state()` → **0x22** (misurato). Con quello CW.APP può
   avvisare se la radio non è in CW.
4. L'etichetta "CW" sull'indicatore di modo si confonde con il CW vero della radio. Se crea
   ambiguità, un nome come `CWD` è un'alternativa (massimo 4 caratteri).

**Provato sulla radio (IC-705 E, v14, 2026-11-20, foto `ext/sd-backup/CW-DECODE-rttyr.jpeg`):**

1. Con `keep` la radio resta in **RTTY-R**, l'ultimo modo del ciclo: non torna al CW di partenza.
2. La schermata DECODE **si apre** anche con `keep`, intitolata con il nome del modo ("CW DECODE").
3. Premendo poi il tasto del modo **CW** il decoder resta attivo e la radio torna in CW. In RTTY-R
   l'audio è nella banda RTTY (~2 kHz, si vede nello spettro di DECODE) e il decoder non sente
   niente fra 300 e 1300 Hz: per questo CW.APP, se il modo non è 0x22, scrive "premi CW".
4. Su CW DECODE è comparsa una "V" verde, quasi certamente una riga di CW.APP decodificata dal
   rumore in RTTY-R: sembra che DECODE mostri solo la parte dopo " : ". Da confermare con un
   segnale vero.

Procedura d'uso: schermata MODE, RTTY → RTTY-R → CW (parte CW.APP, si apre CW DECODE), poi tasto CW.

**Modo d'uso che funziona comunque:** radio in CW, poi CW.APP da MENU > SET > Apps. Nessun modo
viene toccato e `icap_mode()` restituisce 0. Con `--background` EXIT nasconde la finestra e la
decodifica continua. La riga in ricezione e l'ultima riga emessa compaiono anche sulla finestra
dell'app (righe 6–7), quindi il testo si legge anche se DECODE non è visibile.

## Rilocazioni e ingresso

Invariati rispetto a docs/ICAP_FORMAT.md, ora con gli strumenti dell'SDK. L'immagine è linkata
a 0 con `tools/app.ld`; il loader rilocca solo word `R_ARM_ABS32` (lista dopo l'immagine) e
`mkapp.py` rifiuta MOVW/MOVT assoluti e puntatori oltre la fine del bss. L'ingresso è
`tools/icap_start.S`: `vmrs r1, fpscr` (la prima istruzione VFP va in trap una volta e il firmware
accende il coprocessore per il task), poi `b app_main`.
