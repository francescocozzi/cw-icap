# Message to SQ6EMM

Draft, to be sent as an issue at https://github.com/sq6emm/icap-sdk/issues (issues are open,
discussions are not). The first message (request for the ABI header) was superseded by the
official SDK of 2026-11-20.

Already answered by tests, so not asked:
- in CW the audio of `icap_audio_read` comes after the IF filter (noise shaped by FIL3 reached the
  app);
- the AF gain dependence can be measured with PROBE.APP.

---

**Subject:** CW decoder app on ICAP: character output to DECODE, KEEP mode, Apps menu limit

Hi Dawid,

thanks for ICAP and the SDK. I've written a CW (Morse) decoder app on the SDK:
https://github.com/francescocozzi/cw-icap (MIT, RX only). It uses `icap_audio_read`,
`icap_decode_out` and `icap_set_line`, and is packed with `--background --mode CW:keep`.
Tested on IC-705 E, firmware and SD runtime 20261119v14. Reception on air is good. If you think
it's useful, I'd be glad to have it in the bundle.

Three questions/requests:

1. **Character output to DECODE.** CW, like RTTY, is read as it arrives, but every
   `icap_decode_out()` call becomes its own line. From the v14 patch I see the loader queues the
   string, turns control characters into spaces and passes it on as `"\r" + text + "\r"`. Could
   there be a way to append text to the current line, for example a new slot `decode_put()`
   that sends the text as is and lets the app send `\r` itself? For now I send one word per
   call, so the DECODE screen shows one word per line.

2. **`ICAP_RMODE_KEEP` and the RTTY button cycle.** With `keep` the radio stays in RTTY-R (the
   last mode of the cycle) and the screen opens as "CW DECODE". Pressing CW afterwards keeps the
   app running and the radio goes back to CW, so it works with one extra key press. Could
   `keep` restore the mode the radio was in before the cycle started? In RTTY-R the audio sits
   in the RTTY passband, so a CW decoder hears nothing.

3. **Apps menu limit.** MENU > SET > Apps lists 8 apps at most. With the full v14 bundle (8
   apps) a ninth `PROBE.APP` did not show up; after removing `PI4.APP` it did. In the patch the
   scan stops after the 8th `.APP` in directory order (`cmp r4, #7` at 0x2037ca66), the name
   table has 8 entries and the menu page has 8 rows. Could a second page or a longer list be
   added, so third-party apps don't have to replace a bundle app? Until then, it might help to
   say in the README that only the first 8 files in directory order are listed.

<!-- only if the project's receive-only rule changes:
4. **CW text through the radio's keyer.** For QSO/contest macros with the decoded callsign
   filled in (as on the RTTY screen), an app would need to send text through the radio's own
   CW keyer (speed, break-in and sidetone as set on the radio), not as audio in USB-D. Would
   something like `cw_send(const char *text)`, gated like TX, be possible?
-->

73, Francesco IZ7KHR
