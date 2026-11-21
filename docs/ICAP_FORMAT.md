# ICAP container format (.APP)

> **Superseded by the official SDK** (`struct icap_header` in `app/icap.h`, packer
> `ext/icap-sdk/tools/mkapp.py`). Kept as a record of the reconstruction: the correct field names
> and the corrections are in docs/ICAP_ABI.md, section "Header". In short: 0x08 is `models`
> (not `abi_min`), 0x0c `fw_build`, 0x18 `entry_off`, flags bit2 = `BACKGROUND`, the extension
> registers the decoder modes.

Reconstructed from the apps published in the SQ6EMM bundle `ICAP-apps-*-20261119v8.zip`
(CLOCK, FT8, JT4, PI4, Q65, SYSINFO, WSPR, SPEED). Check at the time: unpack + repack reproduced
all 15 apps of the two bundles byte for byte (commands later removed from icap_tool.py).

## Layout

```
0x00  header (0x40 bytes, or 0x60 with extension)
hs    image (image_size bytes)       code + rodata + data, linked at address 0
      relocation table (nrelocs x u32)
      data segment (data_size bytes) only if flags & 1
```

| Off | Type | Field | Notes |
|---|---|---|---|
| 0x00 | char[4] | magic | `ICAP` |
| 0x04 | u16 | ver | 3 in recent apps, 1 in SYSINFO |
| 0x06 | u16 | hdr_size | 0x40 or 0x60 |
| 0x08 | u32 | abi_min | minimum ABI required from the loader (1, 2 or 3) |
| 0x0c | u32 | — | always 0 |
| 0x10 | u32 | image_size | multiple of 4 |
| 0x14 | u32 | bss_size | zeroed by the loader (presumably) right after the image |
| 0x18 | u32 | — | always 0 |
| 0x1c | u32 | nrelocs | number of words to relocate |
| 0x20 | u32 | stack_size | 0x1000 / 0x2000 / 0x4000 (FT8 705) |
| 0x24 | u32 | crc32 | CRC-32 (zlib) of **everything after the header** |
| 0x28 | char[16] | name | app name in the menu |
| 0x38 | u32 | flags | bit0 = data segment present (Q65); bit2 (=4) present in all "mode" apps (CLOCK, decoders) and absent in SYSINFO/SPEED — meaning to be confirmed (entry in the DECODE menu?) |
| 0x3c | u32 | data_size | only with flags & 1 |
| 0x40 | 32 bytes | extension | only with hdr_size 0x60: mode name (`FT8`, `WSPR`…) + u16 = 1 at +6. Probably registers the app as a decoder mode |

## Relocations

Each entry is the **offset in the image** of a 32-bit word to which the loader adds the load
address (the code is linked at 0). There are no other types: no MOVW/MOVT, no GOT. That is why
`app.ld` links at 0, the compiler uses literal pools (`-mno-movt`) and `icap_tool.py pack`
collected all `R_ARM_ABS32` from the ELF (`--emit-relocs`).

Q65 has `nrelocs = 0` and a separate data segment (flags bit0): probably position-independent
code with the data loaded in the "data region" (see ABI 0x48/0x4c). Not needed for CW.APP.

## Entry

Offset 0 of the image, in ARM mode:

```
vmrs r1, fpscr      ; every app starts like this
b    app_main       ; r0 = pointer to the API table
```

`app_main` returns an int (0 = normal exit, 1 = error/insufficient ABI in the original apps).

## Accessory files in the bundle (not ICAP)

- `ICAPSPOT.BIN` (magic `ISPT`), `ICAPRT.BIN` (`ICRT`/`ICRX`), `ICAPTX.BIN`: Thumb modules tied
  to the firmware version (PSKReporter, runtime, TX). The README says to always copy them
  together with the bundle. CW.APP does not need them, but they must stay on the SD card.
- `*.CFG`: text configuration `key=value`, `#` comment.
