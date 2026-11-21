# ICAP ABI: reconstruction vs official SDK

Since 2026-11-20 the ABI is the one of SQ6EMM's official SDK, https://github.com/sq6emm/icap-sdk
(MIT, status **v1rc**, commit `1ca9f04`; written for firmware IC-705 E 20261119v8, IC-9700
20261119v6, SD runtime 20261119v8; release 20261119v14 does not change the SDK). `app/icap.h` is
an identical copy (`make -C app check-sdk`).

This document compares the map we had reconstructed from the disassembly of the 20261119v8 bundle
with the SDK, to know which earlier conclusions were wrong. The code now only uses the
`icap_*()` wrappers. It also collects what was measured on the radio and what the v14 loader
code shows where the SDK says nothing.

Result legend: **=** right · **≈** right function, wrong detail · **✗** wrong ·
**+** not identified (new field for us).

## struct icap_svc (passed in `r0` to `app_main`)

| Off | Reconstruction (level) | SDK | Result | Notes |
|---|---|---|---|---|
| 0x00 | `abi` [C] | `abi_version` | = | 3 = v1rc |
| 0x04 | `_04` [?] | `radio_id` | + | `0x0705` / `0x9700` |
| 0x08 | `_08` [?] | `fw_build` | + | |
| 0x0c | `size` [C] | `size` | = | the wrappers compare it with `offsetof` (macro `ICAP_HAS`) |
| 0x10 | `set_title` [P] | `print(s)` | ✗ | not a title: appends a line to the panel and scrolls |
| 0x14 | `set_line(line, s)` [C] | `set_line(line, s)` | = | lines **0..7** |
| 0x18 | `_18()` [?] "clear?" | `clear()` | = | the guess was right |
| 0x1c | `file_open(name, write)` [C] | `f_open(name, mode)` | ≈ | `mode` = `ICAP_O_READ`/`ICAP_O_WRITE`, OR `ICAP_O_ROOT`/`ICAP_O_RADIO`. "−2 = not found" was **wrong**: −2 is `ICAP_E_NOFD` (too many open files, max 4) |
| 0x20 | `file_read` [C] | `f_read(fd, buf, u32)` | = | |
| 0x24 | `file_write` [C] | `f_write(fd, buf, u32)` | = | |
| 0x28 | `file_close` [C] | `f_close(fd)` | = | |
| 0x2c | `sleep_ms` [C] | `sleep_ms` | = | |
| 0x30 | `_30(u32)` [?] | `yield(void)` | ✗ | no argument |
| 0x34 | `tick_ms` [C] | `tick_ms` | = | |
| 0x38 | `_38` [?] | `mem_info(*mi)` | + | app window, stack, free space |
| 0x3c | `_3c` [?] | `radio_state(*rs)` | + | MAIN/SUB frequency, mode code (log only), `is_tx` |
| 0x40 | `exit_requested` [C] | `should_stop` | = | **must be called at least every 500 ms**; after ~3 s the loader kills the app |
| 0x44 | `_44` [?] | `task_id` | + | |
| 0x48 | `data_region` [C] | `heap_base` | = | large separate heap, 0 if absent |
| 0x4c | `data_region_size` [C] | `heap_size` | = | |
| 0x50 | `get_clock` [C], "−6 = not set" | `clock_now(*t)` | ≈ | struct right (`icap_time`). −6 is `ICAP_E_NOSYS` (service missing), not "clock not set" |
| 0x54 | `audio_read(buf, max)` [C], −7 = gap | `audio_read` | = | −7 = `ICAP_E_OVERRUN`. Returns 0 when no samples are ready |
| 0x58 | `audio_rate` [C] | `audio_rate` | = | 8000 |
| 0x5c | `log_line` [P] | `decode_out(utf8)` | ✗ | it was the output to the DECODE screen we were looking for (open question 5). CLOCK calls it once a minute to test it |
| 0x60 | TX [P] | `tx_begin` | = | **not used** (receive-only project) |
| 0x64 | TX [?] | `tx_audio_write` | = | same |
| 0x68 | TX [?] | `tx_end` | = | same |
| 0x6c–0x80 | — | `net_status`, `net_open`, `net_send`, `net_recv`, `net_close`, `net_error` | + | WLAN (705) / LAN (9700), max 2 connections |
| 0x84 | — | `popup(text)` | + | radio pop-up, 7 lines × 31 characters |
| 0x88 | — | `remove(name, flags)` | + | |
| 0x8c | — | `mode(buf, max)` | + | name of the decoder mode that started the app, 0 if started from Apps |

