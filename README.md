# cw-icap — CW decoder for IC-705 / IC-9700 (SD app for the SQ6EMM firmware)

A CW (Morse) decoder that runs **inside the radio**, as an `.APP` for the ICAP loader included in
SQ6EMM's custom firmware. The firmware is not modified: install the ICAP release once with
SQ6EMM's web patcher, copy `CW.APP` to `\IC-705\Apps\` and start it from MENU > SET > Apps (or
from the "CW" decoder mode on the RTTY button). Built with SQ6EMM's official ICAP SDK
(https://github.com/sq6emm/icap-sdk).

## Status

| Part | Status |
|---|---|
| CW decoder in C (`cwdec/`) | CER 0–3% on synthetic signals down to 3 dB SNR, 12–45 WPM, Farnsworth, drift; does not decode IF-filtered noise |
| PC test bench | Signal generator + regression suite with CER |
| `CW.APP` | "CW" decoder mode on the RTTY button (RTTY → RTTY-R → CW) or MENU > SET > Apps; text in the app window and on the DECODE screen |
| Emulator | `tools/icap_sim.py` runs the ARM `.APP` with a fake `struct icap_svc` and audio from a WAV file |
| On the radio | Tested on IC-705 (SQ6EMM firmware 20261119v14): good reception on air |

Known limits (details in `docs/ICAP_ABI.md`):
- the Apps menu of the v14 loader lists at most **8 apps** (the first 8 in directory order): to
  add `CW.APP` to the full bundle you have to remove one;
- from the RTTY button the radio stays in RTTY-R: press **CW** after selecting the decoder mode;
- on the DECODE screen every call is a line of its own: text arrives **one word per line**
  (`out=word`) or in whole lines (`out=line`), not character by character.

## Installing on the radio

You need SQ6EMM's custom firmware with the ICAP loader
(https://github.com/sq6emm/icom-custom-firmware). Keep a copy of the SD card with the original
bundle. Copy `CW.APP` and `CW.CFG` to `\IC-705\Apps\` (or `\IC-9700\Apps\`); on macOS use
`cp -X` and delete the `._*` files. To remove the app just delete the file. Options in `CW.CFG`:
`pitch`, `bw`, `wpm`, `squelch`, `out`.

## Building

```sh
./tools/fetch_upstream.sh                                # official SDK and reference apps into ext/
python3 -m venv .venv && . .venv/bin/activate
pip install capstone pyelftools unicorn numpy
brew install arm-none-eabi-gcc arm-none-eabi-binutils    # macOS
make -C cwdec test && make -C app && make -C app sim
```

Requirements: python3, a C compiler for the PC, `arm-none-eabi-gcc` (as the SDK uses; unofficial
alternative `clang` + `ld.lld` with `make -C app TOOLCHAIN=clang`), git.

## Safety

- CW.APP is receive only; the TX functions of the API are not used.
- Apps cannot change the radio's frequency or mode (SDK rule).
- First run on a radio: **PROBE.APP**, which only reads and measures.
- If an app hangs: switch the radio off and remove the file from the SD card. The firmware is
  not touched.
- Do not publish Icom firmware or decoded firmware images.

## License

MIT (see `LICENSE`), like the ICAP SDK. `app/icap.h` is a copy of SQ6EMM's SDK header (MIT).
The GPL-3.0 apps of the SDK (FT8, JT4, ...) are studied but not copied here.
