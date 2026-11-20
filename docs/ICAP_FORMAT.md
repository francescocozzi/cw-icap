# Formato del contenitore ICAP (.APP)

> **Superato dall'SDK ufficiale** (`struct icap_header` in `app/icap.h`, packer
> `ext/icap-sdk/tools/mkapp.py`). Resta come traccia della ricostruzione: i nomi corretti dei
> campi e le correzioni sono in docs/ICAP_ABI.md, sezione "Header". In breve: 0x08 è `models`
> (non `abi_min`), 0x0c `fw_build`, 0x18 `entry_off`, flags bit2 = `BACKGROUND`, l'estensione
> registra i modi decoder.

Ricostruito dalle app pubblicate nel bundle SQ6EMM `ICAP-apps-*-20261119v8.zip`
(CLOCK, FT8, JT4, PI4, Q65, SYSINFO, WSPR, SPEED). Verifica dell'epoca: unpack + repack
riproduceva byte per byte tutte le 15 app dei due bundle (comandi poi rimossi da icap_tool.py).

## Layout

```
0x00  header (0x40 byte, oppure 0x60 con estensione)
hs    immagine (image_size byte)    codice + rodata + data, linkata a indirizzo 0
      tabella rilocazioni (nrelocs x u32)
      segmento dati (data_size byte) solo se flags & 1
```

| Off | Tipo | Campo | Note |
|---|---|---|---|
| 0x00 | char[4] | magic | `ICAP` |
| 0x04 | u16 | ver | 3 nelle app recenti, 1 in SYSINFO |
| 0x06 | u16 | hdr_size | 0x40 o 0x60 |
| 0x08 | u32 | abi_min | ABI minima richiesta al loader (1, 2 o 3) |
| 0x0c | u32 | — | sempre 0 |
| 0x10 | u32 | image_size | multiplo di 4 |
| 0x14 | u32 | bss_size | azzerata dal loader (presunto) subito dopo l'immagine |
| 0x18 | u32 | — | sempre 0 |
| 0x1c | u32 | nrelocs | numero di word da rilocare |
| 0x20 | u32 | stack_size | 0x1000 / 0x2000 / 0x4000 (FT8 705) |
| 0x24 | u32 | crc32 | CRC-32 (zlib) di **tutto ciò che segue l'header** |
| 0x28 | char[16] | name | nome dell'app nel menu |
| 0x38 | u32 | flags | bit0 = segmento dati presente (Q65); bit2 (=4) presente in tutte le app "di modo" (CLOCK, decoder) e assente in SYSINFO/SPEED — significato da confermare (voce nel menu DECODE?) |
| 0x3c | u32 | data_size | solo con flags & 1 |
| 0x40 | 32 byte | estensione | solo con hdr_size 0x60: nome del modo (`FT8`, `WSPR`…) + u16 = 1 a +6. Probabilmente registra l'app come modo decoder |

## Rilocazioni

Ogni voce è l'**offset nell'immagine** di una word a 32 bit a cui il loader somma l'indirizzo
di caricamento (il codice è linkato a 0). Non ci sono altri tipi: niente MOVW/MOVT, niente GOT.
Per questo `app.ld` linka a 0, il compilatore usa literal pool (`-mno-movt`) e
`icap_tool.py pack` raccoglie tutte le `R_ARM_ABS32` dall'ELF (`--emit-relocs`).

Q65 ha `nrelocs = 0` e un segmento dati separato (flags bit0): probabilmente codice
position-independent con i dati caricati nella "data region" (vedi ABI 0x48/0x4c). Non serve
per CW.APP.

## Ingresso

Offset 0 dell'immagine, in modalità ARM:

```
vmrs r1, fpscr      ; tutte le app iniziano così
b    app_main       ; r0 = puntatore alla tabella API
```

`app_main` ritorna un int (0 = uscita normale, 1 = errore/ABI insufficiente nelle app originali).

## File accessori nel bundle (non ICAP)

- `ICAPSPOT.BIN` (magic `ISPT`), `ICAPRT.BIN` (`ICRT`/`ICRX`), `ICAPTX.BIN`: moduli Thumb
  legati alla versione del firmware (PSKReporter, runtime, TX). Il README dice di copiarli
  sempre insieme al bundle. CW.APP non ne ha bisogno, ma vanno lasciati sulla SD.
- `*.CFG`: configurazione testuale `chiave=valore`, `#` commento.