`size` = 0x90 in the current firmware. Services from 0x50 on may be missing on older firmware:
the wrappers return `ICAP_E_NOSYS`.

**Score.** Of the 15 fields marked [C], 13 were right without reservations. The other two (0x1c,
0x50) had the right function but a misread error code. Of the three [P], the TX one was right and
the two text outputs were wrong (0x10, 0x5c). The table stopped at 0x6c.

## .APP file header (`struct icap_header`)

| Off | Reconstruction | SDK | Result |
|---|---|---|---|
| 0x04 | `ver` (u16) | `abi_version` | = |
| 0x06 | `hdr_size` | `hdr_size` (64, or 96 with the extension) | = |
| 0x08 | `abi_min` | `models`: bitmask 1 = IC-705, 2 = IC-9700 | ✗ (FT8 and Q65 have 1: built for the 705 only, not "minimum ABI 1") |
| 0x0c | always 0 | `fw_build`: 0 = any build | + |
| 0x10 | `image_size` | `load_size` | = |
| 0x14 | `bss_size` | `bss_size` | = |
| 0x18 | always 0 | `entry_off` (bit0 = Thumb) | + |
| 0x1c | `nrelocs` | `reloc_count` | = |
| 0x20 | `stack_size` | `stack_size` | = |
| 0x24 | `crc32` of what follows the header | `crc32` of image + relocations (+ seg2), after the optional extension | = |
| 0x28 | `name[16]` | `name[16]` | = |
| 0x38 | `flags`: bit0 data segment, bit2 "?" | `ICAP_FLAG_SPLIT` 0x1, `ICAP_FLAG_AUTOSTART` 0x2, `ICAP_FLAG_BACKGROUND` 0x4 | ≈ (bit2 = EXIT hides the window and the app keeps running) |
| 0x3c | `data_size` | `seg2_size` | = |
| 0x40 | extension: "mode name + u16 = 1" | `struct icap_header_ext`: 4 × `{char name[6]; u8 radio_mode; u8 flags}` | ≈ (the "u16 = 1" is `radio_mode = ICAP_RMODE_USBD`, `flags = 0`) |

## Error codes

`ICAP_E_ARG` −1, `E_NOFD` −2, `E_IO` −3, `E_NAME` −4, `E_PERM` −5, `E_NOSYS` −6,
`E_OVERRUN` −7, `E_BUSY` −8, `E_NONET` −9, `E_DNS` −10, `E_CONN` −11, `E_TIMEOUT` −12.

## Apps menu limit (tested on the radio, 2026-11-20)

On IC-705 E with firmware and SD apps 20261119v14, MENU > SET > Apps lists **at most 8 apps**.
The v14 bundle already has 8 (APRS, CLOCK, FT8, JT4, PI4, Q65, SYSINFO, WSPR): PROBE.APP, the
ninth, did not show up; after removing PI4 it did. The SDK does not document this. To test our
apps we keep at most 8 `.APP` files on the SD card. Finder `._*` files were not the cause, but
avoid them anyway: copy with `cp -X` or run `dot_clean -m`.

### Where the limit comes from (analysis of patch 705E140_20261119v14, SQ6EMM's MIT code)

The loader code is in clear in the patch file (`patches/` in SQ6EMM's firmware repository), as
bytes with addresses; the Icom firmware is not needed. Addresses in the main CPU image:

- **Scan of `C:\IC-705\Apps`** (`0x2037c9b2`…`0x2037caa0`): reads at most 256 directory entries
  and accepts 8.3 names 5–12 characters long ending in `.APP` (upper or lower case). With
  `cmp r4, #7 / bgt` (`0x2037ca66`) it **stops registering after the eighth**: the others are
  ignored without notice. The first 8 in **FAT directory order** count (in practice the copy
  order), not alphabetical order.
- The names go into a fixed table of 8 entries of 13 bytes (`0x2037efd4`), followed by a per-app
  table (`0x2037f57c`, 8 × 181 bytes). Then a sort by name (`0x2037cac0`): the menu shows the
  apps in alphabetical order.
- **The menu has 8 fixed rows**: the functions at `0x2037b384` and `0x2037b3e8` fill and clear
  exactly 8 entries (`cmp r4, #8`), the rows of a reused Icom menu page.

So the limit cannot be lifted with files or settings: it needs a firmware change (larger tables
and a multi-page menu), which is up to SQ6EMM. Meanwhile: keep at most 8 `.APP` files on the SD
card, or copy first the ones you want to see. Note: a Finder hidden file such as `._CLOCK.APP`
on FAT has a short name ending in `.APP` and might take a slot (not verified): always delete them.

## PROBE.APP measurements on the radio (IC-705 E, 20261119v14, 2026-11-20)

Radio in CW on 7027 kHz, app started from Apps.

- `fw_build` 0x07051405, `size` 0x90, all v1rc services present.
- App window 96 KB (0x20831000..0x20849000). 16 KB stack at the top of the window, although we
  asked for 8. About 66 KB free after PROBE's bss. Separate 240 KB heap. CW.APP (about 16 KB of
  image and bss) fits easily.
- **CW mode code = 0x22** (`icap_radio_state`). `@FREQ` in CW gives `usb=1`.
- **Audio:** real 8000 Hz (79986 samples in 10002 ms). At most **60 samples per call**: calls
  alternate a block (40 samples = 5 ms on average) and an empty read. No overruns, longest loop
  7 ms. The simulator now delivers at most 60 samples per call (`--max-per-call`).
- **Low level:** peak 580, RMS ~143 out of 32767 (about −47 dBFS RMS), on a band with noise and
  signals. The decoder works on relative thresholds: on the PC it decodes the whole text even
  with the signal 4 times lower (peak 150) and 15 times lower (peak 40). Still to check whether
  the level depends on AF gain.
- **Audio after the IF filter:** with the narrow CW filter (FIL3) the app receives noise shaped by
  the filter, so `icap_audio_read` is taken after the IF filter.
- **Window (ruler photo):** all 8 lines are visible, between a firmware title bar
  ("PROBE - running") and a status bar ("EXIT: stop"). The font is proportional: about
  **39 characters** of capitals and digits fit, then the line is cut (no wrapping). In CW.APP:
  `WIN_W` 39.
- **`decode_out` from Apps = `print`:** with the app started from MENU > Apps, `icap_decode_out`
  does not go to DECODE but behaves like `print()`: it writes the line at the panel cursor and,
  when the panel is full, scrolls the window, dragging along the lines written with `set_line`.
  Seen with PROBE and with the first CW.APP. CW.APP now calls `decode_out` only if
  `icap_mode() > 0` and keeps the history in the window; the simulator flags `decode_out` from
  Apps as a violation.
- **Noise after the IF filter:** without antenna, with FIL3 in CW, the first CW.APP decoded noise
  (E/T at "200 WPM", SNR 15 dB, tone present). The presence check compared the peak with the mean
  of the whole Goertzel bank, and filtered noise only fills the bins inside the passband. Fixed in
  cwdec (the peak must also stand above the bins at 75–150 Hz, `RING_K`), cases `noise_*` and
  `fil*` in run_suite.py.
- `clock_now` returns UTC, the same time shown by the radio.
- `@CLOCK` has a richer format than the README says (`K=… S=… L=1 P=60000 O=1000 N=…`): `P`/`O`
  are in ms, not s.

## `decode_out` in the v14 loader (patch analysis, 2026-11-21)

Tested on air (2026-11-21): reception is good, but on the DECODE screen the text arrived only
when a line was closed (CW.APP sent whole lines), while the app window updated character by
character. From the loader code:

- `struct icap_svc` is at `0x2037ecd0`; `decode_out` (+0x5c) points to `0x2037c444`;
- if the app was not started from a decoder mode (word at `0x2037ed78+0x848` zero) it jumps to
  `print` (+0x10): the "from Apps" behaviour seen on the radio;
- otherwise it copies the string into a **queue of 8 lines of 84 bytes** (at most 83 characters;
  characters outside 0x20..0x7e become spaces, so the app cannot send `\r`/`\n`). When the queue
  is full the line is dropped without error;
- another function (`0x2037db44`) takes one line at a time from the queue and passes it to the
  firmware as **`"\r" + text + "\r"`**, through the same routine (`0x2003e018`) that the native
  RTTY decoder appears to use, which writes a character stream.

So every call is a new line and a line already written cannot be extended: real-time output
"like RTTY" would need a loader variant without the two `\r`. CW.APP now sends to DECODE
**each word as soon as it is complete** (`out=word`, default in CW.CFG, no prefix) or whole lines
(`out=line`). The simulator truncates to 83 characters like the loader and reports it.

## Earlier open questions

1. **Screen lines** → 8 (`set_line` 0..7), about 39 characters (measured). The width of the
   native DECODE screen is still to be checked.
2. **`audio_read` granularity** → measured: at most 60 samples per call, blocks of ~5 ms.
3. **Audio in CW mode** → after the IF filter (seen with FIL3). AF gain dependence still to check.
4. **flags bit2 / extension 0x60** → solved: `BACKGROUND` and decoder-mode registration.
5. **How to write to DECODE** → solved: `decode_out` (0x5c), one line per call.

## CW mode: KEEP or USBD

An app can register up to 4 decoder modes. On the MODE screen the RTTY button cycles through them:
RTTY → RTTY-R → registered modes → RTTY. Selecting a mode starts the app. `radio_mode` says what
the radio does at that moment:

- **`ICAP_RMODE_USBD`** (all bundle apps): switches to USB-D and opens DECODE. For CW this is the
  opposite of what we want: wide filter, so more signals and more noise into the decoder (which
  copes, with the automatic pitch search 300–1300 Hz); the frequency is shown with the sideband
  offset instead of the CW one.
- **`ICAP_RMODE_KEEP`**: "leave the radio's mode alone". **Chosen for CW.APP**
  (`--mode CW:keep` in the Makefile), because it is the only one of the two that does not take
  the radio out of CW.

What the SDK did not say (answers below, "Tested on the radio"):

1. To reach the "CW" mode you go through RTTY and RTTY-R. With KEEP the radio might therefore stay
   in **RTTY-R** instead of going back to the starting CW. Apps cannot change mode (SDK rule 5),
   so CW.APP cannot fix this by itself.
2. It was not certain that KEEP opens the DECODE screen: the `icap.h` comment ties it to USBD.
3. The CW mode code in `icap_radio_state()` → **0x22** (measured). With it CW.APP can warn when
   the radio is not in CW.
4. The "CW" label on the mode indicator can be confused with the radio's real CW. If it is
   ambiguous, a name like `CWD` is an alternative (at most 4 characters).

**Tested on the radio (IC-705 E, v14, 2026-11-20):**

1. With `keep` the radio stays in **RTTY-R**, the last mode of the cycle: it does not go back to
   the starting CW.
2. The DECODE screen **opens** with `keep` too, titled with the mode name ("CW DECODE").
3. Pressing the **CW** mode key afterwards keeps the decoder running and the radio goes back to
   CW. In RTTY-R the audio is in the RTTY passband (~2 kHz, visible in the DECODE spectrum) and
   the decoder hears nothing between 300 and 1300 Hz: that is why CW.APP shows "press CW"
   when the mode is not 0x22.
4. A green "V" appeared on CW DECODE, almost certainly a CW.APP line decoded from noise in RTTY-R.

How to use it: MODE screen, RTTY → RTTY-R → CW (CW.APP starts, CW DECODE opens), then the CW key.

**Mode of use that always works:** radio in CW, then CW.APP from MENU > SET > Apps. No mode is
touched and `icap_mode()` returns 0. With `--background` EXIT hides the window and decoding
continues. The last 4 decoded lines (lines 3–6) and the line being received (line 7) are shown in
the app window, so the text can be read even without DECODE.

## Relocations and entry

Unchanged from docs/ICAP_FORMAT.md, now with the SDK tools. The image is linked at 0 with
`tools/app.ld`; the loader relocates only `R_ARM_ABS32` words (list after the image) and
`mkapp.py` rejects absolute MOVW/MOVT and pointers beyond the end of the bss. The entry is
`tools/icap_start.S`: `vmrs r1, fpscr` (the first VFP instruction traps once and the firmware
enables the coprocessor for the task), then `b app_main`.
